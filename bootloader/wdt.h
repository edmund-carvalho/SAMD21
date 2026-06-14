#ifndef WDT_H
#define WDT_H

/*
 * wdt.h - Watchdog timer for boot confirmation window
 *
 * Source : OSCULP32K (32768 Hz, always running, no config needed)
 * Period : PER=14, 16384 cycles = exactly 500 ms
 * Scope  : armed by bootloader before jump, disarmed by confirm_boot()
 *          Never re-armed after confirmation.
 *
 * ALWAYSON is NOT set - WDT can be disabled by the application.
 */

/* Arm WDT with 500 ms timeout. Called once before jumping to app. */
void wdt_arm(void);

/* Disarm WDT. Called from confirm_boot() in the application. */
void wdt_disarm(void);

/* Feed WDT (reset counter). Used inside bootloader command loop
 * to prevent accidental reset during long flash operations. */
void wdt_feed(void);

#endif /* WDT_H */