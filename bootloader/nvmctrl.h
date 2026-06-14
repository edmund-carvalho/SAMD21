#ifndef NVMCTRL_H
#define NVMCTRL_H

#include <stdint.h>
#include <stddef.h>

/*
 * nvmctrl.h — SAMD21J18A internal flash driver
 *
 * Geometry:
 *   Page =  64 bytes  (smallest writable unit)
 *   Row  = 256 bytes  (smallest erasable unit = 4 pages)
 *
 * All addresses are byte addresses in the NVM address space (0x00000000+).
 * Write addresses must be page-aligned (64-byte).
 * Erase addresses must be row-aligned (256-byte).
 *
 * Reading requires no driver — flash is memory-mapped, use memcpy or
 * direct pointer dereference.
 */

#define NVM_PAGE_SIZE       64U
#define NVM_ROW_SIZE        256U
#define NVM_PAGES_PER_ROW   (NVM_ROW_SIZE / NVM_PAGE_SIZE)   /* 4 */

/* Flash layout symbols — must match linker scripts */
#define NVM_BOOTLOADER_START    0x00000000UL
#define NVM_BOOTLOADER_SIZE     (8U  * 1024U)
#define NVM_SLOT_A_START        0x00002000UL
#define NVM_SLOT_A_SIZE         (120U * 1024U)
#define NVM_SLOT_B_START        0x00020000UL
#define NVM_SLOT_B_SIZE         (120U * 1024U)
#define NVM_META_START          0x0003E000UL
#define NVM_META_SIZE           (8U  * 1024U)
#define NVM_FLASH_END           0x00040000UL

typedef enum {
    NVM_OK           = 0,
    NVM_ERR_ALIGN,       /* address not page/row aligned    */
    NVM_ERR_RANGE,       /* address outside writable region */
    NVM_ERR_PROTECTED,   /* address in BOOTPROT region      */
    NVM_ERR_TIMEOUT,     /* READY flag never set            */
} nvm_err_t;

/*
 * nvm_init — set manual write mode (MANW=1).
 * Must be called once before any erase or write.
 */
void nvm_init(void);

/*
 * nvm_erase_row — erase one 256-byte row.
 * addr must be row-aligned (addr % 256 == 0).
 * Refuses to erase the bootloader region (addr < NVM_SLOT_A_START).
 */
nvm_err_t nvm_erase_row(uint32_t addr);

/*
 * nvm_write_page — write exactly 64 bytes to one page.
 * addr must be page-aligned (addr % 64 == 0).
 * buf must be exactly NVM_PAGE_SIZE bytes.
 * Refuses to write the bootloader region.
 * Caller must erase the containing row before writing.
 */
nvm_err_t nvm_write_page(uint32_t addr, const uint8_t *buf);

/*
 * nvm_read — read n bytes from flash into buf.
 * Convenience wrapper over memcpy. No alignment requirement.
 */
void nvm_read(uint32_t addr, uint8_t *buf, size_t n);

/*
 * nvm_erase_slot — erase all rows covering a slot.
 * slot: 0 = Slot A, 1 = Slot B.
 */
nvm_err_t nvm_erase_slot(uint8_t slot);

#endif /* NVMCTRL_H */