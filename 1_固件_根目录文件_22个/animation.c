/* YuexinCat dance player: faithful port of the reference decoder.
 *  - XOR delta + Elias-gamma gaps, identical bit order to the reference.
 *  - 33/33/34 ms deadlines (average 30 fps) without fractional drift.
 *  - Ping-pong loop: XOR deltas are reversible, delta[k] toggles k <-> k-1.
 *  - Draw: byte-shift the 7 source pages by 4 rows into 8 OLED pages at
 *    x=36..91, y=4..59 (screen centre). Set only cat foreground bits: zero
 *    bits are transparent, so fireworks remain visible behind the cat. */
#include "main.h"

#if (YCAT_DRAW_X + YCAT_WIDTH > OLED_W) || (YCAT_DRAW_Y + YCAT_HEIGHT > OLED_H) || (YCAT_DRAW_Y != 4u)
#error "YuexinCat region must fit the OLED and the blit assumes a 4-row page offset"
#endif
/* Left column used by a 64-pixel-wide video frame (centred on the 128-column
 * display). Only used when YCAT_*_VIDEO_ENABLED is 1. */
#define YCAT_VIDEO_X 32u

static uint8_t  s_cur[YCAT_BITMAP_BYTES];
static uint16_t s_idx = 0u;
static int8_t   s_dir = 1;
static uint32_t s_next = 0u;
static uint8_t  s_phase = 0u;
#if YCAT_ROLL_ENABLED || YCAT_ANGRY_ENABLED
static uint32_t s_animStart = 0u;
#endif
static uint8_t  s_rollActive = 0u;
static uint8_t  s_angryActive = 0u;

/* V24: 增量位流用"静态上下文二进制算术编码"存储 (LZMA range coder).
 * 解码器读入的仍然是完全相同的 gamma 间隔位 + 8 位新值, 只是每个"位"来自
 * 算术解码而不是直接读 Flash: 同一个上下文概率表 g_ycat_m_* 把 -log2(p)
 * 的代价花在该位真正需要的位数上, 而不再固定 1 位。
 * 纯查表、无自适应状态 => 解码结果与读取顺序无关, 单帧可独立解码 (乒乓/
 * 倒放不需要保存模型), 也保证了和旧位流逐位一致的画面。 */
typedef struct { const uint8_t *data; uint32_t pos, range, code; } BitReader;
/* 0 = the active asset's counts[] are uint8_t, 1 = uint16_t. Set together with
 * the asset pointer so ApplyDeltaTo reads the right width. */
