# SAMD21J18A Dual-Slot UART/SWD Bootloader

A bare-metal, MCUboot-compatible dual-slot bootloader for the SAM D21
Xplained Pro, with automatic fallback, hardware CRC32 verification, and
SWD/DCC-based development tooling. No IDE, no vendor HAL.

---

## High-level architecture

### Flash layout

```
Address     Region          Size    Notes
──────────────────────────────────────────────────────────────
0x00000     Bootloader       8 KB   BOOTPROT protected (fuse)
0x02000     App Slot A     120 KB   MCUboot header + image + CRC32 TLV
0x20000     App Slot B     120 KB   MCUboot header + image + CRC32 TLV
0x3E000     Metadata         8 KB   boot_meta_t (64B used, row-aligned)
0x40000     end of flash
──────────────────────────────────────────────────────────────
All boundaries row-aligned (256B). NVM page = 64B, row = 256B.
```

### Image format — MCUboot-compatible

Each slot contains a 32-byte MCUboot header, the raw application binary,
and a TLV trailer with a CRC32 (Phase 1) or SHA256 (Phase 2, not yet
implemented) checksum.

```
┌─────────────────────────────────────┐ ← slot_start (0x2000 / 0x20000)
│  image_header_t       (32 bytes)    │  magic, version, img_size
├─────────────────────────────────────┤ ← slot_start + 32
│  Application code     (ih_img_size) │  raw .bin, VTOR-relocated
├─────────────────────────────────────┤
│  image_tlv_info_t     (4 bytes)     │  TLV area header
│  image_tlv_t + CRC32  (8 bytes)     │  type 0x08, 4-byte CRC32
└─────────────────────────────────────┘
```

The application's vector table starts at `slot_start + 32`. Every app's
linker script and `Reset_Handler` must account for this offset (see
Procedure, Step 3).

### Slot state machine

States are encoded so transitions only clear bits (flash erases to
`0xFF`, writes can only clear bits — guards against partial-write
corruption producing a false VALID state):

```
SLOT_EMPTY   = 0xFF   erased, no image
SLOT_PENDING = 0xFE   image written, CRC32 not yet verified
SLOT_VALID   = 0xFC   CRC32 verified once, app confirmed boot
SLOT_INVALID = 0xF0   exceeded boot attempts — do not boot
```

### Boot decision flow

```
Reset
  │
  ▼
Read metadata @ 0x3E000 (magic + CRC32 check)
  │
  ├── invalid ────────────────────────────────► command mode
  │
  ▼
SW0 held at reset?
  ├── yes ────────────────────────────────────► command mode
  │
  ▼
active_slot state?
  │
  ├── EMPTY / INVALID ─────────────────────────► try other slot
  │
  ├── boot_attempts >= 3
  │       └── mark INVALID, try other slot
  │
  ├── PENDING
  │       └── full validate: header + DSU CRC32 (~15ms/120KB)
  │           ├── pass → boot (attempts++, arm WDT 500ms)
  │           └── fail → mark INVALID, try other slot
  │
  └── VALID
          └── header check only (CRC32 already verified once)
              ├── pass → boot (attempts++, arm WDT 500ms)
              └── fail → mark INVALID, try other slot

try other slot:
  ├── VALID/PENDING → swap active_slot, validate as above
  └── EMPTY/INVALID → command mode
```

### Boot confirmation — WDT + LED0

```
Bootloader                          Application
──────────                          ───────────
wdt_arm()  500ms (OSCULP32K)
PER=14 = 16384 cycles = exactly 500ms
  │
  ▼
jump to app ──────────────────────► main()
                                       LED0 ON (PB30 low)
                                       printf("APP vX.Y.Z booted")
                                       confirm_boot()
                                         ├── slot state = VALID
                                         ├── boot_attempts = 0
                                         ├── wdt_disarm()
                                         └── LED0 OFF
                                     while(1) { ... }

If confirm_boot() is never called:
  WDT fires at 500ms → reset → boot_attempts++
  After 3 such resets → slot marked INVALID → fallback
```

### Interfaces — what runs where

