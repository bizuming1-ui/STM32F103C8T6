/* Original software-I2C transport and pixel geometry retained.
 * Transfers are bounded; failed ACKs never enter an infinite error loop. */
#include "OLED.h"
uint8_t OLED_GRAM[OLED_H/8][OLED_W];
static int32_t s_clipCx=0, s_clipCy=0, s_clipR2=-1;
/* V20: optional rectangular clip (x1 < x0 disables it); the countdown drum uses
 * it so a digit sliding in or out is cut exactly at the frame border. */
static int32_t s_clipX0=0, s_clipY0=0, s_clipX1=-1, s_clipY1=0;
static uint8_t s_transferOK=1u;
/* V26: 某些界面(日期编辑器的首帧)一次就改满整屏。这种情况下局部刷新要发出
 * 几十次寻址事务, 反而比一次整屏写慢得多, 所以由界面"请求一次整屏刷新"。
 * 这是刻意的一次性优化, 不影响任何其它界面。 */
static uint8_t s_wantFullFlush=0u;
void OLED_RequestFullFlush(void) { s_wantFullFlush=1u; }
uint8_t OLED_TakeFullFlush(void) { uint8_t v=s_wantFullFlush; s_wantFullFlush=0u; return v; }
/* V21: dirty-column tracking for the stopwatch's partial flush. The panel is
 * written page by page; a page is only sent when at least one of its pixels
 * changed, and only between the first and the last changed column. This is
 * what makes a full-speed 128x64 stopwatch possible on a software I2C bus:
 * a 1 ms frame touches ~20 columns instead of 1024 bytes (~10 ms).
 * 0xFF in s_dirtyMin means "this page is clean". */
#define OLED_DIRTY_SEG 4               /* column ranges remembered per page */
static uint8_t s_dMin[8][OLED_DIRTY_SEG], s_dMax[8][OLED_DIRTY_SEG], s_dN[8];
static void Dirty_All(void)
{
    uint8_t p;
    for(p=0u;p<8u;p++) { s_dMin[p][0]=0u; s_dMax[p][0]=127u; s_dN[p]=1u; }
}
/* Record one changed column. Ranges that touch (or nearly touch) are merged;
 * if a page has more than OLED_DIRTY_SEG ranges they are coalesced, so the
 * tracker can never overflow. */
static void Dirty_Add(uint8_t page,uint8_t cx)
{
    uint8_t i,n=s_dN[page];
    for(i=0u;i<n;i++) {
        if(cx+1u>=s_dMin[page][i] && cx<=s_dMax[page][i]+1u) {
            if(cx<s_dMin[page][i]) { s_dMin[page][i]=cx; }
            if(cx>s_dMax[page][i]) { s_dMax[page][i]=cx; }
            return;
        }
    }
    if(n<OLED_DIRTY_SEG) {
        s_dMin[page][n]=cx; s_dMax[page][n]=cx; s_dN[page]=(uint8_t)(n+1u);
        return;
    }
    /* coalesce: merge the two closest ranges and add the new column */
    {
        uint8_t best=0u,gap=0xFFu;
        for(i=0u;i+1u<n;i++) {
            uint8_t g=(uint8_t)(s_dMin[page][i+1u]-s_dMax[page][i]);
            if(g<gap) { gap=g; best=i; }
        }
        if(s_dMin[page][best+1u]<s_dMin[page][best]) { s_dMin[page][best]=s_dMin[page][best+1u]; }
        if(s_dMax[page][best+1u]>s_dMax[page][best]) { s_dMax[page][best]=s_dMax[page][best+1u]; }
        for(i=(uint8_t)(best+1u);i+1u<n;i++) { s_dMin[page][i]=s_dMin[page][i+1u]; s_dMax[page][i]=s_dMax[page][i+1u]; }
        n--; s_dN[page]=n;
        Dirty_Add(page,cx);
    }
}
#define SCL_H() (OLED_SCL_PORT->BSRR=OLED_SCL_PIN)
#define SCL_L() (OLED_SCL_PORT->BRR=OLED_SCL_PIN)
#define SDA_H() (OLED_SDA_PORT->BSRR=OLED_SDA_PIN)
#define SDA_L() (OLED_SDA_PORT->BRR=OLED_SDA_PIN)
#define SDA_READ() ((OLED_SDA_PORT->IDR & OLED_SDA_PIN)!=0u)
#if defined(FW_SIMULATOR)
/* The wasm build has no real bus: the delays only waste time, the pixel data
 * itself is read straight out of OLED_GRAM by the page. Everything else in
 * this file (command sequences, geometry, fonts) stays bit-identical. */
