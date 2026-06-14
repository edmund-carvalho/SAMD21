#ifndef UART_H
#define UART_H

#include <stdint.h>

/*
 * uart.h — SERCOM3 USART, 115200 8N1
 *
 * Hardware (SAM D21 Xplained Pro):
 *   PA22  SERCOM3/PAD[0]  TX → EDBG RX → /dev/ttyACM1
 *   PA23  SERCOM3/PAD[1]  RX ← EDBG TX ← /dev/ttyACM1
 *
 * Side effect of uart_init():
 *   OSC8M PRESC set to 0 → system clock becomes 8 MHz.
 *   Update delay_ms: SysTick LOAD = 7999 (not 999).
 */

#define F_CPU_HZ    8000000UL   /* After uart_init() OSC8M undivided */

void uart_init(void);
void uart_putc(uint8_t c);
void uart_puts(const char *s);

#endif /* UART_H */