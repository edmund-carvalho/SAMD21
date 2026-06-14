#ifndef TRANSPORT_H
#define TRANSPORT_H

#include <stdint.h>

/*
 * transport.h - abstract command transport interface
 *
 * Two implementations:
 *   transport_uart  byte-stream, STX+LEN+CRC16, full command set
 *   transport_dcc   word-based,  bit31=VALID,   high-level commands only
 *
 * Wire format comparison:
 *
 *   UART frame:
 *     [STX(1)][CMD(1)][LEN_L(1)][LEN_H(1)][DATA(N)][CRC_L(1)][CRC_H(1)]
 *
 *   DCC header word (request):
 *     bit31      : VALID = 1 (always set when sending)
 *     bits[30:24]: CMD   (same CMD_* values as UART)
 *     bits[23:16]: NBYTES (payload byte count, 0..PROTO_MAX_DCC_PAYLOAD)
 *     bits[15:8] : BYTE0 (first payload byte, 0 if none)
 *     bits[7:0]  : BYTE1 (second payload byte, 0 if none)
 *
 *   DCC data word (additional payload beyond 2 bytes):
 *     bit31      : VALID = 1
 *     bits[23:16]: BYTE_N
 *     bits[15:8] : BYTE_N+1
 *     bits[7:0]  : BYTE_N+2
 *
 *   DCC handshake per word (host -> CPU):
 *     1. host writes word | 0x80000000 to EXT_DCC0 (0x41002010)
 *     2. CPU detects bit31 set in INT_DCC0 (0x41002110), reads value
 *     3. CPU writes 0 to INT_DCC0  (ACK)
 *     4. host polls EXT_DCC0 until 0 (ACK received)
 *
 *   DCC handshake per word (CPU -> host) is symmetric using DCC1.
 *
 * All CMD_* constants and RSP_* constants are shared between both
 * transports - defined in proto.h. DCC simply does not generate
 * CMD_WRITE_PAGE or CMD_ERASE_SLOT since flash is written directly
 * by OpenOCD over SWD, bypassing the protocol entirely.
 */

typedef struct {
    /*
     * recv - block until one complete command arrives.
     * Returns: CMD byte
     * Fills:   buf with payload bytes, *out_len with byte count
     */
    uint8_t (*recv)(uint8_t *buf, uint16_t *out_len);

    /*
     * send - transmit one response.
     * status: RSP_OK or RSP_ERR_*
     * data:   payload (NULL if len == 0)
     * len:    payload byte count
     */
    void (*send)(uint8_t status, const uint8_t *data, uint16_t len);
} transport_t;

/* Provided implementations */
extern const transport_t transport_uart;
extern const transport_t transport_dcc;

#endif /* TRANSPORT_H */