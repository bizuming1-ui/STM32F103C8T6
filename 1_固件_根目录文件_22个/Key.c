/* Original four independent debouncers and PA5/PA6 repeat semantics.
 *
 * V18 addition (required by the settings screen):
 *   - Key_Long()    : fires ONCE while a key is held for KEY_LONG_MS (2 s).
 *                     This is the "长按2秒PA4" gesture.
 *   - Key_Release() : fires ONCE when a key is let go BEFORE KEY_LONG_MS,
 *                     i.e. a genuine short tap. Actions that must not happen
 *                     at the beginning of a long press (menu selection,
 *                     countdown digit select) use this instead of the press
 *                     event, so holding PA4 to leave a screen can never also
 *                     trigger the short action.
 * Key_Scan() keeps returning the same press events as before, so every
 * existing behaviour (view switching, timer adjust, repeat on PA5/PA6)
 * is bit-for-bit unchanged.
 */
#include "main.h"
static uint8_t s_raw[4], s_stable[4], s_armed[4];
static uint32_t s_rawSince[4], s_pressAt[4], s_repeatAt[4];
static uint8_t s_repeatMask;
static uint8_t s_holdValid[4], s_longFired[4];
static uint32_t s_holdAt[4];
static uint8_t s_longMask, s_releaseMask;
#if defined(FW_SIMULATOR)
static uint32_t s_scanCalls=0u;
#endif
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
        s_rawSince[i] = s_pressAt[i] = s_repeatAt[i] = s_holdAt[i] = 0u;
        /* A key that is already down at reset never produces press, long or
         * release events until it has been let go first. */
        s_holdValid[i] = 0u; s_longFired[i] = 0u;
    }
    s_repeatMask = 0u; s_longMask = 0u; s_releaseMask = 0u;
}
uint8_t Key_Scan(uint32_t nowMs)
{
    uint8_t i, level, eventMask=0u;
#if defined(FW_SIMULATOR)
    s_scanCalls++;
#endif
    s_repeatMask=0u; s_longMask=0u; s_releaseMask=0u;
    for (i=0u; i<4u; i++) {
        uint8_t bit=(uint8_t)(KEY_EVENT_PA4 << i);
        level = (KEY_PORT->IDR & (KEY_PIN_PA4 << i)) ? 1u : 0u;
        if (level != s_raw[i]) { s_raw[i]=level; s_rawSince[i]=nowMs; }
        if (level != s_stable[i] && (uint32_t)(nowMs-s_rawSince[i]) >= KEY_DEBOUNCE_MS) {
            s_stable[i]=level;
            if (level == KEY_PRESSED_LEVEL) {
                if (s_armed[i]) {
                    eventMask |= bit; s_armed[i]=0u;
                    s_pressAt[i]=nowMs; s_repeatAt[i]=nowMs;
                    s_holdAt[i]=nowMs; s_holdValid[i]=1u; s_longFired[i]=0u;
                }
            } else {
                s_armed[i]=1u;
                if (s_holdValid[i]) {
                    s_holdValid[i]=0u;
                    if (!s_longFired[i]) { s_releaseMask |= bit; }  /* short tap */
                    s_longFired[i]=0u;
                }
            }
        }
        /* PA5/PA6 auto-repeat has two stages (V19): after 2 s of holding it
         * repeats every 100 ms, and after 4 s it speeds up to 40 ms - that is
         * the "长按2秒进入快速加减" behaviour of the countdown panel. */
        if ((i==1u || i==2u) && s_stable[i]==KEY_PRESSED_LEVEL && level==KEY_PRESSED_LEVEL && !s_armed[i]) {
            uint32_t held=(uint32_t)(nowMs-s_pressAt[i]);
            uint32_t rate=(held>=KEY_REPEAT_FAST_DELAY_MS)?KEY_REPEAT_FAST_RATE_MS:KEY_REPEAT_RATE_MS;
            if (held>=KEY_REPEAT_DELAY_MS && (uint32_t)(nowMs-s_repeatAt[i])>=rate) {
                s_repeatMask |= bit; s_repeatAt[i]=nowMs;
            }
        }
        /* Long press: exactly one event per hold, KEY_LONG_MS after the press
         * was accepted (so a key already held at reset is ignored). */
        if (s_holdValid[i] && !s_longFired[i] && (uint32_t)(nowMs-s_holdAt[i])>=KEY_LONG_MS) {
            s_longFired[i]=1u; s_longMask |= bit;
        }
    }
    return eventMask;
}
#if defined(FW_SIMULATOR)
/* Simulator-only probe (never compiled by Keil): exposes PA5's debounce state
 * so the page/harness can show exactly what the driver sees. */
__attribute__((export_name("sim_key_calls"))) uint32_t Sim_KeyCalls(void) { return s_scanCalls; }
__attribute__((export_name("sim_key_debug"))) uint32_t Sim_KeyDebug(void)
{
    uint32_t v=0u;
    v |= (uint32_t)s_armed[1];
    v |= (uint32_t)s_raw[1]<<1;
    v |= (uint32_t)s_stable[1]<<2;
    v |= (uint32_t)s_holdValid[1]<<3;
    v |= (uint32_t)s_longFired[1]<<4;
    v |= (uint32_t)s_repeatMask<<8;
    v |= (uint32_t)s_longMask<<16;
    v |= (uint32_t)s_releaseMask<<24;
    return v;
}
#endif
uint8_t Key_Repeat(void) { return s_repeatMask; }
uint8_t Key_Long(void) { return s_longMask; }
uint8_t Key_Release(void) { return s_releaseMask; }
uint8_t Key_IsDown(void)
{
    uint8_t i, m=0u;
    for(i=0u;i<4u;i++) { if(s_stable[i]==KEY_PRESSED_LEVEL) { m |= (uint8_t)(KEY_EVENT_PA4 << i); } }
    return m;
}
