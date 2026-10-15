/* Original four independent debouncers and PA5/PA6 repeat semantics. */
#include "main.h"
static uint8_t s_raw[4], s_stable[4], s_armed[4];
static uint32_t s_rawSince[4], s_pressAt[4], s_repeatAt[4];
static uint8_t s_repeatMask;
uint8_t Key_Level(void) { return (uint8_t)((KEY_PORT->IDR & KEY_ALL_PINS) >> KEY_FIRST_PIN); }
void Key_Init(void)
{
    uint8_t i;
    RCC->APB2ENR |= KEY_RCC_MASK;
    KEY_PORT->CRL = (KEY_PORT->CRL & ~(0xFFFFuL << 16)) | (0x8888uL << 16);
#if KEY_ACTIVE_HIGH
    KEY_PORT->BRR = KEY_ALL_PINS;
#else
    KEY_PORT->BSRR = KEY_ALL_PINS;
#endif
    for (i=0u; i<4u; i++) {
        s_raw[i] = (KEY_PORT->IDR & (KEY_PIN_PA4 << i)) ? 1u : 0u;
        s_stable[i] = s_raw[i]; s_armed[i] = (uint8_t)(s_stable[i] == KEY_RELEASED_LEVEL);
        s_rawSince[i] = s_pressAt[i] = s_repeatAt[i] = 0u;
    }
    s_repeatMask = 0u;
}
uint8_t Key_Scan(uint32_t nowMs)
{
    uint8_t i, level, eventMask=0u;
    s_repeatMask=0u;
    for (i=0u; i<4u; i++) {
        level = (KEY_PORT->IDR & (KEY_PIN_PA4 << i)) ? 1u : 0u;
        if (level != s_raw[i]) { s_raw[i]=level; s_rawSince[i]=nowMs; }
        if (level != s_stable[i] && (uint32_t)(nowMs-s_rawSince[i]) >= KEY_DEBOUNCE_MS) {
            s_stable[i]=level;
            if (level == KEY_PRESSED_LEVEL) {
                if (s_armed[i]) { eventMask |= (uint8_t)(KEY_EVENT_PA4 << i); s_armed[i]=0u; s_pressAt[i]=nowMs; s_repeatAt[i]=nowMs; }
            } else { s_armed[i]=1u; }
        }
        if ((i==1u || i==2u) && s_stable[i]==KEY_PRESSED_LEVEL && level==KEY_PRESSED_LEVEL && !s_armed[i] &&
            (uint32_t)(nowMs-s_pressAt[i])>=KEY_REPEAT_DELAY_MS && (uint32_t)(nowMs-s_repeatAt[i])>=KEY_REPEAT_RATE_MS) {
            s_repeatMask |= (uint8_t)(KEY_EVENT_PA4 << i); s_repeatAt[i]=nowMs;
        }
    }
    return eventMask;
}
uint8_t Key_Repeat(void) { return s_repeatMask; }
uint8_t Key_IsDown(void)
{
    uint8_t i, m=0u;
    for(i=0u;i<4u;i++) { if(s_stable[i]==KEY_PRESSED_LEVEL) { m |= (uint8_t)(KEY_EVENT_PA4 << i); } }
    return m;
}
