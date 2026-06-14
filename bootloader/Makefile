CC      = arm-none-eabi-gcc
OBJCOPY = arm-none-eabi-objcopy
SIZE    = arm-none-eabi-size

TARGET  = bootloader
LD      = samd21j18a_bl.ld

DFP_INC = ../Microchip.SAMD21_DFP.3.8.270/samd21a/include
CMSIS   = ../cmsis

CPU     = -mcpu=cortex-m0plus -mthumb

# ── Transport selection ──────────────────────────────────────────
# Default: DCC (development — debugger required, UART is logs only)
# Field:   make all USE_UART=1  (no debugger, full UART protocol)
ifdef USE_UART
TRANSPORT_FLAG = -DUSE_UART
TRANSPORT_SRCS = transport_uart.c
else
TRANSPORT_FLAG =                  # USE_DCC is the absence of USE_UART
TRANSPORT_SRCS = transport_uart.c transport_dcc.c   # both compiled, DCC selected at runtime
endif

CFLAGS  = $(CPU) -O1 -Wall -Wextra -std=c99          \
          -ffunction-sections -fdata-sections          \
          -I$(DFP_INC) -I$(CMSIS)                      \
          -D__SAMD21J18A__                              \
          $(TRANSPORT_FLAG)

LIBS    = --specs=nano.specs --specs=nosys.specs -lc -lgcc

LDFLAGS = $(CPU) -T$(LD)                              \
          -Wl,--gc-sections                            \
          -Wl,-Map=$(TARGET).map                       \
          -nostartfiles

SRCS    = startup.c main.c uart.c                     \
          nvmctrl.c metadata.c image.c dsu.c           \
          wdt.c proto.c                                \
          transport_uart.c transport_dcc.c

OBJS    = $(SRCS:.c=.o)

# ----------------------------------------------------------------
.PHONY: all clean flash size flash_a flash_b swap_app \
        status versions erase_a erase_b recover bootprot bootprot-none

all: $(TARGET).elf $(TARGET).bin

$(TARGET).elf: $(OBJS)
	$(CC) $(LDFLAGS) $^ $(LIBS) -o $@
	$(SIZE) $@

$(TARGET).bin: $(TARGET).elf
	$(OBJCOPY) -O binary $< $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

size: $(TARGET).elf
	@echo "--- Flash budget (must be < 8192 bytes) ---"
	$(SIZE) $<

flash: $(TARGET).elf
	openocd -f samd21_xpro.cfg \
	        -c "program $(TARGET).elf verify reset exit"

# ── Slot management (SWD + DCC via OpenOCD) ─────────────────────
VERSION ?= 1.0.0

flash_a: $(TARGET).bin
	python3 flash_slot.py load_app a $< --version $(VERSION)

flash_b: $(TARGET).bin
	python3 flash_slot.py load_app b $< --version $(VERSION)

swap_app:
	python3 flash_slot.py swap_app

status:
	python3 flash_slot.py get_status

versions:
	python3 flash_slot.py get_app_versions

erase_a:
	python3 flash_slot.py erase_slot a

erase_b:
	python3 flash_slot.py erase_slot b

# ── Recovery / fuse management ───────────────────────────────────
recover:
	openocd -f samd21_xpro.cfg \
	        -c "init; at91samd chip-erase; exit"

bootprot:
	openocd -f samd21_xpro.cfg \
	        -c "init; at91samd bootprotect 8192; exit"

bootprot-none:
	openocd -f samd21_xpro.cfg \
	        -c "init; at91samd bootprotect none; exit"

clean:
	rm -f $(OBJS) $(TARGET).elf $(TARGET).bin $(TARGET).map