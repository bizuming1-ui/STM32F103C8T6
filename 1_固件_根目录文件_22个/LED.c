/* Active buzzer behind a high-side PNP: PA8 HIGH = silent, LOW = sounding.
 * The alert's note table controls on/off cadence; an active buzzer cannot
 * reproduce the indicated note pitches or a frequency sweep. */
#include "main.h"
typedef struct { uint16_t hz, ms; } MelodyNote;
static const MelodyNote s_melody[19] = {{392,417},{440,417},{523,833},{494,417},{440,417},{392,833},{330,417},{392,417},{440,833},{523,417},{587,417},{659,1250},{587,417},{523,417},{494,833},{440,417},{392,417},{440,1250},{0,417}};
#define MELODY_COUNT (sizeof(s_melody)/sizeof(s_melody[0]))
static uint8_t s_alertActive=0u;
static uint32_t s_alertStart=0u, s_melodyTotal=0u;
static uint16_t s_curHz=0xFFFFu;
#if (BUZZER_MODE_PWM != 0u) || (BUZZER_ACTIVE_HIGH != 0u) || (BUZZER_SILENT_LEVEL_IS_LOW != 0u)
#error "PNP active-buzzer circuit requires GPIO mode, PA8 HIGH for silence"
#endif
static void GPIO_Output2MHz(GPIO_TypeDef *port, uint8_t pin)
{
    uint32_t shift;
    if(pin<8u) { shift=(uint32_t)pin*4u; port->CRL=(port->CRL & ~(0xFuL<<shift)) | (0x2uL<<shift); }
    else { shift=(uint32_t)(pin-8u)*4u; port->CRH=(port->CRH & ~(0xFuL<<shift)) | (0x2uL<<shift); }
}
#if defined(FW_SIMULATOR)
/* The simulator cannot watch writes to the write-only BSRR/BRR registers, so
 * the electrical level of PC13 (LED) and PA8 (buzzer) is mirrored here.
 * The real bit-band write above/below is untouched: on the board nothing
 * changes, and the page shows exactly what the pin would do. */
