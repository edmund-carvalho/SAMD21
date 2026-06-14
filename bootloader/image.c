#include <string.h>
#include "image.h"
#include "metadata.h"   /* crc32_compute — retained for small buffers */
#include "dsu.h"        /* hardware CRC32 for image verification      */
#include "nvmctrl.h"    /* NVM_SLOT_A_SIZE for range checks           */

int image_header_valid(uint32_t slot_start, uint32_t slot_size)
{
    const image_header_t *hdr = (const image_header_t *)slot_start;

    if (hdr->ih_magic != IMAGE_MAGIC)       return 0;
    if (hdr->ih_hdr_size != IMAGE_HDR_SIZE) return 0;
    if (hdr->ih_img_size == 0)              return 0;

    /* Image + header + smallest possible TLV must fit in slot */
    uint32_t min_total = hdr->ih_hdr_size + hdr->ih_img_size
                       + sizeof(image_tlv_info_t)
                       + sizeof(image_tlv_t) + 4U;
    if (min_total > slot_size)              return 0;

    if (hdr->ih_flags & IMAGE_F_NON_BOOTABLE) return 0;

    return 1;
}

int image_verify_crc32(uint32_t slot_start)
{
    const image_header_t  *hdr      = (const image_header_t *)slot_start;
    uint32_t               tlv_base = slot_start
                                      + hdr->ih_hdr_size
                                      + hdr->ih_img_size;
    const image_tlv_info_t *tlv_info = (const image_tlv_info_t *)tlv_base;
    uint32_t               offset;
    uint32_t               stored_crc;
    uint32_t               computed_crc;

    if (tlv_info->it_magic != IMAGE_TLV_INFO_MAGIC) return -1;

    /*
     * CRC32 covers header + image data + TLV info header — contiguous
     * in flash.
     *
     * Use DSU hardware CRC32 (~15ms for 120KB) instead of software
     * (~614ms). cover_len must be 4-byte aligned — guaranteed because
     * prepare_image.py pads images to 4-byte alignment.
     *
     * TODO: when upgrading to SHA256 (IMAGE_TLV_SHA256 = 0x10),
     *       replace dsu_crc32() with a SHA256 computation over the
     *       same byte range and verify against the 32-byte digest in
     *       the TLV. DSU does not have a SHA256 hardware accelerator —
     *       use a software SHA256 implementation (~300 bytes of code).
     */
    uint32_t cover_len = hdr->ih_hdr_size
                       + hdr->ih_img_size
                       + (uint32_t)sizeof(image_tlv_info_t);

    /* Round down to 4-byte boundary for DSU; handle remainder in software */
    uint32_t dsu_len      = cover_len & ~0x3U;
    uint32_t remainder    = cover_len - dsu_len;

    dsu_err_t dsu_err = dsu_crc32(slot_start, dsu_len, &computed_crc);
    if (dsu_err != DSU_CRC_OK) return -1;

    /* Handle trailing bytes (0-3) in software if cover_len not aligned */
    if (remainder > 0U) {
        /* Rare case — TLV info is 4 bytes so this only occurs if
         * ih_img_size is not 4-byte aligned, which prepare_image.py
         * prevents. Software continuation would need a stateful CRC32. */
        return -1;   /* reject unaligned images */
    }

    /* Walk TLV entries looking for IMAGE_TLV_CRC32 */
    offset = (uint32_t)sizeof(image_tlv_info_t);

    while (offset + (uint32_t)sizeof(image_tlv_t) <= tlv_info->it_tlv_tot) {
        const image_tlv_t *tlv =
            (const image_tlv_t *)(tlv_base + offset);

        if (tlv->it_type == IMAGE_TLV_CRC32) {
            if (tlv->it_len != 4U) return -1;
            memcpy(&stored_crc,
                   (const uint8_t *)(tlv_base + offset
                                     + (uint32_t)sizeof(image_tlv_t)),
                   4U);
            return (computed_crc == stored_crc) ? 0 : -1;
        }

        /*
         * TODO: IMAGE_TLV_SHA256 (0x10) handler here.
         * When imgtool with --key is used (Phase 2), the TLV will
         * contain a 32-byte SHA256 digest instead of a 4-byte CRC32.
         * Verify by computing SHA256 over the same cover_len bytes
         * and comparing to the stored digest. No key needed for
         * pure hash verification — add key check for RSA/ECDSA signing.
         */

        offset += (uint32_t)sizeof(image_tlv_t) + tlv->it_len;
    }

    return -1;  /* IMAGE_TLV_CRC32 entry not found */
}