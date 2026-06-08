# SAMD21 Bare-Metal Examples

Bare-metal examples for the SAM D21 Xplained Pro development board.
No IDE, no HAL, no RTOS. Built with open-source tools only.

---

## Hardware

| Item | Detail |
|---|---|
| MCU | ATSAMD21J18A - ARM Cortex-M0+, 256 KB flash, 32 KB RAM |
| Board | SAM D21 Xplained Pro |
| Programmer | On-board EDBG (CMSIS-DAP) - same USB cable for flash and UART |
| LED0 | PB30, active low |
| SW0 | PA15, active low, internal pull-up |
| UART TX | PA22 → SERCOM3/PAD[0] → EDBG → `/dev/ttyACM0` |
| UART RX | PA23 ← SERCOM3/PAD[1] ← EDBG ← `/dev/ttyACM0` |

---

## Repository structure

```
SAMD21/
├── cmsis/                          ARM CMSIS core headers (see Dependencies)
├── Microchip.SAMD21_DFP.3.8.270/  Microchip device pack (see Dependencies)
├── led_toggle/                     Project 1 - LED + button, no UART
│   ├── main.c
│   ├── startup.c
│   ├── samd21j18a.ld
│   ├── Makefile
│   └── samd21_xpro.cfg
├── uart/                           Project 2 - LED + button + UART console
│   ├── main.c
│   ├── uart.h
│   ├── uart.c
│   ├── startup.c
│   ├── samd21j18a.ld
│   ├── Makefile
│   └── samd21_xpro.cfg
├── LICENSE
└── README.md
```

---

## Prerequisites

### Toolchain (Ubuntu)

```bash
sudo apt install gcc-arm-none-eabi binutils-arm-none-eabi make
```

Verify:

```bash
arm-none-eabi-gcc --version   # tested: 10.3.1 and 13.2.1
openocd --version             # tested: xPack 0.12.0
make --version                # tested: 4.3+
```

OpenOCD via apt or from
https://github.com/xpack-binaries/openocd-xpack/releases

### Linux permissions

```bash
sudo usermod -aG dialout $USER
# log out and back in for it to take effect
```

Required for `/dev/ttyACM*` access without sudo.

---

## Dependencies

Both must be present in the repository root before building either project.

### 1. Microchip SAMD21 Device Family Pack

Download the `.atpack` file (it is a ZIP archive):

```
https://packs.download.microchip.com/Microchip.SAMD21_DFP.3.8.270.atpack
```

Extract into the repository root:

```bash
unzip Microchip.SAMD21_DFP.3.8.270.atpack -d Microchip.SAMD21_DFP.3.8.270
```

The Makefile in each project expects headers at:

```
../Microchip.SAMD21_DFP.3.8.270/samd21a/include/
```

### 2. ARM CMSIS Core Headers

```bash
mkdir -p cmsis && cd cmsis
BASE=https://raw.githubusercontent.com/ARM-software/CMSIS_5/develop/CMSIS/Core/Include
wget $BASE/core_cm0plus.h $BASE/cmsis_compiler.h $BASE/cmsis_gcc.h \
     $BASE/cmsis_version.h $BASE/mpu_armv7.h
cd ..
```

The Makefile in each project expects headers at:

```
../cmsis/
```

---

## Project 1 - led_toggle

LED toggles on each SW0 press with debounce. No UART.

**System clock:** 1 MHz (OSC8M default, PRESC=3)

### Build and flash

```bash
cd led_toggle
make clean && make all
make flash
```

### Behaviour

Press SW0 → LED0 toggles. Release and wait 50 ms debounce before next press
is recognised.

---

## Project 2 - uart

LED toggle with UART debug console. Each button press prints a message to
`/dev/ttyACM0` via the EDBG Virtual COM port.

**System clock:** 8 MHz (OSC8M, PRESC=0 - set in `uart_init`)

### Build and flash

```bash
cd uart
make clean && make all
make flash
```

### Open terminal

```bash
ls /dev/ttyACM*
# /dev/ttyACM0   CMSIS-DAP HID (debug)
# /dev/ttyACM0   Virtual COM port (UART bridge)

minicom -D /dev/ttyACM0 -b 115200
```

### Expected output

```
boot ok - 8 MHz - 115200 8N1
SW0 press #1 - LED toggled
SW0 press #2 - LED toggled
```

---

## UART driver - implementation notes

### Layer 1 - Clock tree

The SAMD21 boots with OSC8M divided by 8 → 1 MHz. At 1 MHz the maximum
baud rate with 16× oversampling is 62,500 - below 115,200. `uart_init`
removes the prescaler first:

```c
SYSCTRL_REGS->SYSCTRL_OSC8M =
    (SYSCTRL_REGS->SYSCTRL_OSC8M & ~SYSCTRL_OSC8M_PRESC_Msk)
    | SYSCTRL_OSC8M_PRESC(0);   // divide by 1 → 8 MHz
```

This affects the whole chip - the CPU also runs at 8 MHz after this call.
`delay_ms` uses `F_CPU_HZ = 8000000` (defined in `uart.h`) so SysTick is
recalculated correctly.

Each SERCOM has an independent clock channel. GCLK Generator 1 is configured
at 8 MHz and routed to `SERCOM3_CORE`. Generator 0 (CPU) is left untouched:

