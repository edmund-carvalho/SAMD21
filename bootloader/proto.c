#include <string.h>
#include "sam.h"
#include "proto.h"
#include "transport.h"
#include "nvmctrl.h"
#include "metadata.h"
#include "image.h"
#include "wdt.h"
#include "uart.h"

/*
 * proto.c - command handlers only, no transport code.
 *
 * g_transport is set once by proto_run(). All handlers call
 * proto_send_response() which delegates to g_transport->send().
 * Handlers have zero knowledge of the physical channel.
 */

static const transport_t *g_transport;

void proto_send_response(uint8_t status, const uint8_t *data, uint16_t len)
{
    g_transport->send(status, data, len);
}

/* ---------------------------------------------------------------
 * Command handlers
 * --------------------------------------------------------------- */

static void handle_ping(void)
{
    static const uint8_t pong[] = { 'P', 'O', 'N', 'G' };
    proto_send_response(RSP_OK, pong, 4U);
}

static void handle_get_info(const boot_meta_t *meta)
{
    uint8_t buf[20];
    const image_header_t *ha = image_get_header(NVM_SLOT_A_START);
    const image_header_t *hb = image_get_header(NVM_SLOT_B_START);

    buf[0] = meta->active_slot;
    buf[1] = meta->slot_a_state;
    buf[2] = meta->slot_b_state;
    buf[3] = meta->boot_attempts;

    if (ha->ih_magic == IMAGE_MAGIC) {
        buf[4] = ha->ih_ver.iv_major;
        buf[5] = ha->ih_ver.iv_minor;
        buf[6] = (uint8_t)(ha->ih_ver.iv_revision & 0xFFU);
        buf[7] = (uint8_t)((ha->ih_ver.iv_revision >> 8) & 0xFFU);
        memcpy(buf + 12, &ha->ih_img_size, 4U);
    } else {
        memset(buf + 4, 0, 4U);
        memset(buf + 12, 0, 4U);
    }
    if (hb->ih_magic == IMAGE_MAGIC) {
        buf[8]  = hb->ih_ver.iv_major;
        buf[9]  = hb->ih_ver.iv_minor;
        buf[10] = (uint8_t)(hb->ih_ver.iv_revision & 0xFFU);
        buf[11] = (uint8_t)((hb->ih_ver.iv_revision >> 8) & 0xFFU);
        memcpy(buf + 16, &hb->ih_img_size, 4U);
    } else {
        memset(buf + 8,  0, 4U);
        memset(buf + 16, 0, 4U);
    }
    proto_send_response(RSP_OK, buf, sizeof(buf));
}

static void handle_erase_slot(const uint8_t *p, uint16_t len,
                               boot_meta_t *meta)
{
    if (len < 1U) { proto_send_response(RSP_ERR_CMD,  NULL, 0); return; }
    uint8_t slot = p[0];
    if (slot > 1U) { proto_send_response(RSP_ERR_ADDR, NULL, 0); return; }

    wdt_feed();
    if (nvm_erase_slot(slot) != NVM_OK) {
        proto_send_response(RSP_ERR_FLASH, NULL, 0); return;
    }
    meta_set_slot_state(meta, slot, SLOT_EMPTY);
    meta_write(meta);
    proto_send_response(RSP_OK, NULL, 0);
}

static void handle_write_page(const uint8_t *p, uint16_t len)
{
    if (len != 4U + NVM_PAGE_SIZE) {
        proto_send_response(RSP_ERR_CMD, NULL, 0); return;
    }
    uint32_t addr;
    memcpy(&addr, p, 4U);
    wdt_feed();
    nvm_err_t e = nvm_write_page(addr, p + 4U);
    if      (e == NVM_ERR_PROTECTED) proto_send_response(RSP_ERR_PROTECTED, NULL, 0);
    else if (e != NVM_OK)            proto_send_response(RSP_ERR_FLASH,     NULL, 0);
    else                             proto_send_response(RSP_OK,            NULL, 0);
}

static void handle_verify(const uint8_t *p, uint16_t len)
{
    if (len != 8U) { proto_send_response(RSP_ERR_CMD, NULL, 0); return; }
    uint32_t addr, size;
    memcpy(&addr, p,      4U);
    memcpy(&size, p + 4U, 4U);
    if (addr + size > NVM_FLASH_END) {
        proto_send_response(RSP_ERR_ADDR, NULL, 0); return;
    }
    uint32_t crc = crc32_compute((const uint8_t *)addr, size);
    uint8_t  rsp[4];
    memcpy(rsp, &crc, 4U);
    proto_send_response(RSP_OK, rsp, 4U);
}

static void handle_set_pending(const uint8_t *p, uint16_t len,
                                boot_meta_t *meta)
{
    if (len < 1U) { proto_send_response(RSP_ERR_CMD,  NULL, 0); return; }
    uint8_t slot = p[0];
    if (slot > 1U) { proto_send_response(RSP_ERR_ADDR, NULL, 0); return; }

    uint32_t start = (slot == 0U) ? NVM_SLOT_A_START : NVM_SLOT_B_START;
    if (!image_header_valid(start, NVM_SLOT_A_SIZE)) {
        proto_send_response(RSP_ERR_FLASH, NULL, 0); return;
    }
    meta_set_slot_state(meta, slot, SLOT_PENDING);
    meta_write(meta);
    proto_send_response(RSP_OK, NULL, 0);
}

static void handle_boot(void)
{
    proto_send_response(RSP_OK, NULL, 0);
    while (!(SERCOM3_REGS->USART_INT.SERCOM_INTFLAG &
             SERCOM_USART_INT_INTFLAG_TXC_Msk));
    NVIC_SystemReset();
}

static void handle_reset(void)
{
    proto_send_response(RSP_OK, NULL, 0);
    NVIC_SystemReset();
}

/* ---------------------------------------------------------------
 * proto_run - transport-agnostic command loop
 * --------------------------------------------------------------- */
void proto_run(boot_meta_t *meta, const transport_t *t)
{
    static uint8_t payload[PROTO_MAX_PAYLOAD];
    uint16_t       len;
    uint8_t        cmd;

    g_transport = t;

    uart_puts("BL ready\r\n");   /* always logged on UART regardless of transport */

    for (;;) {
        cmd = t->recv(payload, &len);

        switch (cmd) {
            case CMD_PING:        handle_ping();                           break;
            case CMD_GET_INFO:    handle_get_info(meta);                   break;
            case CMD_ERASE_SLOT:  handle_erase_slot(payload, len, meta);   break;
            case CMD_WRITE_PAGE:  handle_write_page(payload, len);         break;
            case CMD_VERIFY:      handle_verify(payload, len);             break;
            case CMD_SET_PENDING: handle_set_pending(payload, len, meta);  break;
            case CMD_BOOT:        handle_boot();                           break;
            case CMD_RESET:       handle_reset();                          break;
            default:              proto_send_response(RSP_ERR_CMD, NULL, 0); break;
        }
    }
}