static void I2C_Delay(void) { }
#else
static void I2C_Delay(void) { volatile uint32_t n=OLED_I2C_DELAY_TICKS; while(n--) { } }
#endif
#if defined(FW_SIMULATOR)
/* ---------------------------------------------------------------------------
 * V21 bus-level panel shadow.
 * The page used to read OLED_GRAM directly, so a missed partial flush was
 * invisible. This records what the SSD1306 would REALLY have received over
 * I2C (control bytes, page/column addressing and every data byte), which is
 * what sim_panel_ptr() hands to the browser and to the tests: "the screen
 * shows exactly what the hardware panel got". Nothing here is compiled by
 * Keil. ------------------------------------------------------------------- */
uint8_t OLED_SimPanel[8][128];
static uint8_t s_busStage=0u;      /* 0 idle, 1 address, 2 control, 3 command, 4 data */
static uint8_t s_busCmd=0u, s_busArgN=0u, s_busArgI=0u, s_busArgTmp[2]={0u,0u};
static uint8_t s_busColStart=0u,s_busColEnd=127u,s_busPageStart=0u,s_busPageEnd=7u;
static uint8_t s_busPage=0u,s_busCol=0u,s_busMode=0u;   /* 0 horizontal, 2 page */
uint32_t Sim_BusData=0u, Sim_BusFlushes=0u;
static void Bus_Start(void) { s_busStage=1u; }
static void Bus_Stop(void) { s_busStage=0u; }
static void Bus_Data(uint8_t b)
{
    OLED_SimPanel[s_busPage&7u][s_busCol]=b;
    Sim_BusData++;
    if(s_busMode==2u) { s_busCol=(uint8_t)((s_busCol+1u)&0x7Fu); }
    else {
        s_busCol++;
        if(s_busCol>s_busColEnd) { s_busCol=s_busColStart; s_busPage++; if(s_busPage>s_busPageEnd) { s_busPage=s_busPageStart; } }
    }
}
static void Bus_Command(uint8_t b)
{
    /* 参数按到达顺序写入 tmp[0], tmp[1]. 旧写法用 2-argN 当下标, 只有 1 个
     * 参数的 0x20 (寻址模式) 会把参数存到 tmp[1] 却仍从 tmp[0] 读 —— 影子
     * 就永远停在"水平寻址", 真机上 0x20,0x02 之后的整屏刷新花屏在模拟器里
     * 完全看不到。改成显式序号后, 影子与 SSD1306 的真实行为一致。 */
    if(s_busArgN) { s_busArgTmp[s_busArgI++]=b; s_busArgN--;
        if(!s_busArgN) {
            if(s_busCmd==0x21u) { s_busColStart=s_busArgTmp[0]; s_busColEnd=s_busArgTmp[1]; s_busCol=s_busColStart; }
            else if(s_busCmd==0x22u) { s_busPageStart=s_busArgTmp[0]; s_busPageEnd=s_busArgTmp[1]; s_busPage=s_busPageStart; }
            else if(s_busCmd==0x20u) { s_busMode=(s_busArgTmp[0]==0x02u)?2u:0u; }
        }
        return;
    }
    if(b==0x21u || b==0x22u || b==0x20u) { s_busCmd=b; s_busArgN=(b==0x20u)?1u:2u; s_busArgI=0u; return; }
    if((b&0xF8u)==0xB0u) { s_busPage=(uint8_t)(b&0x07u); return; }
    if((b&0xF0u)==0x00u) { s_busCol=(uint8_t)((s_busCol&0xF0u)|(b&0x0Fu)); return; }
    if((b&0xF0u)==0x10u) { s_busCol=(uint8_t)((s_busCol&0x0Fu)|((b&0x0Fu)<<4)); return; }
}
static void Bus_Byte(uint8_t b)
{
    switch(s_busStage) {
        case 0u: break;                       /* byte before a START: ignore */
        case 1u: s_busStage=2u; break;        /* slave address */
        case 2u: s_busStage=(b==0x40u)?4u:3u; break;
        default: if(s_busStage==4u) { Bus_Data(b); } else { Bus_Command(b); } break;
    }
}
#endif

