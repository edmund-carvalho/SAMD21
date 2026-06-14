#ifndef METADATA_H
#define METADATA_H

#include <stdint.h>
#include "nvmctrl.h"

/*
 * metadata.h - boot slot state management
 *
 * Size/CRC/version are no longer stored here - they live in the
 * MCUboot image header and TLV trailer inside each slot.
 *
 * One boot_meta_t page lives at NVM_META_START (0x3E000).
 * Exactly NVM_PAGE_SIZE (64 bytes) - one NVM page write per update.
 * The containing row is erased before every write.
 *
 * Slot state transitions (bit-clearing from erased 0xFF):
 *   EMPTY   0xFF  erased, no image
 *   PENDING 0xFE  image written, CRC not yet verified
 *   VALID   0xFC  CRC verified, app called confirm_boot()
 *   INVALID 0xF0  boot_attempts exceeded - do not boot
 */

#define META_MAGIC          0xB007AB1EUL
#define META_MAX_ATTEMPTS   3U

typedef enum {
    SLOT_EMPTY   = 0xFF,
    SLOT_PENDING = 0xFE,
    SLOT_VALID   = 0xFC,
    SLOT_INVALID = 0xF0,
} slot_state_t;

typedef struct {
    uint32_t magic;             /* META_MAGIC if valid              */
    uint8_t  active_slot;       /* 0 = Slot A, 1 = Slot B          */
    uint8_t  slot_a_state;      /* slot_state_t                     */
    uint8_t  slot_b_state;      /* slot_state_t                     */
    uint8_t  boot_attempts;     /* reset to 0 on VALID              */
    uint8_t  _pad[52];          /* reserved - pad to 60 bytes       */
    uint32_t crc32;             /* CRC32 of all fields above        */
} __attribute__((packed)) boot_meta_t;

/* Compile-time size check - must be one NVM page (64 bytes) */
typedef char meta_size_check[(sizeof(boot_meta_t) == 64U) ? 1 : -1];

/* ---------------------------------------------------------------
 * API
 * --------------------------------------------------------------- */

int          meta_read(boot_meta_t *dst);
nvm_err_t    meta_write(boot_meta_t *src);
void         meta_default(boot_meta_t *dst);
slot_state_t meta_slot_state(const boot_meta_t *m, uint8_t slot);
void         meta_set_slot_state(boot_meta_t *m, uint8_t slot, slot_state_t s);

/*
 * crc32_compute - CRC32 (ISO 3309 / Ethernet poly 0xEDB88320).
 * Used for both metadata integrity and image TLV verification.
 */
uint32_t crc32_compute(const uint8_t *data, uint32_t len);

#endif /* METADATA_H */