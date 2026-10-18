
/* V15. Preserve boot, floating dial, black-hole, and countdown state machine.
 * TIM2 is polled, so the project does not add duplicate interrupt handlers. */
#include "main.h"
volatile uint32_t system_tick=0u;
#define USE_BUILD_TIME 0
#define INIT_YEAR 2026
#define INIT_MONTH 5
#define INIT_DAY 17
#define INIT_HOUR 23
#define INIT_MIN 56
#define INIT_SEC 18
#define BUILD_TIME_OFFSET 20u
#define WRITE_TOTAL_MS 5000u
#define WRITE_PAUSE_MS 100u
#define HOLD_MS 600u
#define IRIS_MS 500u
#define BOOT_DONE_MS 6100u
#define MS_PER_DAY 86400000uL
#define DIAL_CX 64
#define DIAL_CY 32
#define CXQ 16512
#define CYQ 8320
/* V16: the hour hand and its flowing ring are gone; the hour is drawn as
 * black-hole style stroke digits opposite the minute hand. */
#define HOUR_TEXT_R 22
#define MIN_R0 3
#define MIN_R1 20
#define MIN_WO 512
#define MIN_WI 256
#define RING_M_L 23
#define RING_M_R 153
#define RING_M_WIN 5
#define RING_M_NUMOFF 5
#define SEC_LEN 29
#define SEC_TAIL 6
#define SEC_LUME0 26
#define SEC_LUME1 29
#define RING_S_L 29
#define RING_S_R 229
#define RING_S_WIN 3
#define RING_S_NUMOFF 6
#if (INIT_YEAR < 2000) || (INIT_YEAR > 2099) || (INIT_MONTH < 1) || (INIT_MONTH > 12) || (INIT_DAY < 1) || (INIT_DAY > 31)
#error "Invalid initial date"
#endif
#if (INIT_HOUR < 0) || (INIT_HOUR > 23) || (INIT_MIN < 0) || (INIT_MIN > 59) || (INIT_SEC < 0) || (INIT_SEC > 59)
#error "Invalid initial time"
#endif
#if ((INIT_MONTH==4)||(INIT_MONTH==6)||(INIT_MONTH==9)||(INIT_MONTH==11)) && (INIT_DAY>30)
#error "Invalid 30-day month date"
#endif
#if (INIT_MONTH==2) && ((INIT_DAY>29)||((INIT_DAY==29)&&((INIT_YEAR%4)!=0)))
#error "Invalid February date"
#endif
typedef struct { uint16_t year; uint8_t month,day,wday; } DateT;
static DateT s_date;
static uint8_t Date_IsLeap(uint16_t y) { return (uint8_t)(((y%4u==0u)&&(y%100u!=0u))||(y%400u==0u)); }
/* V26: the date helpers must be TOTAL functions. The new 年月日 editor can
 * hand over any value while the user is flipping (月 0, 日 32, 闰年 2月29日 ...),
 * and the old code indexed dim[m-1] / t[m-1] directly: with m==0 that reads the
 * byte *before* the table in Flash (or the whole array for m>12). Clamping
 * first makes every input defined; for valid dates the result is unchanged. */
#define DATE_YEAR_MIN 2000u
#define DATE_YEAR_MAX 2099u
static uint8_t Date_ClampMonth(uint8_t m) { if(m<1u) { return 1u; } if(m>12u) { return 12u; } return m; }
static uint16_t Date_ClampYear(uint16_t y)
{
    if(y<DATE_YEAR_MIN) { return DATE_YEAR_MIN; }
    if(y>DATE_YEAR_MAX) { return DATE_YEAR_MAX; }
    return y;
}
static uint8_t Date_DaysIn(uint16_t y,uint8_t m)
{
    static const uint8_t dim[12]={31,28,31,30,31,30,31,31,30,31,30,31};
    m=Date_ClampMonth(m);
    return (m==2u && Date_IsLeap(y))?29u:dim[m-1u];
}
static uint8_t Date_Weekday(uint16_t y,uint8_t m,uint8_t d)
{
    static const uint8_t t[12]={0,3,2,5,0,3,5,1,4,6,2,4};
    uint32_t yy=y;
    m=Date_ClampMonth(m);
    if(m<3u) { yy--; }
    return (uint8_t)((yy+yy/4u-yy/100u+yy/400u+t[m-1u]+d)%7u);
}
/* A day that does not exist in the month is clamped (2月30日 -> 2月28/29日). */
static uint8_t Date_ClampDay(uint16_t y,uint8_t m,uint8_t d)
{
    uint8_t dim=Date_DaysIn(y,m);
    if(d<1u) { return 1u; }
    if(d>dim) { return dim; }
    return d;
}
static void Date_Set(uint16_t y,uint8_t m,uint8_t d)
{
    y=Date_ClampYear(y); m=Date_ClampMonth(m); d=Date_ClampDay(y,m,d);
    s_date.year=y; s_date.month=m; s_date.day=d; s_date.wday=Date_Weekday(y,m,d);
}
static void Date_NextDay(void)
{
    s_date.day++;
    if(s_date.day>Date_DaysIn(s_date.year,s_date.month)) {
        s_date.day=1u; s_date.month++;
        if(s_date.month>12u) { s_date.month=1u; s_date.year++; }
    }
    s_date.wday=(uint8_t)((s_date.wday+1u)%7u);
}
static uint16_t s_lastCnt=0u;
static uint32_t s_tenth=0u,s_ms=0u,s_dayMs=0u;
static void Tick(void)
{
    uint16_t now=(uint16_t)TIM2->CNT;
    uint32_t tenths=s_tenth+(uint32_t)(uint16_t)(now-s_lastCnt),inc=tenths/10u;
    s_lastCnt=now; s_tenth=tenths%10u; s_ms+=inc; s_dayMs+=inc;
    if(s_dayMs>=MS_PER_DAY) { s_dayMs-=MS_PER_DAY; Date_NextDay(); }
}
static uint32_t Millis(void) { Tick(); return s_ms; }
static uint32_t Clock_MsOfDay(void) { Tick(); return s_dayMs; }
static void TimeBase_Init(void)
{
    RCC->APB1ENR |= RCC_APB1ENR_TIM2EN;
    TIM2->CR1=0u; TIM2->PSC=7199u; TIM2->ARR=0xFFFFu; TIM2->CNT=0u;
    TIM2->EGR=TIM_EGR_UG; TIM2->DIER=0u; TIM2->CR1=TIM_CR1_CEN;
    s_lastCnt=(uint16_t)TIM2->CNT; s_tenth=s_ms=s_dayMs=0u;
    Date_Set(INIT_YEAR,INIT_MONTH,INIT_DAY);
}
void Clock_SetTime(uint8_t h,uint8_t m,uint8_t s)
{
    if(h>=24u || m>=60u || s>=60u) { return; }
    Tick(); s_dayMs=((uint32_t)h*3600u+(uint32_t)m*60u+s)*1000u;
}
/* V26: setting the date and the time must be ONE atomic operation.
 * Clock_SetTime() starts with Tick(), and Tick() is what advances the date at
 * midnight. A caller that did  Date_Set(...); Clock_SetTime(...)  could
 * therefore have its new date rolled forward to the next day: open the editor
 * at 23:59:59.9, press PA7 a moment later, and the date you just chose becomes
 * tomorrow. This version ticks exactly once and then writes both fields, so the
 * only thing that can happen in between is what the hardware already did. */
void Clock_SetDateTime(uint16_t y,uint8_t mo,uint8_t d,uint8_t h,uint8_t mi,uint8_t s)
{
    if(h>=24u || mi>=60u || s>=60u) { return; }
    Tick();                                   /* absorb elapsed time first   */
    Date_Set(y,mo,d);                         /* then overwrite the calendar */
    s_dayMs=((uint32_t)h*3600u+(uint32_t)mi*60u+s)*1000u;
}
#if defined(FW_SIMULATOR)
/* V26: date probes for the automated tests.  A 32-bit word cannot carry
 * year+month+day+weekday, so the weekday travels in its own export. */
__attribute__((export_name("sim_date_get"))) uint32_t Sim_DateGet(void)
{
    return (uint32_t)s_date.year | ((uint32_t)s_date.month<<16) | ((uint32_t)s_date.day<<24);
}
__attribute__((export_name("sim_date_wday"))) uint32_t Sim_DateWday(void) { return (uint32_t)s_date.wday; }
__attribute__((export_name("sim_date_set"))) uint32_t Sim_DateSet(uint32_t packed)
{
    Date_Set((uint16_t)(packed & 0xFFFFu),(uint8_t)((packed>>16)&0xFFu),(uint8_t)((packed>>24)&0xFFu));
    return Sim_DateGet();
}
/* Non-atomic variant: exactly what the naive call order would do. */
__attribute__((export_name("sim_set_naive"))) uint32_t Sim_SetNaive(uint32_t packed,uint32_t hms)
{
    Date_Set((uint16_t)(packed & 0xFFFFu),(uint8_t)((packed>>16)&0xFFu),(uint8_t)((packed>>24)&0xFFu));
    Clock_SetTime((uint8_t)(hms>>16),(uint8_t)((hms>>8)&0xFFu),(uint8_t)(hms&0xFFu));
    return Sim_DateGet();
}
__attribute__((export_name("sim_set_datetime"))) uint32_t Sim_SetDateTime(uint32_t packed,uint32_t hms)
{
    Clock_SetDateTime((uint16_t)(packed & 0xFFFFu),(uint8_t)((packed>>16)&0xFFu),(uint8_t)((packed>>24)&0xFFu),
                      (uint8_t)(hms>>16),(uint8_t)((hms>>8)&0xFFu),(uint8_t)(hms&0xFFu));
    return Sim_DateGet();
}
__attribute__((export_name("sim_day_ms"))) uint32_t Sim_DayMs(void) { Tick(); return s_dayMs; }
#endif

