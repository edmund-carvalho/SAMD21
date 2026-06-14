#include "sam.h"
#include "wdt.h"

/* ---------------------------------------------------------------
 * OSCULP32K is the WDT clock source.
 * It runs by default after reset — no explicit enable required.
 * Route it to the WDT via GCLK GEN2 (GEN0/GEN1 already in use).
 *
 * PER=14: 2^14 = 16384 cycles at 32768 Hz = exactly 500 ms.
 * --------------------------------------------------------------- */

void wdt_arm(void)
{
    /* Step 1 — Configure GCLK GEN2: source = OSCULP32K, no division */
    GCLK_REGS->GCLK_GENCTRL = GCLK_GENCTRL_ID(2)           |
                                GCLK_GENCTRL_SRC_OSCULP32K  |
                                GCLK_GENCTRL_GENEN_Msk;
    while (GCLK_REGS->GCLK_STATUS & GCLK_STATUS_SYNCBUSY_Msk);

    /* Step 2 — Route GEN2 → WDT */
    GCLK_REGS->GCLK_CLKCTRL = (uint16_t)(GCLK_CLKCTRL_ID_WDT    |
                                           GCLK_CLKCTRL_GEN_GCLK2 |
                                           GCLK_CLKCTRL_CLKEN_Msk);
    while (GCLK_REGS->GCLK_STATUS & GCLK_STATUS_SYNCBUSY_Msk);

    /* Step 3 — Set period PER=14 (500 ms), no window mode */
    WDT_REGS->WDT_CONFIG = WDT_CONFIG_PER_CYC16384;
    while (WDT_REGS->WDT_STATUS & WDT_STATUS_SYNCBUSY_Msk);

    /* Step 4 — Enable (ALWAYSON not set — app can disable) */
    WDT_REGS->WDT_CTRL = WDT_CTRL_ENABLE_Msk;
    while (WDT_REGS->WDT_STATUS & WDT_STATUS_SYNCBUSY_Msk);
}

void wdt_disarm(void)
{
    WDT_REGS->WDT_CTRL &= ~WDT_CTRL_ENABLE_Msk;
    while (WDT_REGS->WDT_STATUS & WDT_STATUS_SYNCBUSY_Msk);
}

void wdt_feed(void)
{
    /* Write the clear key to reset the WDT counter.
     * Used during bootloader flash operations to avoid
     * accidental timeout while receiving a large image. */
    WDT_REGS->WDT_CLEAR = WDT_CLEAR_CLEAR_KEY;
    while (WDT_REGS->WDT_STATUS & WDT_STATUS_SYNCBUSY_Msk);
}