#ifndef DSU_H
#define DSU_H

#include <stdint.h>

/*
 * dsu.h - SAMD21 Device Service Unit, hardware CRC32
 *
 * DSU CRC32 is 40x faster than software at 8MHz (15ms vs 614ms for 120KB).
 * Used for MCUboot image TLV verification on SLOT_PENDING boot.
 * Software CRC32 (metadata.c) is retained for the 64-byte metadata struct.
 *
 * DSU CRC32 polynomial: ISO 3309 (0xEDB88320 reflected).
 * Identical to our software crc32_compute() - results are interchangeable.
 *
 * DSU chip erase (dsu_chip_erase) is intentionally NOT exposed here.
 * It erases the entire device including bootloader and is a recovery
 * tool only. Use: make recover (OpenOCD at91samd chip-erase command).
 * See Makefile for the recover target.
 *
 * TODO: DSU DCC0/DCC1 registers left unused here. They are memory-mapped
 *       at DSU base + 0x10/0x14, accessible from both CPU and debugger.
 *       Future use: live diagnostic channel from a running application,
 *       separate from the bootloader. Not needed for flash loading (OpenOCD
 *       writes flash directly via SWD NVMCTRL driver, no protocol needed).
 */

typedef enum {
    DSU_CRC_OK        = 0,
    DSU_CRC_ERR_ALIGN,    /* addr or len not 4-byte aligned */
    DSU_CRC_ERR_BUS,      /* AHB bus error during computation */
    DSU_CRC_ERR_TIMEOUT,  /* DONE flag never set             */
} dsu_err_t;

/*
 * dsu_crc32 - compute CRC32 over a flash region using DSU hardware.
 *
 * addr : byte address, must be 4-byte aligned
 * len  : byte count,   must be 4-byte aligned
 * out  : receives computed CRC32 value
 *
 * Produces the same result as crc32_compute() in metadata.c.
 * The trailing bytes (len % 4 != 0) must be handled by the caller
 * in software if needed - MCUboot images padded by prepare_image.py
 * are always 4-byte aligned so this is not an issue in practice.
 */
dsu_err_t dsu_crc32(uint32_t addr, uint32_t len, uint32_t *out);

#endif /* DSU_H */

/* ---------------------------------------------------------------
 * DCC - Debug Communication Channel
 *
 * CPU accesses DCC via Internal APB (always accessible):
 *   INT_DCC0 = 0x41002110  CPU reads  commands  from host
 *   INT_DCC1 = 0x41002114  CPU writes responses to   host
 *
 * Host (OpenOCD) accesses DCC via External APB (debugger present):
 *   EXT_DCC0 = 0x41002010  host writes commands  to   CPU  (mww)
 *   EXT_DCC1 = 0x41002014  host reads  responses from CPU  (mdw)
 *
 * Handshake convention - bit31 = VALID flag:
 *   Sender writes (word | 0x80000000).
 *   Receiver detects bit31=1, reads word & 0x7FFFFFFF.
 *   Receiver writes 0 to ACK.
 *   Sender polls until 0.
 *
 * This ensures zero data values are correctly transferred.
 * See transport_dcc.c for the full frame format.
 * --------------------------------------------------------------- */

/* Internal APB - CPU side */
#define DSU_INT_DCC0    (*(volatile uint32_t *)0x41002110UL)
#define DSU_INT_DCC1    (*(volatile uint32_t *)0x41002114UL)

/* External APB addresses - for OpenOCD reference in flash_slot.py */
#define DSU_EXT_DCC0_ADDR   0x41002010UL  /* host writes commands  */
#define DSU_EXT_DCC1_ADDR   0x41002014UL  /* host reads  responses */

/*
 * dcc_recv_word - block until host writes a word to DCC0.
 * Returns the 31-bit data value (bit31 stripped).
 */
uint32_t dcc_recv_word(void);

/*
 * dcc_send_word - write a word to DCC1 and block until host ACKs.
 * data must fit in 31 bits (bit31 is used as VALID flag).
 */
void dcc_send_word(uint32_t data);