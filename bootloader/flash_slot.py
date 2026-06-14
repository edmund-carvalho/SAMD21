#!/usr/bin/env python3
"""
flash_slot.py — SAMD21J18A slot management via SWD + DCC

SWD operations  (CPU halted)  : flash erase, flash write, metadata r/w
DCC operations  (CPU running) : ping, get_info, set_pending, boot, reset

Both go through one persistent OpenOCD session (TCL port 4444).
UART is never touched by this script — it remains free for app logging.

Commands:
  load_app   [a|b] <binary> [--version X.Y.Z]
  swap_app
  get_app_versions
  get_status
  erase_slot [a|b]
  confirm_slot [a|b]
  dcc_ping                   — verify bootloader alive via DCC
  dcc_boot                   — trigger boot via DCC (bootloader must be in cmd mode)

DCC register map (External APB — host/debugger side):
  EXT_DCC0  0x41002010  host writes commands  → CPU reads
  EXT_DCC1  0x41002014  host reads  responses ← CPU writes

Handshake: sender sets bit31=VALID, receiver reads, clears to ACK.
"""

import argparse
import binascii
import os
import socket
import struct
import subprocess
import sys
import tempfile
import time

# ── Flash layout (must match nvmctrl.h) ──────────────────────────
NVM_SLOT_A_START = 0x00002000
NVM_SLOT_B_START = 0x00020000
NVM_SLOT_SIZE    = 0x1E000        # 120 KB
NVM_META_START   = 0x0003E000
NVM_META_ROW     = 256
NVM_PAGE_SIZE    = 64

# ── Metadata (must match metadata.h boot_meta_t) ─────────────────
META_MAGIC   = 0xB007AB1E
META_FMT     = '<IBBBB52sI'       # 64 bytes
META_SIZE    = struct.calcsize(META_FMT)
assert META_SIZE == 64

SLOT_EMPTY   = 0xFF
SLOT_PENDING = 0xFE
SLOT_VALID   = 0xFC
SLOT_INVALID = 0xF0
STATE_NAMES  = {SLOT_EMPTY:'EMPTY', SLOT_PENDING:'PENDING',
                SLOT_VALID:'VALID', SLOT_INVALID:'INVALID'}

# ── MCUboot header (must match image.h) ──────────────────────────
IMAGE_MAGIC    = 0x96f3b83d
IMAGE_HDR_SIZE = 32
IMAGE_HDR_FMT  = '<II HH II BBHI I'

# ── DCC register addresses (External APB — debugger side) ─────────
EXT_DCC0   = 0x41002010    # host → CPU
EXT_DCC1   = 0x41002014    # CPU  → host
VALID_BIT  = 0x80000000

# ── Protocol (must match proto.h) ─────────────────────────────────
CMD_PING         = 0x01
CMD_GET_INFO     = 0x02
CMD_ERASE_SLOT   = 0x03
CMD_WRITE_PAGE   = 0x04
CMD_VERIFY       = 0x05
CMD_SET_PENDING  = 0x06
CMD_BOOT         = 0x07
CMD_RESET        = 0x08
RSP_OK           = 0x00

OPENOCD_CFG    = 'samd21_xpro.cfg'
PREPARE_IMAGE  = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                               'prepare_image.py')

# ─────────────────────────────────────────────────────────────────
# OpenOCD TCL session
# ─────────────────────────────────────────────────────────────────

