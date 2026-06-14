#!/usr/bin/env python3
"""
prepare_image.py — Phase 1 MCUboot-compatible image builder

Prepends a standard 32-byte MCUboot header to a raw .bin file and
appends a CRC32 TLV trailer (type 0x08).

Phase 2 migration: replace this script with:
    imgtool sign --key <dev.pem> --align 4 --version X.Y.Z \\
                 --header-size 32 --slot-size 0x1E000 \\
                 app.bin app_signed.bin
imgtool generates IMAGE_TLV_SHA256 (type 0x10) instead of CRC32.
Update image.h IMAGE_TLV_CRC32 -> IMAGE_TLV_SHA256 and
replace dsu_crc32() with SHA256 computation in image.c.
"""

import argparse
import binascii
import struct
import sys
import os

# ── MCUboot constants ────────────────────────────────────────────
IMAGE_MAGIC          = 0x96f3b83d
IMAGE_HDR_SIZE       = 32
IMAGE_TLV_INFO_MAGIC = 0x6907
IMAGE_TLV_CRC32      = 0x08   # custom type; imgtool uses 0x10 (SHA256)

# Header format: little-endian
# magic(4) load_addr(4) hdr_size(2) protect_tlv_size(2)
# img_size(4) flags(4)
# version: major(1) minor(1) revision(2) build_num(4)
# pad(4)
HDR_FMT = '<II HH II BBHI I'


def parse_version(s):
    parts = (s + '.0.0.0').split('.')[:4]
    return tuple(int(p) for p in parts)


def crc32(data: bytes) -> int:
    """Standard CRC32 (ISO 3309). Matches firmware crc32_compute()."""
    return binascii.crc32(data) & 0xFFFFFFFF


def build_image(raw_bin: bytes, version_str: str, slot_size: int) -> bytes:
    major, minor, revision, build = parse_version(version_str)
    img_size = len(raw_bin)

    # ── header ──────────────────────────────────────────────────
    header = struct.pack(HDR_FMT,
        IMAGE_MAGIC,     # magic
        0,               # load_addr (position independent)
        IMAGE_HDR_SIZE,  # hdr_size
        0,               # protect_tlv_size
        img_size,        # img_size (raw binary bytes, no header)
        0,               # flags
        major, minor, revision, build,
        0                # pad
    )
    assert len(header) == IMAGE_HDR_SIZE, f"Header is {len(header)}B, expected {IMAGE_HDR_SIZE}B"

    # ── TLV info header ──────────────────────────────────────────
    # total TLV area = tlv_info(4) + tlv_entry_hdr(4) + crc32_value(4) = 12
    tlv_total = 12
    tlv_info  = struct.pack('<HH', IMAGE_TLV_INFO_MAGIC, tlv_total)

    # ── CRC32 over header + image + tlv_info ─────────────────────
    covered = header + raw_bin + tlv_info
    crc_val  = crc32(covered)

    # ── CRC32 TLV entry ──────────────────────────────────────────
    # entry header: type(1) pad(1) len(2) = 4 bytes
    # entry data:   crc32 value = 4 bytes
    tlv_entry = struct.pack('<BBH', IMAGE_TLV_CRC32, 0, 4)
    tlv_value = struct.pack('<I', crc_val)

    image = header + raw_bin + tlv_info + tlv_entry + tlv_value

    if len(image) > slot_size:
        print(f"ERROR: image {len(image)}B exceeds slot size {slot_size}B", file=sys.stderr)
        sys.exit(1)

    return image, crc_val


def main():
    p = argparse.ArgumentParser(
        description='Build MCUboot-compatible image with CRC32 TLV')
    p.add_argument('input',    help='Raw .bin from arm-none-eabi-objcopy')
    p.add_argument('output',   help='Output signed image file')
    p.add_argument('--version',   default='1.0.0',
                   help='Version string major.minor.revision[.build] (default: 1.0.0)')
    p.add_argument('--slot-size', default='0x1E000', type=lambda x: int(x, 0),
                   help='Slot size in bytes (default: 0x1E000 = 120KB)')
    args = p.parse_args()

    with open(args.input, 'rb') as f:
        raw = f.read()

    image, crc_val = build_image(raw, args.version, args.slot_size)

    with open(args.output, 'wb') as f:
        f.write(image)

    print(f"  input : {args.input}  ({len(raw):,} bytes)")
    print(f"  output: {args.output}  ({len(image):,} bytes)")
    print(f"  version: {args.version}")
    print(f"  CRC32:   0x{crc_val:08X}")


if __name__ == '__main__':
    main()