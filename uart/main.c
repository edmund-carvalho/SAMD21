#include <stdint.h>
#include <stdio.h>
#include "sam.h"
#include "uart.h"

/* ---------------------------------------------------------------
 * Pin assignments (SAM D21 Xplained Pro)
 * --------------------------------------------------------------- */
#define LED0_PIN    30U     /* PB30, active low */
#define SW0_PIN     15U     /* PA15, active low */

/* ---------------------------------------------------------------
 * delay_ms - polling SysTick (no interrupt)
 * Uses F_CPU_HZ from uart.h (8 MHz after uart_init).
 * LOAD = (8000000 / 1000) - 1 = 7999
 * --------------------------------------------------------------- */
static void delay_ms(uint32_t ms)
{
    SysTick->LOAD = (F_CPU_HZ / 1000UL) - 1UL;
    SysTick->VAL  = 0;
    SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_ENABLE_Msk;
    for (uint32_t i = 0; i < ms; i++) {
        while (!(SysTick->CTRL & SysTick_CTRL_COUNTFLAG_Msk));
    }
    SysTick->CTRL = 0;
}

/* ---------------------------------------------------------------
 * gpio_init
 * --------------------------------------------------------------- */
static void gpio_init(void)
{
    /* LED0: PB30 output, drive HIGH → LED off (active low) */
    PORT_REGS->GROUP[1].PORT_DIRSET             = (1UL << LED0_PIN);
    PORT_REGS->GROUP[1].PORT_OUTSET             = (1UL << LED0_PIN);

    /* SW0: PA15 input with internal pull-up */
    PORT_REGS->GROUP[0].PORT_DIRCLR             = (1UL << SW0_PIN);
    PORT_REGS->GROUP[0].PORT_PINCFG[SW0_PIN]    = PORT_PINCFG_INEN_Msk |
                                                   PORT_PINCFG_PULLEN_Msk;
    PORT_REGS->GROUP[0].PORT_OUTSET             = (1UL << SW0_PIN);
}

/* ---------------------------------------------------------------
 * sw0_pressed - returns 1 while button held
 * --------------------------------------------------------------- */
static int sw0_pressed(void)
{
    return !(PORT_REGS->GROUP[0].PORT_IN & (1UL << SW0_PIN));
}

/* ---------------------------------------------------------------
 * main
 * --------------------------------------------------------------- */
int main(void)
{
    uint32_t presses = 0;

    uart_init();    /* clocks, SERCOM3, PA22/PA23 - must be first */
    gpio_init();

    printf("boot ok - 8 MHz - 115200 8N1\r\n");

    while (1) {
        if (sw0_pressed()) {
            PORT_REGS->GROUP[1].PORT_OUTTGL = (1UL << LED0_PIN);
            presses++;
            printf("SW0 press #%lu - LED toggled\r\n", presses);
            while (sw0_pressed());
            delay_ms(50);
        }
    }

    return 0;
}