class OpenOCDSession:
    """
    Manages one OpenOCD server process and a TCL connection on port 4444.

    All SWD operations (halt, flash, mww, mdw) and DCC word transfers
    go through a single persistent connection, eliminating the overhead
    of spawning a new process per command.
    """

    TCL_PORT   = 4444
    TERM       = b'\x1a'

    def __init__(self, cfg=OPENOCD_CFG):
        self._proc = subprocess.Popen(
            ['openocd', '-f', cfg],
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT
        )
        self._wait_ready()
        self._sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self._sock.connect(('localhost', self.TCL_PORT))
        self._sock.settimeout(10.0)
        self._flush()

    def _wait_ready(self, timeout=8.0):
        """Wait for OpenOCD to print its ready message."""
        deadline = time.time() + timeout
        buf = b''
        self._proc.stdout.fileno()
        import select
        while time.time() < deadline:
            r, _, _ = select.select([self._proc.stdout], [], [], 0.1)
            if r:
                chunk = self._proc.stdout.read1(1024)
                buf += chunk
                if b'Listening on port' in buf:
                    time.sleep(0.1)   # let all ports open
                    return
        raise RuntimeError('OpenOCD did not start in time')

    def _flush(self):
        try:
            self._sock.settimeout(0.3)
            while True:
                d = self._sock.recv(4096)
                if not d or self.TERM in d:
                    break
        except (socket.timeout, OSError):
            pass
        finally:
            self._sock.settimeout(10.0)

    def tcl(self, command):
        """Send a TCL command string, return response string."""
        self._sock.sendall((command + '\x1a').encode())
        buf = b''
        while True:
            chunk = self._sock.recv(4096)
            buf += chunk
            if self.TERM in buf:
                break
        return buf.replace(self.TERM, b'').decode(errors='replace').strip()

    # ── Memory access ────────────────────────────────────────────

    def mww(self, addr, value):
        self.tcl(f'mww 0x{addr:08X} 0x{value:08X}')

    def mdw(self, addr):
        """Read one 32-bit word. Returns int."""
        resp = self.tcl(f'mdw 0x{addr:08X}')
        # Response: "0x0003e000: 0xb007ab1e "
        if ':' in resp:
            value_str = resp.split(':')[1].strip().split()[0]
            return int(value_str, 16)
        return 0

    def read_bytes(self, addr, length):
        """Read length bytes from addr via dump_image."""
        with tempfile.NamedTemporaryFile(suffix='.bin', delete=False) as f:
            tmp = f.name
        try:
            self.tcl(f'dump_image {tmp} 0x{addr:08X} {length}')
            with open(tmp, 'rb') as f:
                return f.read()
        finally:
            if os.path.exists(tmp):
                os.unlink(tmp)

    # ── CPU control ──────────────────────────────────────────────

    def halt(self):        self.tcl('halt')
    def resume(self):      self.tcl('resume')
    def reset_halt(self):  self.tcl('reset halt')
    def reset_run(self):   self.tcl('reset run')

    # ── Flash operations ─────────────────────────────────────────

    def flash_erase(self, addr, size):
        self.tcl(f'flash erase_address 0x{addr:08X} {size}')

    def flash_write_file(self, path, addr):
        self.tcl(f'flash write_bank 0 {path} 0x{addr:08X}')

    # ── DCC word handshake ───────────────────────────────────────

    def dcc_send_word(self, data, timeout=5.0):
        """Write 31-bit data to CPU via DCC0. Block until CPU ACKs."""
        self.mww(EXT_DCC0, (data & 0x7FFFFFFF) | VALID_BIT)
        deadline = time.time() + timeout
        while time.time() < deadline:
            if self.mdw(EXT_DCC0) == 0:
                return
            time.sleep(0.01)
        raise TimeoutError('DCC: CPU did not ACK DCC0')

    def dcc_recv_word(self, timeout=5.0):
        """Read 31-bit data from CPU via DCC1. Block until CPU writes."""
        deadline = time.time() + timeout
        while time.time() < deadline:
            w = self.mdw(EXT_DCC1)
            if w & VALID_BIT:
                self.mww(EXT_DCC1, 0)   # ACK
                return w & 0x7FFFFFFF
            time.sleep(0.01)
        raise TimeoutError('DCC: CPU did not write DCC1')

    # ── DCC frame exchange ────────────────────────────────────────

    def dcc_command(self, cmd, payload=b''):
        """
        Send one DCC command frame, receive response.
        Returns (status, data_bytes).

        Frame layout — see transport.h for full specification.
        """
        nbytes = len(payload)
        byte0  = payload[0] if nbytes > 0 else 0
        byte1  = payload[1] if nbytes > 1 else 0
        hdr    = (cmd << 24) | (nbytes << 16) | (byte0 << 8) | byte1
        self.dcc_send_word(hdr)

        # Additional payload words (3 bytes each)
        i = 2
        while i < nbytes:
            b0 = payload[i]     if i     < nbytes else 0
            b1 = payload[i + 1] if i + 1 < nbytes else 0
            b2 = payload[i + 2] if i + 2 < nbytes else 0
            self.dcc_send_word((b0 << 16) | (b1 << 8) | b2)
            i += 3

        # Receive response header word
        rhdr   = self.dcc_recv_word()
        status = (rhdr >> 24) & 0x7F
        rn     = (rhdr >> 16) & 0xFF
        data   = bytearray()
        if rn > 0: data.append((rhdr >> 8) & 0xFF)
        if rn > 1: data.append( rhdr       & 0xFF)

        # Additional response words
        received = len(data)
        while received < rn:
            w = self.dcc_recv_word()
            if received < rn: data.append((w >> 16) & 0xFF); received += 1
            if received < rn: data.append((w >>  8) & 0xFF); received += 1
            if received < rn: data.append( w        & 0xFF); received += 1

        return status, bytes(data)

    def close(self):
        try:
            self._sock.close()
        except Exception:
            pass
        self._proc.terminate()
        self._proc.wait()


