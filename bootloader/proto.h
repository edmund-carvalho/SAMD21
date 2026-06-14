#ifndef PROTO_H
#define PROTO_H

#include <stdint.h>
#include "metadata.h"
#include "transport.h"

/*
 * proto.h - command-response protocol
 *
 * Command IDs and response status codes are transport-agnostic.
 * transport_uart and transport_dcc both use the same CMD_* values.
 *
 * DCC note: CMD_ERASE_SLOT and CMD_WRITE_PAGE are defined here but
 * the DCC transport never generates them - those operations are
 * performed directly by OpenOCD over SWD. They remain available
 * via the UART transport for field updates without a debugger.
 */

/* Frame constants (UART only) */
#define PROTO_STX               0x02U
#define PROTO_MAX_PAYLOAD       512U
#define PROTO_MAX_DCC_PAYLOAD   32U    /* DCC high-level commands only */

/* Commands - shared by both transports */
#define CMD_PING                0x01U
#define CMD_GET_INFO            0x02U
#define CMD_ERASE_SLOT          0x03U  /* UART only - SWD uses OpenOCD directly */
#define CMD_WRITE_PAGE          0x04U  /* UART only - SWD uses OpenOCD directly */
#define CMD_VERIFY              0x05U
#define CMD_SET_PENDING         0x06U
#define CMD_BOOT                0x07U
#define CMD_RESET               0x08U

/* Response status */
#define RSP_OK                  0x00U
#define RSP_ERR_CMD             0x01U
#define RSP_ERR_CRC             0x02U
#define RSP_ERR_ADDR            0x03U
#define RSP_ERR_FLASH           0x04U
#define RSP_ERR_PROTECTED       0x05U

/*
 * proto_send_response - send a response via the current transport.
 * Delegates to g_transport->send() set by proto_run().
 * Called by all command handlers - they have no transport knowledge.
 */
void proto_send_response(uint8_t status, const uint8_t *data, uint16_t len);

/*
 * proto_run - blocking command loop.
 * Selects transport at startup; never returns.
 *
 * main.c passes either &transport_uart or &transport_dcc.
 * Compile-time default: USE_DCC (set in Makefile).
 */
void proto_run(boot_meta_t *meta, const transport_t *t);

/*
 * crc16_ccitt - used by transport_uart.c only.
 * Exposed here for flash_slot.py reference (same poly as host tool).
 */
uint16_t crc16_ccitt(const uint8_t *data, uint16_t len);

#endif /* PROTO_H */