#if USE_BUILD_TIME
static void Date_FromBuild(void)
{
    static const char mon[12][4]={"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
    const char *s=__DATE__;
    uint8_t m=1u,d,i;
    uint16_t y;
    for(i=0u;i<12u;i++) { if(s[0]==mon[i][0] && s[1]==mon[i][1] && s[2]==mon[i][2]) { m=(uint8_t)(i+1u); break; } }
    d=(uint8_t)(((s[4]==' ')?0:(s[4]-'0'))*10+s[5]-'0');
    y=(uint16_t)((s[7]-'0')*1000+(s[8]-'0')*100+(s[9]-'0')*10+s[10]-'0'); Date_Set(y,m,d);
}
static void Clock_SeedFromBuild(void)
{
    const char *t=__TIME__;
    uint32_t total=(uint32_t)((t[0]-'0')*10+t[1]-'0')*3600u+(uint32_t)((t[3]-'0')*10+t[4]-'0')*60u+(uint32_t)((t[6]-'0')*10+t[7]-'0');
    total+=BUILD_TIME_OFFSET;
    if(total>=86400u) { Date_NextDay(); total-=86400u; }
    Clock_SetTime((uint8_t)(total/3600u),(uint8_t)((total/60u)%60u),(uint8_t)(total%60u));
}
#endif
/* This table is identical to the browser's Q14 table, stored in Flash.
 * The MCU no longer needs startup sin()/floor() or a writable 2KiB sine table. */
/* V30: 正弦表 1024 -> 512 项 (省 1024 B Flash)。相邻点间隔 0.7 度,
 * 线性插值的最大误差 < 1e-4, 远小于一个像素, 所以秒表指针 / 烟花弹道 /
 * 黑洞环的观感与 1024 项版本完全一致。 */
/* V32: 正弦表只存 1/4 周期 (0..90 度, 129 项, 258 B)。整圆 512 项满足
 * s[256-k]=s[k] 且 s[k+256]=-s[k] (已逐项验证 512 项完全一致), 折叠即可还原,
 * 输出与 V31 的 512 项表逐位相同, 省出 766 B Flash 给"访问"页的二维码。 */
static const int16_t s_sin[129] = {
    0, 201, 402, 603, 804, 1005, 1205, 1406, 1606, 1806, 2006, 2205, 2404, 2603, 2801, 2999,
    3196, 3393, 3590, 3786, 3981, 4176, 4370, 4563, 4756, 4948, 5139, 5330, 5520, 5708, 5897, 6084,
    6270, 6455, 6639, 6823, 7005, 7186, 7366, 7545, 7723, 7900, 8076, 8250, 8423, 8595, 8765, 8935,
    9102, 9269, 9434, 9598, 9760, 9921, 10080, 10238, 10394, 10549, 10702, 10853, 11003, 11151, 11297, 11442,
    11585, 11727, 11866, 12004, 12140, 12274, 12406, 12537, 12665, 12792, 12916, 13039, 13160, 13279, 13395, 13510,
    13623, 13733, 13842, 13949, 14053, 14155, 14256, 14354, 14449, 14543, 14635, 14724, 14811, 14896, 14978, 15059,
    15137, 15213, 15286, 15357, 15426, 15493, 15557, 15619, 15679, 15736, 15791, 15843, 15893, 15941, 15986, 16029,
    16069, 16107, 16143, 16176, 16207, 16235, 16261, 16284, 16305, 16324, 16340, 16353, 16364, 16373, 16379, 16383,
    16384
};
static int32_t Sin_Q(uint32_t i)
{
    uint32_t n=i&256u;                      /* 下半圈取负 */
    int32_t v;
    i&=255u; if(i>128u) { i=256u-i; }
    v=s_sin[i];
    return n?-v:v;
}


int32_t Sin14(uint16_t a)
{
    uint32_t idx=a>>7;                      /* 512 项 */
    int32_t frac=(a>>1)&63;                 /* 0..63 细分 */
    int32_t s0=Sin_Q(idx),s1=Sin_Q(idx+1u);
    return s0+(((s1-s0)*frac)>>6);
}
int32_t Cos14(uint16_t a) { return Sin14((uint16_t)(a+16384u)); }
static void Polar(int32_t cxq,int32_t cyq,int32_t rq,uint16_t a,int32_t *xq,int32_t *yq)
{
    *xq=cxq+((rq*Sin14(a))>>14); *yq=cyq-((rq*Cos14(a))>>14);
}
typedef struct { int8_t x,y,w; } BrushPt;
typedef struct { const BrushPt *pts; uint8_t n; int8_t ox,oy; } Stroke;
static const BrushPt s_brush0[4] = {{18,3,3},{13,10,4},{8,17,3},{4,23,1}};
static const BrushPt s_brush1[4] = {{11,14,2},{12,17,4},{11,33,3},{11,48,4}};
static const BrushPt s_brush2[4] = {{36,2,1},{33,6,3},{29,10,2},{26,13,1}};
static const BrushPt s_brush3[5] = {{26,17,2},{36,16,3},{46,15,4},{46,19,3},{40,23,1}};
static const BrushPt s_brush4[5] = {{36,17,3},{36,27,4},{36,43,3},{34,50,3},{28,46,1}};
static const BrushPt s_brush5[3] = {{30,34,1},{26,40,3},{23,44,3}};
static const BrushPt s_brush6[3] = {{42,34,1},{45,39,3},{47,43,3}};
static const BrushPt s_brush7[5] = {{18,4,3},{12,14,4},{6,26,3},{9,31,3},{14,36,4}};
static const BrushPt s_brush8[4] = {{20,6,2},{14,20,4},{8,34,3},{2,50,1}};
static const BrushPt s_brush9[3] = {{2,36,3},{12,32,3},{24,28,1}};
static const BrushPt s_brush10[5] = {{28,9,2},{38,8,3},{48,8,4},{45,14,3},{34,26,1}};
static const BrushPt s_brush11[5] = {{36,24,3},{38,32,4},{38,44,3},{36,50,3},{30,46,1}};
static const BrushPt s_brush12[3] = {{28,38,2},{38,37,3},{49,36,4}};
static const Stroke s_strokes[13] = {
    {s_brush0,4,12,5},
    {s_brush1,4,12,5},
    {s_brush2,4,12,5},
    {s_brush3,5,12,5},
    {s_brush4,5,12,5},
    {s_brush5,3,12,5},
    {s_brush6,3,12,5},
    {s_brush7,5,66,5},
    {s_brush8,4,66,5},
    {s_brush9,3,66,5},
    {s_brush10,5,66,5},
    {s_brush11,5,66,5},
    {s_brush12,3,66,5}
};
#define STROKE_N 13u
static uint32_t s_strokeLen16[13],s_strokeStart[13],s_strokeDur[13];
static uint32_t Stroke_Len16(const Stroke *s)
{
    uint32_t sum=0u;
    uint8_t i;
    for(i=0u;i+1u<s->n;i++) { int32_t dx=s->pts[i+1u].x-s->pts[i].x,dy=s->pts[i+1u].y-s->pts[i].y; sum+=OLED_Isqrt((uint32_t)(dx*dx+dy*dy)<<8); }
    return sum;
}
static void Write_Init(void)
{
    uint32_t total=0u,drawMs,acc=0u;
    uint8_t i;
    for(i=0u;i<13u;i++) { s_strokeLen16[i]=Stroke_Len16(&s_strokes[i]); total+=s_strokeLen16[i]; }
    drawMs=WRITE_TOTAL_MS-12u*WRITE_PAUSE_MS;
    for(i=0u;i<13u;i++) { s_strokeDur[i]=drawMs*s_strokeLen16[i]/total; if(!s_strokeDur[i]) { s_strokeDur[i]=1u; } s_strokeStart[i]=acc; acc+=s_strokeDur[i]+WRITE_PAUSE_MS; }
    s_strokeDur[12]=WRITE_TOTAL_MS-s_strokeStart[12];
}
static void Write_DrawStroke(const Stroke *s,uint32_t target16)
{
    uint32_t acc=0u;
    uint8_t i;
    for(i=0u;i+1u<s->n;i++) {
        const BrushPt *a=&s->pts[i],*b=&s->pts[i+1u];
        int32_t ax=(s->ox+a->x)*256+128,ay=(s->oy+a->y)*256+128,ar=a->w*128;
        int32_t bx=(s->ox+b->x)*256+128,by=(s->oy+b->y)*256+128,br=b->w*128;
        int32_t dx=b->x-a->x,dy=b->y-a->y,adx=(dx<0)?-dx:dx,ady=(dy<0)?-dy:dy,n=((adx>ady)?adx:ady)*2,imax,k;
        uint32_t seg=OLED_Isqrt((uint32_t)(dx*dx+dy*dy)<<8);
        uint8_t done=0u;
        if(!seg) { continue; } if(n<1) { n=1; }
        if(acc+seg<=target16) { imax=n; acc+=seg; } else { imax=(int32_t)((uint32_t)n*(target16-acc)/seg); done=1u; }
        for(k=0;k<=imax;k++) { OLED_FillDiscQ8(ax+(bx-ax)*k/n,ay+(by-ay)*k/n,ar+(br-ar)*k/n,OLED_WHITE); }
        if(done) { return; }
    }
}
static void Write_Render(uint32_t t)
{
    uint8_t i;
    OLED_Clear();
    for(i=0u;i<13u;i++) {
        if(t>=s_strokeStart[i]+s_strokeDur[i]) { Write_DrawStroke(&s_strokes[i],0xFFFFFFFFuL); }
        else if(t>=s_strokeStart[i]) { uint32_t target=s_strokeLen16[i]*(t-s_strokeStart[i])/s_strokeDur[i]; Write_DrawStroke(&s_strokes[i],target); break; }
        else { break; }
    }
}
static void DrawRing(uint16_t handA,int32_t L,int32_t R,int32_t cur,int32_t win,int32_t numOff)
{
    int32_t ccx,ccy,k,x0,y0,x1,y1;
    Polar(CXQ,CYQ,(L-R)*256,handA,&ccx,&ccy);
    for(k=-win*4;k<=win*4;k++) {
        int32_t u=cur*4+k,len=(k%4==0)?4:2;
        uint16_t a;
        u%=240; if(u<0) { u+=240; } a=(uint16_t)(u*65536L/240);
        Polar(ccx,ccy,R*256,a,&x0,&y0); Polar(ccx,ccy,(R+len)*256,a,&x1,&y1);
        OLED_DrawLine(x0>>8,y0>>8,x1>>8,y1>>8,OLED_WHITE);
    }
    for(k=-win;k<=win;k++) {
        int32_t v=cur+k;
        uint16_t a;
        v%=60; if(v<0) { v+=60; } a=(uint16_t)(v*4*65536L/240);
        Polar(ccx,ccy,(R-numOff)*256,a,&x0,&y0); OLED_DrawTinyNum2(x0>>8,y0>>8,(uint8_t)v,OLED_WHITE);
    }
}
static void DrawPillHand(uint16_t a,int32_t r0,int32_t r1,int32_t wo,int32_t wi)
{
    int32_t x0,y0,x1,y1;
    Polar(CXQ,CYQ,r0*256,a,&x0,&y0); Polar(CXQ,CYQ,r1*256,a,&x1,&y1); OLED_DrawBrushSegQ8(x0,y0,wo,x1,y1,wo,OLED_WHITE);
    Polar(CXQ,CYQ,(r0+1)*256,a,&x0,&y0); Polar(CXQ,CYQ,(r1-1)*256,a,&x1,&y1); OLED_DrawBrushSegQ8(x0,y0,wi,x1,y1,wi,OLED_BLACK);
}
static void DrawSecondHand(uint16_t a)
{
    int32_t x0,y0,x1,y1;
    Polar(CXQ,CYQ,-SEC_TAIL*256,a,&x0,&y0); Polar(CXQ,CYQ,SEC_LEN*256,a,&x1,&y1); OLED_DrawLine(x0>>8,y0>>8,x1>>8,y1>>8,OLED_WHITE);
    Polar(CXQ,CYQ,SEC_LUME0*256,a,&x0,&y0); Polar(CXQ,CYQ,SEC_LUME1*256,a,&x1,&y1); OLED_DrawBrushSegQ8(x0,y0,128,x1,y1,128,OLED_WHITE);
}
/* V17 dial: the hour follows the minute hand's opposite side, but Scene.c
 * searches the rendered OLED pixels and moves it into a collision-free dark gap. */
static void DrawHourText(uint16_t minA,uint32_t hour,uint32_t nowMs)
{
    int32_t x0,y0;
    /* This is a moving preference, not a fixed placement: the stroke-font
     * renderer validates every candidate against both flowing dials and hands. */
    Polar(CXQ,CYQ,HOUR_TEXT_R*256,(uint16_t)(minA+32768u),&x0,&y0);
    Scene_DrawStrokeNum(x0>>8,y0>>8,(uint8_t)(hour%24u),nowMs);
}
static void Face_Draw(uint32_t msOfDay,uint32_t nowMs)
{
    uint32_t msIn=msOfDay%1000u,sec=(msOfDay/1000u)%60u,mnt=(msOfDay/60000u)%60u,hour=(msOfDay/3600000u)%24u;
    uint16_t secA=(uint16_t)((sec*1000u+msIn)*65536u/60000u);
    uint16_t minA=(uint16_t)(((mnt*60u+sec)*10u+msIn/100u)*65536u/36000u);
    DrawRing(minA,RING_M_L,RING_M_R,(int32_t)mnt,RING_M_WIN,RING_M_NUMOFF);
    DrawRing(secA,RING_S_L,RING_S_R,(int32_t)sec,RING_S_WIN,RING_S_NUMOFF);
    DrawPillHand(minA,MIN_R0,MIN_R1,MIN_WO,MIN_WI); DrawSecondHand(secA);
    /* Draw the hub before the moving hour label so it participates in its
     * framebuffer collision test and the label can never overwrite the pin. */
    OLED_FillCircle(64,32,2,OLED_BLACK); OLED_DrawCircle(64,32,2,OLED_WHITE); OLED_DrawPixel(64,32,OLED_WHITE);
    /* The expanding boot iris still contains signature pixels outside its clip;
     * start collision-aware hour rendering once the complete dial is visible. */
    if(nowMs>=BOOT_DONE_MS) { DrawHourText(minA,hour,nowMs); }
}
#define VIEW_DIAL 0u
#define VIEW_HOLE 1u
#define TIMER_IDLE 0u
#define TIMER_SET 1u
#define TIMER_RUN 2u
#define TIMER_ALARM 3u
#define TIMER_VIEW 4u
/* V18 top-level screens. PA5 no longer jumps straight into the countdown: it
 * opens SCREEN_MENU first, and the menu decides where to go. */
#define SCR_TIME 0u
#define SCR_MENU 1u
#define SCR_TIMER 2u
#define SCR_STOP 3u
#define MENU_SEL_BELL 0u
#define MENU_SEL_STOP 1u
#define SCR_MENU2 4u                                                   /* V23 设置2 */
#define SCR_GAME  5u                                                   /* V23 游戏(黑屏占位) */
#define SCR_CALC  6u                                                   /* V23 计算器 */
#define SCR_SET   7u                                                   /* V26 设置3: 齿轮/访问 */
#define SCR_ACC   8u                                                   /* V26 访问: 占位(功能后续开发) */
#define SCR_DATE  9u                                                   /* V26 日期/时间编辑器 */
#define SCR_TETRIS 10u                                                 /* V30 俄罗斯方块 */
#define SCR_JUMP  11u                                                  /* V30 跳一跳 */
/* V26 日期编辑器字段: 年 月 日 时 分 秒 星期 */
#define DE_F_YEAR 0u
#define DE_F_MON  1u
#define DE_F_DAY  2u
#define DE_F_HOUR 3u
#define DE_F_MIN  4u
#define DE_F_SEC  5u
#define DE_F_WK   6u
#define DE_F_NUM  7u
/* V27: PA4 在**可编辑**字段之间巡览 —— 星期由年月日推算, 不是可编辑项,
 * 所以它不在表里。表长与 DE_F_NUM 故意不同名, 避免以后误用 DE_F_NUM 遍历。 */
#define DE_F_SEL_NUM 6u
#define MENU2_SEL_CALC 0u
#define MENU2_SEL_GAME 1u
static uint8_t s_view=VIEW_DIAL,s_transOn=0u,s_transFrom=VIEW_DIAL,s_transTo=VIEW_HOLE,s_pending=0u,s_bootDone=0u;
static uint32_t s_transStart=0u;
static uint8_t s_timerMode=TIMER_IDLE,s_pa5BlockOn=0u,s_timerReturnView=VIEW_DIAL;
static uint8_t s_timerHour=0u,s_timerMinute=0u,s_timerSecond=0u,s_timerSelected=2u;
static uint32_t s_pa5BlockAt=0u,s_timerStart=0u,s_timerDuration=0u;
static uint8_t s_gramA[8][128],s_gramB[8][128];
/* V18 screen state + anti-mistouch lock */
static uint8_t s_screen=SCR_TIME,s_menuSel=MENU_SEL_BELL,s_stopFresh=0u;
static uint8_t s_setSel=MENU2_SEL_CALC;   /* V26 设置3 选中项: 0=齿轮 1=访问 */
/* V26 日期编辑器: 编辑副本, 只有 PA7 保存才写回系统时钟 */
static uint8_t  s_deSel=DE_F_YEAR;
/* PA4 巡览顺序: 年(0) 月(1) 日(2) 星期(6) 时(3) 分(4) 秒(5) */
static const uint8_t s_deSelOrder[DE_F_SEL_NUM]={DE_F_YEAR,DE_F_MON,DE_F_DAY,DE_F_HOUR,DE_F_MIN,DE_F_SEC};
static uint8_t  s_deIdx=0u;      /* s_deSel 在 s_deSelOrder 里的下标 */
static void De_Select(uint8_t field);   /* 定义在 Date_Enter 之后 */
static uint16_t s_deYear=0u;
static uint8_t  s_deMon=0u,s_deDay=0u,s_deHour=0u,s_deMin=0u,s_deSec=0u;
static uint8_t s_pa4BlockOn=0u;
static uint32_t s_pa4BlockAt=0u;
static uint8_t Boot_IsDone(uint32_t t) { if(t>=BOOT_DONE_MS) { s_bootDone=1u; } return s_bootDone; }
static void View_StartTrans(uint32_t now) { s_transFrom=s_view; s_transTo=(s_view==VIEW_DIAL)?VIEW_HOLE:VIEW_DIAL; s_transOn=1u; s_transStart=now; }
static uint32_t Timer_TotalSeconds(void) { return (uint32_t)s_timerHour*3600u+(uint32_t)s_timerMinute*60u+s_timerSecond; }
static void Timer_EnterSetup(void)
{
    if(s_timerSelected>2u) { s_timerSelected=0u; }   /* V27: Start 不可选中 */
    s_timerReturnView=s_transOn?s_transTo:s_view; s_view=s_timerReturnView; s_timerMode=TIMER_SET;
    s_timerHour=s_timerMinute=s_timerSecond=0u; s_timerSelected=2u; s_transOn=s_pending=0u; Alert_Stop();
    Scene_TimerPanelReset();   /* V20: the drum digits start settled */
}
/* V19: the panel edits one field at a time, but the value is a single
 * 时:分:秒 counter, so carries and borrows come out of plain arithmetic:
 *   - 秒 field: +-1 s   -> 59+1 rolls the minute up, 00-1 borrows from it
 *   - 分 field: +-60 s  -> minutes carry into the hour
 *   - 时 field: +-3600 s-> hours run 00..99 and wrap 99 -> 00
 * The whole counter wraps at 99:59:59 <-> 00:00:00, so rolling down from
 * 00:00:00 gives 99:59:59 (hour 00 -> 99) instead of sticking. */
static void Timer_Step(int8_t direction)
{
    uint32_t total=Timer_TotalSeconds();
    uint32_t step=(s_timerSelected==0u)?3600u:((s_timerSelected==1u)?60u:1u);
    uint32_t span=99u*3600u+59u*60u+59u+1u;      /* 360000 = 100 h in seconds */
    if(direction>0) { total+=step; if(total>=span) { total-=span; } }
    else if(direction<0) { if(total<step) { total+=span-step; } else { total-=step; } }
    s_timerHour=(uint8_t)(total/3600u);
    s_timerMinute=(uint8_t)((total/60u)%60u);
    s_timerSecond=(uint8_t)(total%60u);
}
static uint8_t Timer_Start(uint32_t now)
{
    uint32_t seconds=Timer_TotalSeconds();
    if(!seconds) { return 0u; }
    s_timerDuration=seconds*1000u; s_timerStart=now; s_timerMode=TIMER_RUN; s_view=s_timerReturnView; Buzzer_Off(); LED_On(); return 1u;
}
static void Timer_Expire(uint32_t now)
{
    s_timerMode=TIMER_ALARM; s_view=s_timerReturnView; s_transOn=s_pending=0u; Scene_AlarmBegin(now); Alert_Start(now);
}
static void Timer_Dismiss(void) { s_timerMode=TIMER_IDLE; s_view=s_timerReturnView; Alert_Stop(); }
static void Timer_CancelEarly(uint32_t now)
{
    s_timerMode=TIMER_IDLE; s_view=s_timerReturnView; s_pa5BlockOn=1u; s_pa5BlockAt=now; Buzzer_Off(); LED_Off();
}
static uint8_t Timer_Pa5Blocked(uint32_t now)
{
    if(!s_pa5BlockOn) { return 0u; }
    if((uint32_t)(now-s_pa5BlockAt)>=PA5_BLOCK_MS) { s_pa5BlockOn=0u; return 0u; } return 1u;
}
/* ---- V18 screen helpers -------------------------------------------------- */
/* Leave any sub-screen and land on the clock face. The 1 second PA4 lock is
 * armed here, so the very gesture that returned to the clock can never also
 * switch the clock program by accident. */
static void Screen_ToTime(uint32_t now)
{
    s_screen=SCR_TIME; s_pa4BlockOn=1u; s_pa4BlockAt=now;
}
static uint8_t Pa4_ViewBlocked(uint32_t now)
{
    if(!s_pa4BlockOn) { return 0u; }
    if((uint32_t)(now-s_pa4BlockAt)>=PA4_BLOCK_MS) { s_pa4BlockOn=0u; return 0u; }
    return 1u;
}
/* Enter the settings screen: always opens with 铃声 selected, and finishes any
 * half-played clock transition so the menu is never composed mid-morph. */
static void Menu_Enter(void)
{
    s_screen=SCR_MENU; s_menuSel=MENU_SEL_BELL;
    if(s_transOn) { s_transOn=0u; s_view=s_transTo; } s_pending=0u;
}
/* 秒表 is a black screen for now (后续再开发): clear once, then stop updating
 * the panel so the OLED really stays black until PA4 is held again. */
/* ---------------------------------------------------------------------------
 * V21 stopwatch engine (秒表)
 *  - time base: the same polled TIM2 (10 kHz = 0.1 ms per tick) as the clock,
 *    consumed as a counter delta so the millisecond display never drifts;
 *  - PA5: start -> pause -> (again) restart from zero -> pause ...
 *  - PA6: record a lap while running (100-lap ring buffer, software-unlimited);
 *  - PA7: browse the recorded laps (the marker of the row blinks), auto-return
 *    to "follow the newest" after SW_LAP_KEEP_MS;
 *  - 99:59.999 is the hard cap: the watch stops there instead of wrapping;
 *  - leaving the screen keeps the watch running (a real stopwatch does).
 * ------------------------------------------------------------------------- */
static uint8_t  s_swState=0u;              /* 0 idle, 1 running, 2 paused */
static uint32_t s_swMs=0u;                 /* accumulated milliseconds when running */
static uint32_t s_swTenth=0u;              /* 0.1 ms remainder carried between polls */
static uint16_t s_swLastCnt=0u;
static SwLapT  s_swLap[SW_LAP_MAX];
static uint16_t s_swLapTotal=0u;           /* laps ever recorded (lap numbers 1..n) */
static uint16_t s_swLapHead=0u;            /* ring index of the newest lap */
static uint16_t s_swLapStored=0u;          /* how many ring slots are valid */
static uint16_t s_swView=0u;               /* PA7 browsing: rows back from newest */
static uint32_t s_swViewAt=0u;
static uint8_t  s_swPa7Down=0u;
static uint32_t s_swPa7Next=0u;
static uint8_t  s_swDirtyAll=1u;           /* first frame after entering: full repaint */
/* Consume the TIM2 delta. Runs on every loop iteration, running or not, so the
 * counter snapshot is never stale after a pause. */
static void Sw_Tick(void)
{
    uint16_t now=(uint16_t)TIM2->CNT;
    uint32_t ticks=s_swTenth+(uint32_t)(uint16_t)(now-s_swLastCnt);
    s_swLastCnt=now;
    if(s_swState==1u) {
        uint32_t whole=ticks/10u;
        s_swTenth=ticks%10u;
        if(s_swMs+whole>=SW_MAX_MS) { s_swMs=SW_MAX_MS; s_swState=2u; s_swTenth=0u; }
        else { s_swMs+=whole; }
    } else { s_swTenth=0u; }
}
static void Stopwatch_Enter(void)
{
    s_screen=SCR_STOP; s_stopFresh=1u; s_swDirtyAll=1u;
    Scene_StopwatchReset();                /* next frame repaints the whole scene */
    s_swView=0u; s_swPa7Down=0u;
    s_swLastCnt=(uint16_t)TIM2->CNT;       /* resync the tick delta */
    if(s_transOn) { s_transOn=0u; s_view=s_transTo; } s_pending=0u;
}
/* PA5: run <-> pause, and from pause the next press restarts from zero. */
static void Stopwatch_Key5(void)
{
    if(s_swState==1u) { s_swState=2u; }
    else {
        if(s_swState==2u) {
            /* 从头开始: 归零, 并清空分圈记录(和参考视频里"重置"的行为一致) */
            s_swMs=0u; s_swLapTotal=0u; s_swLapHead=0u; s_swLapStored=0u;
            s_swView=0u; Scene_StopwatchReset();
        }
        s_swState=1u; s_swLastCnt=(uint16_t)TIM2->CNT; s_swTenth=0u;
    }
    s_swView=0u; s_swDirtyAll=1u;
}
static void Stopwatch_Lap(void)
{
    uint32_t prevTotal=0u;
    if(s_swState!=1u) { return; }            /* laps only while running */
    if(s_swLapStored) { prevTotal=s_swLap[s_swLapHead].totalMs; }
    s_swLapHead=(uint16_t)((s_swLapHead+1u)%SW_LAP_MAX);
    s_swLapTotal++;
    s_swLap[s_swLapHead].lapMs=s_swMs-prevTotal;
    s_swLap[s_swLapHead].totalMs=s_swMs;
    s_swLap[s_swLapHead].index=s_swLapTotal;
    if(s_swLapStored<SW_LAP_MAX) { s_swLapStored++; }
    s_swView=0u; s_swDirtyAll=1u;            /* the new lap is the newest row */
}
/* PA7: step the browsing cursor towards older laps; held down it repeats. */
static void Stopwatch_Browse(uint32_t now,uint8_t press,uint8_t down)
{
    uint8_t step=0u;
    if(press) { step=1u; s_swPa7Next=now+SW_LAP_REPEAT_MS; }
    else if(down) {
        if(!s_swPa7Down) { s_swPa7Next=now+SW_LAP_REPEAT_MS; }
        else if((int32_t)(now-s_swPa7Next)>=0) { step=1u; s_swPa7Next=now+SW_LAP_REPEAT_RATE; }
    }
    s_swPa7Down=down;
    if(step && s_swLapStored) {
        s_swView=(uint16_t)((s_swView+1u)%s_swLapStored);   /* wraps back to newest */
        s_swViewAt=now; s_swDirtyAll=1u;
    }
    if(s_swView && (uint32_t)(now-s_swViewAt)>=SW_LAP_KEEP_MS) { s_swView=0u; s_swDirtyAll=1u; }
}
#if defined(FW_SIMULATOR)
/* Simulator-only: force the watch into a known state so the tests can check
 * the display formatting and the 99-minute cap without waiting for it. */
__attribute__((export_name("sim_sw_set"))) void Sim_SwSet(uint32_t ms)
{
    s_swMs=(ms>SW_MAX_MS)?SW_MAX_MS:ms;
    s_swTenth=0u; s_swLastCnt=(uint16_t)TIM2->CNT;
    s_swDirtyAll=1u;
    if(s_swMs>=SW_MAX_MS) { s_swState=2u; }
}
__attribute__((export_name("sim_sw_state"))) uint32_t Sim_SwState(void)
{
    return (uint32_t)((s_swMs&0xFFFFFFu) | ((uint32_t)s_swState<<24) | ((uint32_t)s_swLapTotal<<26));
}
__attribute__((export_name("sim_sw_laps"))) uint32_t Sim_SwLaps(void)
{
    return (uint32_t)s_swLapStored | ((uint32_t)s_swLapHead<<8) | ((uint32_t)s_swView<<16);
}
__attribute__((export_name("sim_sw_lap_at"))) uint32_t Sim_SwLapAt(uint32_t logical)
{
    /* logical 0 = newest row, 1 = one older, ... (what the screen shows) */
    uint32_t idx;
    if(!s_swLapStored || logical>=s_swLapStored) { return 0xFFFFFFFFu; }
    idx=((uint32_t)s_swLapHead+SW_LAP_MAX-logical)%SW_LAP_MAX;
    return s_swLap[idx].totalMs;
}
__attribute__((export_name("sim_sw_lap_ms"))) uint32_t Sim_SwLapMs(uint32_t logical)
{
    uint32_t idx;
    if(!s_swLapStored || logical>=s_swLapStored) { return 0xFFFFFFFFu; }
    idx=((uint32_t)s_swLapHead+SW_LAP_MAX-logical)%SW_LAP_MAX;
    return s_swLap[idx].lapMs;
}
__attribute__((export_name("sim_sw_lap_index"))) uint32_t Sim_SwLapIndex(uint32_t logical)
{
    uint32_t idx;
    if(!s_swLapStored || logical>=s_swLapStored) { return 0xFFFFFFFFu; }
    idx=((uint32_t)s_swLapHead+SW_LAP_MAX-logical)%SW_LAP_MAX;
    return s_swLap[idx].index;
}
#endif

/* ===========================================================================
 * V23 计算器内核  +  设置2(计算器/游戏) 状态机
 * ---------------------------------------------------------------------------
 * 数值表示 (第一性原理: STM32F103 没有 FPU, 本工程全程零浮点, 所以计算器也
 * 绝不用 double/float —— 用袖珍计算器的经典表示法):
 *        value = m × 10^e ,  |m| ≤ 999,999,999  (9 位有效数字)
 *   - 加减: 指数对齐后用 int64 相加 (相差 > 9 个数量级的那一项对 9 位有效数字
 *     没有任何影响, 直接忽略 —— 这不是偷懒, 是数学上严格的);
 *   - 乘:   m1*m2 ≤ 1e18, int64 装得下, 精确;
 *   - 除:   (m1 × 1e9) / m2 ≤ 1e18, int64 装得下, 商保留 9~10 位有效数字;
 *   - 显示: 最多 8 位小数; 位数太多时自动按 9 位有效数字舍入并去掉尾部 0
 *     (这就是「省去一些小数位」).
 * 输入模型:
 *   - 整个计算器状态 = 一条表达式字符串 + 光标位置;
 *   - 「无限退回」(AC): 状态被设计成**按键日志的纯函数** —— 退回 = 日志长度减一
 *     后完整重放, 因此任何一步都能准确回到, 且行为完全确定 (测试可逐位验证).
 * =========================================================================== */
#define CALC_LOG_MAX  192
#define CALC_MANT_MAX 999999999LL

/* 6 行 × 4 列: 行 0 = [AC][实时显示行]; 行 1..4 = 16 个按键; 行 5 = [清空][ ][ ][ ]
 * (按键 id 与这张表都在 main.h 里声明, Scene.c 直接用同一张表绘图) */
const uint8_t s_calcKeys[6][4]={
    { CK_AC   , CK_NONE , CK_NONE , CK_NONE },
    { 7u      , 8u      , 9u      , CK_DIV  },
    { 4u      , 5u      , 6u      , CK_MUL  },
    { 1u      , 2u      , 3u      , CK_SUB  },
    { 0u      , CK_DOT , CK_EQ   , CK_ADD  },
    { CK_CLEAR, CK_NONE , CK_NONE , CK_NONE }
};

typedef struct { int32_t m; int8_t e; } CalcNum;

static uint8_t  s_menu2Sel=MENU2_SEL_CALC;
static uint8_t  s_gameFresh=0u;
static char     s_calcExpr[CALC_EXPR_MAX+1];
static uint8_t  s_calcLen=0u;
static uint8_t  s_calcCur=0u;
static uint8_t  s_calcIsResult=0u;
static uint8_t  s_calcSelR=4u,s_calcSelC=1u;        /* 默认选中 "0" */
static uint8_t  s_calcLog[CALC_LOG_MAX];          /* 每一步的按键 */
static uint8_t  s_calcLogCur[CALC_LOG_MAX];       /* 以及按下去那一刻的光标位置 */
static uint16_t s_calcLogN=0u;
static uint8_t  s_calcFresh=1u;                     /* 首帧整屏重画 */

static void Calc_StrCopy(char *d,const char *c) { while(*c) { *d++=*c++; } *d=0; }
static uint8_t Calc_StrLen(const char *c) { uint8_t n=0u; while(c[n]) { n++; } return n; }
static int64_t Calc_Pow10(int32_t n) { int64_t r=1; while(n>0) { r*=10LL; n--; } return r; }
static uint8_t Calc_NumDigits(uint32_t v) { uint8_t n=1u; while(v>=10u) { v/=10u; n++; } return n; }

/* 把 int64 尾数 + 十进制指数归一化成 CalcNum (四舍五入到 9 位有效数字) */
static uint8_t Calc_SetFrom(int64_t m,int32_t e,CalcNum *r)
{
    while(m>CALC_MANT_MAX || m<-CALC_MANT_MAX) { m=(m>=0)?((m+5)/10):((m-5)/10); e++; }
    if(e>40 || e<-45) { return 0u; }                 /* 指数越界 = 溢出 */
    r->m=(int32_t)m; r->e=(int8_t)e;
    return 1u;
}
static uint8_t Calc_AddSub(CalcNum a,char op,CalcNum b,CalcNum *r)
{
    int32_t d=(int32_t)a.e-(int32_t)b.e;
    int64_t ma=a.m,mb=b.m;
    if(op=='-') { mb=-mb; }
    if(d>9) { *r=a; return 1u; }                     /* 相差 10 个数量级: 小的不影响有效数字 */
    if(d<-9) { r->m=(int32_t)mb; r->e=b.e; return 1u; }
    if(d>0) { ma*=Calc_Pow10(d); } else if(d<0) { mb*=Calc_Pow10(-d); }
    return Calc_SetFrom(ma+mb,(d>0)?(int32_t)b.e:(int32_t)a.e,r);
}
static uint8_t Calc_MulDiv(CalcNum a,char op,CalcNum b,CalcNum *r)
{
    int64_t x;
    int32_t e;
    if(op=='*') {
        x=(int64_t)a.m*(int64_t)b.m;
        e=(int32_t)a.e+(int32_t)b.e;
    } else {
        if(b.m==0) { return 0u; }                    /* 除零 → Error */
        x=((int64_t)a.m*1000000000LL)/(int64_t)b.m;
        e=(int32_t)a.e-(int32_t)b.e-9;
    }
    return Calc_SetFrom(x,e,r);
}
/* 解析一个无符号数字 (可带小数点) */
static uint8_t Calc_ParseNum(const char *s,uint16_t *ip,CalcNum *v)
{
    uint16_t i=*ip;
    int64_t m=0;
    int32_t dec=0;
    uint8_t any=0u;
    while(s[i]>='0' && s[i]<='9') { if(m<=99999999LL) { m=m*10+(s[i]-'0'); } any=1u; i++; }
    if(s[i]=='.') {
        i++;
        while(s[i]>='0' && s[i]<='9') { if(dec<8) { m=m*10+(s[i]-'0'); dec++; } any=1u; i++; }
    }
    if(!any) { return 0u; }
    *ip=i;
    return Calc_SetFrom(m,-dec,v);
}
/* 表达式求值: 先乘除后加减, 全程整数 */
static uint8_t Calc_Evaluate(const char *s,CalcNum *out)
{
    uint16_t i=0u;
    CalcNum acc,cur,nx;
    char pend='+';
    acc.m=0; acc.e=0;
    if(!s[0]) { return 0u; }
    for(;;) {
        if(!Calc_ParseNum(s,&i,&cur)) { return 0u; }
        for(;;) {
            char op=s[i];
            if(op!='*' && op!='/') { break; }
            i++;
            if(!Calc_ParseNum(s,&i,&nx)) { return 0u; }
            if(!Calc_MulDiv(cur,op,nx,&cur)) { return 0u; }
        }
        if(!Calc_AddSub(acc,pend,cur,&acc)) { return 0u; }
        if(!s[i]) { break; }
        if(s[i]!='+' && s[i]!='-') { return 0u; }
        pend=s[i]; i++;
    }
    *out=acc;
    return 1u;
}
/* 结果 → 文本: [-]整数[.小数], 最多 8 位小数, 去掉尾部 0, 位数太多时省去小数位 */
static void Calc_FmtNum(CalcNum v,char *out)
{
    char tmp[26];
    uint32_t mag;
    int64_t pw;
    int32_t e=v.e,nd,i=0,k,frac,idig,fd;
    uint8_t neg;
    neg=(v.m<0)?1u:0u;
    mag=(uint32_t)(neg?(-(int64_t)v.m):(int64_t)v.m);
    if(mag==0u) { out[0]='0'; out[1]=0; return; }
    nd=(int32_t)Calc_NumDigits(mag);
    if(e>=0) {
        if(nd+e>10) { Calc_StrCopy(out,"Error"); return; }       /* 超出显示范围 */
        if(neg) { tmp[i++]='-'; }                                /* 负号不能丢 */
        for(k=0;k<nd;k++) { pw=Calc_Pow10(nd-1-k); tmp[i++]=(char)('0'+(mag/(uint32_t)pw)%10u); }
        for(k=0;k<e;k++) { tmp[i++]='0'; }
        tmp[i]=0;
    } else {
        frac=-e;
        if(frac>8) { mag/=(uint32_t)Calc_Pow10(frac-8); frac=8; }  /* 截到 8 位小数 */
        pw=Calc_Pow10(frac);
        if(neg) { tmp[i++]='-'; }
        idig=(int32_t)Calc_NumDigits(mag/(uint32_t)pw);
        if(idig>10) { Calc_StrCopy(out,"Error"); return; }
        for(k=0;k<idig;k++) { tmp[i++]=(char)('0'+((mag/(uint32_t)pw)/Calc_Pow10(idig-1-k))%10u); }
        fd=0;                                              /* fd = 最后一个非零小数位的位置 */
        for(k=0;k<frac;k++) { if((((mag%(uint32_t)pw)/Calc_Pow10(frac-1-k))%10u)!=0u) { fd=k+1; } }
        if(fd>0) {
            tmp[i++]='.';
            for(k=0;k<fd;k++) { tmp[i++]=(char)('0'+((mag%(uint32_t)pw)/Calc_Pow10(frac-1-k))%10u); }
        }
        tmp[i]=0;
    }
    Calc_StrCopy(out,tmp);
}
/* ---- 表达式编辑 (全部走字符串, 在光标处插入) ----------------------------- */
static void Calc_ResetExpr(void) { s_calcLen=0u; s_calcCur=0u; s_calcExpr[0]=0; s_calcIsResult=0u; }
static uint8_t Calc_NumStart(void)
{
    uint8_t i=s_calcCur;
    while(i>0u && ((s_calcExpr[i-1u]>='0'&&s_calcExpr[i-1u]<='9')||s_calcExpr[i-1u]=='.')) { i--; }
    return i;
}
static void Calc_Insert(char ch)
{
    uint8_t i;
    if(s_calcLen>=(uint8_t)CALC_EXPR_MAX) { return; }
    for(i=s_calcLen;i>s_calcCur;i--) { s_calcExpr[i]=s_calcExpr[i-1]; }
    s_calcExpr[s_calcCur]=ch;
    s_calcCur++; s_calcLen++; s_calcExpr[s_calcLen]=0;
}
static void Calc_DeleteAt(uint8_t at)
{
    uint8_t i=at;
    while(i<s_calcLen) { s_calcExpr[i]=s_calcExpr[i+1]; i++; }
    if(s_calcLen) { s_calcLen--; s_calcExpr[s_calcLen]=0; }
}
/* 应用一个按键 (纯函数: 只依赖当前表达式 + 该按键 → 这正是「可重放」的基础) */
static void Calc_ApplyKey(uint8_t key)
{
    if(s_calcIsResult) { Calc_ResetExpr(); }         /* 结果状态下按任意键: 清掉结果从新开始 */
    if(key<=9u) {
        uint8_t st=Calc_NumStart(),i,hd=0u,idig=0u,fdig=0u;
        for(i=st;i<s_calcCur;i++) {
            if(s_calcExpr[i]=='.') { hd=1u; } else if(hd) { fdig++; } else { idig++; }
        }
        if(!hd && idig>=9u) { return; }              /* 整数最多 9 位 */
        if(hd && fdig>=8u) { return; }               /* 小数最多 8 位 */
        if(!hd && (uint8_t)(s_calcCur-st)==1u && s_calcExpr[st]=='0') {   /* "0" → 直接换成新数字 */
            Calc_DeleteAt(st); s_calcCur=st;
        }
        Calc_Insert((char)('0'+(char)key));
        return;
    }
    switch(key) {
        case CK_DOT: {
            uint8_t st=Calc_NumStart(),i,hd=0u;
            for(i=st;i<s_calcCur;i++) { if(s_calcExpr[i]=='.') { hd=1u; } }
            if(hd) { return; }
            if(s_calcCur==0u || !(s_calcExpr[s_calcCur-1u]>='0'&&s_calcExpr[s_calcCur-1u]<='9')) {
                if(s_calcLen+2u>(uint8_t)CALC_EXPR_MAX) { return; }
                Calc_Insert('0'); Calc_Insert('.');      /* 前面没有数字 → 自动补 "0." */
            } else { Calc_Insert('.'); }
            break;
        }
        case CK_ADD: case CK_SUB: case CK_MUL: case CK_DIV: {
            char ch=(key==CK_ADD)?'+':(key==CK_SUB)?'-':(key==CK_MUL)?'*':'/';
            if(s_calcLen==0u) { return; }
            if(s_calcCur>0u) {
                char p=s_calcExpr[s_calcCur-1u];
                if(p=='+'||p=='-'||p=='*'||p=='/') { s_calcExpr[s_calcCur-1u]=ch; return; }  /* 连按运算符 = 替换 */
                if(p=='.') { return; }
            }
            Calc_Insert(ch);
            break;
        }
        case CK_EQ: {
            CalcNum v;
            char b[CALC_EXPR_MAX+1];
            if(s_calcLen==0u) { return; }
            if(s_calcCur>0u) {
                char p=s_calcExpr[s_calcCur-1u];
                if(p=='+'||p=='-'||p=='*'||p=='/'||p=='.') { return; }   /* 表达式还没写完 */
            }
            if(Calc_Evaluate(s_calcExpr,&v)) { Calc_FmtNum(v,b); } else { Calc_StrCopy(b,"Error"); }
            Calc_StrCopy(s_calcExpr,b);
            s_calcLen=Calc_StrLen(s_calcExpr);
            s_calcCur=s_calcLen;
            s_calcIsResult=1u;
            break;
        }
        default: break;      /* AC / 清空 由 Calc_Press 单独处理 */
    }
}
/* 完整重放按键日志 → 「表达式 + 光标」都是日志的纯函数
 * (每一步都记下了当时的光标, 所以中间插入/无限退回都能精确重现) */
static void Calc_Replay(void)
{
    uint16_t i;
    Calc_ResetExpr();
    for(i=0u;i<s_calcLogN;i++) {
        s_calcCur=(s_calcLogCur[i]<=s_calcLen)?s_calcLogCur[i]:s_calcLen;
        Calc_ApplyKey(s_calcLog[i]);
    }
}
static void Calc_Press(uint8_t key)
{
    if(key==CK_NONE) { return; }
    if(key==CK_AC) {                                  /* AC: 退回前一步, 可以一直退到底 */
        if(s_calcLogN) { s_calcLogN--; Calc_Replay(); }
        return;
    }
    if(key==CK_CLEAR) { s_calcLogN=0u; Calc_ResetExpr(); return; }   /* 清空实时计算数据 */
    if(s_calcLogN>=CALC_LOG_MAX) {                    /* 日志满: 丢掉最旧一半再追加 */
        uint16_t k;
        for(k=0u;k<(uint16_t)(CALC_LOG_MAX/2u);k++) {
            s_calcLog[k]=s_calcLog[k+(uint16_t)(CALC_LOG_MAX/2u)];
            s_calcLogCur[k]=s_calcLogCur[k+(uint16_t)(CALC_LOG_MAX/2u)];
        }
        s_calcLogN=(uint16_t)(CALC_LOG_MAX/2u);
    }
    s_calcLogCur[s_calcLogN]=s_calcCur;               /* 记下按这一下时的光标 */
    s_calcLog[s_calcLogN]=(uint8_t)key;
    s_calcLogN++;
    Calc_Replay();
}
/* PA4 短按: 在实时显示行里前后移动光标 (最左端再按 → 跳回末尾, 可以来回改) */
static void Calc_CursorPrev(void)
{
    if(s_calcIsResult) { Calc_ResetExpr(); return; }  /* 结果状态下 PA4 也是「清结果重新算」 */
    if(s_calcLen==0u) { return; }
    if(s_calcCur==0u) { s_calcCur=s_calcLen; } else { s_calcCur--; }
}
/* PA5 横向 / PA6 竖向: 只落在有按键的格子上; 走到边界会**绕回**(循环) ——
 * 这样选择永远不会卡住, 也永远到不了空格子 (AC、清空也都是正常格子)。 */
static void Calc_MoveSel(int32_t dr,int32_t dc)
{
    int32_t r=s_calcSelR,c=s_calcSelC,tries;
    if(s_calcIsResult) { Calc_ResetExpr(); }          /* 任意按键先清结果 */
    for(tries=0;tries<48;tries++) {
        r+=dr; c+=dc;
        if(r<0) { r=5; } if(r>5) { r=0; }             /* 竖向循环 */
        if(c<0) { c=3; } if(c>3) { c=0; }             /* 横向循环 */
        if(s_calcKeys[r][c]!=CK_NONE) { s_calcSelR=(uint8_t)r; s_calcSelC=(uint8_t)c; return; }
    }
}
static void Calc_Enter(void)
{
    s_calcLogN=0u; Calc_ResetExpr();
    s_calcSelR=4u; s_calcSelC=1u;
    s_calcFresh=1u; Scene_CalcReset();
    s_screen=SCR_CALC;
    if(s_transOn) { s_transOn=0u; s_view=s_transTo; } s_pending=0u;
}
static void Menu2_Enter(void)
{
    s_menu2Sel=MENU2_SEL_CALC;
    s_screen=SCR_MENU2;
    if(s_transOn) { s_transOn=0u; s_view=s_transTo; } s_pending=0u;
}

/* ===========================================================================
 * V30 俄罗斯方块内核
 * ---------------------------------------------------------------------------
 * 方块表示: 每种方块是 4x4 网格里的 4 个格子 (每格 1 字节: 高 4 位 x, 低 4 位 y)。
 * 顺时针旋转 90 度 = (x,y) -> (3-y, x), 纯算术, 不查 4 张旋转表 —— 7 种方块
 * 只需要 28 字节, 而不是 28*4=112 字节。
 *
 * 场地: 10 列 x 20 行, 每行一个 uint16 (低 10 位有效) = 40 字节。
 * 重力: 起步 600ms, 每消 4 行加快 60ms, 最快 120ms。
 * =========================================================================== */
#define GT_NCOL 10u
#define GT_NROW 20u
static uint16_t s_tBoard[GT_NROW];
static uint8_t  s_tShape[7][4]={           /* (x<<4)|y, 4x4 网格 */
    {0x01,0x11,0x21,0x31},                 /* I */
    {0x10,0x20,0x11,0x21},                 /* O */
    {0x01,0x11,0x21,0x10},                 /* T */
    {0x11,0x21,0x01,0x10},                 /* S */
    {0x01,0x11,0x21,0x12},                 /* Z */
    {0x01,0x02,0x12,0x22},                 /* J */
    {0x21,0x02,0x12,0x22}                  /* L */
};
static uint8_t  s_tCur,s_tNext,s_tRot;
static int8_t   s_tX,s_tY;
static uint32_t s_tScore,s_tDropAt;
static uint8_t  s_tPaused,s_tCount,s_tOver,s_tLines;
static uint8_t  s_goSel=0u;   /* V30 结算界面: 0=返回游戏界面(房子) 1=重新开始(环形箭头) */
static uint8_t  s_tOverSeen=0u;
static uint8_t  s_jpFirst=1u;
static uint32_t s_tCountAt;                /* 3-2-1 倒数起点 */
static uint8_t  s_tCells[8];               /* 当前方块 4 个 (c,r), 已算好 */
static uint8_t  s_tNextCells[8];
static uint8_t  s_tMenuSel;                /* 游戏选择: 0=跳一跳 1=俄罗斯方块 */

/* 顺时针旋转 r 次后的第 i 个格子的 (x,y), 写进 *ox,*oy */
/* 方块序列的伪随机源。xorshift16, 只在"需要新方块"时推进一次 —— 状态只
 * 依赖调用次数, 所以实物和模拟器在同样的操作序列下拿到同样的方块, 回归
 * 测试可以复现。种子取开机毫秒数, 每局不同。 */
static uint16_t s_tRand=0xACE1u;
static uint8_t Sim_Rand(void)
{
    s_tRand^=(uint16_t)(s_tRand<<7);
    s_tRand^=(uint16_t)(s_tRand>>9);
    s_tRand^=(uint16_t)(s_tRand<<8);
    return (uint8_t)(s_tRand&7u);
}
static void Tet_Rot(uint8_t pid,uint8_t rot,uint8_t i,uint8_t *ox,uint8_t *oy)
{
    uint8_t b=s_tShape[pid][i],x=(uint8_t)(b>>4),y=(uint8_t)(b&0x0Fu),k;
    for(k=0u;k<rot;k++) {
        uint8_t ny=(uint8_t)(3u-x);
        x=y; y=ny;
    }
    *ox=x; *oy=y;
}
/* 能不能放在 (px,py)/rot? 1=可以 */
static uint8_t Tet_Fits(uint8_t pid,uint8_t rot,int8_t px,int8_t py)
{
    uint8_t i;
    for(i=0u;i<4u;i++) {
        uint8_t cx,cy;
        int16_t bx,by;
        Tet_Rot(pid,rot,i,&cx,&cy);
        bx=(int16_t)(px+(int8_t)cx); by=(int16_t)(py+(int8_t)cy);
        if(bx<0 || bx>=(int16_t)GT_NCOL) { return 0u; }
        if(by>=(int16_t)GT_NROW) { return 0u; }
        if(by<0) { continue; }
        if(s_tBoard[by] & (uint16_t)(1u<<bx)) { return 0u; }
    }
    return 1u;
}
/* 把当前方块算成 s_tCells[] (给绘制用) */
static void Tet_Cells(void)
{
    uint8_t i;
    for(i=0u;i<4u;i++) {
        uint8_t cx,cy;
        int16_t bx,by;
        Tet_Rot(s_tCur,s_tRot,i,&cx,&cy);
        bx=(int16_t)(s_tX+(int8_t)cx); by=(int16_t)(s_tY+(int8_t)cy);
        s_tCells[i*2]=(uint8_t)((bx<0||bx>=(int16_t)GT_NCOL)?0xFFu:(uint8_t)bx);
        s_tCells[i*2+1]=(uint8_t)((by<0||by>=(int16_t)GT_NROW)?0xFFu:(uint8_t)by);
    }
}
static void Tet_NextCells(void)
{
    uint8_t i;
    for(i=0u;i<4u;i++) {
        uint8_t cx,cy;
        Tet_Rot(s_tNext,0u,i,&cx,&cy);
        s_tNextCells[i*2]=cx; s_tNextCells[i*2+1]=cy;
    }
}
static void Tet_Spawn(uint32_t now)
{
    s_tCur=s_tNext;
    s_tNext=(uint8_t)(Sim_Rand()%7u);
    s_tRot=(uint8_t)((s_tCur==0u)?1u:0u);   /* I 型竖着生成立即能看见 */
    s_tX=3; s_tY=0;
    Tet_NextCells();
    if(!Tet_Fits(s_tCur,0u,s_tX,s_tY)) { s_tOver=1u; }
    Tet_Cells();
    s_tDropAt=now;
}
/* 锁定并消行 */
static void Tet_Lock(uint32_t now)
{
    /* r 必须是**有符号**的: 下面的压实循环从 GT_NROW-1 数到 0 再减 1。
     * 写成 uint8_t 时 0-1 = 255, "r>=0" 恒真 -> 死循环, 整机卡住。 */
    uint8_t i,c;
    int8_t r;
    uint8_t cleared=0u;
    /* s_tCells 里已经是"绝对列 / 绝对行" (Tet_Cells 换算好的), 不要再用
     * Tet_Rot 旋转一遍 —— 那是 4x4 网格坐标, 会锁到错的位置。 */
    for(i=0u;i<4u;i++) {
        uint8_t bx=s_tCells[i*2], by=s_tCells[i*2+1];
        if(bx<(uint8_t)GT_NCOL && by<(uint8_t)GT_NROW) {
            s_tBoard[by] |= (uint16_t)(1u<<bx);
        }
    }
    /* 消行: 从下往上压实 */
    {
        int8_t dst=(int8_t)(GT_NROW-1);
        for(r=(int8_t)(GT_NROW-1);r>=0;r--) {
            if(s_tBoard[r]==0x3FFu) { cleared++; continue; }
            if(dst!=r) { s_tBoard[dst]=s_tBoard[r]; }
            dst--;
        }
        for(r=dst;r>=0;r--) { s_tBoard[r]=0u; }
    }
    (void)c;
    /* V31: **每块落地就加分** (+10), 消行再额外加成, 这样右边栏的分数
     * 在整局里持续跳动, 而不是只有消行时才变一次。 */
    s_tScore+=10u;
    if(cleared) {
        s_tLines=(uint8_t)(s_tLines+cleared);
        s_tScore+=(uint32_t)cleared*(uint32_t)cleared*50u;   /* 1行50 2行200 3行450 4行800 */
    }
    Tet_Spawn(now);
    s_tDropAt=now;
}
/* 重力步长 */
static uint32_t Tet_Gravity(void)
{
    uint32_t g=600u-(uint32_t)(s_tLines/4u)*60u;
    return (g<120u)?120u:g;
}
static void Tet_Step(uint32_t now)
{
    if(s_tPaused || s_tCount || s_tOver) { return; }
    if(Tet_Fits(s_tCur,s_tRot,s_tX,(int8_t)(s_tY+1))) { s_tY++; Tet_Cells(); }
    else { Tet_Lock(now); }
}
static void Tet_Left(void)
{
    if(s_tPaused || s_tCount || s_tOver) { return; }
    if(Tet_Fits(s_tCur,s_tRot,(int8_t)(s_tX-1),s_tY)) { s_tX--; Tet_Cells(); }
}
static void Tet_Right(void)
{
    if(s_tPaused || s_tCount || s_tOver) { return; }
    if(Tet_Fits(s_tCur,s_tRot,(int8_t)(s_tX+1),s_tY)) { s_tX++; Tet_Cells(); }
}
static void Tet_Rotate(void)
{
    uint8_t nr;
    if(s_tPaused || s_tCount || s_tOver) { return; }
    nr=(uint8_t)((s_tRot+1u)&3u);
    if(Tet_Fits(s_tCur,nr,s_tX,s_tY)) { s_tRot=nr; }
    else if(Tet_Fits(s_tCur,nr,(int8_t)(s_tX-1),s_tY)) { s_tX--; s_tRot=nr; }   /* 贴墙踢一脚 */
    else if(Tet_Fits(s_tCur,nr,(int8_t)(s_tX+1),s_tY)) { s_tX++; s_tRot=nr; }
    else { return; }
    Tet_Cells();
}
static void Tet_Start(uint32_t now)
{
    uint8_t r;
    OLED_Clear(); Scene_TetrisReset();
    for(r=0u;r<GT_NROW;r++) { s_tBoard[r]=0u; }
    s_tScore=0u; s_tLines=0u; s_tOver=0u; s_tPaused=0u;
    s_tCount=3u; s_tCountAt=now;          /* 开局先 3-2-1, 不然第一块直接掉了 */
    s_tNext=(uint8_t)(Sim_Rand()%7u);
    Tet_Spawn(now);
}
static void Tet_Enter(uint32_t now)
{
    s_screen=SCR_TETRIS;
    OLED_Clear();     /* 必须清: 上一屏是游戏选择界面, 不清会留下中文残影 */
    Scene_TetrisReset();
    Tet_Start(now);
    if(s_transOn) { s_transOn=0u; s_view=s_transTo; } s_pending=0u;
}

/* ===========================================================================
 * V30 跳一跳 (Jump)  ——  按用户上传的 592x1280 视频重做
 * ---------------------------------------------------------------------------
 * 视频逐帧研究结论: 角色是"圆柱身体 + 圆头"; 蓄力时**整体压扁**; 起跳后
 * 空中翻跟头 (约 180°); 落点踩中方块得 1 分, 踩空则翻滚坠落后倒地。
 * 这里用侧视简化: 三个方块按世界坐标排开, 相机把角色钉在屏幕 x=30 附近。
 * 蓄力: PA5 按住, pow 每 7 ms +1 (上限 100) -> 距离 10..56 px。
 * 空中: 水平线性, 垂直 4Ht(1-t) 抛物线, 头绕身体中心 4 档翻滚。
 * 全部用整数, 无浮点, 无数组查表 (Flash 只剩不到 1 KB)。
 * =========================================================================== */
#define JP_HZ     52        /* 方块顶面 y (= 角色脚下) */
static int16_t  s_jpPw[3];   /* 三个方块的世界左边缘 */
static uint8_t  s_jpWd[3];   /* 三个方块的宽度 */
static int16_t  s_jpXw;      /* 角色世界 x */
static int16_t  s_jpCam;     /* 相机: 屏幕 x = 世界 x - cam */
static int16_t  s_jpX0;      /* 起跳世界 x */
static int16_t  s_jpDx;      /* 本跳水平距离 */
static uint16_t s_jpT,s_jpDur;
static uint32_t s_jpAt;
static uint8_t  s_jpSt;      /* 0 站 1 蓄力 2 空中 3 摔落 4 结束 5 落地缓冲 */
static uint8_t  s_jpLand;    /* 落地缓冲的压缩量 0..100 (0->60->0) */
static uint8_t  s_jpQueued;  /* 落地缓冲期间按下的 PA5: 缓冲一结束立刻起跳 */
static uint8_t  s_jpPause,s_jpCnt;
static uint32_t s_jpCntAt;
static uint8_t  s_jpPow,s_jpScore;

/* 生成第 i 个方块: 宽度 16..33, 与前一个的间隙 12..37 */
static void Jp_Block(uint8_t i,uint32_t r)
{
    /* V31: 间隙 18..44 (V30 只有 6..18)。这是"再远一点"的核心参数:
     *   落点(下一块中心) = 间隙 + 宽度/2 -> 需要 24..54 px 的跳距;
     *   蓄力 8..72 px 线性映射, 于是需要的力度覆盖 25%..72%,
     *   既不会"随便按一下就中", 也不会出现按满也够不着的情况。
     *   间隙和宽度**每次独立随机**, 所以远近、大小都不重复。 */
    s_jpWd[i]=(uint8_t)(12u+(r>>4)%9u);
    if(i==0u) { s_jpPw[0]=0; }
    else { s_jpPw[i]=(int16_t)(s_jpPw[i-1]+s_jpWd[i-1]+18+(int16_t)(r%27u)); }
}
static void Jp_Enter(uint32_t now)
{
    uint32_t r=Sim_Rand();
    Jp_Block(0u,r); Jp_Block(1u,r>>8); Jp_Block(2u,r>>16);
    s_jpXw=(int16_t)(s_jpWd[0]/2u);
    s_jpCam=(int16_t)(s_jpXw-30);
    s_jpSt=0u; s_jpPow=0u; s_jpScore=0u; s_jpAt=now; s_jpQueued=0u; s_jpLand=0u;
    OLED_Clear();
}
/* 画角色: x 屏幕横坐标, yb 脚下 y, squat 压扁量 0..100, rot 0..3 翻滚档 */
static void Jp_Hero(int32_t x,int32_t yb,uint8_t squat,uint8_t rot)
{
    static const int8_t ox[4]={0,1,0,-1}, oy[4]={-1,0,1,0};
    int32_t h=(int32_t)(12u-squat*4u/100u);      /* 身高 12 -> 8 (蹲下) */
    int32_t w=(int32_t)(7u+squat*3u/100u);       /* 身宽 7  -> 10 (变粗) */
    int32_t cy=yb-h/2;
    OLED_FillRect(x-w/2,yb-h,w,h,OLED_WHITE);
    OLED_FillCircle(x+ox[rot&3]*(h/2),cy+oy[rot&3]*(h/2),3,OLED_WHITE);
}
static void Jp_Render(uint32_t now)
{
    int32_t i,x;
    {   /* 相机平滑跟随: 角色尽量停在屏幕 x=30 (落地后镜头滑回去) */
        int16_t tg=(int16_t)(s_jpXw-30);
        s_jpCam=(int16_t)(s_jpCam+(tg-s_jpCam)/3);
    }
    OLED_Clear();
    for(i=0;i<3;i++) {
        x=s_jpPw[i]-s_jpCam;
        if(x>127 || x+(int32_t)s_jpWd[i]<0) { continue; }
        OLED_DrawRect(x,JP_HZ,s_jpWd[i],64-JP_HZ,OLED_WHITE);
        OLED_FillRect(x,JP_HZ,s_jpWd[i],2,OLED_WHITE);
    }
    /* 得分: 右上角两位小数字 */
    OLED_DrawTinyDigit(122,2,(uint8_t)(s_jpScore%10u),OLED_WHITE);
    OLED_DrawTinyDigit(116,2,(uint8_t)((s_jpScore/10u)%10u),OLED_WHITE);
    x=s_jpXw-s_jpCam;
    if(s_jpSt<2u) {
        Jp_Hero(x,JP_HZ,s_jpPow,0u);                  /* 站立 / 蓄力压扁 */
    } else if(s_jpSt==2u) {
        int32_t tt=(int32_t)(s_jpT*100u/s_jpDur);
        int32_t px=s_jpX0+(int32_t)s_jpDx*tt/100-s_jpCam;
        /* 抛物线: 4H*t(1-t), 最高 30px; 起跳/落地时略微拉长 (squash-stretch) */
        int32_t py=JP_HZ-(tt*(100-tt)*120)/10000;
        Jp_Hero(px,py,0u,(uint8_t)(tt/26));
    } else if(s_jpSt==5u) {
        Jp_Hero(x,JP_HZ,s_jpLand,0u);                 /* 落地缓冲: 一压一弹 */
    } else {
        /* 摔落: 600ms 内加速下坠 + 边转边掉, 掉出屏幕才结算 */
        uint32_t el=(uint32_t)(now-s_jpAt);
        int32_t d=(int32_t)(el*el*70u/640u/1000u);
        if(d>40) { d=40; }
        Jp_Hero(x,JP_HZ+d,0u,(uint8_t)(el/70u)&3u);
    }
    /* 暂停: 两条竖杠; 3-2-1: 大号数字 (复用 OLED 的 ASCII 字体) */
    if(s_jpPause) { OLED_FillRect(56,26,6,18,OLED_WHITE); OLED_FillRect(66,26,6,18,OLED_WHITE); }
    if(s_jpCnt) { Scene_BigDigit(58,24,s_jpCnt,4u); }
}
#if defined(FW_SIMULATOR)
__attribute__((export_name("sim_jump"))) uint32_t Sim_Jump(void)
{
    return (uint32_t)(s_jpSt|((uint32_t)s_jpPow<<8)|((uint32_t)s_jpScore<<16)
        |((uint32_t)(uint16_t)s_jpXw<<24));
}
__attribute__((export_name("sim_jppw"))) uint32_t Sim_JpPw(uint32_t i)
{ return (i<3u)?(uint32_t)(uint16_t)s_jpPw[i]:0u; }
#endif
/* V30: 设置2 的"游戏"图标进入的是**游戏选择界面** (不再是黑屏)。
 * 默认选中"俄罗斯方块" (索引 1) —— 它是本轮完整实现的游戏。 */
static void Game_Enter(void)
{
    s_tMenuSel=1u;
    s_gameFresh=1u; s_screen=SCR_GAME;
    if(s_transOn) { s_transOn=0u; s_view=s_transTo; } s_pending=0u;
}
/* V26 设置3: 时间界面短按 PA7 进入, 默认选中齿轮。
 * 和设置1/设置2 一样先收尾半途中的表盘过渡, 避免在形变中途叠菜单。 */
static void SetMenu_Enter(void)
{
    s_setSel=MENU3_SEL_GEAR;
    s_screen=SCR_SET;
    if(s_transOn) { s_transOn=0u; s_view=s_transTo; } s_pending=0u;
}
static uint8_t De_DaysIn(uint16_t y,uint8_t m)
{
    static const uint8_t dim[12]={31,28,31,30,31,30,31,31,30,31,30,31};
    uint8_t leap=(uint8_t)(((y%4u==0u)&&(y%100u!=0u))||(y%400u==0u));
    if(m<1u) { m=1u; } if(m>12u) { m=12u; }
    return (m==2u && leap)?29u:dim[m-1u];
}
/* 星期由年月日唯一决定 —— 不允许独立设置, 所以永远和日期一致 */
static uint8_t De_Weekday(uint16_t y,uint8_t m,uint8_t d)
{
    static const uint8_t t[12]={0,3,2,5,0,3,5,1,4,6,2,4};
    uint32_t yy=y;
    if(m<1u) { m=1u; } if(m>12u) { m=12u; }
    if(m<3u) { yy--; }
    return (uint8_t)((yy+yy/4u-yy/100u+yy/400u+t[m-1u]+d)%7u);
}
/* 进入编辑器: 把当前系统时间完整抄一份出来编辑 (长按PA4取消不影响系统时钟) */
static void Date_Enter(void)
{
    uint32_t ms=Clock_MsOfDay();
    s_deYear=s_date.year; s_deMon=s_date.month; s_deDay=s_date.day;
    s_deHour=(uint8_t)(ms/3600000u);
    s_deMin=(uint8_t)((ms/60000u)%60u);
    s_deSec=(uint8_t)((ms/1000u)%60u);
    if(s_deMon<1u) { s_deMon=1u; } if(s_deMon>12u) { s_deMon=12u; }
    if(s_deDay<1u) { s_deDay=1u; }
    if(s_deDay>De_DaysIn(s_deYear,s_deMon)) { s_deDay=De_DaysIn(s_deYear,s_deMon); }
    De_Select(DE_F_YEAR);
    Scene_DateEditorReset();
    s_screen=SCR_DATE;
    if(s_transOn) { s_transOn=0u; s_view=s_transTo; } s_pending=0u;
}
/* 唯一允许改 s_deSel 的入口: 顺手把 s_deIdx 对齐, 两个变量就不会走散。
 * (直接写 s_deSel 会让 PA4 的巡览停在同一格来回跳。) */
static void De_Select(uint8_t field)
{
    uint8_t i;
    s_deSel=field;
    for(i=0u;i<(uint8_t)DE_F_SEL_NUM;i++) {
        if(s_deSelOrder[i]==field) { s_deIdx=i; return; }
    }
    /* 星期(或任何越界值)不是可编辑项: 回落到"年", 保证选中态永远有效 */
    s_deSel=DE_F_YEAR; s_deIdx=0u;
}

static void De_Step(uint8_t field,int32_t dir)
{
    int32_t v;
    switch(field) {
        case DE_F_YEAR:
            v=(int32_t)s_deYear+dir;
            if(v>2099) { v=2000; } if(v<2000) { v=2099; }
            s_deYear=(uint16_t)v;
            break;
        case DE_F_MON:
            /* 跨年进位/借位: 12月 -> 次年1月, 1月 -> 上年12月.
             * 只回绕到 1 月而不动年份, 会出现"12月31日 月+1 -> 1月31日(同一年)"
             * 这种反直觉的结果 (V26 的测试抓到的真 bug)。 */
            v=(int32_t)s_deMon+dir;
            if(v>12) {
                v=1;
                s_deYear=(uint16_t)((s_deYear>=2099u)?2000u:(s_deYear+1u));
            } else if(v<1) {
                v=12;
                s_deYear=(uint16_t)((s_deYear<=2000u)?2099u:(s_deYear-1u));
            }
            s_deMon=(uint8_t)v;
            break;
        case DE_F_DAY:
        case DE_F_WK:
            v=(int32_t)s_deDay+dir;
            if(v>(int32_t)De_DaysIn(s_deYear,s_deMon)) { v=1; }
            if(v<1) { v=(int32_t)De_DaysIn(s_deYear,s_deMon); }
            s_deDay=(uint8_t)v;
            break;
        case DE_F_HOUR:
            v=(int32_t)s_deHour+dir; if(v>23) { v=0; } if(v<0) { v=23; }
            s_deHour=(uint8_t)v;
            break;
        case DE_F_MIN:
            v=(int32_t)s_deMin+dir; if(v>59) { v=0; } if(v<0) { v=59; }
            s_deMin=(uint8_t)v;
            break;
        default:
            v=(int32_t)s_deSec+dir; if(v>59) { v=0; } if(v<0) { v=59; }
            s_deSec=(uint8_t)v;
            break;
    }
    /* 换年/换月后日期可能越界 (1月31日 -> 2月) */
    { uint8_t dim=De_DaysIn(s_deYear,s_deMon);
      if(s_deDay>dim) { s_deDay=dim; } if(s_deDay<1u) { s_deDay=1u; } }
}
/* V32 访问页: 8 个汉字(扫一查看使用手册, 16x16, 每行 1 个 uint16, bit15=最左) */
static const uint16_t s_accCn[8][16]={
    {0x0000,0x0000,0x0800,0x09FC,0x3E0C,0x3C0C,0x080C,0x09FC,0x1FFC,0x380C,0x080C,0x080C,0x09FC,0x39FC,0x1000,0x0000},
    {0x0000,0x0000,0x0000,0x0000,0x0000,0x3FFE,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000},
    {0x0000,0x0000,0x0180,0x3FFC,0x3FFC,0x07E0,0x1DB8,0x399C,0x0FF0,0x0FF0,0x0C30,0x0FF0,0x0000,0x3FFC,0x0000,0x0000},
    {0x0000,0x0000,0x1FF8,0x0300,0x1FF8,0x0600,0x3FFC,0x0C00,0x0FF8,0x3FF8,0x3C18,0x0FF8,0x0C18,0x0FF8,0x0408,0x0000},
    {0x0000,0x0000,0x0C60,0x0FFE,0x1860,0x3BFC,0x7A6C,0x7A64,0x1BFC,0x1A60,0x1B40,0x19C0,0x19E0,0x1F3C,0x0000,0x0000},
    {0x0000,0x0000,0x1FF8,0x1988,0x1188,0x1FF8,0x1FF8,0x1188,0x1FF8,0x1FF8,0x1188,0x3188,0x21B8,0x21B8,0x0000,0x0000},
    {0x0000,0x0010,0x1FF8,0x1F80,0x0180,0x1FF8,0x1FFC,0x0180,0x0180,0x3FFE,0x0180,0x0180,0x0180,0x0780,0x0000,0x0000},
    {0x0000,0x0000,0x1EF8,0x12C8,0x12C8,0x12C8,0x3FFC,0x3FFC,0x12C8,0x12C8,0x12C8,0x3288,0x2798,0x26B8,0x0000,0x0000}
};
/* 二维码 V3-L 29x29, 内容 = 说明书网址; 每行 1 个 uint32, bit0=最左 */
static const uint32_t s_accQr[29]={
    0x1FCE527F,0x1053F941,0x175B9F5D,0x174D8E5D,0x1740A15D,0x105DEB41,0x1FD5557F,0x001B5300,0x0DC824CB,0x12606D36,0x0FED6667,0x0C99E02B,0x1AFBF041,0x003EDE96,0x1E9333DB,0x0B9FF515,0x08AB3858,0x12E1E7AC,0x19A670E1,0x19B34F94,0x05FC3FD5,0x1D19CB00,0x0953637F,0x07197E41,0x09F39C5D,0x0FC9435D,0x17065E5D,0x09973741,0x0B7AC17F
};
static uint32_t s_accAt;                 /* 进入访问页的时刻 */
static uint8_t  s_accPh;                 /* 0 = 3 秒提示, 1 = 二维码 */
static void Acc_Txt(int32_t x,int32_t y,const uint8_t *id,uint8_t n)
{
    uint8_t i,r,c;
    for(i=0u;i<n;i++) for(r=0u;r<16u;r++) for(c=0u;c<16u;c++) {
        if((s_accCn[id[i]][r]>>(15u-c))&1u) { OLED_DrawPixel(x+i*16+c,y+r,OLED_WHITE); }
    }
}
/* 访问页: 先 3 秒"扫一扫查看使用手册"(底部进度条), 然后整屏二维码。
 * 二维码 = 白底 64x64 + 每个模块 2x2 像素(四周静区 3px, 经 OpenCV 解码验证)。 */
static void Acc_Render(uint32_t now)
{
    static const uint8_t t1[3]={0u,1u,0u}, t2[6]={2u,3u,4u,5u,6u,7u};
    uint32_t el=(uint32_t)(now-s_accAt);
    uint8_t x,y;
    if(s_gameFresh) {
        s_gameFresh=0u; s_accAt=now; s_accPh=0u; el=0u;
        OLED_Clear();
        Acc_Txt(40,10,t1,3u); Acc_Txt(16,32,t2,6u);
        OLED_DrawRect(13,55,102,5,OLED_WHITE);
    } else if(s_accPh==0u && el>=3000u) {
        s_accPh=1u;
        OLED_Fill(OLED_BLACK); OLED_FillRect(32,0,64,64,OLED_WHITE);
        for(y=0u;y<29u;y++) for(x=0u;x<29u;x++) {
            if((s_accQr[y]>>x)&1u) { OLED_FillRect(35+x*2,3+y*2,2,2,OLED_BLACK); }
        }
    }
    if(s_accPh==0u) { OLED_FillRect(14,56,(int32_t)(el/30u),3,OLED_WHITE); }
}
static void Access_Enter(void)
{
    s_gameFresh=1u; s_screen=SCR_ACC;
    if(s_transOn) { s_transOn=0u; s_view=s_transTo; } s_pending=0u;
}
static void Calc_Render(uint32_t t)
{
    Scene_Calc(t,s_calcExpr,(uint8_t)s_calcCur,(uint8_t)s_calcSelR,(uint8_t)s_calcSelC,s_calcIsResult);
}
#if defined(FW_SIMULATOR)
__attribute__((export_name("sim_menu2_sel"))) uint32_t Sim_Menu2Sel(void) { return (uint32_t)s_menu2Sel; }
__attribute__((export_name("sim_calc_state"))) uint32_t Sim_CalcState(void)
{
    return (uint32_t)s_calcLen | ((uint32_t)s_calcCur<<8) | ((uint32_t)s_calcSelR<<16)
         | ((uint32_t)s_calcSelC<<19) | ((uint32_t)s_calcIsResult<<21) | ((uint32_t)s_calcLogN<<24);
}
__attribute__((export_name("sim_calc_expr"))) uint32_t Sim_CalcExpr(void)
{
    return (uint32_t)(uintptr_t)s_calcExpr;
}
__attribute__((export_name("sim_calc_press"))) void Sim_CalcPress(uint32_t key) { Calc_Press((uint8_t)key); }
__attribute__((export_name("sim_calc_set"))) void Sim_CalcSet(uint32_t key) { Calc_ApplyKey((uint8_t)key); }
__attribute__((export_name("sim_calc_move"))) void Sim_CalcMove(int32_t dr,int32_t dc) { Calc_MoveSel(dr,dc); }
__attribute__((export_name("sim_calc_fmt"))) uint32_t Sim_CalcFmt(int32_t m,int32_t e)
{
    static char b[26];
    CalcNum v;
    v.m=m; v.e=(int8_t)e;
    Calc_FmtNum(v,b);
    return (uint32_t)(uintptr_t)b;
}
#endif

static void Stopwatch_Render(uint32_t t,uint32_t now)
{
    (void)now;
    Scene_Stopwatch(t,s_swMs,s_swLap,s_swLapStored,s_swLapHead,s_swView,(uint8_t)(s_swState==1u));
}
/* LED mirrors the countdown: on while it runs (setup/progress and while the
 * clock face is displayed), untouched during the alarm (that blinks it). */
static void Led_FollowTimer(void)
{
    if(s_timerMode==TIMER_RUN || s_timerMode==TIMER_VIEW) { LED_On(); }
    else if(s_timerMode!=TIMER_ALARM) { LED_Off(); }
}
static void Timer_RenderProgress(uint32_t now,uint32_t msOfDay)
{
    uint32_t elapsed=now-s_timerStart,remain,pct;
    if(elapsed>s_timerDuration) { elapsed=s_timerDuration; }
    remain=s_timerDuration-elapsed;
    pct=s_timerDuration?(elapsed*1000u/s_timerDuration):1000u;
    if(pct>1000u) { pct=1000u; }
    Scene_TimerRunning(msOfDay,remain,pct);
}
static void View_Step(uint32_t now,uint8_t keyEv)
{
    if(keyEv) { if(s_transOn) { s_pending^=1u; } else { s_pending=0u; View_StartTrans(now); } }
    if(s_transOn && (uint32_t)(now-s_transStart)>=TRANS_MS) {
        s_transOn=0u; s_view=s_transTo; if(s_pending) { s_pending=0u; View_StartTrans(now); }
    }
}
static uint16_t View_Progress(uint32_t now) { uint32_t el=now-s_transStart; return (el>=TRANS_MS)?256u:(uint16_t)(el*256u/TRANS_MS); }
static void FillInfo(SceneInfo *i)
{
    i->year=s_date.year; i->month=s_date.month; i->day=s_date.day; i->weekday=s_date.wday;
    i->tempTenths=DS18B20_GetTenths(); i->tempValid=DS18B20_IsValid();
}
static void RenderView(uint8_t view,uint32_t t,uint32_t ms,const SceneInfo *info)
{
    if(view==VIEW_HOLE) { Scene_BlackHole(t,ms,info); } else { OLED_Clear(); Face_Draw(ms,t); }
}
static void CopyGram(uint8_t dst[8][128]) { uint8_t p,x; for(p=0u;p<8u;p++) { for(x=0u;x<128u;x++) { dst[p][x]=OLED_GRAM[p][x]; } } }
/* Clock face, including the flowing dial <-> black hole morph. */
static void RenderTimeScreen(uint32_t now,uint32_t t,uint32_t ms,const SceneInfo *info)
{
    if(s_transOn) {
        uint16_t p=View_Progress(now);
        RenderView(s_transFrom,t,ms,info); CopyGram(s_gramA); RenderView(s_transTo,t,ms,info); CopyGram(s_gramB); Scene_Compose(s_gramA,s_gramB,p);
    } else { RenderView(s_view,t,ms,info); }
}
#if defined(FW_SIMULATOR)
/* Simulator-only introspection: lets the page (and the automated regression
 * harness) read the state machine without guessing from pixels. */
__attribute__((export_name("sim_tboard"))) uint32_t Sim_TBoard(uint32_t i)
{ return (i<(uint32_t)GT_NROW)?(uint32_t)s_tBoard[i]:0u; }
__attribute__((export_name("sim_tetris"))) uint32_t Sim_Tetris(void)
{
    return (uint32_t)s_tCur | ((uint32_t)(uint8_t)s_tX<<4) | ((uint32_t)(uint8_t)s_tY<<8)
         | ((uint32_t)s_tOver<<16) | ((uint32_t)s_tPaused<<17) | ((uint32_t)s_tCount<<18)
         | ((uint32_t)s_tScore<<24);
}
__attribute__((export_name("sim_timer_state"))) uint32_t Sim_TimerState(void)
{
    return (uint32_t)s_timerHour | ((uint32_t)s_timerMinute<<8) | ((uint32_t)s_timerSecond<<16)
         | ((uint32_t)s_timerSelected<<24);
}
__attribute__((export_name("sim_app_state"))) uint32_t Sim_AppState(void)
{
    return (uint32_t)s_screen
         | ((uint32_t)s_timerMode<<4)
         | ((uint32_t)s_view<<8)
         | ((uint32_t)s_menuSel<<9)
         | ((uint32_t)s_pa4BlockOn<<10)
         | ((uint32_t)s_transOn<<11)
         | ((uint32_t)s_bootDone<<12)
         | ((uint32_t)s_timerSelected<<13)
         | ((uint32_t)(s_pa5BlockOn?1u:0u)<<15);
}
#endif
#if defined(FW_SIMULATOR)
/* Simulator-only hooks, implemented in simulator/sim_hal.c. The browser runs
 * one loop iteration per call and re-enters main() at the loop label, exactly
 * like the board runs the loop in hardware. Nothing here is compiled by Keil. */
extern uint8_t Sim_Yield(void);
static uint32_t s_simT0=0u;
__attribute__((export_name("fw_step"))) int main(void)
#else
int main(void)
#endif
{
    uint32_t t0;
#if defined(FW_SIMULATOR)
    /* Resume the control loop without re-initialising the peripherals. */
    static uint8_t s_simInited=0u;
    if(s_simInited) { t0=s_simT0; goto sim_loop_body; }
    s_simInited=1u;
#endif
    /* R5 drives the base of a high-side PNP, not the buzzer directly.
     * Preload PA8 HIGH while still input; HIGH turns Q1 off. */
    RCC->APB2ENR |= BUZZER_RCC_MASK;
    BUZZER_PORT->BSRR = BUZZER_PIN;             /* ODR = 1 before output mode */
    BUZZER_PORT->CRH = (BUZZER_PORT->CRH & ~0x0000000FuL) | 0x00000002uL; /* PA8: 2MHz push-pull */
    BUZZER_PORT->BSRR = BUZZER_PIN;             /* HIGH = Q1 off = silent */
    LED_Init(); OLED_Init(); TimeBase_Init(); Key_Init(); DS18B20_Init(); Write_Init();
#if USE_BUILD_TIME
    Date_FromBuild(); Clock_SeedFromBuild();
#else
    Date_Set(INIT_YEAR,INIT_MONTH,INIT_DAY); Clock_SetTime(INIT_HOUR,INIT_MIN,INIT_SEC);
#endif
    t0=Millis();
#if defined(FW_SIMULATOR)
    s_simT0=t0;
#endif
    for(;;) {
#if defined(FW_SIMULATOR)
        /* Resume point for the browser: the whole loop body - key scanning
         * included - must run on every call, exactly like one pass of the
         * board's main loop. */
sim_loop_body: ;
#endif
        uint32_t now=Millis(),t=now-t0,ms;
        uint8_t keyMask=Key_Scan(now),repeatMask=Key_Repeat();
        uint8_t longMask=Key_Long(),releaseMask=Key_Release();
        uint8_t oledDirty=1u,oledPartial=0u;
        SceneInfo info;
        DS18B20_Update(now); ms=Clock_MsOfDay();
        if(s_screen!=SCR_STOP) { Sw_Tick(); }      /* keep the tick delta in sync */
        if(!Boot_IsDone(t) && t<WRITE_TOTAL_MS+HOLD_MS) { Write_Render(t); }
        else if(!s_bootDone) {
            int32_t r=(int32_t)((t-WRITE_TOTAL_MS-HOLD_MS)*72u/IRIS_MS);
            Write_Render(WRITE_TOTAL_MS); OLED_FillCircle(64,32,r,OLED_BLACK); OLED_SetClipCircle(64,32,r); Face_Draw(ms,t); OLED_SetClipCircle(0,0,-1);
        } else {
            FillInfo(&info);
            /* --- 1) the countdown expires no matter which screen is shown ---- */
            if((s_timerMode==TIMER_RUN || s_timerMode==TIMER_VIEW) && (uint32_t)(now-s_timerStart)>=s_timerDuration) {
                Timer_Expire(now); s_screen=SCR_TIMER;
            }
            /* --- 2) input ---------------------------------------------------- */
            if(s_timerMode==TIMER_ALARM) {
                /* Any new key press dismisses the alarm at once (unchanged). */
                if(keyMask) { Timer_Dismiss(); Screen_ToTime(now); }
            } else if(s_screen==SCR_MENU) {
                if(longMask & KEY_EVENT_PA4) { Screen_ToTime(now); }             /* 长按2秒PA4: 回时间界面 */
                else if(releaseMask & KEY_EVENT_PA4) { s_menuSel=(uint8_t)(s_menuSel^1u); } /* 短按PA4: 选择 */
                else if(keyMask & KEY_EVENT_PA5) {                               /* 短按PA5: 进入 */
                    if(s_menuSel==MENU_SEL_BELL) {
                        /* 铃声:直接进入倒计时界面(空闲=设置界面, 运行中=进度界面) */
                        if(s_timerMode==TIMER_IDLE) { Timer_EnterSetup(); } else { s_timerMode=TIMER_VIEW; }
                        s_screen=SCR_TIMER;
                    } else { Stopwatch_Enter(); }                                /* 秒表:黑屏界面 */
                }
            } else if(s_screen==SCR_STOP) {
                /* V21 秒表界面: 长按2秒PA4 回时间界面(并武装1秒防误触) */
                Sw_Tick();
                if(longMask & KEY_EVENT_PA4) { s_swPa7Down=0u; Screen_ToTime(now); }
                else {
                    if(keyMask & KEY_EVENT_PA5) { Stopwatch_Key5(); }             /* 开始/暂停/重开 */
                    if(keyMask & KEY_EVENT_PA6) { Stopwatch_Lap(); }             /* 记一圈 */
                    Stopwatch_Browse(now,(uint8_t)((keyMask&KEY_EVENT_PA7)?1u:0u),
                                          (uint8_t)((Key_IsDown()&KEY_EVENT_PA7)?1u:0u));
                }
            } else if(s_screen==SCR_MENU2) {
                /* V23 设置2: 长按2秒PA4 回时间界面; 短按PA4 切换 计算器/游戏; PA5 进入 */
                if(longMask & KEY_EVENT_PA4) { Screen_ToTime(now); }
                else if(releaseMask & KEY_EVENT_PA4) { s_menu2Sel=(uint8_t)(s_menu2Sel^1u); }
                else if(keyMask & KEY_EVENT_PA5) {
                    if(s_menu2Sel==MENU2_SEL_CALC) { Calc_Enter(); } else { Game_Enter(); }
                }
            } else if(s_screen==SCR_SET) {
                /* V26 设置3: 长按2秒PA4 回时间界面(并武装1秒防误触);
                 * 短按PA4 在 齿轮/访问 之间切换; 短按PA5 进入 */
                if(longMask & KEY_EVENT_PA4) { Screen_ToTime(now); }
                else if(releaseMask & KEY_EVENT_PA4) { s_setSel=(uint8_t)(s_setSel^1u); }
                else if(keyMask & KEY_EVENT_PA5) {
                    if(s_setSel==MENU3_SEL_GEAR) { Date_Enter(); }
                    else { Access_Enter(); }
                }
            } else if(s_screen==SCR_DATE) {
                /* V26 日期编辑器:
                 *   PA4 短按 = 下一个字段 (年月日时分秒星期 循环)
                 *   PA5 / PA6 = 选中字段 下一页 / 上一页 (长按自动连翻)
                 *   PA7       = 保存并回时间界面
                 *   长按 PA4  = 放弃修改回时间界面 */
                if(longMask & KEY_EVENT_PA4) { Screen_ToTime(now); }
                else {
                    /* PA4 的巡览顺序按**屏幕上的阅读顺序**: 年 月 日 星期,
                     * 然后才是第二行的 时 分 秒。枚举值 DE_F_* 里星期排在
                     * 最后(它是唯一由年月日推算出来的字段), 所以这里查表跳转,
                     * 直接 +1 会变成 年->月->日->时->分->秒->星期, 星期被
                     * 挪到秒后面, 和眼睛看到的排布对不上。 */
                    if(releaseMask & KEY_EVENT_PA4) { De_Select(s_deSelOrder[(s_deIdx+1u)%DE_F_SEL_NUM]); }
                    if((keyMask|repeatMask) & KEY_EVENT_PA5) { De_Step(s_deSel,1); }
                    if((keyMask|repeatMask) & KEY_EVENT_PA6) { De_Step(s_deSel,-1); }
                    if(keyMask & KEY_EVENT_PA7) {
                        Clock_SetDateTime(s_deYear,s_deMon,s_deDay,s_deHour,s_deMin,s_deSec);
                        Screen_ToTime(now);
                    }
                }
            } else if(s_screen==SCR_ACC) {
                /* 访问页: 长按 PA4 回时间界面 */
                if(longMask & KEY_EVENT_PA4) { Screen_ToTime(now); }
            } else if(s_screen==SCR_GAME) {
                /* V30 游戏选择界面: 长按2秒PA4 回时间界面; 短按PA4 切换; PA5 进入 */
                if(longMask & KEY_EVENT_PA4) { Screen_ToTime(now); }
                else if(releaseMask & KEY_EVENT_PA4) { s_tMenuSel=(uint8_t)(s_tMenuSel^1u); }
                else if(keyMask & KEY_EVENT_PA5) {
                    if(s_tMenuSel) { Tet_Enter(now); }
                    else { Jp_Enter(now); s_screen=SCR_JUMP; s_jpFirst=1u; }
                }
            } else if(s_screen==SCR_TETRIS) {
                /* V30 俄罗斯方块: PA5 左移 / PA6 右移 / PA7 顺时针旋转 / PA4 暂停 */
                if(longMask & KEY_EVENT_PA4) { Screen_ToTime(now); }
                else if(s_tCount) {                      /* 3-2-1 倒数中: 只走计时, 不吃按键 */
                    ;
                } else if(s_tPaused) {
                    if(keyMask) { s_tPaused=0u; s_tCount=3u; s_tCountAt=now; Scene_TetrisOvlReset(); }
                } else if(s_tOver) {
                    /* V30 结算: PA4 在[房子][环形箭头]间切换(细线框闪烁), PA5 确认 */
                    if(releaseMask & KEY_EVENT_PA4) { s_goSel=(uint8_t)(s_goSel^1u); }
                    else if(keyMask & KEY_EVENT_PA5) {
                        if(s_goSel) { Tet_Start(now); s_tCount=3u; s_tCountAt=now; }
                        else { s_gameFresh=1u; s_screen=SCR_GAME;
                               s_pa4BlockOn=1u; s_pa4BlockAt=now; }  /* 返回游戏选择界面 */
                    }
                } else {
                    if(releaseMask & KEY_EVENT_PA4) { s_tPaused=1u; Scene_TetrisOvlReset(); }
                    if(keyMask & KEY_EVENT_PA5) { Tet_Left(); }
                    if(keyMask & KEY_EVENT_PA6) { Tet_Right(); }
                    if(keyMask & KEY_EVENT_PA7) { Tet_Rotate(); }
                }
            } else if(s_screen==SCR_JUMP) {
                /* V30 跳一跳: PA5 按住蓄力, 松开起跳; PA4 短按暂停 */
                int16_t g;
                if(longMask & KEY_EVENT_PA4) { Screen_ToTime(now); }
                else if(s_jpSt==4u) {                     /* 结算 */
                    if(releaseMask & KEY_EVENT_PA4) { s_goSel=(uint8_t)(s_goSel^1u); }
                    else if(keyMask & KEY_EVENT_PA5) {
                        if(s_goSel) { Jp_Enter(now); s_jpFirst=1u; }
                        else { s_gameFresh=1u; s_screen=SCR_GAME;
                               s_pa4BlockOn=1u; s_pa4BlockAt=now; }
                    }
                } else if(s_jpSt==3u) {                   /* 摔落中: 落下去再结算 (640ms, 看得清) */
                    if((uint32_t)(now-s_jpAt)>=640u) { s_jpSt=4u; s_goSel=0u; Scene_GameOverBegin(now); }
                } else if(s_jpSt==5u) {                   /* 落地缓冲: 0->60->0 压缩, 300ms */
                    uint32_t el=(uint32_t)(now-s_jpAt);
                    /* 缓冲期间按下 PA5 不要丢: 记下来, 缓冲结束立刻起跳,
                     * 这样连续跳的手感是连贯的 (否则这 300ms 的按键会被忽略)。 */
                    if(keyMask & KEY_EVENT_PA5) { s_jpQueued=1u; }
                    if(el>=300u) {
                        s_jpSt=0u; s_jpLand=0u;
                        if(s_jpQueued) { s_jpQueued=0u; s_jpSt=1u; s_jpAt=now; s_jpPow=0u; }
                    } else { s_jpLand=(uint8_t)((el<150u)?(el*60u/150u):((300u-el)*60u/150u)); }
                } else if(s_jpCnt) {
                    if((uint32_t)(now-s_jpCntAt)>=3000u) { s_jpCnt=0u; }
                    else { s_jpCnt=(uint8_t)(3u-(uint32_t)(now-s_jpCntAt)/1000u); }
                } else if(s_jpPause) {
                    if(keyMask) { s_jpPause=0u; s_jpCnt=3u; s_jpCntAt=now; }
                } else if(s_jpSt==0u) {
                    if(releaseMask & KEY_EVENT_PA4) { s_jpPause=1u; }
                    else if(keyMask & KEY_EVENT_PA5) { s_jpSt=1u; s_jpAt=now; s_jpPow=0u; s_jpQueued=0u; }
                } else if(s_jpSt==1u) {
                    g=(int16_t)((uint32_t)(now-s_jpAt)/11u);
                    s_jpPow=(uint8_t)(g>100?100:g);
                    if(releaseMask & KEY_EVENT_PA5) {
                        s_jpDx=(int16_t)(8u+s_jpPow*64u/100u);
                        s_jpDur=(uint16_t)(420u+s_jpPow*3u);
                        s_jpX0=s_jpXw; s_jpT=0u; s_jpAt=now; s_jpSt=2u;
                    }
                } else {                                  /* 空中 */
                    s_jpT=(uint16_t)(now-s_jpAt);
                    if(s_jpT>=s_jpDur) {
                        int16_t b=s_jpPw[1]; uint8_t w1=s_jpWd[1];
                        s_jpXw=(int16_t)(s_jpX0+s_jpDx);
                        if(s_jpXw>=b && s_jpXw<(int16_t)(b+ (int16_t)s_jpWd[1])) {
                            s_jpScore++;
                            s_jpXw=(int16_t)(s_jpXw-b);
                            s_jpPw[0]=0; s_jpWd[0]=w1;
                            s_jpPw[1]=(int16_t)(s_jpPw[2]-b); s_jpWd[1]=s_jpWd[2];
                            Jp_Block(2u,Sim_Rand());
                            s_jpSt=5u; s_jpAt=now; s_jpLand=0u;   /* 落地缓冲 */
                        } else { s_jpSt=3u; s_jpAt=now; }
                    }
                }
            } else if(s_screen==SCR_CALC) {
                /* V23 计算器: PA5 横向 / PA6 竖向 / PA7 确认 / 短按PA4 光标前后 / 长按2秒PA4 回去 */
                if(longMask & KEY_EVENT_PA4) { Screen_ToTime(now); }
                else {
                    if(keyMask & KEY_EVENT_PA5) { Calc_MoveSel(0,1); }    /* PA5: 向右 */
                    if(keyMask & KEY_EVENT_PA6) { Calc_MoveSel(1,0); }    /* PA6: 向下 */
                    if(keyMask & KEY_EVENT_PA7) { Calc_Press(s_calcKeys[s_calcSelR][s_calcSelC]); }
                    if(releaseMask & KEY_EVENT_PA4) { Calc_CursorPrev(); }
                }
            } else if(s_screen==SCR_TIMER) {
                if(s_timerMode==TIMER_SET) {
                    /* 长按2秒PA4: 回时间界面(并武装1秒PA4防误触) */
                    if(longMask & KEY_EVENT_PA4) { s_timerMode=TIMER_IDLE; Screen_ToTime(now); }
                    else {
                        /* V27: PA4 只在**时分秒**之间循环, 不再轮到 Start。
                         * Start 只是 PA7 的提示框, 不是一个可以"选中"的槽位;
                         * 它会一直保持常态高亮(不闪烁), 也不会再吞掉 PA5/PA6。 */
                        if(releaseMask & KEY_EVENT_PA4) {
                            s_timerSelected=(uint8_t)((s_timerSelected+1u)%3u);
                        }
                        if((keyMask|repeatMask) & KEY_EVENT_PA5) { Timer_Step(1); }
                        if((keyMask|repeatMask) & KEY_EVENT_PA6) { Timer_Step(-1); }
                        /* PA7 = Start: 开始倒计时并回到时间界面 */
                        if(keyMask & KEY_EVENT_PA7) { if(Timer_Start(now)) { s_screen=SCR_TIME; } }
                    }
                } else if(s_timerMode==TIMER_VIEW) {
                    /* 运行中界面: 只有长按才有动作(避免误触), 短按不做任何事 */
                    if(longMask & KEY_EVENT_PA4) { s_timerMode=TIMER_RUN; Screen_ToTime(now); }
                    else if(repeatMask & KEY_EVENT_PA5) { Timer_CancelEarly(now); Screen_ToTime(now); }
                } else if(s_timerMode==TIMER_RUN) {
                    if(longMask & KEY_EVENT_PA4) { Screen_ToTime(now); }           /* 长按2秒PA4: 回时间界面(倒计时继续) */
                    /* V15 behaviour kept: a short PA5 shows the progress screen
                     * again while the countdown keeps running. */
                    else if((keyMask & KEY_EVENT_PA5) && !Timer_Pa5Blocked(now)) { s_timerMode=TIMER_VIEW; }
                } else { Screen_ToTime(now); }
            } else {
                /* --- clock screen ------------------------------------------- */
                if(keyMask & KEY_EVENT_PA5) { Menu_Enter(); }                      /* 短按PA5: 打开设置界面 */
                else if(keyMask & KEY_EVENT_PA6) { Menu2_Enter(); }                /* 短按PA6: 打开设置2 */
                else if(keyMask & KEY_EVENT_PA7) { SetMenu_Enter(); }              /* V26 短按PA7: 打开设置3 */
                else {
                    uint8_t pa4=(uint8_t)(((keyMask & KEY_EVENT_PA4)!=0u && !Pa4_ViewBlocked(now))?KEY_EVENT_PA4:0u);
                    View_Step(now,pa4);                                            /* 流动表盘 <-> 黑洞时间界面 */
                }
            }
            /* --- 3) render whatever screen is current now -------------------- */
            if(s_timerMode==TIMER_ALARM) { Alert_Update(now); oledDirty=Scene_Alarm(now); }
            else if(s_screen==SCR_MENU) { Scene_Menu(t,s_menuSel); }
            else if(s_screen==SCR_STOP) {
                /* V21: full-speed stopwatch. Only the first frame is a full
                 * repaint; every later frame flushes just the changed columns. */
                if(s_stopFresh) { s_stopFresh=0u; OLED_Clear(); }
                Stopwatch_Render(t,now);
                oledPartial=1u;
            } else if(s_screen==SCR_MENU2) { Scene_Menu2(t,s_menu2Sel); }
            else if(s_screen==SCR_GAME) {
                if(s_gameFresh) { s_gameFresh=0u; OLED_Clear(); }
                Scene_GameMenu(t,s_tMenuSel);
                oledPartial=0u;                       /* 整屏重画, 走整屏刷新更省 */
            } else if(s_screen==SCR_JUMP) {
                if(s_jpFirst) { s_jpFirst=0u; OLED_Clear(); }
                if(s_jpSt==4u) { Scene_GameOver(t,s_jpScore,s_goSel); }  /* 摔落后结算 */
                else           { Jp_Render(t); }
                oledPartial=0u;
            } else if(s_screen==SCR_TETRIS) {
                /* 重力 + 321 倒数 */
                if(s_tCount) {
                    uint32_t el=now-s_tCountAt;
                    uint8_t left=(uint8_t)(3u-(uint8_t)(el/1000u));
                    if(el>=3000u) { s_tCount=0u; s_tDropAt=now; }
                    else if(left!=s_tCount) { s_tCount=left; }
                } else if(!s_tPaused && !s_tOver) {
                    if((uint32_t)(now-s_tDropAt)>=Tet_Gravity()) { s_tDropAt=now; Tet_Step(now); }
                }
                if(s_tOver && !s_tOverSeen) { s_tOverSeen=1u; s_goSel=0u; Scene_GameOverBegin(now); }
                if(!s_tOver) { s_tOverSeen=0u; }
                if(s_tOver) {
                    Scene_GameOver(t,s_tScore,s_goSel);   /* 烟花+得分+两图标 */
                    oledPartial=0u;                      /* 烟花整屏, 必须整刷 */
                } else {
                    Scene_Tetris(t,s_tBoard,s_tCells,4u,s_tScore,
                                 s_tNextCells,4u,s_tPaused,s_tCount);
                    oledPartial=1u;  /* 增量重画: 只发变化的几十字节 */
                }
            } else if(s_screen==SCR_CALC) {
                if(s_calcFresh) { s_calcFresh=0u; OLED_Clear(); }
                Calc_Render(t);
                oledPartial=1u;
            } else if(s_screen==SCR_SET) {
                Scene_SetMenu(t,s_setSel);
            } else if(s_screen==SCR_DATE) {
                Scene_DateEditor(t,s_deYear,s_deMon,s_deDay,
                                 De_Weekday(s_deYear,s_deMon,s_deDay),
                                 s_deHour,s_deMin,s_deSec,s_deSel);
                /* 编辑器每帧只改一两格 -> 局部刷新。但它请求整屏时(首帧)本帧
                 * 就整屏发一次, 不能先局部发一次再整屏发一次。 */
                oledPartial=(uint8_t)(OLED_TakeFullFlush()?0u:1u);
            } else if(s_screen==SCR_ACC) {
                Acc_Render(now);
                oledPartial=1u;
            } else if(s_screen==SCR_TIMER) {
                if(s_timerMode==TIMER_SET) { Scene_TimerPanel(t,ms,s_timerHour,s_timerMinute,s_timerSecond,s_timerSelected); }
                else if(s_timerMode==TIMER_VIEW) { Timer_RenderProgress(now,ms); }
                else { RenderTimeScreen(now,t,ms,&info); }
            } else { RenderTimeScreen(now,t,ms,&info); }
            Led_FollowTimer();
        }
        if(oledDirty) { if(oledPartial) { OLED_UpdatePartial(); } else { OLED_Update(); } }
#if defined(FW_SIMULATOR)
        if(Sim_Yield()) { return 0; }   /* one hardware loop iteration per call */
#endif
    }
}