static void I2C_Stop(void) { SDA_L(); I2C_Delay(); SCL_H(); I2C_Delay(); SDA_H(); I2C_Delay();
#if defined(FW_SIMULATOR)
    Sim_BusFlushes++; Bus_Stop();
#endif
}
static void I2C_Start(void) { SDA_H(); SCL_H(); I2C_Delay(); SDA_L(); I2C_Delay(); SCL_L(); I2C_Delay();
#if defined(FW_SIMULATOR)
    Bus_Start();
#endif
}
static uint8_t I2C_WriteByte(uint8_t b)
{
    uint8_t i,ack;
#if defined(FW_SIMULATOR)
    Bus_Byte(b);
#endif
    for(i=0u;i<8u;i++) {
        if(b&0x80u) { SDA_H(); } else { SDA_L(); }
        b=(uint8_t)(b<<1); I2C_Delay(); SCL_H(); I2C_Delay(); SCL_L();
    }
    SDA_H(); I2C_Delay(); SCL_H(); I2C_Delay(); ack=SDA_READ()?0u:1u; SCL_L();
    if(!ack) { s_transferOK=0u; }
    return ack;
}
static void I2C_GPIO_Init(void)
{
    uint8_t i;
    RCC->APB2ENR |= OLED_GPIO_RCC_MASK;
    GPIOB->BSRR=OLED_SCL_PIN|OLED_SDA_PIN;
    GPIOB->CRL=(GPIOB->CRL & ~0xFF000000uL) | 0x77000000uL;
    SDA_H();
    for(i=0u;i<9u;i++) { SCL_L(); I2C_Delay(); I2C_Delay(); SCL_H(); I2C_Delay(); I2C_Delay(); }
    I2C_Stop();
}
void OLED_DelayMs(uint32_t ms)
{
#if defined(FW_SIMULATOR)
    (void)ms;   /* no panel warm-up time is needed in the browser */
#else
    while(ms--) { volatile uint32_t n=12000u; while(n--) { } }
#endif
}
void OLED_WriteCommand(uint8_t cmd)
{
    I2C_Start(); I2C_WriteByte(OLED_I2C_ADDR); I2C_WriteByte(0x00u); I2C_WriteByte(cmd); I2C_Stop();
}
void OLED_DisplayOn(void) { OLED_WriteCommand(0x8Du); OLED_WriteCommand(0x14u); OLED_WriteCommand(0xAFu); }
void OLED_DisplayOff(void) { OLED_WriteCommand(0x8Du); OLED_WriteCommand(0x10u); OLED_WriteCommand(0xAEu); }
void OLED_SetContrast(uint8_t v) { OLED_WriteCommand(0x81u); OLED_WriteCommand(v); }
uint8_t OLED_LastTransferOK(void) { return s_transferOK; }
void OLED_Update(void)
{
    uint16_t i;
#if OLED_DRIVER_SH1106
    uint8_t page;
#else
    const uint8_t *p=&OLED_GRAM[0][0];
#endif
    s_transferOK=1u;
#if OLED_DRIVER_SH1106
    for(page=0u;page<8u;page++) {
        I2C_Start(); I2C_WriteByte(OLED_I2C_ADDR); I2C_WriteByte(0x00u);
        I2C_WriteByte((uint8_t)(0xB0u|page)); I2C_WriteByte(0x02u); I2C_WriteByte(0x10u); I2C_Stop();
        I2C_Start();
        if(!I2C_WriteByte(OLED_I2C_ADDR)) { I2C_Stop(); return; }
        I2C_WriteByte(0x40u);
        for(i=0u;i<128u;i++) { if(!I2C_WriteByte(OLED_GRAM[page][i])) { I2C_Stop(); return; } }
        I2C_Stop();
    }
#else
    I2C_Start();
    if(!I2C_WriteByte(OLED_I2C_ADDR)) { I2C_Stop(); return; }
    I2C_WriteByte(0x00u);                     /* Co=0, D/C#=0: 后面是命令流 */
    /* V25 关键修复: 必须先显式回到"水平寻址"(0x20,0x00) 再设窗口。
     * OLED_UpdatePartial() 为了逐页局部刷新, 会把面板切到"页寻址"(0x20,0x02)
     * 并一直保留; 而页寻址下 0x21/0x22 的窗口设置是无效的 —— 1024 字节会从
     * 当前页的当前列开始写, 写满 127 回绕到同一页, 结果 7 页停留旧画面、1 页
     * 被覆盖, 这就是秒表/计算器长按 PA4 回时间界面时的"花屏"。
     * 整屏刷新自己声明寻址模式, 不再依赖上一次刷新留下的副作用。 */
    I2C_WriteByte(0x20u); I2C_WriteByte(0x00u);
    I2C_WriteByte(0x21u); I2C_WriteByte(0x00u); I2C_WriteByte(0x7Fu);
    I2C_WriteByte(0x22u); I2C_WriteByte(0x00u); I2C_WriteByte(0x07u); I2C_Stop();
    I2C_Start();
    if(!I2C_WriteByte(OLED_I2C_ADDR)) { I2C_Stop(); return; }
    I2C_WriteByte(0x40u);
    for(i=0u;i<1024u;i++) { if(!I2C_WriteByte(p[i])) { I2C_Stop(); return; } }
    I2C_Stop();
    /* V26: a full flush already covered every page and column, so the dirty
     * tracker has nothing left to report. Without this the ranges written by
     * the last full frame stayed queued, and the first partial flush of the
     * next screen (stopwatch / calculator / date editor) re-sent the whole
     * 1024 bytes - one 24.6 ms hiccup at exactly the moment the new screen
     * appears. */
    { uint8_t pg; for(pg=0u;pg<8u;pg++) { s_dN[pg]=0u; } }
#endif
}
/* V21: send only the pages/columns that changed since the last flush.
 * Uses the panel's page-addressing mode (0x20,0x02) so each page can start at
 * an arbitrary column; a plain OLED_Update() later restores horizontal mode
 * (0x20,0x00), so both paths stay independent. */
