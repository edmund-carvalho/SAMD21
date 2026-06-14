#include <stdint.h>
#include <stdio.h>
#include "sam.h"
#include "uart.h"
#include "nvmctrl.h"
#include "metadata.h"
#include "image.h"
#include "wdt.h"
#include "proto.h"
#include "transport.h"

/*
 * Transport selection:
 *   USE_DCC  (default) - DCC over SWD, requires debugger.
 *                        UART is pure logs. No binary frames on UART.
 *   USE_UART           - UART byte-stream protocol.
 *                        Field update path: no debugger needed.
 *
 * Set in Makefile:
 *   make all              → DCC (development)
 *   make all USE_UART=1   → UART (field / production test)
 */
#ifdef USE_UART
#  define ACTIVE_TRANSPORT  (&transport_uart)
#  define TRANSPORT_NAME    "UART"
#else
#  define ACTIVE_TRANSPORT  (&transport_dcc)
#  define TRANSPORT_NAME    "DCC"
#endif

/* ---------------------------------------------------------------
 * Pin assignments
 * --------------------------------------------------------------- */
#define SW0_PIN     15U     /* PA15, active low */
#define LED0_PIN    30U     /* PB30, active low */

static void gpio_init(void)
{
    PORT_REGS->GROUP[1].PORT_DIRSET          = (1UL << LED0_PIN);
    PORT_REGS->GROUP[1].PORT_OUTSET          = (1UL << LED0_PIN);
    PORT_REGS->GROUP[0].PORT_DIRCLR          = (1UL << SW0_PIN);
    PORT_REGS->GROUP[0].PORT_PINCFG[SW0_PIN] = PORT_PINCFG_INEN_Msk |
                                                PORT_PINCFG_PULLEN_Msk;
    PORT_REGS->GROUP[0].PORT_OUTSET          = (1UL << SW0_PIN);
}

static int sw0_held(void)
{
    return !(PORT_REGS->GROUP[0].PORT_IN & (1UL << SW0_PIN));
}

/* ---------------------------------------------------------------
 * Jump helpers
 * --------------------------------------------------------------- */
static uint32_t slot_start(uint8_t slot)
{
    return (slot == 0U) ? NVM_SLOT_A_START : NVM_SLOT_B_START;
}

static int validate_slot(uint8_t slot, int full_verify)
{
    uint32_t s = slot_start(slot);
    if (!image_header_valid(s, NVM_SLOT_A_SIZE)) {
        printf("BL: slot %u header invalid\r\n", slot);
        return 0;
    }
    if (full_verify) {
        printf("BL: slot %u CRC32 verify...\r\n", slot);
        if (image_verify_crc32(s) != 0) {
            printf("BL: slot %u CRC32 FAIL\r\n", slot);
            return 0;
        }
        printf("BL: slot %u CRC32 OK\r\n", slot);
    }
    return 1;
}

static void jump_to_app(uint8_t slot)
{
    typedef void (*app_t)(void);
    uint32_t vtor    = image_vtor(slot_start(slot));
    uint32_t app_sp  = *(volatile uint32_t *)(vtor + 0U);
    app_t    app_rst = (app_t)(*(volatile uint32_t *)(vtor + 4U));

    __disable_irq();
    SCB->VTOR = vtor;
    __set_MSP(app_sp);
    __enable_irq();
    app_rst();
    for (;;);
}

static void try_boot(uint8_t slot, boot_meta_t *meta, int pending)
{
    if (!validate_slot(slot, pending)) {
        meta_set_slot_state(meta, slot, SLOT_INVALID);
        meta_write(meta);
        return;
    }
    const image_header_t *hdr = image_get_header(slot_start(slot));
    printf("BL: slot %u v%u.%u.%u attempt %u/%u\r\n",
           slot,
           hdr->ih_ver.iv_major, hdr->ih_ver.iv_minor, hdr->ih_ver.iv_revision,
           meta->boot_attempts + 1U, META_MAX_ATTEMPTS);
    meta->active_slot = slot;
    meta->boot_attempts++;
    meta_write(meta);
    wdt_arm();
    jump_to_app(slot);
}

/* ---------------------------------------------------------------
 * main
 * --------------------------------------------------------------- */
int main(void)
{
    boot_meta_t  meta;
    uint8_t      primary, fallback;
    slot_state_t pstate, fstate;

    uart_init();
    gpio_init();
    nvm_init();

    printf("BL v1.0 - SAMD21J18A  transport:%s\r\n", TRANSPORT_NAME);

    /* Step 1: metadata */
    if (meta_read(&meta) != 0) {
        printf("BL: metadata invalid - defaults\r\n");
        meta_default(&meta);
        meta_write(&meta);
        goto command_mode;
    }

    /* Step 2: forced entry */
    if (sw0_held()) {
        printf("BL: SW0 held\r\n");
        goto command_mode;
    }

    /* Step 3: active slot */
    primary  = meta.active_slot;
    fallback = primary ^ 1U;
    pstate   = meta_slot_state(&meta, primary);

    if ((pstate == SLOT_VALID || pstate == SLOT_PENDING) &&
         meta.boot_attempts >= META_MAX_ATTEMPTS) {
        printf("BL: slot %u max attempts - INVALID\r\n", primary);
        meta_set_slot_state(&meta, primary, SLOT_INVALID);
        meta.boot_attempts = 0U;
        meta_write(&meta);
        pstate = SLOT_INVALID;
    }

    if (pstate == SLOT_VALID || pstate == SLOT_PENDING) {
        try_boot(primary, &meta, pstate == SLOT_PENDING);
    }

    /* Step 4: fallback */
    fstate = meta_slot_state(&meta, fallback);
    if (fstate == SLOT_VALID || fstate == SLOT_PENDING) {
        printf("BL: fallback slot %u\r\n", fallback);
        meta.boot_attempts = 0U;
        meta_write(&meta);
        try_boot(fallback, &meta, fstate == SLOT_PENDING);
    }

command_mode:
    printf("BL: command mode [%s]\r\n", TRANSPORT_NAME);
    proto_run(&meta, ACTIVE_TRANSPORT);
    return 0;
}