static uint8_t s_deltaCountsWide = 0u;
static uint8_t ArBit(BitReader *r, const uint8_t *p)
{
    uint32_t bound = (r->range >> 8) * (uint32_t)(*p);
    uint8_t b;
    if (r->code < bound) { r->range = bound; b = 0u; }
    else { r->code -= bound; r->range -= bound; b = 1u; }
    /* 每次重整化只多读 1 字节; 编码端每帧结尾写了 5 字节 flush, 因此不会读
     * 越过本帧码块 (最后一段流尾部另有 8 字节填充)。 */
    while (r->range < 0x01000000u) {
        r->range <<= 8u;
        r->code = (r->code << 8) | r->data[r->pos++];
    }
    return b;
}
static uint16_t GammaGap(BitReader *r)
{
    uint8_t zeros = 0u, i;
    uint16_t v = 1u;
    while (ArBit(r, &g_ycat_m_lead[zeros]) == 0u) { zeros++; if (zeros > 9u) { return 0u; } }
    for (i = 0u; i < zeros; i++) {
        v = (uint16_t)((v << 1) | ArBit(r, &g_ycat_m_suf[(uint16_t)zeros * 10u + i]));
    }
    return v;
}
static uint8_t ByteMsb(BitReader *r, uint8_t cls)
{
    /* 8 位二叉树: 节点号 1..255 就是"已读前缀", 直接当上下文;
     * cls = 前一个变化字节的类别 (高 3 位), 相邻字节的值高度相关。 */
    const uint8_t *t = &g_ycat_m_val[(uint16_t)cls << 8];
    uint16_t node = 1u;
    uint8_t i;
    for (i = 0u; i < 8u; i++) { node = (uint16_t)((node << 1) | ArBit(r, &t[node])); }
    return (uint8_t)(node & 0xFFu);
}
static void ApplyDeltaTo(uint8_t *dst,uint32_t bitmapBytes,const uint8_t *stream,uint32_t streamBytes,
                         const uint16_t *offsets,const void *counts,uint16_t f)
{
    uint16_t n, gap, limit;
    int32_t prev = -1, idx;
    uint32_t start=(uint32_t)offsets[f],end=(uint32_t)offsets[f+1u];
    BitReader r;
    uint8_t x, cls = 0u, i;
    /* Stop at the first invalid code. A damaged or truncated Flash resource gives
     * GammaGap()==0, which would XOR the same byte twice (visible as garbage).
     * Valid data never triggers this return, so decoding is bit-identical. */
    if(start>end || end>streamBytes) { return; }
    /* counts[] may be 8-bit or 16-bit wide (64x64 sources can change >255 bytes
     * between frames). Both layouts select the same frame, so this is a pure
     * accessor change: existing 8-bit assets decode bit-identically. */
    if(s_deltaCountsWide) { limit=((const uint16_t *)counts)[f]; }
    else                  { limit=((const uint8_t  *)counts)[f]; }
    r.data=stream+start; r.pos=0u; r.range=0xFFFFFFFFu; r.code=0u;
    for (i = 0u; i < 5u; i++) { r.code = (r.code << 8) | r.data[r.pos++]; }
    for (n = 0u; n < limit; n++) {
        gap = GammaGap(&r);
        if(gap == 0u) { return; }
        idx = prev + (int32_t)gap;
        x = ByteMsb(&r, cls);
        if (idx < 0 || (uint32_t)idx >= bitmapBytes) { return; }
        dst[idx] ^= x; prev = idx;
        cls = (uint8_t)(((uint16_t)x * YCAT_M_CLASSES) >> 8);
    }
}
static void ApplyDanceDelta(uint16_t f)
{
    s_deltaCountsWide=0u; /* dance asset always uses 8-bit counts */
    ApplyDeltaTo(s_cur,YCAT_BITMAP_BYTES,g_delta_stream,YCAT_STREAM_BYTES,
                 g_delta_offsets,g_delta_counts,f);
}
#if YCAT_ROLL_VIDEO_ENABLED || YCAT_ANGRY_VIDEO_ENABLED
/* One scratch frame is sufficient: roll and angry are never active together. */
/* Scratch frame for a decompressed video frame. It must hold the LARGEST
 * asset (roll 82x64 = 656 B; angry 64x64 = 512 B), not the 56x56 sprite,
 * otherwise the delta decoder would write past the buffer or bail out and
 * the picture would freeze. */
#define YCAT_VIDEO_MAX_BYTES 656u
#if (YCAT_WIDTH * YCAT_HEIGHT / 8) > YCAT_VIDEO_MAX_BYTES
#error "YCAT_VIDEO_MAX_BYTES is smaller than the local sprite size"
#endif
static uint8_t s_videoRaw[YCAT_VIDEO_MAX_BYTES];
#if defined(FW_SIMULATOR)
/* ---- 仅模拟器: 逐帧回归探针 ----------------------------------------
 * 顺序重放任意一段动画 (0=跳舞 1=生气 2=打滚), 每步返回当前 1-bit 位图的
 * FNV-1a 哈希, 供 simulator/test/cat_frames.mjs 与"旧位流解码结果"逐帧
 * 比对。产品固件不包含这段代码。 */
