#include <stdint.h>
#include <unistd.h>     /* ssize_t for _write */
#include "sam.h"
#include "uart.h"

/*
 * uart.c — SERCOM3 USART init + write, printf retarget
 *
 * Clock tree after uart_init():
 *   OSC8M PRESC=0 → 8 MHz
 *   GCLK GEN0 source OSC8M → 8 MHz  (CPU)
 *   GCLK GEN1 source OSC8M → 8 MHz  (SERCOM3_CORE)
 *
 * BAUD = 65536 × (1 − 16 × 115200 / 8000000) = 50437  (error 0.003%)
 */

#define SERCOM3_BAUD_115200     50437U

/* ---------------------------------------------------------------
 * Step 1 — OSC8M: remove reset prescaler, run at 8 MHz
 * --------------------------------------------------------------- */
static void clock_init(void)
{
    /* OSC8M PRESC reset default = 0x3 (÷8 = 1 MHz).
     * Clear PRESC to 0x0 (÷1 = 8 MHz).
     * Keep ENABLE bit and all calibration bits intact. */
    SYSCTRL_REGS->SYSCTRL_OSC8M = (SYSCTRL_REGS->SYSCTRL_OSC8M
                                    & ~SYSCTRL_OSC8M_PRESC_Msk)
                                    | SYSCTRL_OSC8M_PRESC(0);

    /* Wait for OSC8M to be ready */
    while (!(SYSCTRL_REGS->SYSCTRL_PCLKSR & SYSCTRL_PCLKSR_OSC8MRDY_Msk));

    /* GCLK GEN1: source = OSC8M, no division, enable */
    GCLK_REGS->GCLK_GENCTRL = GCLK_GENCTRL_ID(1)          |
                                GCLK_GENCTRL_SRC_OSC8M      |
                                GCLK_GENCTRL_GENEN_Msk;
    while (GCLK_REGS->GCLK_STATUS & GCLK_STATUS_SYNCBUSY_Msk);

    /* Route GEN1 → SERCOM3_CORE */
    GCLK_REGS->GCLK_CLKCTRL = (uint16_t)(GCLK_CLKCTRL_ID_SERCOM3_CORE |
                                           GCLK_CLKCTRL_GEN_GCLK1       |
                                           GCLK_CLKCTRL_CLKEN_Msk);
    while (GCLK_REGS->GCLK_STATUS & GCLK_STATUS_SYNCBUSY_Msk);
}

/* ---------------------------------------------------------------
 * Step 2 — PM: enable SERCOM3 APB clock
 * --------------------------------------------------------------- */
static void pm_init(void)
{
    PM_REGS->PM_APBCMASK |= PM_APBCMASK_SERCOM3_Msk;
}

/* ---------------------------------------------------------------
 * Step 3 — PORT: mux PA22/PA23 to SERCOM3 function C
 *
 *   PA22 = SERCOM3/PAD[0] = TX  (even pin → PMUX[11] lower nibble)
 *   PA23 = SERCOM3/PAD[1] = RX  (odd  pin → PMUX[11] upper nibble)
 *   Function C = 0x2
 * --------------------------------------------------------------- */
static void port_init(void)
{
    /* Enable peripheral mux on both pins */
    PORT_REGS->GROUP[0].PORT_PINCFG[22] = PORT_PINCFG_PMUXEN_Msk;
    PORT_REGS->GROUP[0].PORT_PINCFG[23] = PORT_PINCFG_PMUXEN_Msk |
                                           PORT_PINCFG_INEN_Msk;

    /* Set both to function C (SERCOM3) in shared PMUX[11] register */
    PORT_REGS->GROUP[0].PORT_PMUX[11] = PORT_PMUX_PMUXE(0x2U) |
                                         PORT_PMUX_PMUXO(0x2U);
}

/* ---------------------------------------------------------------
 * Step 4 — SERCOM3: configure and enable USART 115200 8N1
 * --------------------------------------------------------------- */
