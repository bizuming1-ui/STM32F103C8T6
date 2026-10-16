/* ===========================================================================
 * simulator/sim_hal.c
 * ---------------------------------------------------------------------------
 * The only file that exists purely for the browser.  It gives the firmware's
 * own C sources something to talk to instead of silicon:
 *
 *   - one flat register block (RCC / GPIOA / GPIOB / GPIOC / TIM1 / TIM2) that
 *     JavaScript can poke directly, so the page drives IDR (keys) and TIM2->CNT
 *     (the millisecond time base) and reads the pins back;
 *   - the LED and buzzer level mirrors (BSRR/BRR are write-only registers, so
 *     LED.c reports the resulting pin level here);
 *   - the DS18B20 value the page asks for;
 *   - Sim_Yield(): hands control back to the page after every firmware loop
 *     iteration, which is what makes main() re-enterable.
 *
 * Nothing in here is part of the Keil project.
 * ===========================================================================
 */
#include "main.h"

volatile uint32_t g_simRegs[SIM_REG_WORDS];
volatile uint32_t g_simDwt[4];

static volatile uint8_t  s_led=0u;
static volatile uint8_t  s_buzzer=0u;
static volatile int16_t  s_sensorTenths=245;

/* ------------------------------------------------------------ LED / buzzer */
void Sim_LedState(uint8_t on)    { s_led = (uint8_t)(on?1u:0u); }
void Sim_BuzzerState(uint8_t on) { s_buzzer = (uint8_t)(on?1u:0u); }
/* ------------------------------------------------------------- temperature */
int16_t Sim_Sensor_Tenths(void) { return s_sensorTenths; }
/* ------------------------------------------------------------------ yield */
uint8_t Sim_Yield(void) { return 1u; }

/* ------------------------------------------------------- exported to the page */
__attribute__((export_name("sim_reg_base")))
uint32_t Sim_RegBase(void) { return (uint32_t)(uintptr_t)g_simRegs; }

__attribute__((export_name("sim_gram_ptr")))
uint32_t Sim_GramPtr(void) { return (uint32_t)(uintptr_t)OLED_GRAM; }

/* PA4..PA7 sit in bits 4..7 of GPIOA->IDR (KEY_ACTIVE_HIGH = 1 = pressed). */
__attribute__((export_name("sim_set_keys")))
void Sim_SetKeys(uint32_t pressed) { GPIOA->IDR = (GPIOA->IDR & ~0x00F0u) | ((pressed & 0x000Fu) << 4); }

__attribute__((export_name("sim_set_tim2_ticks")))
void Sim_SetTim2Ticks(uint32_t ticks) { TIM2->CNT = ticks & 0xFFFFu; }

__attribute__((export_name("sim_set_sensor")))
void Sim_SetSensor(int32_t tenths) { s_sensorTenths = (int16_t)tenths; }

__attribute__((export_name("sim_get_led")))
uint32_t Sim_GetLed(void) { return (uint32_t)s_led; }

__attribute__((export_name("sim_get_buzzer")))
uint32_t Sim_GetBuzzer(void) { return (uint32_t)s_buzzer; }

__attribute__((export_name("sim_key_level")))
uint32_t Sim_KeyLevel(void) { return (uint32_t)Key_Level(); }

__attribute__((export_name("sim_gpioa_idr")))
uint32_t Sim_GpioaIdr(void) { return GPIOA->IDR; }

__attribute__((export_name("sim_get_state")))
uint32_t Sim_GetState(void)
{
    /* Bit layout mirrored in simulator/sim.js - useful for the status line and
     * for the automated regression harness. */
    uint32_t v=0u;
    v |= (uint32_t)s_led;                                            /* bit 0  */
    v |= ((uint32_t)s_buzzer)<<1;                                    /* bit 1  */
    v |= ((uint32_t)(GPIOA->IDR>>4)&0x0Fu)<<2;                       /* bit 2-5 keys */
    v |= (uint32_t)(TIM2->CNT & 0xFFFFu)<<6;                         /* bit 6-21 */
    v |= ((uint32_t)(GPIOB->ODR & 0xC0u))<<16;                       /* bit 22-23 I2C pins */
    return v;
}
