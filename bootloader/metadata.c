#include <string.h>
#include "metadata.h"
#include "nvmctrl.h"

/* ---------------------------------------------------------------
 * CRC32 — ISO 3309 / Ethernet poly 0xEDB88320, bit-by-bit
 *
 * Used for metadata struct integrity and image TLV verification.
 *
 * TODO: replace with 256-entry table implementation for ~15x speedup
 *       if boot latency from CRC32 image verification becomes an issue.
 *       Table costs ~1KB ROM. At 8MHz, bit-by-bit takes ~900ms for 120KB.
 * --------------------------------------------------------------- */
uint32_t crc32_compute(const uint8_t *data, uint32_t len)
{
    uint32_t crc = 0xFFFFFFFFUL;
    uint32_t i, b;

    for (i = 0; i < len; i++) {
        crc ^= data[i];
        for (b = 0; b < 8U; b++) {
            crc = (crc & 1U) ? ((crc >> 1) ^ 0xEDB88320UL) : (crc >> 1);
        }
    }
    return crc ^ 0xFFFFFFFFUL;
}

/* ---------------------------------------------------------------
 * Internal
 * --------------------------------------------------------------- */
static uint32_t meta_crc(const boot_meta_t *m)
{
    return crc32_compute((const uint8_t *)m,
                         sizeof(boot_meta_t) - sizeof(uint32_t));
}

/* ---------------------------------------------------------------
 * Public API
 * --------------------------------------------------------------- */

int meta_read(boot_meta_t *dst)
{
    nvm_read(NVM_META_START, (uint8_t *)dst, sizeof(boot_meta_t));

    if (dst->magic != META_MAGIC)    return -1;
    if (dst->crc32 != meta_crc(dst)) return -1;
    return 0;
}

nvm_err_t meta_write(boot_meta_t *src)
{
    nvm_err_t err;

    src->crc32 = meta_crc(src);

    err = nvm_erase_row(NVM_META_START);
    if (err != NVM_OK) return err;

    return nvm_write_page(NVM_META_START, (const uint8_t *)src);
}

void meta_default(boot_meta_t *dst)
{
    memset(dst, 0, sizeof(boot_meta_t));
    dst->magic         = META_MAGIC;
    dst->active_slot   = 0U;
    dst->slot_a_state  = (uint8_t)SLOT_EMPTY;
    dst->slot_b_state  = (uint8_t)SLOT_EMPTY;
    dst->boot_attempts = 0U;
    /* crc32 computed on next meta_write() */
}

slot_state_t meta_slot_state(const boot_meta_t *m, uint8_t slot)
{
    return (slot == 0U) ? (slot_state_t)m->slot_a_state
                        : (slot_state_t)m->slot_b_state;
}

void meta_set_slot_state(boot_meta_t *m, uint8_t slot, slot_state_t state)
{
    if (slot == 0U) m->slot_a_state = (uint8_t)state;
    else            m->slot_b_state = (uint8_t)state;
}