static uint8_t s_simAsset = 0u;
static uint32_t Sim_Fnv(const uint8_t *p, uint32_t n)
{
    uint32_t h = 2166136261u, i;
    for(i = 0u; i < n; i++) { h ^= p[i]; h *= 16777619u; }
    return h;
}
__attribute__((export_name("sim_cat_reset"))) uint32_t Sim_CatReset(uint32_t asset)
{
    const uint8_t *base;
    uint32_t bm, i;
    s_simAsset = (uint8_t)asset;
    if(asset == 0u) {
        base = g_cat_frame0; bm = YCAT_BITMAP_BYTES; s_deltaCountsWide = 0u;
    } else if(asset == 1u) {
        base = g_ycat_angry_video.frame0; bm = g_ycat_angry_video.bitmapBytes;
        s_deltaCountsWide = (uint8_t)g_ycat_angry_video.countsWide;
    } else {
        base = g_ycat_roll_video.frame0; bm = g_ycat_roll_video.bitmapBytes;
        s_deltaCountsWide = (uint8_t)g_ycat_roll_video.countsWide;
    }
    for(i = 0u; i < bm; i++) { s_videoRaw[i] = base[i]; }
    return Sim_Fnv(s_videoRaw, bm);   /* 帧 0 的哈希 */
}
__attribute__((export_name("sim_cat_step"))) uint32_t Sim_CatStep(uint32_t f)
{
    const YcatVideoAsset *a;
    if(s_simAsset == 0u) {
        ApplyDeltaTo(s_videoRaw, YCAT_BITMAP_BYTES, g_delta_stream, YCAT_STREAM_BYTES,
                     g_delta_offsets, g_delta_counts, (uint16_t)f);
        return Sim_Fnv(s_videoRaw, YCAT_BITMAP_BYTES);
    }
    a = (s_simAsset == 1u) ? &g_ycat_angry_video : &g_ycat_roll_video;
    ApplyDeltaTo(s_videoRaw, a->bitmapBytes, a->stream, a->streamBytes,
                 a->offsets, a->counts, (uint16_t)f);
    return Sim_Fnv(s_videoRaw, a->bitmapBytes);
}
#endif
static void Ycat_VideoStart(const YcatVideoAsset *a)
{
    uint16_t i;
    for(i=0u;i<YCAT_VIDEO_MAX_BYTES;i++) { s_videoRaw[i]=0u; }
    for(i=0u;i<a->bitmapBytes && i<YCAT_VIDEO_MAX_BYTES;i++) { s_videoRaw[i]=a->frame0[i]; }
}
static void Ycat_VideoDelta(const YcatVideoAsset *a,uint16_t f)
{
    s_deltaCountsWide=(uint8_t)a->countsWide;
    ApplyDeltaTo(s_videoRaw,a->bitmapBytes,a->stream,a->streamBytes,a->offsets,a->counts,f);
}
/* Draw a decompressed video frame 1:1 into OLED_GRAM, page by page, at the
 * given left column. This is byte-for-byte the same blit the reference player
 * performs with oled_draw_cat(), so the on-screen character is identical.
 * Fireworks are drawn first by Scene_Alarm() and the frame is OR-ed on top,
 * so the black background of the sprite stays transparent. */
static void Ycat_VideoDrawGram(const YcatVideoAsset *a)
{
    uint8_t page,x;
    if(!a->width || !a->height) { return; }
    if(a->bitmapBytes > YCAT_VIDEO_MAX_BYTES) { return; }
    if(a->height != 64u || (a->width % 8u) != 0u || a->width > 128u) { return; }
    for(page=0u;page<8u;page++) {
        const uint8_t *src=&s_videoRaw[(uint16_t)page*a->width];
        uint8_t *dst=&OLED_GRAM[page][YCAT_VIDEO_X];
        for(x=0u;x<a->width;x++) { dst[x] = (uint8_t)(dst[x] | src[x]); }
    }
}
static void Ycat_VideoDraw(const YcatVideoAsset *a)
{
    uint16_t x,y,sx,sy,i;
    for(i=0u;i<YCAT_BITMAP_BYTES;i++) { s_cur[i]=0u; }
    if(!a->width || !a->height) { return; }
    if(a->bitmapBytes > YCAT_VIDEO_MAX_BYTES) { return; }
    if(a->width > 128u || a->height > 64u) { return; }
    for(y=0u;y<YCAT_HEIGHT;y++) {
        sy=(uint16_t)((uint32_t)y*a->height/YCAT_HEIGHT);
        for(x=0u;x<YCAT_WIDTH;x++) {
            uint8_t bit;
            sx=(uint16_t)((uint32_t)x*a->width/YCAT_WIDTH);
            bit=(uint8_t)((s_videoRaw[((uint32_t)(sy>>3)*a->width)+sx]>>(sy&7u))&1u);
            if(bit) { s_cur[((uint32_t)(y>>3)*YCAT_WIDTH)+x]|=(uint8_t)(1u<<(y&7u)); }
        }
    }
}
#endif
#if YCAT_ROLL_ENABLED || YCAT_ANGRY_ENABLED
/* Roll and angry are rigid-body warps of the dance-cat sprite, so the outline
 * is the dance silhouette at every frame and the motion is continuous. The
 * inverse mapping is rotated by 180 degrees (dest -> source) and sampled 2x2
 * per pixel for a smooth, non-ragged edge; the cost is ~1 ms per frame at 72 MHz. */
