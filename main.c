
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
static uint8_t Date_DaysIn(uint16_t y,uint8_t m)
{
    static const uint8_t dim[12]={31,28,31,30,31,30,31,31,30,31,30,31};
    return (m==2u && Date_IsLeap(y))?29u:dim[m-1u];
}
static uint8_t Date_Weekday(uint16_t y,uint8_t m,uint8_t d)
{
    static const uint8_t t[12]={0,3,2,5,0,3,5,1,4,6,2,4};
    uint32_t yy=y;
    if(m<3u) { yy--; }
    return (uint8_t)((yy+yy/4u-yy/100u+yy/400u+t[m-1u]+d)%7u);
}
static void Date_Set(uint16_t y,uint8_t m,uint8_t d) { s_date.year=y; s_date.month=m; s_date.day=d; s_date.wday=Date_Weekday(y,m,d); }
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
static const int16_t s_sin[1024] = {
    0, 101, 201, 302, 402, 503, 603, 704, 804, 904, 1005, 1105, 1205, 1306, 1406, 1506,
    1606, 1706, 1806, 1906, 2006, 2105, 2205, 2305, 2404, 2503, 2603, 2702, 2801, 2900, 2999, 3098,
    3196, 3295, 3393, 3492, 3590, 3688, 3786, 3883, 3981, 4078, 4176, 4273, 4370, 4467, 4563, 4660,
    4756, 4852, 4948, 5044, 5139, 5235, 5330, 5425, 5520, 5614, 5708, 5803, 5897, 5990, 6084, 6177,
    6270, 6363, 6455, 6547, 6639, 6731, 6823, 6914, 7005, 7096, 7186, 7276, 7366, 7456, 7545, 7635,
    7723, 7812, 7900, 7988, 8076, 8163, 8250, 8337, 8423, 8509, 8595, 8680, 8765, 8850, 8935, 9019,
    9102, 9186, 9269, 9352, 9434, 9516, 9598, 9679, 9760, 9841, 9921, 10001, 10080, 10159, 10238, 10316,
    10394, 10471, 10549, 10625, 10702, 10778, 10853, 10928, 11003, 11077, 11151, 11224, 11297, 11370, 11442, 11514,
    11585, 11656, 11727, 11797, 11866, 11935, 12004, 12072, 12140, 12207, 12274, 12340, 12406, 12472, 12537, 12601,
    12665, 12729, 12792, 12854, 12916, 12978, 13039, 13100, 13160, 13219, 13279, 13337, 13395, 13453, 13510, 13567,
    13623, 13678, 13733, 13788, 13842, 13896, 13949, 14001, 14053, 14104, 14155, 14206, 14256, 14305, 14354, 14402,
    14449, 14497, 14543, 14589, 14635, 14680, 14724, 14768, 14811, 14854, 14896, 14937, 14978, 15019, 15059, 15098,
    15137, 15175, 15213, 15250, 15286, 15322, 15357, 15392, 15426, 15460, 15493, 15525, 15557, 15588, 15619, 15649,
    15679, 15707, 15736, 15763, 15791, 15817, 15843, 15868, 15893, 15917, 15941, 15964, 15986, 16008, 16029, 16049,
    16069, 16088, 16107, 16125, 16143, 16160, 16176, 16192, 16207, 16221, 16235, 16248, 16261, 16273, 16284, 16295,
    16305, 16315, 16324, 16332, 16340, 16347, 16353, 16359, 16364, 16369, 16373, 16376, 16379, 16381, 16383, 16384,
    16384, 16384, 16383, 16381, 16379, 16376, 16373, 16369, 16364, 16359, 16353, 16347, 16340, 16332, 16324, 16315,
    16305, 16295, 16284, 16273, 16261, 16248, 16235, 16221, 16207, 16192, 16176, 16160, 16143, 16125, 16107, 16088,
    16069, 16049, 16029, 16008, 15986, 15964, 15941, 15917, 15893, 15868, 15843, 15817, 15791, 15763, 15736, 15707,
    15679, 15649, 15619, 15588, 15557, 15525, 15493, 15460, 15426, 15392, 15357, 15322, 15286, 15250, 15213, 15175,
    15137, 15098, 15059, 15019, 14978, 14937, 14896, 14854, 14811, 14768, 14724, 14680, 14635, 14589, 14543, 14497,
    14449, 14402, 14354, 14305, 14256, 14206, 14155, 14104, 14053, 14001, 13949, 13896, 13842, 13788, 13733, 13678,
    13623, 13567, 13510, 13453, 13395, 13337, 13279, 13219, 13160, 13100, 13039, 12978, 12916, 12854, 12792, 12729,
    12665, 12601, 12537, 12472, 12406, 12340, 12274, 12207, 12140, 12072, 12004, 11935, 11866, 11797, 11727, 11656,
    11585, 11514, 11442, 11370, 11297, 11224, 11151, 11077, 11003, 10928, 10853, 10778, 10702, 10625, 10549, 10471,
    10394, 10316, 10238, 10159, 10080, 10001, 9921, 9841, 9760, 9679, 9598, 9516, 9434, 9352, 9269, 9186,
    9102, 9019, 8935, 8850, 8765, 8680, 8595, 8509, 8423, 8337, 8250, 8163, 8076, 7988, 7900, 7812,
    7723, 7635, 7545, 7456, 7366, 7276, 7186, 7096, 7005, 6914, 6823, 6731, 6639, 6547, 6455, 6363,
    6270, 6177, 6084, 5990, 5897, 5803, 5708, 5614, 5520, 5425, 5330, 5235, 5139, 5044, 4948, 4852,
    4756, 4660, 4563, 4467, 4370, 4273, 4176, 4078, 3981, 3883, 3786, 3688, 3590, 3492, 3393, 3295,
    3196, 3098, 2999, 2900, 2801, 2702, 2603, 2503, 2404, 2305, 2205, 2105, 2006, 1906, 1806, 1706,
    1606, 1506, 1406, 1306, 1205, 1105, 1005, 904, 804, 704, 603, 503, 402, 302, 201, 101,
    0, -101, -201, -302, -402, -503, -603, -704, -804, -904, -1005, -1105, -1205, -1306, -1406, -1506,
    -1606, -1706, -1806, -1906, -2006, -2105, -2205, -2305, -2404, -2503, -2603, -2702, -2801, -2900, -2999, -3098,
    -3196, -3295, -3393, -3492, -3590, -3688, -3786, -3883, -3981, -4078, -4176, -4273, -4370, -4467, -4563, -4660,
    -4756, -4852, -4948, -5044, -5139, -5235, -5330, -5425, -5520, -5614, -5708, -5803, -5897, -5990, -6084, -6177,
    -6270, -6363, -6455, -6547, -6639, -6731, -6823, -6914, -7005, -7096, -7186, -7276, -7366, -7456, -7545, -7635,
    -7723, -7812, -7900, -7988, -8076, -8163, -8250, -8337, -8423, -8509, -8595, -8680, -8765, -8850, -8935, -9019,
    -9102, -9186, -9269, -9352, -9434, -9516, -9598, -9679, -9760, -9841, -9921, -10001, -10080, -10159, -10238, -10316,
    -10394, -10471, -10549, -10625, -10702, -10778, -10853, -10928, -11003, -11077, -11151, -11224, -11297, -11370, -11442, -11514,
    -11585, -11656, -11727, -11797, -11866, -11935, -12004, -12072, -12140, -12207, -12274, -12340, -12406, -12472, -12537, -12601,
    -12665, -12729, -12792, -12854, -12916, -12978, -13039, -13100, -13160, -13219, -13279, -13337, -13395, -13453, -13510, -13567,
    -13623, -13678, -13733, -13788, -13842, -13896, -13949, -14001, -14053, -14104, -14155, -14206, -14256, -14305, -14354, -14402,
    -14449, -14497, -14543, -14589, -14635, -14680, -14724, -14768, -14811, -14854, -14896, -14937, -14978, -15019, -15059, -15098,
    -15137, -15175, -15213, -15250, -15286, -15322, -15357, -15392, -15426, -15460, -15493, -15525, -15557, -15588, -15619, -15649,
    -15679, -15707, -15736, -15763, -15791, -15817, -15843, -15868, -15893, -15917, -15941, -15964, -15986, -16008, -16029, -16049,
    -16069, -16088, -16107, -16125, -16143, -16160, -16176, -16192, -16207, -16221, -16235, -16248, -16261, -16273, -16284, -16295,
    -16305, -16315, -16324, -16332, -16340, -16347, -16353, -16359, -16364, -16369, -16373, -16376, -16379, -16381, -16383, -16384,
    -16384, -16384, -16383, -16381, -16379, -16376, -16373, -16369, -16364, -16359, -16353, -16347, -16340, -16332, -16324, -16315,
    -16305, -16295, -16284, -16273, -16261, -16248, -16235, -16221, -16207, -16192, -16176, -16160, -16143, -16125, -16107, -16088,
    -16069, -16049, -16029, -16008, -15986, -15964, -15941, -15917, -15893, -15868, -15843, -15817, -15791, -15763, -15736, -15707,
    -15679, -15649, -15619, -15588, -15557, -15525, -15493, -15460, -15426, -15392, -15357, -15322, -15286, -15250, -15213, -15175,
    -15137, -15098, -15059, -15019, -14978, -14937, -14896, -14854, -14811, -14768, -14724, -14680, -14635, -14589, -14543, -14497,
    -14449, -14402, -14354, -14305, -14256, -14206, -14155, -14104, -14053, -14001, -13949, -13896, -13842, -13788, -13733, -13678,
    -13623, -13567, -13510, -13453, -13395, -13337, -13279, -13219, -13160, -13100, -13039, -12978, -12916, -12854, -12792, -12729,
    -12665, -12601, -12537, -12472, -12406, -12340, -12274, -12207, -12140, -12072, -12004, -11935, -11866, -11797, -11727, -11656,
    -11585, -11514, -11442, -11370, -11297, -11224, -11151, -11077, -11003, -10928, -10853, -10778, -10702, -10625, -10549, -10471,
    -10394, -10316, -10238, -10159, -10080, -10001, -9921, -9841, -9760, -9679, -9598, -9516, -9434, -9352, -9269, -9186,
    -9102, -9019, -8935, -8850, -8765, -8680, -8595, -8509, -8423, -8337, -8250, -8163, -8076, -7988, -7900, -7812,
    -7723, -7635, -7545, -7456, -7366, -7276, -7186, -7096, -7005, -6914, -6823, -6731, -6639, -6547, -6455, -6363,
    -6270, -6177, -6084, -5990, -5897, -5803, -5708, -5614, -5520, -5425, -5330, -5235, -5139, -5044, -4948, -4852,
    -4756, -4660, -4563, -4467, -4370, -4273, -4176, -4078, -3981, -3883, -3786, -3688, -3590, -3492, -3393, -3295,
    -3196, -3098, -2999, -2900, -2801, -2702, -2603, -2503, -2404, -2305, -2205, -2105, -2006, -1906, -1806, -1706,
    -1606, -1506, -1406, -1306, -1205, -1105, -1005, -904, -804, -704, -603, -503, -402, -302, -201, -101
};