void OLED_UpdatePartial(void)
{
    uint8_t page,seg;
    int32_t c0,c1;
    s_transferOK=1u;
    for(page=0u;page<8u;page++) {
        uint8_t n=s_dN[page];
        s_dN[page]=0u;                      /* consume: this page is clean again */
        for(seg=0u;seg<n;seg++) {
            c0=(int32_t)s_dMin[page][seg]; c1=(int32_t)s_dMax[page][seg];
            if(c0<0) { c0=0; } if(c1>127) { c1=127; }
            if(c0>c1) { continue; }
            /* 1) addressing transaction (0x00 = command control byte) */
            I2C_Start();
            if(!I2C_WriteByte(OLED_I2C_ADDR)) { I2C_Stop(); continue; }
            I2C_WriteByte(0x00u);
#if OLED_DRIVER_SH1106
            I2C_WriteByte(0x20u); I2C_WriteByte(0x02u);
            I2C_WriteByte((uint8_t)(0xB0u|page));
            I2C_WriteByte((uint8_t)(0x02u+((c0>>4)&0x0Fu)));
            I2C_WriteByte((uint8_t)(0x10u|(c0&0x0Fu)));
#else
            I2C_WriteByte(0x20u); I2C_WriteByte(0x02u);        /* page addressing */
            I2C_WriteByte((uint8_t)(0xB0u|page));
            I2C_WriteByte((uint8_t)(c0&0x0Fu));
            I2C_WriteByte((uint8_t)(0x10u|((c0>>4)&0x0Fu)));
#endif
            I2C_Stop();
            /* 2) data transaction: 0x40 must start a NEW transfer, otherwise the
             * panel reads it as another command (caught by the bus shadow) */
            I2C_Start();
            if(!I2C_WriteByte(OLED_I2C_ADDR)) { I2C_Stop(); continue; }
            I2C_WriteByte(0x40u);
            while(c0<=c1) {
                if(!I2C_WriteByte(OLED_GRAM[page][c0])) { I2C_Stop(); return; }
                c0++;
            }
            I2C_Stop();
        }
    }
}
uint8_t OLED_Init(void)
{
    uint8_t ack=0u,retry;
    I2C_GPIO_Init(); OLED_DelayMs(120u);
    for(retry=0u;retry<3u && !ack;retry++) { I2C_Start(); ack=I2C_WriteByte(OLED_I2C_ADDR); I2C_Stop(); if(!ack) { OLED_DelayMs(50u); } }
    OLED_WriteCommand(0xAEu); OLED_WriteCommand(0xD5u); OLED_WriteCommand(0x80u);
    OLED_WriteCommand(0xA8u); OLED_WriteCommand(0x3Fu); OLED_WriteCommand(0xD3u); OLED_WriteCommand(0x00u);
    OLED_WriteCommand(0x40u); OLED_WriteCommand(0x8Du); OLED_WriteCommand(0x14u);
#if !OLED_DRIVER_SH1106
    OLED_WriteCommand(0x20u); OLED_WriteCommand(0x00u);
#endif
#if OLED_FLIP_180
    OLED_WriteCommand(0xA0u); OLED_WriteCommand(0xC0u);
#else
    OLED_WriteCommand(0xA1u); OLED_WriteCommand(0xC8u);
#endif
    OLED_WriteCommand(0xDAu); OLED_WriteCommand(0x12u); OLED_WriteCommand(0x81u); OLED_WriteCommand(0xCFu);
    OLED_WriteCommand(0xD9u); OLED_WriteCommand(0xF1u); OLED_WriteCommand(0xDBu); OLED_WriteCommand(0x30u);
    OLED_WriteCommand(0xA4u); OLED_WriteCommand(0xA6u); OLED_WriteCommand(0x2Eu);
    OLED_Clear(); OLED_Update(); OLED_WriteCommand(0xAFu); return ack;
}
void OLED_Clear(void) { uint16_t i; uint8_t *p=&OLED_GRAM[0][0]; for(i=0u;i<1024u;i++) { p[i]=0u; }
    /* V20: a full clear also drops every pending clip, so a scene can never
     * inherit a stale clip rectangle from the scene drawn before it. */
    s_clipR2=-1; s_clipX1=-1; s_clipY1=0; Dirty_All(); }