```
┌─────────────────────────────────────────────────────┐
│  EDBG (on-board, single USB cable)                  │
│                                                      │
│  CMSIS-DAP (SWD)  ──► flash erase/write, metadata   │
│                       r/w, DCC command-response     │
│                       Used by: flash_slot.py         │
│                                                      │
│  CDC (UART)       ──► printf logs ONLY               │
│                       Bootloader logs + app logs     │
│                       /dev/ttyACM1, 115200 8N1       │
└─────────────────────────────────────────────────────┘
```

Two transports are compiled in for the command-response protocol:

| Transport | Channel | When used |
|---|---|---|
| `transport_dcc` (default) | DSU DCC0/DCC1 via SWD | Development — debugger always present, UART stays clean |
| `transport_uart` (`USE_UART=1`) | SERCOM3 byte stream | Field updates — no debugger available |

DCC uses a 1-word handshake (`bit31=VALID`) — see `transport.h` for the
full frame format. UART uses STX + LEN + CRC16 framing — see
`transport_uart.c`.

Flash writes (`ERASE_SLOT`, `WRITE_PAGE`) are **not** sent over DCC —
OpenOCD performs these directly via SWD `flash erase_address` /
`flash write_bank`. DCC is used only for high-level commands
(`PING`, `GET_INFO`, `SET_PENDING`, `BOOT`, `RESET`).

### Hardware acceleration

| Function | Implementation | Speed |
|---|---|---|
| Image CRC32 (TLV verify) | DSU hardware CRC32 | ~15ms for 120KB |
| Image CRC32 (software fallback) | bit-by-bit, metadata.c | ~614ms for 120KB |
| Metadata CRC32 (64B) | software, metadata.c | negligible |

DSU CRC32 is only invoked for `SLOT_PENDING` (first boot after a new
image is written). `SLOT_VALID` skips CRC32 entirely — it was verified
once and is trusted thereafter.

---

## Procedure — compile to running application

### Step 1 — Build and flash the bootloader (once per board)

```bash
cd bootloader
make clean && make all     # DCC transport (default)
make size                   # confirm < 8192 bytes
make flash                  # write to 0x00000000 via SWD
make bootprot                # protect 8KB — BOOTPROT fuse = 0x2
```

### Step 2 — Prepare an application project

Every application needs:

1. Copy `confirm.c/h`, `metadata.c/h`, `nvmctrl.c/h`, `wdt.c/h` from
   `bootloader/` into the app project.

2. In `startup.c` `Reset_Handler`, relocate the vector table after
   copying `.data`/`.bss`:
   ```c
   SCB->VTOR = 0x00002020UL;   /* slot start + 32-byte MCUboot header */
   ```

3. In the linker script, shift `FLASH` origin by 32 bytes:
   ```ld
   FLASH (rx) : ORIGIN = 0x00002020, LENGTH = 0x1DFE0  /* 120KB - 32B */
   ```
   (For slot B: `ORIGIN = 0x00020020`.)

4. In `main()`, signal entry via LED0 and call `confirm_boot()` within
   500ms:
   ```c
   PORT_REGS->GROUP[1].PORT_OUTCLR = (1UL << 30);  /* LED0 ON */
   printf("APP vX.Y.Z booted\r\n");
   confirm_boot();                                  /* LED0 OFF */
   ```

See `examples/app1_main.c` for a complete minimal application.

### Step 3 — Build the application

```bash
cd ../myapp
make clean && make all
# produces myapp.bin (raw binary, app vector table at offset 0)
```

### Step 4 — Flash to a slot

```bash
cd ../bootloader
make flash_a VERSION=1.0.0 BIN=../myapp/myapp.bin
```

Internally:
1. `prepare_image.py` wraps the raw binary with a 32-byte MCUboot
   header and appends a CRC32 TLV trailer.
2. OpenOCD erases and writes the signed image to slot A via SWD.
3. `flash_slot.py` reads, updates (`slot_a_state = PENDING`), and
   rewrites the metadata row.
4. OpenOCD issues `reset run`.

### Step 5 — Boot sequence runs automatically

```
BL v1.0 — SAMD21J18A  transport:DCC
BL: slot A CRC32 verify...
BL: slot A CRC32 OK
BL: slot A v1.0.0 attempt 1/3
APP v1.0.0 — booted OK
APP v1.0.0 — confirmed, running
APP v1.0.0 — uptime 0 s
APP v1.0.0 — uptime 1 s
```