# ─────────────────────────────────────────────────────────────────
# Metadata helpers
# ─────────────────────────────────────────────────────────────────

def meta_crc(data_without_crc):
    return binascii.crc32(data_without_crc) & 0xFFFFFFFF

def parse_meta(raw):
    if len(raw) < META_SIZE:
        return None
    magic, active, a_st, b_st, attempts, pad, crc = struct.unpack(META_FMT, raw)
    if magic != META_MAGIC or meta_crc(raw[:META_SIZE - 4]) != crc:
        return None
    return dict(magic=magic, active_slot=active,
                slot_a_state=a_st, slot_b_state=b_st,
                boot_attempts=attempts, pad=pad)

def pack_meta(m):
    body = struct.pack('<IBBBB52s',
        m['magic'], m['active_slot'],
        m['slot_a_state'], m['slot_b_state'],
        m['boot_attempts'], m['pad'])
    crc = meta_crc(body)
    return body + struct.pack('<I', crc)

def default_meta():
    return dict(magic=META_MAGIC, active_slot=0,
                slot_a_state=SLOT_EMPTY, slot_b_state=SLOT_EMPTY,
                boot_attempts=0, pad=b'\x00' * 52)

def read_meta(ocd):
    ocd.halt()
    raw = ocd.read_bytes(NVM_META_START, META_SIZE)
    return parse_meta(raw)

def write_meta(ocd, m):
    raw    = pack_meta(m)
    padded = raw + b'\xff' * (NVM_META_ROW - META_SIZE)
    with tempfile.NamedTemporaryFile(suffix='.bin', delete=False) as f:
        f.write(padded)
        tmp = f.name
    try:
        ocd.halt()
        ocd.flash_erase(NVM_META_START, NVM_META_ROW)
        ocd.flash_write_file(tmp, NVM_META_START)
    finally:
        os.unlink(tmp)

# ─────────────────────────────────────────────────────────────────
# Image header helpers
# ─────────────────────────────────────────────────────────────────

def read_image_header(ocd, slot):
    start = NVM_SLOT_A_START if slot == 'a' else NVM_SLOT_B_START
    ocd.halt()
    raw = ocd.read_bytes(start, IMAGE_HDR_SIZE)
    magic, _, _, _, img_size, _, major, minor, rev, build, _ = \
        struct.unpack(IMAGE_HDR_FMT, raw)
    if magic != IMAGE_MAGIC:
        return None
    return dict(major=major, minor=minor, revision=rev,
                build=build, img_size=img_size)

# ─────────────────────────────────────────────────────────────────
# Commands
# ─────────────────────────────────────────────────────────────────