void OLED_Fill(uint8_t color) { uint16_t i; uint8_t *p=&OLED_GRAM[0][0]; for(i=0u;i<1024u;i++) { p[i]=color?255u:0u; } Dirty_All(); }
void OLED_SetClipCircle(int32_t cx,int32_t cy,int32_t r) { s_clipCx=cx; s_clipCy=cy; s_clipR2=(r<0)?-1:r*r; }
void OLED_SetClipRect(int32_t x0,int32_t y0,int32_t x1,int32_t y1) { s_clipX0=x0; s_clipY0=y0; s_clipX1=x1; s_clipY1=y1; }
void OLED_DrawPixel(int32_t x,int32_t y,uint8_t c)
{
    int32_t dx,dy;
    if((uint32_t)x>=128u || (uint32_t)y>=64u) { return; }
    if(s_clipX1>=s_clipX0 && (x<s_clipX0 || x>s_clipX1 || y<s_clipY0 || y>s_clipY1)) { return; }
    if(s_clipR2>=0) { dx=x-s_clipCx; dy=y-s_clipCy; if(dx*dx+dy*dy>s_clipR2) { return; } }
    if(c) { OLED_GRAM[y>>3][x] |= (uint8_t)(1u<<(y&7)); }
    else { OLED_GRAM[y>>3][x] &= (uint8_t)~(1u<<(y&7)); }
    Dirty_Add((uint8_t)(y>>3),(uint8_t)x);
}
uint8_t OLED_GetPixel(int32_t x,int32_t y)
{
    if((uint32_t)x>=128u || (uint32_t)y>=64u) { return 0u; }
    return (uint8_t)((OLED_GRAM[y>>3][x]>>(y&7))&1u);
}
void OLED_DrawHLine(int32_t x,int32_t y,int32_t w,uint8_t c) { int32_t i; for(i=0;i<w;i++) { OLED_DrawPixel(x+i,y,c); } }
void OLED_DrawVLine(int32_t x,int32_t y,int32_t h,uint8_t c) { int32_t i; for(i=0;i<h;i++) { OLED_DrawPixel(x,y+i,c); } }
void OLED_DrawLine(int32_t x0,int32_t y0,int32_t x1,int32_t y1,uint8_t c)
{
    int32_t dx=(x1>x0)?x1-x0:x0-x1,dy=(y1>y0)?y0-y1:y1-y0;
    int32_t sx=(x0<x1)?1:-1,sy=(y0<y1)?1:-1,err=dx+dy,e2;
    for(;;) { OLED_DrawPixel(x0,y0,c); if(x0==x1 && y0==y1) { break; } e2=2*err;
        if(e2>=dy) { err+=dy; x0+=sx; } if(e2<=dx) { err+=dx; y0+=sy; }
    }
}
void OLED_DrawRect(int32_t x,int32_t y,int32_t w,int32_t h,uint8_t c)
{
    if(w<=0 || h<=0) { return; }
    OLED_DrawHLine(x,y,w,c); OLED_DrawHLine(x,y+h-1,w,c); OLED_DrawVLine(x,y,h,c); OLED_DrawVLine(x+w-1,y,h,c);
}
void OLED_FillRect(int32_t x,int32_t y,int32_t w,int32_t h,uint8_t c) { int32_t j; for(j=0;j<h;j++) { OLED_DrawHLine(x,y+j,w,c); } }
/* Quarter-circle arc: the midpoint circle rasteriser restricted to one
 * quadrant. quad 0 = right/top, 1 = right/bottom, 2 = left/bottom,
 * 3 = left/top. The first and last plotted pixels are exactly (cx+r,cy)
 * and (cx,cy-r) etc., which is what makes the rounded-rectangle corners
 * join the straight edges with no gap and no double pixel. */