int32_t Sin14(uint16_t a)
{
    uint32_t idx=a>>6;
    int32_t frac=a&63,s0=s_sin[idx],s1=s_sin[(idx+1u)&1023u];
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
static uint8_t s_view=VIEW_DIAL,s_transOn=0u,s_transFrom=VIEW_DIAL,s_transTo=VIEW_HOLE,s_pending=0u,s_bootDone=0u;
static uint32_t s_transStart=0u;
static uint8_t s_timerMode=TIMER_IDLE,s_pa5BlockOn=0u,s_timerReturnView=VIEW_DIAL;
static uint8_t s_timerHour=0u,s_timerMinute=0u,s_timerSecond=0u,s_timerSelected=2u;
static uint32_t s_pa5BlockAt=0u,s_timerStart=0u,s_timerDuration=0u;
static uint8_t s_gramA[8][128],s_gramB[8][128];
/* V18 screen state + anti-mistouch lock */
static uint8_t s_screen=SCR_TIME,s_menuSel=MENU_SEL_BELL,s_stopFresh=0u;
static uint8_t s_pa4BlockOn=0u;
static uint32_t s_pa4BlockAt=0u;
static uint8_t Boot_IsDone(uint32_t t) { if(t>=BOOT_DONE_MS) { s_bootDone=1u; } return s_bootDone; }
static void View_StartTrans(uint32_t now) { s_transFrom=s_view; s_transTo=(s_view==VIEW_DIAL)?VIEW_HOLE:VIEW_DIAL; s_transOn=1u; s_transStart=now; }
static uint32_t Timer_TotalSeconds(void) { return (uint32_t)s_timerHour*3600u+(uint32_t)s_timerMinute*60u+s_timerSecond; }
static void Timer_EnterSetup(void)
{
    s_timerReturnView=s_transOn?s_transTo:s_view; s_view=s_timerReturnView; s_timerMode=TIMER_SET;
    s_timerHour=s_timerMinute=s_timerSecond=0u; s_timerSelected=2u; s_transOn=s_pending=0u; Alert_Stop();
}
static void Timer_Adjust(int8_t direction)
{
    if(direction>0) {
        if(s_timerSelected==0u) { if(s_timerHour<99u) { s_timerHour++; } }
        else if(s_timerSelected==1u) {
            if(s_timerMinute<59u) { s_timerMinute++; } else if(s_timerHour<99u) { s_timerMinute=0u; s_timerHour++; }
        } else {
            if(s_timerSecond<59u) { s_timerSecond++; }
            else if(s_timerMinute<59u) { s_timerSecond=0u; s_timerMinute++; }
            else if(s_timerHour<99u) { s_timerSecond=s_timerMinute=0u; s_timerHour++; }
        }
    } else if(direction<0) {
        if(s_timerSelected==0u) { if(s_timerHour>0u) { s_timerHour--; } }
        else if(s_timerSelected==1u) {
            if(s_timerMinute>0u) { s_timerMinute--; } else if(s_timerHour>0u) { s_timerMinute=59u; s_timerHour--; }
        } else {
            if(s_timerSecond>0u) { s_timerSecond--; }
            else if(s_timerMinute>0u) { s_timerSecond=59u; s_timerMinute--; }
            else if(s_timerHour>0u) { s_timerSecond=s_timerMinute=59u; s_timerHour--; }
        }
    }
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
static void Stopwatch_Enter(void)
{
    s_screen=SCR_STOP; s_stopFresh=1u;
    if(s_transOn) { s_transOn=0u; s_view=s_transTo; } s_pending=0u;
}
/* LED mirrors the countdown: on while it runs (setup/progress and while the
 * clock face is displayed), untouched during the alarm (that blinks it). */
static void Led_FollowTimer(void)
{
    if(s_timerMode==TIMER_RUN || s_timerMode==TIMER_VIEW) { LED_On(); }
    else if(s_timerMode!=TIMER_ALARM) { LED_Off(); }
}
static void Timer_RenderProgress(uint32_t now)
{
    uint32_t elapsed=now-s_timerStart,remain,pct=0u;
    if(elapsed>s_timerDuration) { elapsed=s_timerDuration; } remain=s_timerDuration-elapsed;
    if(s_timerDuration>=1000u) { pct=elapsed/(s_timerDuration/1000u); } Scene_TimerProgress(now,remain,pct);
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
        uint8_t oledDirty=1u;
        SceneInfo info;
        DS18B20_Update(now); ms=Clock_MsOfDay();
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
                if(longMask & KEY_EVENT_PA4) { Screen_ToTime(now); }              /* 长按2秒PA4: 回时间界面 */
            } else if(s_screen==SCR_TIMER) {
                if(s_timerMode==TIMER_SET) {
                    if(longMask & KEY_EVENT_PA4) { s_timerMode=TIMER_IDLE; Screen_ToTime(now); }
                    else {
                        if(releaseMask & KEY_EVENT_PA4) { s_timerSelected=(uint8_t)((s_timerSelected+1u)%3u); }
                        if((keyMask|repeatMask) & KEY_EVENT_PA5) { Timer_Adjust(1); }
                        if((keyMask|repeatMask) & KEY_EVENT_PA6) { Timer_Adjust(-1); }
                        if(keyMask & KEY_EVENT_PA7) { (void)Timer_Start(now); }
                    }
                } else if(s_timerMode==TIMER_VIEW) {
                    if(longMask & KEY_EVENT_PA4) { Screen_ToTime(now); }           /* 长按2秒PA4: 回时间界面 */
                    else if(repeatMask & KEY_EVENT_PA5) { Timer_CancelEarly(now); Screen_ToTime(now); }
                    else if(releaseMask & KEY_EVENT_PA4) { s_timerMode=TIMER_RUN; }
                } else if(s_timerMode==TIMER_RUN) {
                    if(longMask & KEY_EVENT_PA4) { Screen_ToTime(now); }           /* 长按2秒PA4: 回时间界面(倒计时继续) */
                    /* V15 behaviour kept: a short PA5 shows the progress screen
                     * again while the countdown keeps running. */
                    else if((keyMask & KEY_EVENT_PA5) && !Timer_Pa5Blocked(now)) { s_timerMode=TIMER_VIEW; }
                } else { Screen_ToTime(now); }
            } else {
                /* --- clock screen ------------------------------------------- */
                if(keyMask & KEY_EVENT_PA5) { Menu_Enter(); }                      /* 短按PA5: 打开设置界面 */
                else {
                    uint8_t pa4=(uint8_t)(((keyMask & KEY_EVENT_PA4)!=0u && !Pa4_ViewBlocked(now))?KEY_EVENT_PA4:0u);
                    View_Step(now,pa4);                                            /* 流动表盘 <-> 黑洞时间界面 */
                }
            }
            /* --- 3) render whatever screen is current now -------------------- */
            if(s_timerMode==TIMER_ALARM) { Alert_Update(now); oledDirty=Scene_Alarm(now); }
            else if(s_screen==SCR_MENU) { Scene_Menu(t,s_menuSel); }
            else if(s_screen==SCR_STOP) {
                if(s_stopFresh) { s_stopFresh=0u; OLED_Clear(); }
                else { oledDirty=0u; }
            } else if(s_screen==SCR_TIMER) {
                if(s_timerMode==TIMER_SET) { Scene_TimerSetup(now,s_timerHour,s_timerMinute,s_timerSecond,s_timerSelected); }
                else if(s_timerMode==TIMER_VIEW) { Timer_RenderProgress(now); }
                else { RenderTimeScreen(now,t,ms,&info); }
            } else { RenderTimeScreen(now,t,ms,&info); }
            Led_FollowTimer();
        }
        if(oledDirty) { OLED_Update(); }
#if defined(FW_SIMULATOR)
        if(Sim_Yield()) { return 0; }   /* one hardware loop iteration per call */
#endif
    }
}