def cmd_load_app(args):
    slot   = args.slot.lower()
    binary = args.binary

    if not os.path.exists(binary):
        sys.exit(f'File not found: {binary}')

    slot_start = NVM_SLOT_A_START if slot == 'a' else NVM_SLOT_B_START

    # 1. Prepare MCUboot image
    with tempfile.NamedTemporaryFile(suffix='_signed.bin', delete=False) as f:
        signed = f.name
    try:
        print(f'Preparing MCUboot image v{args.version}...')
        r = subprocess.run(
            [sys.executable, PREPARE_IMAGE, binary, signed,
             '--version', args.version, '--slot-size', hex(NVM_SLOT_SIZE)],
            capture_output=True, text=True)
        if r.returncode != 0:
            sys.exit(r.stderr)
        print(r.stdout.strip())

        with open(signed, 'rb') as f:
            image_data = f.read()

        with tempfile.NamedTemporaryFile(suffix='.bin', delete=False) as f:
            f.write(image_data)
            img_tmp = f.name

        ocd = OpenOCDSession()
        try:
            # 2. Erase + write slot
            print(f'Erasing slot {slot.upper()} (0x{slot_start:08X}, '
                  f'{NVM_SLOT_SIZE//1024}KB)...')
            ocd.halt()
            ocd.flash_erase(slot_start, NVM_SLOT_SIZE)

            print(f'Writing {len(image_data):,} bytes...')
            ocd.flash_write_file(img_tmp, slot_start)

            # 3. Update metadata: mark slot PENDING
            print('Updating metadata...')
            meta = read_meta(ocd) or default_meta()
            if slot == 'a':
                meta['slot_a_state'] = SLOT_PENDING
            else:
                meta['slot_b_state'] = SLOT_PENDING
            meta['boot_attempts'] = 0
            write_meta(ocd, meta)

            # 4. Reset — bootloader sees PENDING, verifies CRC32, boots app
            print('Resetting board...')
            ocd.reset_run()
            print(f'Done. Slot {slot.upper()} = PENDING. '
                  f'Bootloader will CRC32-verify and boot.')
        finally:
            ocd.close()
            os.unlink(img_tmp)
    finally:
        if os.path.exists(signed):
            os.unlink(signed)


def cmd_swap_app(args):
    ocd = OpenOCDSession()
    try:
        meta = read_meta(ocd)
        if meta is None:
            sys.exit('Metadata invalid')

        old_name = 'A' if meta['active_slot'] == 0 else 'B'
        meta['active_slot'] ^= 1
        meta['boot_attempts'] = 0
        new_name = 'A' if meta['active_slot'] == 0 else 'B'

        write_meta(ocd, meta)
        ocd.reset_run()
        print(f'Active slot: {old_name} → {new_name}. Board reset.')
    finally:
        ocd.close()


def cmd_get_app_versions(args):
    ocd = OpenOCDSession()
    try:
        for slot_char in ('a', 'b'):
            hdr = read_image_header(ocd, slot_char)
            if hdr:
                print(f'Slot {slot_char.upper()}: '
                      f'v{hdr["major"]}.{hdr["minor"]}.{hdr["revision"]}  '
                      f'({hdr["img_size"]:,} bytes)')
            else:
                print(f'Slot {slot_char.upper()}: no valid MCUboot header')
    finally:
        ocd.close()


def cmd_get_status(args):
    ocd = OpenOCDSession()
    try:
        meta = read_meta(ocd)
        if meta is None:
            print('Metadata: INVALID')
            return
        slot_names = {0: 'A', 1: 'B'}
        print(f'Active slot  : {slot_names[meta["active_slot"]]}')
        print(f'Boot attempts: {meta["boot_attempts"]}')
        print(f'Slot A state : {STATE_NAMES.get(meta["slot_a_state"], hex(meta["slot_a_state"]))}')
        print(f'Slot B state : {STATE_NAMES.get(meta["slot_b_state"], hex(meta["slot_b_state"]))}')
        for slot_char in ('a', 'b'):
            hdr = read_image_header(ocd, slot_char)
            if hdr:
                print(f'Slot {slot_char.upper()} version: '
                      f'v{hdr["major"]}.{hdr["minor"]}.{hdr["revision"]}')
    finally:
        ocd.close()


