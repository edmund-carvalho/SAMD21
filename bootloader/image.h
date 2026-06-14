#ifndef IMAGE_H
#define IMAGE_H

#include <stdint.h>

/*
 * image.h - MCUboot-compatible image format
 *
 * Standard MCUboot 32-byte header is prepended to every application binary.
 * A TLV (Type-Length-Value) trailer is appended after the image data.
 *
 * Binary layout on flash:
 *
 *   ┌─────────────────────────────────────┐ ← slot_start (e.g. 0x00002000)
 *   │  image_header_t       (32 bytes)    │  MCUboot standard header
 *   ├─────────────────────────────────────┤ ← slot_start + ih_hdr_size
 *   │  Application code     (ih_img_size) │  raw .bin content
 *   ├─────────────────────────────────────┤
 *   │  image_tlv_info_t     (4 bytes)     │  TLV area header
 *   │  image_tlv_t + CRC32  (8 bytes)     │  CRC32 TLV entry
 *   └─────────────────────────────────────┘
 *
 * Image produced by imgtool.py:
 *   imgtool sign --key none --align 4 --version 1.0.0 \
 *                --header-size 32 --slot-size 0x1E000 \
 *                app.bin app_signed.bin
 *
 * TODO: replace CRC32 with SHA256 (IMAGE_TLV_SHA256 = 0x10) for
 *       cryptographic integrity. imgtool supports --key <pem> for
 *       RSA or ECDSA signing which implicitly adds SHA256.
 *       Verification requires ~300 bytes of SHA256 code and replaces
 *       image_verify_crc32() with image_verify_sha256().
 */

/* ---------------------------------------------------------------
 * Header constants
 * --------------------------------------------------------------- */
#define IMAGE_MAGIC             0x96f3b83dUL
#define IMAGE_HDR_SIZE          32U             /* fixed, always 32 */
#define IMAGE_TLV_INFO_MAGIC    0x6907U

/* TLV type codes */
#define IMAGE_TLV_CRC32         0x08U   /* CRC32 over header+image+tlv_info
                                         * TODO: replace with IMAGE_TLV_SHA256 */
#define IMAGE_TLV_SHA256        0x10U   /* SHA256 - standard MCUboot type,
                                         * 32-byte digest, use when signing
                                         * is added (see TODO above)         */

/* ih_flags bits */
#define IMAGE_F_NON_BOOTABLE    0x00000010UL  /* skip this image */

/* ---------------------------------------------------------------
 * Structures
 * --------------------------------------------------------------- */

typedef struct {
    uint8_t  iv_major;
    uint8_t  iv_minor;
    uint16_t iv_revision;
    uint32_t iv_build_num;
} __attribute__((packed)) image_version_t;  /* 8 bytes */

typedef struct {
    uint32_t        ih_magic;            /* IMAGE_MAGIC = 0x96f3b83d     */
    uint32_t        ih_load_addr;        /* 0x00000000 = position-indep  */
    uint16_t        ih_hdr_size;         /* always IMAGE_HDR_SIZE = 32   */
    uint16_t        ih_protect_tlv_size; /* protected TLV area size      */
    uint32_t        ih_img_size;         /* image bytes, excluding header */
    uint32_t        ih_flags;            /* IMAGE_F_* flags               */
    image_version_t ih_ver;              /* version embedded in binary    */
    uint32_t        _pad1;               /* reserved                      */
} __attribute__((packed)) image_header_t; /* 32 bytes - compile-checked below */

typedef struct {
    uint16_t it_magic;   /* IMAGE_TLV_INFO_MAGIC = 0x6907              */
    uint16_t it_tlv_tot; /* total TLV area bytes including this header */
} __attribute__((packed)) image_tlv_info_t;  /* 4 bytes */

typedef struct {
    uint8_t  it_type;   /* IMAGE_TLV_* type code  */
    uint8_t  _pad;
    uint16_t it_len;    /* payload length in bytes */
    /* payload bytes follow immediately */
} __attribute__((packed)) image_tlv_t;  /* 4 bytes header, payload appended */

/* Compile-time size checks */
typedef char hdr_size_check  [(sizeof(image_header_t)  == 32U) ? 1 : -1];
typedef char tlvi_size_check [(sizeof(image_tlv_info_t) ==  4U) ? 1 : -1];
typedef char tlv_size_check  [(sizeof(image_tlv_t)      ==  4U) ? 1 : -1];

/* ---------------------------------------------------------------
 * API
 * --------------------------------------------------------------- */

/*
 * image_header_valid - lightweight sanity check on the header.
 * Checks magic, hdr_size, img_size range, and NON_BOOTABLE flag.
 * Does NOT verify the CRC32 - call image_verify_crc32() for that.
 *
 * slot_start : byte address of the slot (e.g. NVM_SLOT_A_START)
 * slot_size  : maximum bytes available in the slot
 * Returns 1 if header looks valid, 0 otherwise.
 */
int image_header_valid(uint32_t slot_start, uint32_t slot_size);

/*
 * image_verify_crc32 - verify TLV CRC32 over header + image + tlv_info.
 *
 * Only called for SLOT_PENDING state (first boot after flash).
 * SLOT_VALID images skip this - they were already verified on first boot.
 *
 * CRC32 covers (contiguous in flash):
 *   header (ih_hdr_size bytes)
 *   image  (ih_img_size bytes)
 *   TLV info header (4 bytes)
 *
 * Returns 0 on match, -1 on mismatch or missing TLV.
 *
 * TODO: replace with image_verify_sha256() when signing is added.
 *       SHA256 covers the same byte range but produces a 32-byte digest
 *       verified against a trusted public key stored in bootloader flash.
 */
int image_verify_crc32(uint32_t slot_start);

/*
 * image_get_header - return pointer to header at slot_start.
 * Caller must call image_header_valid() first.
 */
static inline const image_header_t *image_get_header(uint32_t slot_start)
{
    return (const image_header_t *)slot_start;
}

/*
 * image_vtor - return the vector table address inside the image.
 * This is slot_start + ih_hdr_size (skip the MCUboot header).
 */
static inline uint32_t image_vtor(uint32_t slot_start)
{
    return slot_start + IMAGE_HDR_SIZE;
}

#endif /* IMAGE_H */