/* ===========================================================================
 * simulator/hal/stm32f10x.h
 * ---------------------------------------------------------------------------
 * Browser-simulator replacement for the STM32F10x Standard Peripheral Library
 * header.  It exists ONLY so that the firmware's own .c files can be compiled
 * by clang/zig for wasm32-freestanding and executed inside a web page.
 *
 * It is NOT part of the Keil uVision project: Keil keeps using the real SPL
 * stm32f10x.h from its own install.  Nothing in this file is ever seen by
 * ARMCC because the simulator build always defines FW_SIMULATOR and the
 * include path points here only for the wasm build.
 *
 * Design rules:
 *   1. Field layout matches the real CMSIS device header exactly, so the
 *      byte offsets the JavaScript host pokes (IDR / ODR / CNT) are the same
 *      addresses a datasheet-based emulator would use.
 *   2. Only the registers the firmware actually touches are declared.
 *   3. Everything lives in one flat block of wasm linear memory whose base
 *      address is exported ("sim_reg_base") - the JS UI reads/writes it.
 * ===========================================================================
 */
#ifndef SIM_STM32F10X_H
#define SIM_STM32F10X_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ types */
typedef struct { int32_t dummy; } SIM_Unused;

typedef struct
{
    volatile uint32_t CRL;      /* 0x00 */
    volatile uint32_t CRH;      /* 0x04 */
    volatile uint32_t IDR;      /* 0x08 */
    volatile uint32_t ODR;      /* 0x0C */
    volatile uint32_t BSRR;     /* 0x10 */
    volatile uint32_t BRR;      /* 0x14 */
    volatile uint32_t LCKR;     /* 0x18 */
} GPIO_TypeDef;

typedef struct
{
    volatile uint32_t CR1;      /* 0x00 */
    volatile uint32_t CR2;      /* 0x04 */
    volatile uint32_t SMCR;     /* 0x08 */
    volatile uint32_t DIER;     /* 0x0C */
    volatile uint32_t SR;       /* 0x10 */
    volatile uint32_t EGR;      /* 0x14 */
    volatile uint32_t CCMR1;    /* 0x18 */
    volatile uint32_t CCMR2;    /* 0x1C */
    volatile uint32_t CCER;     /* 0x20 */
    volatile uint32_t CNT;      /* 0x24 */
    volatile uint32_t PSC;      /* 0x28 */
    volatile uint32_t ARR;      /* 0x2C */
    volatile uint32_t RCR;      /* 0x30 */
    volatile uint32_t CCR1;     /* 0x34 */
    volatile uint32_t CCR2;     /* 0x38 */
    volatile uint32_t CCR3;     /* 0x3C */
    volatile uint32_t CCR4;     /* 0x40 */
    volatile uint32_t BDTR;     /* 0x44 */
    volatile uint32_t DCR;      /* 0x48 */
    volatile uint32_t DMAR;     /* 0x4C */
} TIM_TypeDef;

typedef struct
{
    volatile uint32_t CR;       /* 0x00 */
    volatile uint32_t CFGR;     /* 0x04 */
    volatile uint32_t CIR;      /* 0x08 */
    volatile uint32_t APB2RSTR; /* 0x0C */
    volatile uint32_t APB1RSTR; /* 0x10 */
    volatile uint32_t AHBENR;   /* 0x14 */
    volatile uint32_t APB2ENR;  /* 0x18 */
    volatile uint32_t APB1ENR;  /* 0x1C */
    volatile uint32_t BDCR;     /* 0x20 */
    volatile uint32_t CSR;      /* 0x24 */
} RCC_TypeDef;

/* --------------------------------------------------- peripheral instances */
/* One flat RAM block, exported to JavaScript as "sim_reg_base".  The offsets
 * below are part of the simulator's ABI and are mirrored in sim.js. */
#define SIM_REG_WORDS      512u
#define SIM_OFF_RCC        0u                    /* 40 bytes */
#define SIM_OFF_GPIOA      64u
#define SIM_OFF_GPIOB      96u
#define SIM_OFF_GPIOC      128u
#define SIM_OFF_TIM1       160u
#define SIM_OFF_TIM2       256u
#define SIM_OFF_DWT        320u                  /* DEMCR, CTRL, CYCCNT */
#define SIM_OFF_SCRATCH    352u

extern volatile uint32_t g_simRegs[SIM_REG_WORDS];

#define SIM_REG(base, off) ((volatile uint32_t *)((uintptr_t)(base) + (off)))

#define RCC      ((RCC_TypeDef  *)((uintptr_t)g_simRegs + SIM_OFF_RCC))
#define GPIOA    ((GPIO_TypeDef *)((uintptr_t)g_simRegs + SIM_OFF_GPIOA))
#define GPIOB    ((GPIO_TypeDef *)((uintptr_t)g_simRegs + SIM_OFF_GPIOB))
#define GPIOC    ((GPIO_TypeDef *)((uintptr_t)g_simRegs + SIM_OFF_GPIOC))
#define TIM1     ((TIM_TypeDef  *)((uintptr_t)g_simRegs + SIM_OFF_TIM1))
#define TIM2     ((TIM_TypeDef  *)((uintptr_t)g_simRegs + SIM_OFF_TIM2))

/* --------------------------------------------------------------- bit masks */
#define RCC_APB2ENR_AFIOEN      ((uint32_t)0x00000001u)
#define RCC_APB2ENR_IOPAEN      ((uint32_t)0x00000004u)
#define RCC_APB2ENR_IOPBEN      ((uint32_t)0x00000008u)
#define RCC_APB2ENR_IOPCEN      ((uint32_t)0x00000010u)
#define RCC_APB2ENR_TIM1EN      ((uint32_t)0x00000800u)
#define RCC_APB1ENR_TIM2EN      ((uint32_t)0x00000001u)

#define TIM_CR1_CEN             ((uint32_t)0x00000001u)
#define TIM_CR1_ARPE            ((uint32_t)0x00000080u)
#define TIM_EGR_UG              ((uint32_t)0x00000001u)
#define TIM_CCMR1_OC1PE         ((uint32_t)0x00000008u)
#define TIM_CCER_CC1E           ((uint32_t)0x00000001u)
#define TIM_BDTR_MOE            ((uint32_t)0x00008000u)

/* ---------------------------------------------------------- core helpers */
/* ARMCC uses its own __get_PRIMASK/__set_PRIMASK intrinsics (core_cm3.h).
 * For the wasm build they become harmless no-ops: the simulator is single
 * threaded, so a critical section has no effect on timing correctness. */
#ifndef __CC_ARM
static inline uint32_t __get_PRIMASK(void) { return 0u; }
static inline void __set_PRIMASK(uint32_t v) { (void)v; }
static inline void __disable_irq(void) { }
static inline void __enable_irq(void) { }
#endif

/* Older firmware revisions referenced these; kept so the header is a full
 * drop-in for the subset of SPL symbols the project has ever used. */
typedef enum { DISABLE = 0, ENABLE = !DISABLE } FunctionalState;

#ifdef __cplusplus
}
#endif

#endif /* SIM_STM32F10X_H */
