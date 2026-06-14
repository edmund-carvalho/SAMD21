#include "sam.h"
#include "confirm.h"
#include "metadata.h"
#include "nvmctrl.h"
#include "wdt.h"

#define LED0_PIN    30U     /* PB30, active low */

void confirm_boot(void)
{
    boot_meta_t meta;

    /* Set LED0 ON in app main() before calling confirm_boot():
     *   PORT_REGS->GROUP[1].PORT_OUTCLR = (1UL << 30);  // LED ON
     * confirm_boot() toggles it OFF to signal successful confirmation. */

    if (meta_read(&meta) != 0) {
        /* Metadata corrupt — disarm WDT anyway and continue */
        wdt_disarm();
        PORT_REGS->GROUP[1].PORT_OUTTGL = (1UL << LED0_PIN);
        return;
    }

    /* Mark active slot VALID, reset boot attempt counter */
    meta_set_slot_state(&meta, meta.active_slot, SLOT_VALID);
    meta.boot_attempts = 0U;
    meta_write(&meta);

    /* Disarm 500ms WDT — confirmation window closed */
    wdt_disarm();

    /* Toggle LED0: was ON (entered main), now OFF (confirmed) */
    PORT_REGS->GROUP[1].PORT_OUTTGL = (1UL << LED0_PIN);
}