static uint8_t s_warp[YCAT_BITMAP_BYTES];
#if YCAT_ROLL_ENABLED && !YCAT_ROLL_VIDEO_ENABLED
/* Snapshot the exact dance frame at the stage boundary. Roll then transforms
 * this shared silhouette, so the dance -> roll transition has no pixel jump. */
static uint8_t s_rollBase[YCAT_BITMAP_BYTES];
#endif
static uint8_t Ycat_Sample(const uint8_t *src, int32_t xs, int32_t ys)
{
    if((uint32_t)xs >= YCAT_WIDTH || (uint32_t)ys >= YCAT_HEIGHT) { return 0u; }
    return (uint8_t)((src[((uint32_t)ys >> 3) * YCAT_WIDTH + (uint32_t)xs] >> (ys & 7u)) & 1u);
}
static void Ycat_Warp(const uint8_t *src, uint16_t a16, uint16_t scaleQ8, int8_t dy)
{
    int32_t cs = Cos14(a16), sn = Sin14(a16);
    int32_t inv = (int32_t)(4194304uL / (uint32_t)scaleQ8);   /* (256 << 14) / scale */
    int32_t iA = (cs * inv) >> 20, iB = (sn * inv) >> 20;      /* Q8 */
    int32_t xd, yd, u, v, xs, ys, yy;
    uint32_t i;
    uint8_t hit, k;
    for(i = 0u; i < YCAT_BITMAP_BYTES; i++) { s_warp[i] = 0u; }
    for(yd = 0; yd < (int32_t)YCAT_HEIGHT; yd++) {
        for(xd = 0; xd < (int32_t)YCAT_WIDTH; xd++) {
            hit = 0u;
            for(k = 0u; k < 4u; k++) {
                /* Sample around the pixel centre using +/- 1/8px offsets.
                 * At angle=0 / scale=256 this is an exact identity transform,
                 * so the roll's first and last frames preserve dance line weight. */
                u = xd * 256 - (28 << 8) + ((k & 1u) ? 32 : -32);
                v = yd * 256 - (28 << 8) + ((k & 2u) ? 32 : -32);
                /* u/v are Q8, iA/iB are Q8 -> the product is Q16 */
                xs = 28 + (((iA * u + iB * v) + 32768) >> 16);
                ys = 28 + (((iA * v - iB * u) + 32768) >> 16);
                hit = (uint8_t)(hit + Ycat_Sample(src, xs, ys));
            }
            if(hit >= 2u) {
                yy = yd + dy;
                if((uint32_t)yy < YCAT_HEIGHT) { s_warp[(((uint32_t)yy >> 3) * YCAT_WIDTH) + xd] |= (uint8_t)(1u << (yy & 7u)); }
            }
        }
    }
}
static void Ycat_CopyWarp(void)
{
    uint32_t i;
    for(i = 0u; i < YCAT_BITMAP_BYTES; i++) { s_cur[i] = s_warp[i]; }
}
#endif
#if YCAT_ROLL_ENABLED
#if YCAT_ROLL_VIDEO_ENABLED
/* REAL 95-frame roll asset (82x64, re-packed from the user's uploaded
 * reference player): the same YuexinCat character and rolling motion as
 * the reference video, played through the same XOR + Elias-gamma decoder
 * as the dance stage, so the outline and line weight match dance/angry. */
static void Roll_Start(void)
{
    Ycat_VideoStart(&g_ycat_roll_video);
}
static void RollAdvance(void)
{
    uint16_t n=g_ycat_roll_video.frameCount;
    if(n<2u) { return; }
    if(s_dir>0) {
        if((uint16_t)(s_idx+1u)<n) { s_idx++; Ycat_VideoDelta(&g_ycat_roll_video,s_idx); }
        else { s_dir=-1; Ycat_VideoDelta(&g_ycat_roll_video,s_idx); s_idx--; }
    } else if(s_idx>0u) { Ycat_VideoDelta(&g_ycat_roll_video,s_idx); s_idx--; }
    else { s_dir=1; s_idx=1u; Ycat_VideoDelta(&g_ycat_roll_video,s_idx); }
}
/* Called from CatAnim_Draw() AFTER the fireworks: blit the decompressed
 * 82x64 frame 1:1 into OLED_GRAM, page by page, centred on the 128-column
 * display (x = (128-82)/2 = 23, exactly like the reference player). The
 * frame is OR-ed on top, so fireworks remain visible around the cat. */