void OLED_DrawCircleQuad(int32_t cx,int32_t cy,int32_t r,uint8_t quad,uint8_t c)
{
    int32_t x=r,y=0,d=1-r;
    if(r<0) { return; }
    while(x>=y) {
        switch(quad) {
            case 0u: OLED_DrawPixel(cx+x,cy-y,c); OLED_DrawPixel(cx+y,cy-x,c); break;
            case 1u: OLED_DrawPixel(cx+x,cy+y,c); OLED_DrawPixel(cx+y,cy+x,c); break;
            case 2u: OLED_DrawPixel(cx-x,cy+y,c); OLED_DrawPixel(cx-y,cy+x,c); break;
            default: OLED_DrawPixel(cx-x,cy-y,c); OLED_DrawPixel(cx-y,cy-x,c); break;
        }
        y++; if(d<0) { d+=2*y+1; } else { x--; d+=2*(y-x)+1; }
    }
}
/* Rounded rectangle outline (obtuse corners), 1 pixel thick, continuous.
 * It is assembled from four straight runs plus four quarter arcs whose end
 * pixels coincide with the ends of those runs, so the closed curve can never
 * break apart - this is the "连续的细线正方形" the settings screen needs. */
void OLED_DrawRoundRect(int32_t x0,int32_t y0,int32_t x1,int32_t y1,int32_t r,uint8_t c)
{
    if(x1<x0 || y1<y0) { return; }
    if(r<=0) { OLED_DrawRect(x0,y0,x1-x0+1,y1-y0+1,c); return; }
    if(2*r>x1-x0) { r=(x1-x0+1)/2; }
    if(2*r>y1-y0) { r=(y1-y0+1)/2; }
    if(r<1) { r=1; }
    OLED_DrawHLine(x0+r,y0,x1-x0-2*r+1,c);
    OLED_DrawHLine(x0+r,y1,x1-x0-2*r+1,c);
    OLED_DrawVLine(x0,y0+r,y1-y0-2*r+1,c);
    OLED_DrawVLine(x1,y0+r,y1-y0-2*r+1,c);
    OLED_DrawCircleQuad(x0+r,y0+r,r,3u,c);
    OLED_DrawCircleQuad(x1-r,y0+r,r,0u,c);
    OLED_DrawCircleQuad(x0+r,y1-r,r,2u,c);
    OLED_DrawCircleQuad(x1-r,y1-r,r,1u,c);
}
uint32_t OLED_Isqrt(uint32_t n)
{
    uint32_t res=0u,bit=1uL<<30;
    while(bit>n) { bit>>=2; }
    while(bit) { if(n>=res+bit) { n-=res+bit; res=(res>>1)+bit; } else { res>>=1; } bit>>=2; }
    return res;
}
void OLED_DrawCircle(int32_t cx,int32_t cy,int32_t r,uint8_t c)
{
    int32_t x=r,y=0,d=1-r;
    if(r<0) { return; }
    while(x>=y) {
        OLED_DrawPixel(cx+x,cy+y,c); OLED_DrawPixel(cx+y,cy+x,c); OLED_DrawPixel(cx-y,cy+x,c); OLED_DrawPixel(cx-x,cy+y,c);
        OLED_DrawPixel(cx-x,cy-y,c); OLED_DrawPixel(cx-y,cy-x,c); OLED_DrawPixel(cx+y,cy-x,c); OLED_DrawPixel(cx+x,cy-y,c);
        y++; if(d<0) { d+=2*y+1; } else { x--; d+=2*(y-x)+1; }
    }
}
void OLED_FillCircle(int32_t cx,int32_t cy,int32_t r,uint8_t c)
{
    int32_t dy,w;
    if(r<0) { return; }
    for(dy=-r;dy<=r;dy++) { if((uint32_t)(cy+dy)>=64u) { continue; } w=(int32_t)OLED_Isqrt((uint32_t)(r*r-dy*dy)); OLED_DrawHLine(cx-w,cy+dy,2*w+1,c); }
}
void OLED_FillDiscQ8(int32_t xq,int32_t yq,int32_t rq,uint8_t c)
{
    int32_t re=rq+128,re2=re*re,px0=(xq-re)>>8,px1=(xq+re)>>8,py0=(yq-re)>>8,py1=(yq+re)>>8;
    int32_t px,py,dx,dy,dy2;
    for(py=py0;py<=py1;py++) {
        if((uint32_t)py>=64u) { continue; } dy=py*256+128-yq; dy2=dy*dy;
        for(px=px0;px<=px1;px++) { dx=px*256+128-xq; if(dx*dx+dy2<=re2) { OLED_DrawPixel(px,py,c); } }
    }
}
void OLED_DrawBrushSegQ8(int32_t x0q,int32_t y0q,int32_t r0q,int32_t x1q,int32_t y1q,int32_t r1q,uint8_t c)
{
    int32_t dx=x1q-x0q,dy=y1q-y0q,adx=(dx<0)?-dx:dx,ady=(dy<0)?-dy:dy,n=((adx>ady)?adx:ady)>>7,i;
    if(n<1) { n=1; }
    for(i=0;i<=n;i++) { OLED_FillDiscQ8(x0q+dx*i/n,y0q+dy*i/n,r0q+(r1q-r0q)*i/n,c); }
}
void OLED_DrawThickLine(int32_t x0,int32_t y0,int32_t x1,int32_t y1,int32_t r,uint8_t c)
{
    OLED_DrawBrushSegQ8(x0*256+128,y0*256+128,r*256,x1*256+128,y1*256+128,r*256,c);
}
static const uint8_t F6x8[95][5] = {
    {0x00, 0x00, 0x00, 0x00, 0x00},
    {0x00, 0x00, 0x5f, 0x00, 0x00},
    {0x00, 0x07, 0x00, 0x07, 0x00},
    {0x14, 0x7f, 0x14, 0x7f, 0x14},
    {0x24, 0x2a, 0x7f, 0x2a, 0x12},
    {0x23, 0x13, 0x08, 0x64, 0x62},
    {0x36, 0x49, 0x55, 0x22, 0x50},
    {0x00, 0x05, 0x03, 0x00, 0x00},
    {0x00, 0x1c, 0x22, 0x41, 0x00},
    {0x00, 0x41, 0x22, 0x1c, 0x00},
    {0x14, 0x08, 0x3e, 0x08, 0x14},
    {0x08, 0x08, 0x3e, 0x08, 0x08},
    {0x00, 0x50, 0x30, 0x00, 0x00},
    {0x08, 0x08, 0x08, 0x08, 0x08},
    {0x00, 0x60, 0x60, 0x00, 0x00},
    {0x20, 0x10, 0x08, 0x04, 0x02},
    {0x3e, 0x51, 0x49, 0x45, 0x3e},
    {0x00, 0x42, 0x7f, 0x40, 0x00},
    {0x42, 0x61, 0x51, 0x49, 0x46},
    {0x21, 0x41, 0x45, 0x4b, 0x31},
    {0x18, 0x14, 0x12, 0x7f, 0x10},
    {0x27, 0x45, 0x45, 0x45, 0x39},
    {0x3c, 0x4a, 0x49, 0x49, 0x30},
    {0x01, 0x71, 0x09, 0x05, 0x03},
    {0x36, 0x49, 0x49, 0x49, 0x36},
    {0x06, 0x49, 0x49, 0x29, 0x1e},
    {0x00, 0x36, 0x36, 0x00, 0x00},
    {0x00, 0x56, 0x36, 0x00, 0x00},
    {0x08, 0x14, 0x22, 0x41, 0x00},
    {0x14, 0x14, 0x14, 0x14, 0x14},
    {0x00, 0x41, 0x22, 0x14, 0x08},
    {0x02, 0x01, 0x51, 0x09, 0x06},
    {0x32, 0x49, 0x79, 0x41, 0x3e},
    {0x7e, 0x11, 0x11, 0x11, 0x7e},
    {0x7f, 0x49, 0x49, 0x49, 0x36},
    {0x3e, 0x41, 0x41, 0x41, 0x22},
    {0x7f, 0x41, 0x41, 0x22, 0x1c},
    {0x7f, 0x49, 0x49, 0x49, 0x41},
    {0x7f, 0x09, 0x09, 0x09, 0x01},
    {0x3e, 0x41, 0x49, 0x49, 0x7a},
    {0x7f, 0x08, 0x08, 0x08, 0x7f},
    {0x00, 0x41, 0x7f, 0x41, 0x00},
    {0x20, 0x40, 0x41, 0x3f, 0x01},
    {0x7f, 0x08, 0x14, 0x22, 0x41},
    {0x7f, 0x40, 0x40, 0x40, 0x40},
    {0x7f, 0x02, 0x0c, 0x02, 0x7f},
    {0x7f, 0x04, 0x08, 0x10, 0x7f},
    {0x3e, 0x41, 0x41, 0x41, 0x3e},
    {0x7f, 0x09, 0x09, 0x09, 0x06},
    {0x3e, 0x41, 0x51, 0x21, 0x5e},
    {0x7f, 0x09, 0x19, 0x29, 0x46},
    {0x46, 0x49, 0x49, 0x49, 0x31},
    {0x01, 0x01, 0x7f, 0x01, 0x01},
    {0x3f, 0x40, 0x40, 0x40, 0x3f},
    {0x1f, 0x20, 0x40, 0x20, 0x1f},
    {0x3f, 0x40, 0x38, 0x40, 0x3f},
    {0x63, 0x14, 0x08, 0x14, 0x63},
    {0x07, 0x08, 0x70, 0x08, 0x07},
    {0x61, 0x51, 0x49, 0x45, 0x43},
    {0x00, 0x7f, 0x41, 0x41, 0x00},
    {0x02, 0x04, 0x08, 0x10, 0x20},
    {0x00, 0x41, 0x41, 0x7f, 0x00},
    {0x04, 0x02, 0x01, 0x02, 0x04},
    {0x40, 0x40, 0x40, 0x40, 0x40},
    {0x00, 0x01, 0x02, 0x04, 0x00},
    {0x20, 0x54, 0x54, 0x54, 0x78},
    {0x7f, 0x48, 0x44, 0x44, 0x38},
    {0x38, 0x44, 0x44, 0x44, 0x20},
    {0x38, 0x44, 0x44, 0x48, 0x7f},
    {0x38, 0x54, 0x54, 0x54, 0x18},
    {0x08, 0x7e, 0x09, 0x01, 0x02},
    {0x0c, 0x52, 0x52, 0x52, 0x3e},
    {0x7f, 0x08, 0x04, 0x04, 0x78},
    {0x00, 0x44, 0x7d, 0x40, 0x00},
    {0x20, 0x40, 0x44, 0x3d, 0x00},
    {0x7f, 0x10, 0x28, 0x44, 0x00},
    {0x00, 0x41, 0x7f, 0x40, 0x00},
    {0x7c, 0x04, 0x18, 0x04, 0x78},
    {0x7c, 0x08, 0x04, 0x04, 0x78},
    {0x38, 0x44, 0x44, 0x44, 0x38},
    {0x7c, 0x14, 0x14, 0x14, 0x08},
    {0x08, 0x14, 0x14, 0x18, 0x7c},
    {0x7c, 0x08, 0x04, 0x04, 0x08},
    {0x48, 0x54, 0x54, 0x54, 0x20},
    {0x04, 0x3f, 0x44, 0x40, 0x20},
    {0x3c, 0x40, 0x40, 0x20, 0x7c},
    {0x1c, 0x20, 0x40, 0x20, 0x1c},
    {0x3c, 0x40, 0x30, 0x40, 0x3c},
    {0x44, 0x28, 0x10, 0x28, 0x44},
    {0x0c, 0x50, 0x50, 0x50, 0x3c},
    {0x44, 0x64, 0x54, 0x4c, 0x44},
    {0x00, 0x08, 0x36, 0x41, 0x00},
    {0x00, 0x00, 0x7f, 0x00, 0x00},
    {0x00, 0x41, 0x36, 0x08, 0x00},
    {0x08, 0x08, 0x2a, 0x1c, 0x08}
};

