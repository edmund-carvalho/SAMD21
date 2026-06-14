#include <stdint.h>
#include <stdio.h>
#include "sam.h"
#include "uart.h"
#include "confirm.h"

/*
 * app1_main.c — v1.0.0
 *
 * Minimal good application. Prints version, confirms boot within
 * the 500ms WDT window, then loops printing uptime.
 *
 * Build and flash:
 *   make flash_a VERSION=1.0.0
 */

#define LED0_PIN    30U     /* PB30, active low */

static void gpio_init(void)
{
    PORT_REGS->GROUP[1].PORT_DIRSET = (1UL << LED0_PIN);
    PORT_REGS->GROUP[1].PORT_OUTSET = (1UL << LED0_PIN);  /* LED off */
}

int main(void)
{
    uart_init();
    gpio_init();

    /* LED0 ON — signal: entered main(), WDT armed by bootloader */
    PORT_REGS->GROUP[1].PORT_OUTCLR = (1UL << LED0_PIN);

    printf("APP v1.0.0 — booted OK\r\n");

    /* Confirm within 500ms — disarms WDT, sets slot VALID, LED0 OFF */
    confirm_boot();

    printf("APP v1.0.0 — confirmed, running\r\n");

    uint32_t uptime = 0;
    for (;;) {
        printf("APP v1.0.0 — uptime %lu s\r\n", (unsigned long)uptime++);
        for (volatile uint32_t i = 0; i < 8000000U; i++);  /* ~1s @ 8MHz */
    }

    return 0;
}