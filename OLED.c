/* Original software-I2C transport and pixel geometry retained.
 * Transfers are bounded; failed ACKs never enter an infinite error loop. */
#include "OLED.h"
uint8_t OLED_GRAM[OLED_H/8][OLED_W];
static int32_t s_clipCx=0, s_clipCy=0, s_clipR2=-1;
static uint8_t s_transferOK=1u;
#define SCL_H() (OLED_SCL_PORT->BSRR=OLED_SCL_PIN)
#define SCL_L() (OLED_SCL_PORT->BRR=OLED_SCL_PIN)
#define SDA_H() (OLED_SDA_PORT->BSRR=OLED_SDA_PIN)
#define SDA_L() (OLED_SDA_PORT->BRR=OLED_SDA_PIN)
#define SDA_READ() ((OLED_SDA_PORT->IDR & OLED_SDA_PIN)!=0u)
static void I2C_Delay(void) { volatile uint32_t n=OLED_I2C_DELAY_TICKS; while(n--) { } }
static void I2C_Stop(void) { SDA_L(); I2C_Delay(); SCL_H(); I2C_Delay(); SDA_H(); I2C_Delay(); }
static void I2C_Start(void) { SDA_H(); SCL_H(); I2C_Delay(); SDA_L(); I2C_Delay(); SCL_L(); I2C_Delay(); }
static uint8_t I2C_WriteByte(uint8_t b)
{
    uint8_t i,ack;
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
    while(ms--) { volatile uint32_t n=12000u; while(n--) { } }
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
    I2C_WriteByte(0x00u); I2C_WriteByte(0x21u); I2C_WriteByte(0x00u); I2C_WriteByte(0x7Fu);
    I2C_WriteByte(0x22u); I2C_WriteByte(0x00u); I2C_WriteByte(0x07u); I2C_Stop();
    I2C_Start();
    if(!I2C_WriteByte(OLED_I2C_ADDR)) { I2C_Stop(); return; }
    I2C_WriteByte(0x40u);
    for(i=0u;i<1024u;i++) { if(!I2C_WriteByte(p[i])) { I2C_Stop(); return; } }
    I2C_Stop();
#endif
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
void OLED_Clear(void) { uint16_t i; uint8_t *p=&OLED_GRAM[0][0]; for(i=0u;i<1024u;i++) { p[i]=0u; } }
void OLED_Fill(uint8_t color) { uint16_t i; uint8_t *p=&OLED_GRAM[0][0]; for(i=0u;i<1024u;i++) { p[i]=color?255u:0u; } }
void OLED_SetClipCircle(int32_t cx,int32_t cy,int32_t r) { s_clipCx=cx; s_clipCy=cy; s_clipR2=(r<0)?-1:r*r; }
void OLED_DrawPixel(int32_t x,int32_t y,uint8_t c)
{
    int32_t dx,dy;
    if((uint32_t)x>=128u || (uint32_t)y>=64u) { return; }
    if(s_clipR2>=0) { dx=x-s_clipCx; dy=y-s_clipCy; if(dx*dx+dy*dy>s_clipR2) { return; } }
    if(c) { OLED_GRAM[y>>3][x] |= (uint8_t)(1u<<(y&7)); }
    else { OLED_GRAM[y>>3][x] &= (uint8_t)~(1u<<(y&7)); }
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
