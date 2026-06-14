#include <string.h>
#include "sam.h"
#include "transport.h"
#include "proto.h"      /* RSP_ERR_*, PROTO_MAX_PAYLOAD, PROTO_STX */

/* ---------------------------------------------------------------
 * UART register access (SERCOM3)
 * --------------------------------------------------------------- */
static uint8_t uart_read_byte(void)
{
    while (!(SERCOM3_REGS->USART_INT.SERCOM_INTFLAG &
             SERCOM_USART_INT_INTFLAG_RXC_Msk));
    return (uint8_t)SERCOM3_REGS->USART_INT.SERCOM_DATA;
}

static void uart_write_byte(uint8_t b)
{
    while (!(SERCOM3_REGS->USART_INT.SERCOM_INTFLAG &
             SERCOM_USART_INT_INTFLAG_DRE_Msk));
    SERCOM3_REGS->USART_INT.SERCOM_DATA = b;
}

static void uart_write_bytes(const uint8_t *buf, uint16_t len)
{
    for (uint16_t i = 0; i < len; i++) uart_write_byte(buf[i]);
}

/* ---------------------------------------------------------------
 * CRC16-CCITT (poly 0x1021, init 0xFFFF) - single-pass
 * Covers concatenated header[3] + payload[len].
 * --------------------------------------------------------------- */
static uint16_t frame_crc16(uint8_t cmd, uint16_t len, const uint8_t *payload)
{
    uint8_t  hdr[3] = { cmd,
                        (uint8_t)(len & 0xFFU),
                        (uint8_t)((len >> 8) & 0xFFU) };
    uint8_t  tmp[3U + PROTO_MAX_PAYLOAD];
    uint16_t crc = 0xFFFFU;
    uint16_t total, i, b;

    memcpy(tmp,      hdr,     3U);
    memcpy(tmp + 3U, payload, len);
    total = (uint16_t)(3U + len);

    for (i = 0; i < total; i++) {
        crc ^= (uint16_t)((uint16_t)tmp[i] << 8);
        for (b = 0; b < 8U; b++) {
            crc = (crc & 0x8000U)
                ? (uint16_t)((crc << 1) ^ 0x1021U)
                : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

/* ---------------------------------------------------------------
 * transport_uart.recv - block until a valid frame arrives
 * --------------------------------------------------------------- */
static uint8_t uart_recv(uint8_t *buf, uint16_t *out_len)
{
    uint8_t  cmd, len_l, len_h;
    uint16_t len, crc_rx, crc_calc;

    for (;;) {
        /* Wait for STX */
        while (uart_read_byte() != PROTO_STX);

        cmd   = uart_read_byte();
        len_l = uart_read_byte();
        len_h = uart_read_byte();
        len   = (uint16_t)((uint16_t)len_h << 8) | len_l;

        if (len > PROTO_MAX_PAYLOAD) {
            uint8_t err = RSP_ERR_CMD;
            transport_uart.send(RSP_ERR_CMD, NULL, 0);
            (void)err;
            continue;
        }

        for (uint16_t i = 0; i < len; i++) buf[i] = uart_read_byte();

        uint8_t cl = uart_read_byte();
        uint8_t ch = uart_read_byte();
        crc_rx   = (uint16_t)((uint16_t)ch << 8) | cl;
        crc_calc = frame_crc16(cmd, len, buf);

        if (crc_calc != crc_rx) {
            transport_uart.send(RSP_ERR_CRC, NULL, 0);
            continue;
        }

        *out_len = len;
        return cmd;
    }
}

/* ---------------------------------------------------------------
 * transport_uart.send
 * --------------------------------------------------------------- */
static void uart_send(uint8_t status, const uint8_t *data, uint16_t len)
{
    uint8_t  hdr[4];
    uint16_t crc;
    uint8_t  crc_buf[2];

    hdr[0] = PROTO_STX;
    hdr[1] = status;
    hdr[2] = (uint8_t)(len & 0xFFU);
    hdr[3] = (uint8_t)((len >> 8) & 0xFFU);

    crc = frame_crc16(status, len, data ? data : (const uint8_t *)"");
    crc_buf[0] = (uint8_t)(crc & 0xFFU);
    crc_buf[1] = (uint8_t)((crc >> 8) & 0xFFU);

    uart_write_bytes(hdr, 4U);
    if (len > 0U && data != NULL) uart_write_bytes(data, len);
    uart_write_bytes(crc_buf, 2U);
}

/* ---------------------------------------------------------------
 * Exported instance
 * --------------------------------------------------------------- */
const transport_t transport_uart = {
    .recv = uart_recv,
    .send = uart_send,
};