static void Roll_DrawVideo(void)
{
    const YcatVideoAsset *a=&g_ycat_roll_video;
    uint8_t page,x,x0;
    if(!a->width || !a->height) { return; }
    if(a->bitmapBytes > YCAT_VIDEO_MAX_BYTES) { return; }
    if(a->height != 64u || a->width > 128u) { return; }
    x0=(uint8_t)((128u-a->width)/2u);
    for(page=0u;page<8u;page++) {
        const uint8_t *src=&s_videoRaw[(uint16_t)page*a->width];
        uint8_t *dst=&OLED_GRAM[page][x0];
        for(x=0u;x<a->width;x++) { dst[x] = (uint8_t)(dst[x] | src[x]); }
    }
}
#else
/* Procedural fallback (only if a build ever sets YCAT_ROLL_VIDEO_ENABLED
 * back to 0): warp the live dance bitmap through a baked pose table. */
static void Roll_Build(uint16_t f)
{
    const YcatPose *p=&g_ycat_roll_pose[f%g_ycat_roll_frame_count];
    Ycat_Warp(g_cat_frame0,p->angle,p->scale,p->dy);
    Ycat_CopyWarp();
}
static void Roll_Start(void) { Roll_Build(0u); }
static void RollAdvance(void)
{
    uint16_t n=g_ycat_roll_frame_count;
    if(n<2u) { return; }
    if(s_dir>0) {
        if((uint16_t)(s_idx+1u)<n) { s_idx++; Roll_Build(s_idx); }
        else { s_dir=-1; s_idx--; Roll_Build(s_idx); }
    } else if(s_idx>0u) { s_idx--; Roll_Build(s_idx); }
    else { s_dir=1; s_idx=1u; Roll_Build(s_idx); }
}
#endif
#endif
#if YCAT_ANGRY_ENABLED
/* Imported video frame stream, with the procedural angry-cat fallback. */
#if YCAT_ANGRY_VIDEO_ENABLED
/* The angry asset is a REAL 64x64 video recording of the character. Drawing it
 * 1:1 into OLED_GRAM at x=32..95 (exactly what the user's reference player
 * does) preserves every pixel of the original motion; resampling it into the
 * 56x56 dance-sprite buffer would blur and shift the character. */

static void Angry_Start(void)
{
    s_rollActive=0u;
    Ycat_VideoStart(&g_ycat_angry_video);
}
static void AngryAdvance(void)
{
    uint16_t n=g_ycat_angry_video.frameCount;
    if(n<2u) { return; }
    if(s_dir>0) {
        if((uint16_t)(s_idx+1u)<n) { s_idx++; Ycat_VideoDelta(&g_ycat_angry_video,s_idx); }
        else { s_dir=-1; Ycat_VideoDelta(&g_ycat_angry_video,s_idx); s_idx--; }
    } else if(s_idx>0u) { Ycat_VideoDelta(&g_ycat_angry_video,s_idx); s_idx--; }
    else { s_dir=1; s_idx=1u; Ycat_VideoDelta(&g_ycat_angry_video,s_idx); }
}
/* Called from CatAnim_Draw() AFTER the fireworks, so the frame is composited
 * over them exactly like the dance sprite is. */
