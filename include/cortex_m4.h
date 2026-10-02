/*
 * cortex_m4.h - ARM Cortex-M4 core peripheral definitions
 *
 * NVIC, SysTick, SCB, FPU registers used by the AT32F403A on the RT-950 Pro.
 * Reference: ARMv7-M Architecture Reference Manual.
 */

#ifndef CORTEX_M4_H
#define CORTEX_M4_H

#include <stdint.h>

/* SysTick ----------------------------------------------------------- */
#define SYSTICK_BASE        0xE000E010UL

typedef struct {
    volatile uint32_t CTRL;     /* +0x00  Control and Status */
    volatile uint32_t LOAD;     /* +0x04  Reload Value */
    volatile uint32_t VAL;      /* +0x08  Current Value */
    volatile uint32_t CALIB;    /* +0x0C  Calibration Value */
} SysTick_TypeDef;

#define SysTick             ((SysTick_TypeDef *)SYSTICK_BASE)

/* CTRL bits */
#define SYSTICK_CTRL_ENABLE     (1UL << 0)
#define SYSTICK_CTRL_TICKINT    (1UL << 1)
#define SYSTICK_CTRL_CLKSOURCE  (1UL << 2)  /* 1 = processor clock */
#define SYSTICK_CTRL_COUNTFLAG  (1UL << 16)

/* NVIC -------------------------------------------------------------- */
#define NVIC_BASE           0xE000E100UL

typedef struct {
    volatile uint32_t ISER[8];          /* +0x000  Interrupt Set-Enable */
    uint32_t _reserved0[24];
    volatile uint32_t ICER[8];          /* +0x080  Interrupt Clear-Enable */
    uint32_t _reserved1[24];
    volatile uint32_t ISPR[8];          /* +0x100  Interrupt Set-Pending */
    uint32_t _reserved2[24];
    volatile uint32_t ICPR[8];          /* +0x180  Interrupt Clear-Pending */
    uint32_t _reserved3[24];
    volatile uint32_t IABR[8];          /* +0x200  Interrupt Active Bit */
    uint32_t _reserved4[56];
    volatile uint8_t  IP[240];          /* +0x300  Interrupt Priority */
    uint32_t _reserved5[644];
    volatile uint32_t STIR;             /* +0xE00  Software Trigger Interrupt */
} NVIC_TypeDef;

#define NVIC                ((NVIC_TypeDef *)NVIC_BASE)

static inline void NVIC_EnableIRQ(int irqn)
{
    NVIC->ISER[irqn >> 5] = (1UL << (irqn & 0x1F));
}

static inline void NVIC_DisableIRQ(int irqn)
{
    NVIC->ICER[irqn >> 5] = (1UL << (irqn & 0x1F));
}

static inline void NVIC_SetPriority(int irqn, uint8_t prio)
{
    NVIC->IP[irqn] = (prio << 4);  /* AT32 uses upper 4 bits */
}

/* SCB --------------------------------------------------------------- */
#define SCB_BASE            0xE000ED00UL

typedef struct {
    volatile uint32_t CPUID;    /* +0x00 */
    volatile uint32_t ICSR;     /* +0x04  Interrupt Control and State */
    volatile uint32_t VTOR;     /* +0x08  Vector Table Offset */
    volatile uint32_t AIRCR;    /* +0x0C  Application Interrupt / Reset Control */
    volatile uint32_t SCR;      /* +0x10  System Control */
    volatile uint32_t CCR;      /* +0x14  Configuration and Control */
    volatile uint8_t  SHP[12];  /* +0x18  System Handler Priority */
    volatile uint32_t SHCSR;    /* +0x24  System Handler Control and State */
    volatile uint32_t CFSR;     /* +0x28  Configurable Fault Status */
    volatile uint32_t HFSR;     /* +0x2C  Hard Fault Status */
    volatile uint32_t DFSR;     /* +0x30  Debug Fault Status */
    volatile uint32_t MMFAR;    /* +0x34  MemManage Fault Address */
    volatile uint32_t BFAR;     /* +0x38  Bus Fault Address */
    volatile uint32_t AFSR;     /* +0x3C  Auxiliary Fault Status */
} SCB_TypeDef;

#define SCB                 ((SCB_TypeDef *)SCB_BASE)

/* AIRCR bits */
#define SCB_AIRCR_VECTKEY       (0x05FAUL << 16)
#define SCB_AIRCR_PRIGROUP_MASK (7UL << 8)
#define SCB_AIRCR_SYSRESETREQ  (1UL << 2)

/* FPU --------------------------------------------------------------- */
#define FPU_CPACR           (*(volatile uint32_t *)0xE000ED88UL)

/* Enable CP10/CP11 (FPU) full access */
static inline void FPU_Enable(void)
{
    FPU_CPACR |= (0xFUL << 20);  /* CP10 + CP11 = full access */
}

/* Compiler intrinsics ----------------------------------------------- */
static inline void __DSB(void) { __asm volatile ("dsb 0xF" ::: "memory"); }
static inline void __ISB(void) { __asm volatile ("isb 0xF" ::: "memory"); }
static inline void __WFI(void) { __asm volatile ("wfi"); }
static inline void __enable_irq(void)  { __asm volatile ("cpsie i" ::: "memory"); }
static inline void __disable_irq(void) { __asm volatile ("cpsid i" ::: "memory"); }

#endif /* CORTEX_M4_H */
