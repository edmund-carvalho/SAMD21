#include <stdint.h>
#include <stdio.h>
#include "sam.h"
#include "uart.h"
#include "confirm.h"

/*
 * app3_main.c - v3.2.1 - INTENTIONALLY BAD
 *
 * Prints its version, turns LED0 on, then hangs forever WITHOUT
 * calling confirm_boot(). The 500ms WDT (armed by the bootloader
 * before the jump) is never disarmed.
 *
 * Demonstrates the fallback mechanism:
 *
 *   Reset 1: attempt 1/3 -> WDT fires at 500ms -> reset
 *   Reset 2: attempt 2/3 -> WDT fires at 500ms -> reset
 *   Reset 3: attempt 3/3 -> WDT fires at 500ms -> reset
 *   Reset 4: boot_attempts >= 3 -> mark this slot INVALID
 *            -> fall back to other slot (if VALID/PENDING)
 *
 * Build and flash to slot B (with a good app in slot A):
 *   make flash_b VERSION=3.2.1
 *   make swap_app        # make B active to trigger the fallback demo
 *
 * Expected UART log across resets:
 *   BL: slot B v3.2.1 attempt 1/3
 *   APP v3.2.1 - booted, simulating hang (no confirm_boot)
 *   (500ms silence, then reset)
 *   BL: slot B v3.2.1 attempt 2/3
 *   APP v3.2.1 - booted, simulating hang (no confirm_boot)
 *   (500ms silence, then reset)
 *   BL: slot B v3.2.1 attempt 3/3
 *   APP v3.2.1 - booted, simulating hang (no confirm_boot)
 *   (500ms silence, then reset)
 *   BL: slot B max attempts -- INVALID
 *   BL: fallback slot A
 *   BL: slot A v1.0.0 attempt 1/3
 *   APP v1.0.0 - booted OK
 *   APP v1.0.0 - confirmed, running
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

    /* LED0 ON - signal: entered main(), WDT armed by bootloader */
    PORT_REGS->GROUP[1].PORT_OUTCLR = (1UL << LED0_PIN);

    printf("APP v3.2.1 - booted, simulating hang (no confirm_boot)\r\n");

    /* BUG: confirm_boot() intentionally NOT called.
     * WDT armed by bootloader (500ms, PER=14) will fire and reset
     * the chip. LED0 stays ON the whole time - visual indicator
     * that this app never reached the confirmed state. */

    for (;;) {
        /* hang */
    }

    return 0;
}