extern void Sim_LedState(uint8_t on);
extern void Sim_BuzzerState(uint8_t on);
#endif
void LED_On(void) { LED_PORT->BRR=LED_PIN;
#if defined(FW_SIMULATOR)
    Sim_LedState(1u);
#endif
}
void LED_Off(void) { LED_PORT->BSRR=LED_PIN;
#if defined(FW_SIMULATOR)
    Sim_LedState(0u);
#endif
}
void Buzzer_Tone(uint16_t hz)
{
#if BUZZER_MODE_PWM
    uint32_t arr;
    if(!hz) {
        /* Commit CCR1=0 immediately instead of waiting for the next PWM update.
         * This guarantees PA8 is driven LOW as soon as silence is requested. */
        TIM1->CCR1=0u; TIM1->EGR=TIM_EGR_UG; TIM1->SR=0u; return;
    }
    arr=1000000u/(uint32_t)hz-1u; if(arr>65535u) { arr=65535u; }
    TIM1->ARR=(uint16_t)arr; TIM1->CCR1=(uint16_t)((arr+1u)/2u);
    TIM1->EGR=TIM_EGR_UG; TIM1->SR=0u;
#else
    (void)hz;
#endif
}
void Buzzer_On(void)
{
#if BUZZER_MODE_PWM
    Buzzer_Tone(2700u);
#elif BUZZER_ACTIVE_HIGH
    BUZZER_PORT->BSRR=BUZZER_PIN;
#else
    BUZZER_PORT->BRR=BUZZER_PIN;
#endif
#if defined(FW_SIMULATOR)
    Sim_BuzzerState(1u);
#endif
}
void Buzzer_Off(void)
{
#if BUZZER_MODE_PWM
    Buzzer_Tone(0u);
#elif BUZZER_ACTIVE_HIGH
    BUZZER_PORT->BRR=BUZZER_PIN;
#else
    BUZZER_PORT->BSRR=BUZZER_PIN;
#endif
#if defined(FW_SIMULATOR)
    Sim_BuzzerState(0u);
#endif
}
void LED_Init(void)
{
    uint32_t i;
    RCC->APB2ENR |= LED_RCC_MASK | BUZZER_RCC_MASK;
    /* Preload HIGH while PA8 is still an input, then enable push-pull output.
     * LOW would turn Q1 on and make the buzzer sound continuously. */
    BUZZER_PORT->BSRR=BUZZER_PIN;
    GPIO_Output2MHz(BUZZER_PORT, BUZZER_PIN_INDEX);
    BUZZER_PORT->BSRR=BUZZER_PIN;
    /* 2) LED (PC13) afterwards. */
    LED_Off(); GPIO_Output2MHz(LED_PORT, LED_PIN_INDEX); LED_Off();
#if BUZZER_MODE_PWM
    /* Only compiled for a PASSIVE buzzer; the active-buzzer build never runs this. */
    RCC->APB2ENR |= RCC_APB2ENR_TIM1EN | RCC_APB2ENR_AFIOEN;
    /* Configure TIM1 completely while PA8 is still a LOW GPIO and while the
     * channel/main output are disabled. PWM1 with CCR1=0 is always inactive. */
    TIM1->CR1=0u; TIM1->CCER=0u; TIM1->BDTR=0u; TIM1->CR2=0u;
    TIM1->PSC=BUZZER_TIM_PSC; TIM1->ARR=999u; TIM1->CCR1=0u;
    TIM1->CCMR1=TIM_CCMR1_OC1PE | (6u << 4);
    TIM1->EGR=TIM_EGR_UG; TIM1->SR=0u;
    TIM1->CCER=TIM_CCER_CC1E; TIM1->BDTR=TIM_BDTR_MOE;
    TIM1->CR1=TIM_CR1_ARPE | TIM_CR1_CEN;
    /* The timer output is already guaranteed LOW before PA8 changes to AF. */
    BUZZER_PORT->CRH=(BUZZER_PORT->CRH & ~0xFuL) | 0xBuL;
    TIM1->CCR1=0u; TIM1->EGR=TIM_EGR_UG; TIM1->SR=0u;
#else
    Buzzer_Off();
#endif
    s_melodyTotal=0u; for(i=0u;i<MELODY_COUNT;i++) { s_melodyTotal += s_melody[i].ms; }
    s_alertActive=0u; s_curHz=0xFFFFu; Buzzer_Off();
}
void Alert_Start(uint32_t nowMs) { s_alertActive=1u; s_alertStart=nowMs; s_curHz=0xFFFFu; LED_On(); }
void Alert_Stop(void) { s_alertActive=0u; s_curHz=0xFFFFu; Buzzer_Off(); LED_Off(); }
static uint16_t Alert_FireworkHz(uint32_t elapsed)
{
    uint32_t step, rnd;
    if(elapsed < ALERT_LAUNCH_MS) {
        return (uint16_t)(500u + elapsed*2000u/ALERT_LAUNCH_MS); /* 500->2500Hz whistle */
    }
    elapsed -= ALERT_LAUNCH_MS;
    if(elapsed < ALERT_BOOM_MS) {
        step = elapsed/35u; /* new crackle grain every 35ms */
        rnd = step*1664525u + 1013904223u; rnd ^= rnd>>13u; rnd *= 1103515245u; rnd ^= rnd>>16u;
        if((rnd % 5u)==0u) { return 0u; } /* gaps = explosive crackle */
        return (uint16_t)(70u + rnd%380u); /* 70..449Hz boom rumble */
    }
    return 0xFFFFu; /* sentinel: use melody phase */
}
void Alert_Update(uint32_t nowMs)
{
    uint32_t elapsed, pos;
    uint16_t hz=0u;
#if BUZZER_MODE_PWM
    uint32_t acc=0u,i;
    uint16_t fh;
#endif
    if(!s_alertActive || !s_melodyTotal) { return; }
    elapsed=(uint32_t)(nowMs-s_alertStart);
#if !BUZZER_MODE_PWM
    /* Active buzzer: pitch is fixed inside the device, so use stage-specific
     * on/off cadence. 
     * 1) Dance (0-10s): 柔和旋律，长周期 (800ms 周期，仅响 120ms 形成悠缓点缀)
     * 2) Roll (10-15s): 稍微激进，紧凑双击节奏 (360ms 周期，响 100ms + 停 60ms + 响 80ms)
     * 3) Angry (15s+):  极其激进急促警报 (160ms 极速交替，80ms 鸣响 / 80ms 静音) */
    if(elapsed<YCAT_DANCE_MS) { 
        hz=((elapsed%800u)<120u)?1u:0u; 
    }
    else if(elapsed<YCAT_DANCE_MS+YCAT_ROLL_MS) { 
        pos=(elapsed-YCAT_DANCE_MS)%360u;
        hz=(pos<100u || (pos>=160u && pos<240u))?1u:0u;
    }
    else {
        pos=(elapsed-YCAT_DANCE_MS-YCAT_ROLL_MS)%160u;
        hz=(pos<80u)?1u:0u;
    }
#else
    fh=Alert_FireworkHz(elapsed);
    if(fh != 0xFFFFu) { hz=fh; }
    else {
        pos=(elapsed-ALERT_LAUNCH_MS-ALERT_BOOM_MS)%s_melodyTotal;
        for(i=0u;i<MELODY_COUNT;i++) { if(pos<acc+s_melody[i].ms) { hz=s_melody[i].hz; break; } acc += s_melody[i].ms; }
    }
#endif
    if(hz!=s_curHz) {
        s_curHz=hz;
#if BUZZER_MODE_PWM
        Buzzer_Tone(hz);
#else
        if(hz) { Buzzer_On(); } else { Buzzer_Off(); }
#endif
    }
    if((elapsed/ALERT_BLINK_HALF_MS)&1u) { LED_Off(); } else { LED_On(); }
}