def cmd_erase_slot(args):
    slot  = args.slot.lower()
    start = NVM_SLOT_A_START if slot == 'a' else NVM_SLOT_B_START
    ocd   = OpenOCDSession()
    try:
        print(f'Erasing slot {slot.upper()}...')
        ocd.halt()
        ocd.flash_erase(start, NVM_SLOT_SIZE)
        meta = read_meta(ocd) or default_meta()
        if slot == 'a':
            meta['slot_a_state'] = SLOT_EMPTY
        else:
            meta['slot_b_state'] = SLOT_EMPTY
        write_meta(ocd, meta)
        print(f'Slot {slot.upper()} erased and marked EMPTY.')
    finally:
        ocd.close()


def cmd_confirm_slot(args):
    slot = args.slot.lower()
    ocd  = OpenOCDSession()
    try:
        meta = read_meta(ocd)
        if meta is None:
            sys.exit('Metadata invalid')
        if slot == 'a':
            meta['slot_a_state'] = SLOT_VALID
        else:
            meta['slot_b_state'] = SLOT_VALID
        meta['boot_attempts'] = 0
        write_meta(ocd, meta)
        print(f'Slot {slot.upper()} marked VALID.')
    finally:
        ocd.close()


def cmd_dcc_ping(args):
    """Verify bootloader is alive and in DCC command mode."""
    ocd = OpenOCDSession()
    try:
        ocd.resume()   # CPU must be running for DCC
        print('Sending DCC PING...')
        status, data = ocd.dcc_command(CMD_PING)
        if status == RSP_OK:
            pong = data.decode(errors='replace')
            print(f'DCC PONG: "{pong}"  ← bootloader alive in command mode')
        else:
            print(f'DCC error: status=0x{status:02X}')
    except TimeoutError as e:
        print(f'DCC timeout: {e}')
        print('Is the bootloader in command mode? (SW0 held, no valid app)')
    finally:
        ocd.close()


def cmd_dcc_boot(args):
    """Tell bootloader to boot active slot via DCC."""
    ocd = OpenOCDSession()
    try:
        ocd.resume()
        print('Sending DCC BOOT...')
        status, _ = ocd.dcc_command(CMD_BOOT)
        if status == RSP_OK:
            print('Bootloader acknowledged BOOT — resetting.')
        else:
            print(f'DCC error: status=0x{status:02X}')
    except TimeoutError as e:
        print(f'DCC timeout: {e}')
    finally:
        ocd.close()


# ─────────────────────────────────────────────────────────────────
# Entry point
# ─────────────────────────────────────────────────────────────────

def main():
    p = argparse.ArgumentParser(
        description='SAMD21J18A slot management — SWD + DCC via OpenOCD')
    sub = p.add_subparsers(dest='command', required=True)

    s = sub.add_parser('load_app',    help='Prepare and flash image to slot')
    s.add_argument('slot',    choices=['a','b','A','B'])
    s.add_argument('binary',  help='Raw .bin from arm-none-eabi-objcopy')
    s.add_argument('--version', default='1.0.0')

    sub.add_parser('swap_app',        help='Toggle active slot, reset board')
    sub.add_parser('get_app_versions',help='Show version in both slots')
    sub.add_parser('get_status',      help='Show full metadata state')

    s = sub.add_parser('erase_slot',  help='Erase slot and mark EMPTY')
    s.add_argument('slot', choices=['a','b','A','B'])

    s = sub.add_parser('confirm_slot',help='Manually mark slot VALID')
    s.add_argument('slot', choices=['a','b','A','B'])

    sub.add_parser('dcc_ping',        help='Verify bootloader via DCC PING')
    sub.add_parser('dcc_boot',        help='Trigger boot via DCC BOOT')

    args = p.parse_args()

    dispatch = {
        'load_app':          cmd_load_app,
        'swap_app':          cmd_swap_app,
        'get_app_versions':  cmd_get_app_versions,
        'get_status':        cmd_get_status,
        'erase_slot':        cmd_erase_slot,
        'confirm_slot':      cmd_confirm_slot,
        'dcc_ping':          cmd_dcc_ping,
        'dcc_boot':          cmd_dcc_boot,
    }
    dispatch[args.command](args)


if __name__ == '__main__':
    main()