static void sercom_init(void)
{
    /* Software reset — clears all registers */
    SERCOM3_REGS->USART_INT.SERCOM_CTRLA = SERCOM_USART_INT_CTRLA_SWRST_Msk;
    while (SERCOM3_REGS->USART_INT.SERCOM_SYNCBUSY &
           SERCOM_USART_INT_SYNCBUSY_SWRST_Msk);

    /* CTRLA:
     *   MODE   = 0x1  USART with internal clock
     *   SAMPR  = 0x0  16x oversampling, arithmetic baud
     *   TXPO   = 0x0  TX on PAD[0] = PA22
     *   RXPO   = 0x1  RX on PAD[1] = PA23
     *   DORD         LSB first (standard UART)
     */
    SERCOM3_REGS->USART_INT.SERCOM_CTRLA =
        SERCOM_USART_INT_CTRLA_MODE(0x1U)  |
        SERCOM_USART_INT_CTRLA_SAMPR(0x0U) |
        SERCOM_USART_INT_CTRLA_TXPO(0x0U)  |
        SERCOM_USART_INT_CTRLA_RXPO(0x1U)  |
        SERCOM_USART_INT_CTRLA_DORD_Msk;

    /* CTRLB: 8-bit data, 1 stop bit, enable TX + RX */
    SERCOM3_REGS->USART_INT.SERCOM_CTRLB =
        SERCOM_USART_INT_CTRLB_CHSIZE(0x0U) |
        SERCOM_USART_INT_CTRLB_TXEN_Msk     |
        SERCOM_USART_INT_CTRLB_RXEN_Msk;
    while (SERCOM3_REGS->USART_INT.SERCOM_SYNCBUSY &
           SERCOM_USART_INT_SYNCBUSY_CTRLB_Msk);

    /* BAUD: 50437 for 115200 @ 8 MHz (error 0.003%) */
    SERCOM3_REGS->USART_INT.SERCOM_BAUD = SERCOM3_BAUD_115200;

    /* Enable SERCOM3 */
    SERCOM3_REGS->USART_INT.SERCOM_CTRLA |= SERCOM_USART_INT_CTRLA_ENABLE_Msk;
    while (SERCOM3_REGS->USART_INT.SERCOM_SYNCBUSY &
           SERCOM_USART_INT_SYNCBUSY_ENABLE_Msk);
}

/* ---------------------------------------------------------------
 * Public API
 * --------------------------------------------------------------- */
void uart_init(void)
{
    clock_init();
    pm_init();
    port_init();
    sercom_init();
}

void uart_putc(uint8_t c)
{
    /* Wait until Data Register Empty */
    while (!(SERCOM3_REGS->USART_INT.SERCOM_INTFLAG &
             SERCOM_USART_INT_INTFLAG_DRE_Msk));
    SERCOM3_REGS->USART_INT.SERCOM_DATA = (uint16_t)c;
}

void uart_puts(const char *s)
{
    while (*s) {
        uart_putc((uint8_t)*s++);
    }
}

/* ---------------------------------------------------------------
 * _write — retarget printf to UART
 *   Called by newlib for every printf/puts/putchar.
 *   '\n' expanded to '\r\n' for terminal compatibility.
 * --------------------------------------------------------------- */
ssize_t _write(int fd, const void *buf, size_t len)
{
    const uint8_t *p = (const uint8_t *)buf;
    (void)fd;
    for (size_t i = 0; i < len; i++) {
        if (p[i] == '\n') {
            uart_putc('\r');
        }
        uart_putc(p[i]);
    }
    return (ssize_t)len;
}

/* ---------------------------------------------------------------
 * _sbrk — minimal heap for newlib printf internal buffers
 * --------------------------------------------------------------- */
extern uint8_t _end;    /* defined by linker script — end of .bss */

void *_sbrk(int incr)
{
    static uint8_t *heap = NULL;
    uint8_t *prev;

    if (heap == NULL) {
        heap = &_end;
    }
    prev  = heap;
    heap += incr;
    return (void *)prev;
}

/* ---------------------------------------------------------------
 * Minimal syscall stubs — suppress nosys.specs linker warnings.
 * None of these are called in this application.
 * --------------------------------------------------------------- */
#include <sys/stat.h>

int _close(int fd)                   { (void)fd; return -1; }
int _fstat(int fd, struct stat *st)  { (void)fd; st->st_mode = S_IFCHR; return 0; }
int _isatty(int fd)                  { (void)fd; return 1; }
int _lseek(int fd, int ptr, int dir) { (void)fd; (void)ptr; (void)dir; return 0; }
int _read(int fd, char *ptr, int len){ (void)fd; (void)ptr; (void)len; return 0; }