static const uint8_t F3x5[10][3] = {
    {0x1f, 0x11, 0x1f},
    {0x12, 0x1f, 0x10},
    {0x1d, 0x15, 0x17},
    {0x15, 0x15, 0x1f},
    {0x07, 0x04, 0x1f},
    {0x17, 0x15, 0x1d},
    {0x1f, 0x15, 0x1d},
    {0x01, 0x01, 0x1f},
    {0x1f, 0x15, 0x1f},
    {0x17, 0x15, 0x1f}
};

void OLED_ShowChar(int32_t x,int32_t y,char ch,uint8_t c)
{
    uint8_t col,row,idx,bits;
    if((uint8_t)ch<32u || (uint8_t)ch>126u) { ch='?'; } idx=(uint8_t)(ch-32);
    for(col=0u;col<5u;col++) { bits=F6x8[idx][col]; for(row=0u;row<8u;row++) { if(bits & (1u<<row)) { OLED_DrawPixel(x+col,y+row,c); } } }
}
void OLED_ShowString(int32_t x,int32_t y,const char *s,uint8_t c) { while(*s) { OLED_ShowChar(x,y,*s++,c); x+=6; } }
void OLED_DrawTinyDigit(int32_t x,int32_t y,uint8_t d,uint8_t c)
{
    uint8_t col,row;
    if(d>9u) { return; }
    for(col=0u;col<3u;col++) { for(row=0u;row<5u;row++) { if(F3x5[d][col] & (1u<<row)) { OLED_DrawPixel(x+col,y+row,c); } } }
}
void OLED_DrawTinyNum2(int32_t cx,int32_t cy,uint8_t v,uint8_t c) { OLED_DrawTinyDigit(cx-3,cy-2,(uint8_t)(v/10u),c); OLED_DrawTinyDigit(cx+1,cy-2,(uint8_t)(v%10u),c); }
void OLED_DrawTinyNum(int32_t cx,int32_t cy,uint8_t v,uint8_t c) { if(v>=10u) { OLED_DrawTinyNum2(cx,cy,v,c); } else { OLED_DrawTinyDigit(cx-1,cy-2,v,c); } }
