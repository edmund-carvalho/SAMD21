#include <string.h>
#include "sam.h"
#include "nvmctrl.h"

/* ---------------------------------------------------------------
 * Internal helpers
 * --------------------------------------------------------------- */

/* NVMCTRL command execution key — must accompany every command */
#define NVM_CMDEX_KEY   0xA5U

/* Wait for NVM to finish previous operation */
static nvm_err_t nvm_wait_ready(void)
{
    /* Simple spin-wait — bootloader context, no RTOS */
    uint32_t timeout = 100000UL;
    while (!(NVMCTRL_REGS->NVMCTRL_INTFLAG & NVMCTRL_INTFLAG_READY_Msk)) {
        if (--timeout == 0) {
            return NVM_ERR_TIMEOUT;
        }
    }
    return NVM_OK;
}

static void nvm_exec_cmd(uint16_t cmd)
{
    /* Write command with security key in one 16-bit write */
    NVMCTRL_REGS->NVMCTRL_CTRLA = (uint16_t)(NVMCTRL_CTRLA_CMDEX(NVM_CMDEX_KEY)
                                              | cmd);
}

/* ---------------------------------------------------------------
 * Public API
 * --------------------------------------------------------------- */

void nvm_init(void)
{
    /* Manual write mode: page buffer only commits when WP command
     * is explicitly issued. Prevents accidental writes. */
    NVMCTRL_REGS->NVMCTRL_CTRLB |= NVMCTRL_CTRLB_MANW_Msk;
}

nvm_err_t nvm_erase_row(uint32_t addr)
{
    nvm_err_t err;

    /* Alignment check */
    if (addr % NVM_ROW_SIZE != 0) {
        return NVM_ERR_ALIGN;
    }

    /* Range check — do not erase past end of flash */
    if (addr >= NVM_FLASH_END) {
        return NVM_ERR_RANGE;
    }

    /* Protection check — refuse to erase bootloader region */
    if (addr < NVM_SLOT_A_START) {
        return NVM_ERR_PROTECTED;
    }

    err = nvm_wait_ready();
    if (err != NVM_OK) return err;

    /* ADDR register takes byte address in units of 16-bit half-words */
    NVMCTRL_REGS->NVMCTRL_ADDR = (uint32_t)(addr >> 1);

    nvm_exec_cmd(NVMCTRL_CTRLA_CMD_ER);

    return nvm_wait_ready();
}

nvm_err_t nvm_write_page(uint32_t addr, const uint8_t *buf)
{
    nvm_err_t  err;
    uint32_t   i;
    volatile uint32_t *dst;

    /* Alignment check */
    if (addr % NVM_PAGE_SIZE != 0) {
        return NVM_ERR_ALIGN;
    }

    /* Range check */
    if (addr >= NVM_FLASH_END || addr + NVM_PAGE_SIZE > NVM_FLASH_END) {
        return NVM_ERR_RANGE;
    }

    /* Protection check */
    if (addr < NVM_SLOT_A_START) {
        return NVM_ERR_PROTECTED;
    }

    err = nvm_wait_ready();
    if (err != NVM_OK) return err;

    /* Step 1: clear page buffer */
    nvm_exec_cmd(NVMCTRL_CTRLA_CMD_PBC);
    err = nvm_wait_ready();
    if (err != NVM_OK) return err;

    /* Step 2: write 16 × uint32_t to the target address.
     * These writes go into the page buffer, not flash yet.
     * Must be 32-bit writes — 8/16-bit writes to NVM are undefined. */
    dst = (volatile uint32_t *)addr;
    for (i = 0; i < NVM_PAGE_SIZE / 4U; i++) {
        uint32_t word;
        memcpy(&word, buf + i * 4U, 4U);   /* safe unaligned read from buf */
        dst[i] = word;
    }

    /* Step 3: commit page buffer to flash */
    nvm_exec_cmd(NVMCTRL_CTRLA_CMD_WP);

    return nvm_wait_ready();
}

void nvm_read(uint32_t addr, uint8_t *buf, size_t n)
{
    /* Flash is memory-mapped — direct read, no NVMCTRL involved */
    memcpy(buf, (const void *)addr, n);
}

nvm_err_t nvm_erase_slot(uint8_t slot)
{
    uint32_t  start, end, addr;
    nvm_err_t err;

    if (slot == 0) {
        start = NVM_SLOT_A_START;
        end   = NVM_SLOT_A_START + NVM_SLOT_A_SIZE;
    } else if (slot == 1) {
        start = NVM_SLOT_B_START;
        end   = NVM_SLOT_B_START + NVM_SLOT_B_SIZE;
    } else {
        return NVM_ERR_RANGE;
    }

    for (addr = start; addr < end; addr += NVM_ROW_SIZE) {
        err = nvm_erase_row(addr);
        if (err != NVM_OK) return err;
    }

    return NVM_OK;
}