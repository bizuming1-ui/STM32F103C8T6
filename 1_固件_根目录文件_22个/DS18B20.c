/* Original one-wire scheduling/timing. DQ PA3, external 4.7k pull-up. */
#include "main.h"

#if defined(FW_SIMULATOR)
/* ---------------------------------------------------------------------------
 * Browser-simulator sensor model.
 * The wasm build has no 1-Wire bus and no DWT cycle counter, so the DS18B20
 * is replaced by a value fed from the page.  Format, scaling and validity
 * semantics are identical to the real driver (tenths of a degree Celsius,
 * -880..+2000 raw range), so Scene.c draws exactly the same temperature
 * field as the board does with a real sensor attached.
 * ------------------------------------------------------------------------ */
static uint8_t s_valid=1u;
static int16_t s_tenths=245;      /* 24.5 C default room temperature */
extern int16_t Sim_Sensor_Tenths(void);
void DS18B20_Init(void) { s_valid=1u; s_tenths=245; }
void DS18B20_Update(uint32_t nowMs) { (void)nowMs; s_tenths=Sim_Sensor_Tenths(); s_valid=1u; }
uint8_t DS18B20_IsValid(void) { return s_valid; }
int16_t DS18B20_GetTenths(void) { return s_tenths; }
#else
static uint8_t s_valid=0u, s_waiting=0u, s_started=0u, s_timerOk=0u;
static int16_t s_tenths=0;
static uint32_t s_lastConv=0u, s_convAt=0u;
#define DS_REG_DEMCR (*(volatile uint32_t *)0xE000EDFCuL)
#define DS_REG_CTRL (*(volatile uint32_t *)0xE0001000uL)
#define DS_REG_CYCCNT (*(volatile uint32_t *)0xE0001004uL)
#define DS_TRCENA (1uL<<24)
#define DS_CYCCNTENA (1uL<<0)
#define DS_NOCYCCNT (1uL<<25)
#define DQ_LOW() (DS_PORT->BRR=DS_PIN)
#define DQ_RELEASE() (DS_PORT->BSRR=DS_PIN)
#define DQ_READ() ((DS_PORT->IDR & DS_PIN)?1u:0u)
#if defined(__CC_ARM)
__asm uint32_t DS_SaveIRQ(void)
{
    MRS r0, PRIMASK
    CPSID i
    BX lr
}
__asm void DS_RestoreIRQ(uint32_t primask)
{
    MSR PRIMASK, r0
    BX lr
}
#else
static uint32_t DS_SaveIRQ(void)
{
    uint32_t p=__get_PRIMASK();
    __disable_irq();
    return p;
}
static void DS_RestoreIRQ(uint32_t p) { __set_PRIMASK(p); }
#endif
static void DelayUs(uint32_t us)
{
    uint32_t start=DS_REG_CYCCNT, ticks=us*DS_CYC_PER_US, guard=ticks*2u+128u;
    while((uint32_t)(DS_REG_CYCCNT-start)<ticks) { if(--guard==0u) { s_timerOk=0u; break; } }
}
static uint8_t OW_Reset(void)
{
    uint8_t presence;
    uint32_t p;
    if(!s_timerOk || DQ_READ()==0u) { return 0u; }
    DQ_LOW(); DelayUs(480u); p=DS_SaveIRQ(); DQ_RELEASE(); DelayUs(70u);
    presence=(uint8_t)(DQ_READ()==0u); DS_RestoreIRQ(p); DelayUs(410u);
    return (uint8_t)(presence && s_timerOk && DQ_READ());
}
static void OW_WriteBit(uint8_t bit)
{
    uint32_t p=DS_SaveIRQ();
    DQ_LOW();
    if(bit) { DelayUs(6u); DQ_RELEASE(); DS_RestoreIRQ(p); DelayUs(64u); }
    else { DelayUs(60u); DQ_RELEASE(); DS_RestoreIRQ(p); DelayUs(10u); }
}
static uint8_t OW_ReadBit(void)
{
    uint8_t b;
    uint32_t p=DS_SaveIRQ();
    DQ_LOW(); DelayUs(2u); DQ_RELEASE(); DelayUs(8u); b=DQ_READ();
    DS_RestoreIRQ(p); DelayUs(60u); return b;
}
static void OW_WriteByte(uint8_t v)
{
    uint8_t i;
    for(i=0u;i<8u;i++) { OW_WriteBit((uint8_t)(v&1u)); v=(uint8_t)(v>>1); }
}
static uint8_t OW_ReadByte(void)
{
    uint8_t i,v=0u;
    for(i=0u;i<8u;i++) { v=(uint8_t)(v | (uint8_t)(OW_ReadBit()<<i)); }
    return v;
}
static uint8_t CRC8(const uint8_t *d, uint8_t n)
{
    uint8_t c=0u,i,j,b,mix;
    for(i=0u;i<n;i++) {
        b=d[i];
        for(j=0u;j<8u;j++) { mix=(uint8_t)((c^b)&1u); c=(uint8_t)(c>>1); if(mix) { c^=0x8Cu; } b=(uint8_t)(b>>1); }
    }
    return c;
}
static void DS_ReadScratch(void)
{
    uint8_t b[9],i;
    int32_t raw;
    if(!OW_Reset()) { s_valid=0u; return; }
    OW_WriteByte(0xCCu); OW_WriteByte(0xBEu);
    for(i=0u;i<9u;i++) { b[i]=OW_ReadByte(); }
    if(s_timerOk && CRC8(b,8u)==b[8] && (b[4]&0x1Fu)==0x1Fu) {
        raw=(int32_t)(int16_t)(((uint16_t)b[1]<<8)|b[0]);
        if((b[4]&0x60u)==0x00u) { raw &= ~7L; }
        else if((b[4]&0x60u)==0x20u) { raw &= ~3L; }
        else if((b[4]&0x60u)==0x40u) { raw &= ~1L; }
        if(raw < -880 || raw > 2000) { s_valid=0u; return; }
        s_tenths=(int16_t)((raw*5)/8); s_valid=1u;
    } else { s_valid=0u; }
}
void DS18B20_Init(void)
{
    uint32_t before;
    volatile uint32_t i;
    RCC->APB2ENR |= DS_RCC_MASK; DS_PORT->BSRR=DS_PIN;
    DS_PORT->CRL=(DS_PORT->CRL & ~(0xFuL << (DS_PIN_INDEX*4u))) | (0x7uL << (DS_PIN_INDEX*4u));
    DS_REG_DEMCR |= DS_TRCENA; s_timerOk=0u;
    if(!(DS_REG_CTRL & DS_NOCYCCNT)) {
        DS_REG_CTRL |= DS_CYCCNTENA; before=DS_REG_CYCCNT;
        for(i=0u;i<128u;i++) { }
        s_timerOk=(uint8_t)(DS_REG_CYCCNT!=before);
    }
    s_valid=s_waiting=s_started=0u; s_tenths=0; s_lastConv=s_convAt=0u;
}
void DS18B20_Update(uint32_t nowMs)
{
    if(!s_timerOk) { s_valid=0u; DQ_RELEASE(); return; }
    if(!s_waiting) {
        if(!s_started || (uint32_t)(nowMs-s_lastConv)>=DS_CONV_MS) {
            s_started=1u; s_lastConv=nowMs;
            if(OW_Reset()) { OW_WriteByte(0xCCu); OW_WriteByte(0x44u); s_waiting=1u; s_convAt=nowMs; }
            else { s_valid=0u; }
        }
    } else if((uint32_t)(nowMs-s_convAt)>=DS_WAIT_MS) {
        s_waiting=0u; if(OW_ReadBit()) { DS_ReadScratch(); } else { s_valid=0u; }
    }
}
uint8_t DS18B20_IsValid(void) { return s_valid; }
int16_t DS18B20_GetTenths(void) { return s_tenths; }

#endif