```c
GCLK_REGS->GCLK_GENCTRL = GCLK_GENCTRL_ID(1)
                          | GCLK_GENCTRL_SRC_OSC8M
                          | GCLK_GENCTRL_GENEN_Msk;

GCLK_REGS->GCLK_CLKCTRL = GCLK_CLKCTRL_ID_SERCOM3_CORE
                          | GCLK_CLKCTRL_GEN_GCLK1
                          | GCLK_CLKCTRL_CLKEN_Msk;
```

### Layer 2 - Power Manager

The APB bus clock gate must be opened before any SERCOM3 register access:

```c
PM_REGS->PM_APBCMASK |= PM_APBCMASK_SERCOM3_Msk;
```

### Layer 3 - PORT mux

PA22 and PA23 are GPIO by default. Function C maps both to SERCOM3.
PA22 is even → `PMUX[11]` lower nibble (PMUXE).
PA23 is odd  → `PMUX[11]` upper nibble (PMUXO).

```c
PORT_REGS->GROUP[0].PORT_PINCFG[22] = PORT_PINCFG_PMUXEN_Msk;
PORT_REGS->GROUP[0].PORT_PINCFG[23] = PORT_PINCFG_PMUXEN_Msk
                                      | PORT_PINCFG_INEN_Msk;
PORT_REGS->GROUP[0].PORT_PMUX[11]   = PORT_PMUX_PMUXE(0x2U)
                                      | PORT_PMUX_PMUXO(0x2U);
```

PA23 requires `INEN` - the input buffer must be active for the peripheral
to read the RX signal.

### Layer 4 - SERCOM3 USART

SERCOM3 is software-reset first, then configured across three registers.

**CTRLA** - frame format and pin assignment:

```
MODE  = 0x1  USART with internal clock
SAMPR = 0x0  16× oversampling, arithmetic baud generation
TXPO  = 0x0  TX on PAD[0] = PA22
RXPO  = 0x1  RX on PAD[1] = PA23
DORD       LSB first (standard UART)
```

**CTRLB** - data format:

```
CHSIZE = 0x0  8 data bits, 1 stop bit, no parity
TXEN, RXEN    both enabled
```

**BAUD** - 16× arithmetic oversampling:

```
BAUD = 65536 × (1 − 16 × f_baud / f_gclk)
     = 65536 × (1 − 16 × 115200 / 8000000)
     = 50437    (actual: 115196 Hz, error: 0.003%)
```

Every write to CTRLA or CTRLB requires polling `SYNCBUSY` - the SERCOM
clock domain is asynchronous to the CPU bus.

### Layer 5 - Write path and printf retargeting

`uart_putc` polls DRE (Data Register Empty) before each byte - blocking,
acceptable for a debug UART:

```c
while (!(SERCOM3_REGS->USART_INT.SERCOM_INTFLAG & SERCOM_USART_INT_INTFLAG_DRE_Msk));
SERCOM3_REGS->USART_INT.SERCOM_DATA = (uint16_t)c;
```

`_write` overrides the newlib syscall so `printf` routes through `uart_putc`:

```
printf → newlib → _write → uart_putc → SERCOM3 → PA22 → EDBG → /dev/ttyACM0
```

`\n` is expanded to `\r\n` inside `_write` for terminal compatibility.

`_sbrk` provides a minimal heap from `_end` (top of `.bss`) for newlib-nano's
internal `printf` buffer. Five additional stubs (`_close`, `_fstat`,
`_isatty`, `_lseek`, `_read`) suppress linker warnings from `nosys.specs`.

### Design decisions

| Decision | Alternative | Reason |
|---|---|---|
| 8 MHz OSC8M | DFLL48M PLL | Simpler - no PLL lock sequence |
| GEN1 for SERCOM3 | Reuse GEN0 | GEN0 is the CPU clock - keep isolated |
| Blocking TX | Interrupt-driven | Sufficient for debug, avoids IRQ complexity |
| `_write` retarget | Custom `uart_printf` | Standard `printf` works unchanged |
| Syscall stubs in uart.c | Separate syscalls.c | Fewer files for a small project |

---

## Makefile targets

Both projects share the same targets:

| Target | Action |
|---|---|
| `make all` | Compile and link - produces `.elf` and `.bin` |
| `make flash` | Flash via OpenOCD + EDBG |
| `make clean` | Remove all build artefacts |

---

## Known issues

| # | Problem | Cause | Fix |
|---|---|---|---|
| 1 | OpenOCD "unable to find CMSIS-DAP device" on Windows | Zadig installed WinUSB on wrong USB interface | Switched to Ubuntu |
| 2 | `PORT` undeclared, `PORT_PINCFG_INEN` undeclared | Code written for Atmel ASF style; DFP 3.8.270 uses Harmony v3 style | Rewrote to `PORT_REGS->GROUP[]`; masks renamed to `_Msk` suffix |
| 3 | `undefined reference to '_end'` at link time | `_end = .` outside any output section loses RAM context | Moved `_end = .` inside `.bss` section body |
| 4 | No `/dev/ttyACM*` on Ubuntu | Kernel 6.8.0-rc7 compiled with `CONFIG_USB_ACM=not set` | Installed Ubuntu LTS with stock kernel |
| 5 | `/dev/ttyACM*` permission denied | User not in `dialout` group | `sudo usermod -aG dialout $USER` |
| 6 | Linker warnings: `_close` / `_fstat` / `_isatty` / `_lseek` / `_read` | `nosys.specs` weak stubs warn even when unused | Added strong stub implementations in `uart.c` |