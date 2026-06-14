#include "sam.h"
#include "dsu.h"

/*
 * dsu.c - DSU hardware CRC32
 *
 * Computation sequence (SAMD21 datasheet §19.6.2):
 *   1. Write start address to DSU_ADDR (4-byte aligned)
 *   2. Write byte count    to DSU_LENGTH (4-byte aligned)
 *   3. Write 0xFFFFFFFF   to DSU_DATA   (CRC32 init value)
 *   4. Set DSU_CTRL.CRC = 1            (start computation)
 *   5. Poll DSU_STATUSA.DONE
 *   6. Check DSU_STATUSA.BERR for bus errors
 *   7. Read result from DSU_DATA
 *
 * The DSU handles the standard CRC32 bit-reversal and final XOR
 * internally. The result matches crc32_compute() from metadata.c.
 *
 * DSU register access note: the DSU has two APB access regions:
 *   External APB (0x41002000) : accessible only when debugger present
 *   Internal APB (0x41002100) : accessible by CPU always
 * CRC, ADDR, LENGTH, DATA are in the Internal APB region.
 * DSU_REGS in the DFP points to the External region base.
 * Use the internal base offset when running from CPU code.
 */

/*
 * The DFP DSU_REGS points to 0x41002000 (external APB).
 * CRC computation registers are in the internal APB at 0x41002100.
 * Define internal access directly.
 */
#define DSU_INT_BASE        0x41002100UL
#define DSU_INT_CTRL        (*(volatile uint8_t  *)(DSU_INT_BASE + 0x00U))
#define DSU_INT_STATUSA     (*(volatile uint8_t  *)(DSU_INT_BASE + 0x01U))
#define DSU_INT_ADDR        (*(volatile uint32_t *)(DSU_INT_BASE + 0x04U))
#define DSU_INT_LENGTH      (*(volatile uint32_t *)(DSU_INT_BASE + 0x08U))
#define DSU_INT_DATA        (*(volatile uint32_t *)(DSU_INT_BASE + 0x0CU))

/* CTRL bits */
#define DSU_CTRL_CRC_Pos    2U
#define DSU_CTRL_CRC        (1U << DSU_CTRL_CRC_Pos)

/* STATUSA bits */
#define DSU_STATUSA_DONE    (1U << 0)
#define DSU_STATUSA_BERR    (1U << 3)   /* bus error during CRC */

dsu_err_t dsu_crc32(uint32_t addr, uint32_t len, uint32_t *out)
{
    uint32_t timeout = 1000000UL;

    /* Alignment checks */
    if ((addr & 0x3U) != 0U) return DSU_CRC_ERR_ALIGN;
    if ((len  & 0x3U) != 0U) return DSU_CRC_ERR_ALIGN;

    /* Clear any previous status flags (write 1 to clear) */
    DSU_INT_STATUSA = DSU_STATUSA_DONE | DSU_STATUSA_BERR;

    /* Set up computation */
    DSU_INT_ADDR   = addr;
    DSU_INT_LENGTH = len;
    DSU_INT_DATA   = 0xFFFFFFFFUL;   /* CRC32 standard init value */

    /* Start CRC computation */
    DSU_INT_CTRL = DSU_CTRL_CRC;

    /* Poll DONE */
    while (!(DSU_INT_STATUSA & DSU_STATUSA_DONE)) {
        if (--timeout == 0U) return DSU_CRC_ERR_TIMEOUT;
    }

    /* Check for bus error */
    if (DSU_INT_STATUSA & DSU_STATUSA_BERR) {
        return DSU_CRC_ERR_BUS;
    }

    *out = DSU_INT_DATA;
    return DSU_CRC_OK;
}

/* ---------------------------------------------------------------
 * DCC implementation
 * --------------------------------------------------------------- */

uint32_t dcc_recv_word(void)
{
    uint32_t w;
    /* Wait for host to write a word with bit31 set */
    do { w = DSU_INT_DCC0; } while (!(w & 0x80000000UL));
    /* ACK by clearing */
    DSU_INT_DCC0 = 0;
    return w & 0x7FFFFFFFUL;    /* strip VALID bit, return data */
}

void dcc_send_word(uint32_t data)
{
    /* Write with VALID bit set */
    DSU_INT_DCC1 = data | 0x80000000UL;
    /* Wait for host to ACK (write 0) */
    while (DSU_INT_DCC1 != 0);
}