#ifndef CONFIRM_H
#define CONFIRM_H

/*
 * confirm.h - application-side boot confirmation
 *
 * Include this header in every application project.
 * Call confirm_boot() after successful hardware init and self-test,
 * within 500 ms of main() entry.
 *
 * What confirm_boot() does:
 *   1. Sets LED0 OFF (was ON since main() entry)
 *   2. Reads boot metadata from flash
 *   3. Sets active slot state to SLOT_VALID
 *   4. Resets boot_attempts to 0
 *   5. Writes metadata to flash
 *   6. Disarms WDT
 *
 * If confirm_boot() is never called within 500 ms, the WDT resets
 * the chip. After 3 consecutive unconfirmed boots the bootloader
 * marks the slot INVALID and falls back to the other slot.
 */

void confirm_boot(void);

#endif /* CONFIRM_H */