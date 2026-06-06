CC      = arm-none-eabi-gcc
OBJCOPY = arm-none-eabi-objcopy
SIZE    = arm-none-eabi-size

TARGET  = led_toggle
SRCS    = startup.c main.c
LD      = samd21j18a.ld

CPU     = -mcpu=cortex-m0plus -mthumb
CFLAGS  = $(CPU) -O1 -Wall -Wextra -std=c99 \
          -ffunction-sections -fdata-sections
LDFLAGS = $(CPU) -T$(LD) \
          -Wl,--gc-sections \
          -Wl,-Map=$(TARGET).map \
          -nostdlib

OBJS    = $(SRCS:.c=.o)

# ----------------------------------------------------------------
.PHONY: all clean flash

all: $(TARGET).elf $(TARGET).bin

$(TARGET).elf: $(OBJS)
	$(CC) $(LDFLAGS) $^ -o $@
	$(SIZE) $@

$(TARGET).bin: $(TARGET).elf
	$(OBJCOPY) -O binary $< $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Flash via OpenOCD + EDBG (CMSIS-DAP)
flash: $(TARGET).elf
	openocd -f openocd.cfg \
	        -c "program $(TARGET).elf verify reset exit"

clean:
	rm -f $(OBJS) $(TARGET).elf $(TARGET).bin $(TARGET).map