`slot_a_state` is now `VALID`. Every subsequent reset skips the CRC32
check (header check only) and boots in milliseconds.

---

## CLI reference — `flash_slot.py` / Makefile targets

| Command | Effect |
|---|---|
| `make flash_a VERSION=X.Y.Z BIN=<path>` | Sign + flash to slot A, mark PENDING |
| `make flash_b VERSION=X.Y.Z BIN=<path>` | Sign + flash to slot B, mark PENDING |
| `make swap_app` | Toggle active slot, reset board |
| `make status` | Print active slot, states, boot_attempts |
| `make versions` | Print MCUboot version from both slot headers |
| `make erase_a` / `make erase_b` | Erase slot, mark EMPTY |
| `python3 flash_slot.py confirm_slot a` | Manually mark slot VALID (dev only) |
| `python3 flash_slot.py dcc_ping` | Verify bootloader alive via DCC (command mode) |
| `python3 flash_slot.py dcc_boot` | Trigger boot via DCC (command mode) |
| `make recover` | **Last resort** — DSU chip erase, wipes everything |
| `make bootprot` / `make bootprot-none` | Set/clear BOOTPROT fuse |

---

## Example applications

Three sample apps in `examples/` demonstrate the full state machine.
Each is a complete `main()` — combine with `uart.c`, `confirm.c`,
`metadata.c`, `nvmctrl.c`, `wdt.c`, `startup.c` (Step 2 above) to build.

### app1_main.c — v1.0.0 (good)

Prints its version, calls `confirm_boot()` within 500ms, then loops
printing uptime once per second.

```bash
make flash_a VERSION=1.0.0 BIN=app1.bin
```

```
BL: slot A v1.0.0 attempt 1/3
APP v1.0.0 — booted OK
APP v1.0.0 — confirmed, running
APP v1.0.0 — uptime 0 s
```

### app2_main.c — v1.0.123 (good, update)

Identical structure to app1, different version — demonstrates loading
an update to the inactive slot and switching to it.

```bash
make flash_b VERSION=1.0.123 BIN=app2.bin
make swap_app
```

```
BL: slot B v1.0.123 attempt 1/3
APP v1.0.123 — booted OK
APP v1.0.123 — confirmed, running
APP v1.0.123 — uptime 0 s
```

Slot A (`v1.0.0`, `VALID`) remains the automatic fallback.

### app3_main.c — v3.2.1 (bad — never confirms)

Prints its version, turns LED0 on, then hangs forever. Never calls
`confirm_boot()`. Demonstrates the 3-attempt fallback mechanism.

```bash
make flash_b VERSION=3.2.1 BIN=app3.bin
make swap_app
```

Expected sequence across four resets (each ~500ms apart, automatic):

```
BL: slot B v3.2.1 attempt 1/3
APP v3.2.1 — booted, simulating hang (no confirm_boot)
                                    ← 500ms, WDT fires, reset
BL: slot B v3.2.1 attempt 2/3
APP v3.2.1 — booted, simulating hang (no confirm_boot)
                                    ← 500ms, WDT fires, reset
BL: slot B v3.2.1 attempt 3/3
APP v3.2.1 — booted, simulating hang (no confirm_boot)
                                    ← 500ms, WDT fires, reset
BL: slot B max attempts -- INVALID
BL: fallback slot A
BL: slot A v1.0.0 attempt 1/3
APP v1.0.0 — booted OK
APP v1.0.0 — confirmed, running
```

Final state: `slot_b_state = INVALID`, `slot_a_state = VALID`,
`active_slot` unchanged (still B in metadata — fallback is per-boot,
not persistent — fix slot B and reflash to recover it).

---

## Recovery

If the device becomes unresponsive (corrupted bootloader, locked
security bit):

```bash
make recover        # DSU chip-erase — wipes EVERYTHING, including BOOTPROT
make flash          # reflash bootloader
make bootprot       # re-protect
```

---

## TODO — Phase 2

- Replace CRC32 TLV (type `0x08`) with SHA256 (`IMAGE_TLV_SHA256 = 0x10`)
  using `imgtool sign --key <dev.pem>`. See TODO comments in `image.h`
  and `image.c` at each integration point.
- `transport_uart.c` CRC16 framing retained for field updates without
  a debugger — exercise this path with `make all USE_UART=1`.