static void Angry_DrawVideo(void)
{
    Ycat_VideoDrawGram(&g_ycat_angry_video);
}
#else
static void Angry_Build(uint16_t f)
{
    const YcatPose *p=&g_ycat_angry_pose[f%g_ycat_angry_frame_count];
    const uint8_t *mk=g_ycat_angry_marks[p->mark&3u];
    uint32_t i;
    /* Every frame is built FRESH from the clean independent angry sprite via
     * the same 2x2-sampled rigid warp the roll stage uses (proven smooth on
     * hardware). s_warp/s_cur are fully overwritten, so no roll or dance data
     * can remain. The pose table is a pre-sampled 30fps anger shiver:
     * 6Hz head shake, 3Hz bounce + breathing, 7.5Hz vein pulse. */
    Ycat_Warp(g_ycat_angry_base,p->angle,p->scale,p->dy);
    Ycat_CopyWarp();
    for(i=0u;i<YCAT_BITMAP_BYTES;i++) { s_cur[i]|=mk[i]; }
}
static void Angry_Start(void)
{
    s_rollActive=0u;
    Angry_Build(0u);
}
static void AngryAdvance(void)
{
    uint16_t n=g_ycat_angry_frame_count;
    if(n<2u) { return; }
    if(s_dir>0) {
        if((uint16_t)(s_idx+1u)<n) { s_idx++; Angry_Build(s_idx); }
        else { s_dir=-1; s_idx--; Angry_Build(s_idx); }
    } else if(s_idx>0u) { s_idx--; Angry_Build(s_idx); }
    else { s_dir=1; s_idx=1u; Angry_Build(s_idx); }
}
#endif
#endif
static void Schedule(void)
{
    s_next += YCAT_BASE_MS;
    if (++s_phase >= 3u) { s_next++; s_phase = 0u; }
}
static void Advance(void)
{
#if YCAT_ANGRY_ENABLED
    if(s_angryActive) { AngryAdvance(); return; }
#endif
#if YCAT_ROLL_ENABLED
    if(s_rollActive) { RollAdvance(); return; }
#endif
#if YCAT_PING_PONG
    if (s_dir > 0) {
        if ((uint16_t)(s_idx + 1u) < YCAT_FRAME_COUNT) { s_idx++; ApplyDanceDelta(s_idx); }
        else { s_dir = -1; ApplyDanceDelta(s_idx); s_idx--; }
    } else {
        if (s_idx > 0u) { ApplyDanceDelta(s_idx); s_idx--; }
        else { s_dir = 1; s_idx = 1u; ApplyDanceDelta(s_idx); }
    }
#else
    uint16_t i;
    if (++s_idx >= YCAT_FRAME_COUNT) { s_idx = 0u; for (i = 0u; i < YCAT_BITMAP_BYTES; i++) { s_cur[i] = g_cat_frame0[i]; } }
    else { ApplyDanceDelta(s_idx); }
#endif
}
void CatAnim_Reset(uint32_t nowMs)
{
    uint16_t i;
    for (i = 0u; i < YCAT_BITMAP_BYTES; i++) { s_cur[i] = g_cat_frame0[i]; }
    s_idx = 0u; s_dir = 1; s_phase = 0u; s_next = nowMs; s_rollActive=0u; s_angryActive=0u;
#if YCAT_ROLL_ENABLED || YCAT_ANGRY_ENABLED
    s_animStart=nowMs;
#endif
    Schedule();
}
uint8_t CatAnim_Tick(uint32_t nowMs)
{
    uint8_t steps = 0u, s;
#if YCAT_ANGRY_ENABLED
    if(!s_angryActive && g_ycat_angry_frame_count>=2u &&
       (uint32_t)(nowMs-s_animStart)>=YCAT_DANCE_MS+YCAT_ROLL_MS) {
        s_idx=0u; s_dir=1; s_rollActive=0u; s_angryActive=1u; s_phase=0u; s_next=nowMs;
        Angry_Start();
        return 1u;
    }
#endif
#if YCAT_ROLL_ENABLED
    /* Angry is terminal: after the 15 s switch the roll state must never be
     * re-entered, otherwise rollActive would be set again on every tick. */
    if(!s_angryActive && !s_rollActive && g_ycat_roll_frame_count>=2u &&
       (uint32_t)(nowMs-s_animStart)>=YCAT_DANCE_MS) {
        s_idx=0u; s_dir=1; s_rollActive=1u; s_phase=0u; s_next=nowMs;
        Roll_Start();
        return 1u;
    }
#endif
    if ((int32_t)(nowMs - s_next) < 0) { return 0u; }
    do { Schedule(); steps++; } while ((int32_t)(nowMs - s_next) >= 0 && steps < YCAT_MAX_CATCHUP);
    if (steps >= YCAT_MAX_CATCHUP) { s_next = nowMs + YCAT_BASE_MS; s_phase = 0u; }
    for (s = 0u; s < steps; s++) { Advance(); }
    return 1u;
}
void CatAnim_Draw(void)
{
    uint8_t p, x, hi, lo, m, *g;
#if YCAT_ROLL_VIDEO_ENABLED
    /* The roll asset is a real 82x64 video recording. Blit it 1:1 into
     * OLED_GRAM (centred, like the reference player) instead of
     * overdrawing it with the 56x56 dance-sprite buffer. */
    if(s_rollActive) { Roll_DrawVideo(); return; }
#endif
#if YCAT_ANGRY_VIDEO_ENABLED
    /* The angry asset is a real 64x64 video recording. Blit it 1:1 into
     * OLED_GRAM (the same layout the user's reference player uses) instead of
     * overdrawing it with the 56x56 dance-sprite buffer. */
    if(s_angryActive) { Angry_DrawVideo(); return; }
#endif
    for (p = 0u; p < 8u; p++) {
        m = (p == 0u) ? 0xF0u : ((p == 7u) ? 0x0Fu : 0xFFu);
        for (x = 0u; x < YCAT_WIDTH; x++) {
            if(s_rollActive || s_angryActive) {
#if YCAT_ROLL_ENABLED
                hi = (p < 7u) ? (uint8_t)(s_cur[p * YCAT_WIDTH + x] << 4) : 0u;
                lo = (p > 0u) ? (uint8_t)(s_cur[(p - 1u) * YCAT_WIDTH + x] >> 4) : 0u;
#elif YCAT_ANGRY_ENABLED
                hi = (p < 7u) ? (uint8_t)(s_cur[p * YCAT_WIDTH + x] << 4) : 0u;
                lo = (p > 0u) ? (uint8_t)(s_cur[(p - 1u) * YCAT_WIDTH + x] >> 4) : 0u;
#else
                hi=lo=0u;
#endif
            } else {
                hi = (p < 7u) ? (uint8_t)(s_cur[p * YCAT_WIDTH + x] << 4) : 0u;
                lo = (p > 0u) ? (uint8_t)(s_cur[(p - 1u) * YCAT_WIDTH + x] >> 4) : 0u;
            }
            g = &OLED_GRAM[p][YCAT_DRAW_X + x];
            /* Alarm renderer draws fireworks first and the cat second. OR only
             * the cat's lit pixels so its bitmap background cannot erase them. */
            *g = (uint8_t)(*g | ((uint8_t)(hi | lo) & m));
        }
    }
}
uint16_t CatAnim_FrameIndex(void) { return s_idx; }

