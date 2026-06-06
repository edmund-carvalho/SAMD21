#include <stdint.h>

/* ---------------------------------------------------------------
 * PORT peripheral — SAMD21 base: 0x41004400
 * Group A (PA): base + 0x00
 * Group B (PB): base + 0x80
 * --------------------------------------------------------------- */
#define PORT_BASE           0x41004400UL

/* Group A registers */
#define PORTA_DIRCLR        (*(volatile uint32_t *)(PORT_BASE + 0x04))
#define PORTA_OUTSET        (*(volatile uint32_t *)(PORT_BASE + 0x18))
#define PORTA_IN            (*(volatile uint32_t *)(PORT_BASE + 0x20))
#define PORTA_PINCFG(n)     (*(volatile uint8_t  *)(PORT_BASE + 0x40 + (n)))

/* Group B registers (offset 0x80 from PORT_BASE) */
#define PORTB_DIRSET        (*(volatile uint32_t *)(PORT_BASE + 0x88))
#define PORTB_OUTSET        (*(volatile uint32_t *)(PORT_BASE + 0x98))
#define PORTB_OUTTGL        (*(volatile uint32_t *)(PORT_BASE + 0x9C))

/* ---------------------------------------------------------------
 * SysTick — ARM Cortex-M0+ core peripheral
 * --------------------------------------------------------------- */
#define SYSTICK_CTRL        (*(volatile uint32_t *)0xE000E010)
#define SYSTICK_LOAD        (*(volatile uint32_t *)0xE000E014)
#define SYSTICK_VAL         (*(volatile uint32_t *)0xE000E018)
#define SYSTICK_COUNTFLAG   (1UL << 16)

/* ---------------------------------------------------------------
 * Pin assignments (SAM D21 Xplained Pro)
 * --------------------------------------------------------------- */
#define LED0_PIN    30      /* PB30, active low */
#define SW0_PIN     15      /* PA15, active low */

/* PINCFG register bits */
#define PINCFG_INEN     (1U << 1)   /* Input enable  */
#define PINCFG_PULLEN   (1U << 2)   /* Pull enable   */

/* ---------------------------------------------------------------
 * delay_ms — polling SysTick, no interrupt
 * Default CPU clock after reset: OSC8M / 8 = 1 MHz
 * 1 ms = 1000 cycles → LOAD = 999
 * --------------------------------------------------------------- */
static void delay_ms(uint32_t ms)
{
    SYSTICK_LOAD = 999UL;
    SYSTICK_VAL  = 0;
    SYSTICK_CTRL = 0x05U;   /* CLKSOURCE=1 (CPU), ENABLE=1, no IRQ */
    for (uint32_t i = 0; i < ms; i++) {
        while (!(SYSTICK_CTRL & SYSTICK_COUNTFLAG));
    }
    SYSTICK_CTRL = 0;
}

/* ---------------------------------------------------------------
 * gpio_init
 * --------------------------------------------------------------- */
static void gpio_init(void)
{
    /* LED0: PB30 as output, start HIGH (LED off, active low) */
    PORTB_DIRSET          = (1UL << LED0_PIN);
    PORTB_OUTSET          = (1UL << LED0_PIN);

    /* SW0: PA15 as input with internal pull-up
     *   DIRCLR   → input direction
     *   PINCFG   → enable input buffer + pull
     *   OUTSET   → pull direction = pull-up (OUT=1 when PULLEN=1)
     */
    PORTA_DIRCLR          = (1UL << SW0_PIN);
    PORTA_PINCFG(SW0_PIN) = PINCFG_INEN | PINCFG_PULLEN;
    PORTA_OUTSET          = (1UL << SW0_PIN);
}

/* ---------------------------------------------------------------
 * sw0_pressed — returns 1 when button held down
 * --------------------------------------------------------------- */
static int sw0_pressed(void)
{
    return !(PORTA_IN & (1UL << SW0_PIN));   /* active low */
}

/* ---------------------------------------------------------------
 * main
 * --------------------------------------------------------------- */
int main(void)
{
    gpio_init();

    while (1) {
        if (sw0_pressed()) {
            PORTB_OUTTGL = (1UL << LED0_PIN);   /* toggle LED0       */
            while (sw0_pressed());               /* wait for release  */
            delay_ms(50);                        /* debounce          */
        }
    }

    return 0;
}