/* Simulation-only proof hooks. They are compiled out of the Keil/ARMCC build. */
#ifdef __SIM_WASM__
__attribute__((export_name("sim_cat_stage"))) uint32_t CatAnim_DebugStage(void)
{
    /* 3 = angry AND roll simultaneously (the old re-entry bug) — must never happen. */
    if(s_angryActive && s_rollActive) { return 3u; }
    return s_angryActive ? 2u : (s_rollActive ? 1u : 0u);
}
#if YCAT_ANGRY_ENABLED && !YCAT_ANGRY_VIDEO_ENABLED
static uint32_t Angry_DebugExpectedMismatch(void)
{
    uint32_t i,n=0u;
    const YcatPose *p;
    const uint8_t *mk;
    if(!s_angryActive) { return 0xFFFFFFFFu; }
    p=&g_ycat_angry_pose[s_idx%g_ycat_angry_frame_count];
    mk=g_ycat_angry_marks[p->mark&3u];
    Ycat_Warp(g_ycat_angry_base,p->angle,p->scale,p->dy); /* writes scratch s_warp only */
    for(i=0u;i<YCAT_BITMAP_BYTES;i++) {
        if(s_cur[i] != (uint8_t)(s_warp[i]|mk[i])) { n++; }
    }
    return n;
}
__attribute__((export_name("sim_cat_angry_clean"))) uint32_t CatAnim_DebugAngryClean(void)
{
    return (s_angryActive && !s_rollActive && Angry_DebugExpectedMismatch()==0u) ? 1u : 0u;
}
__attribute__((export_name("sim_cat_angry_mismatch"))) uint32_t CatAnim_DebugAngryMismatch(void)
{
    return Angry_DebugExpectedMismatch();
}
#else
__attribute__((export_name("sim_cat_angry_clean"))) uint32_t CatAnim_DebugAngryClean(void) { return 0u; }
__attribute__((export_name("sim_cat_angry_mismatch"))) uint32_t CatAnim_DebugAngryMismatch(void) { return 0xFFFFFFFFu; }
#endif
__attribute__((export_name("sim_cat_angry_index"))) uint32_t CatAnim_DebugAngryIndex(void) { return s_idx; }
#endif
