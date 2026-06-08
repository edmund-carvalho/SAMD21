CC      = arm-none-eabi-gcc
OBJCOPY = arm-none-eabi-objcopy
SIZE    = arm-none-eabi-size

TARGET  = led_toggle
SRCS    = startup.c main.c uart.c
LD      = samd21j18a.ld

DFP_INC = ../Microchip.SAMD21_DFP.3.8.270/samd21a/include
CMSIS   = ../cmsis

CPU     = -mcpu=cortex-m0plus -mthumb
CFLAGS  = $(CPU) -O1 -Wall -Wextra -std=c99 \
          -ffunction-sections -fdata-sections \
          -I$(DFP_INC) -I$(CMSIS) \
          -D__SAMD21J18A__

# --specs=nano.specs  : newlib-nano (smaller printf)
# --specs=nosys.specs : stub syscalls (_exit, _read, etc.)
# -lc -lgcc           : C library + GCC runtime
LIBS    = --specs=nano.specs --specs=nosys.specs -lc -lgcc

LDFLAGS = $(CPU) -T$(LD) \
          -Wl,--gc-sections \
          -Wl,-Map=$(TARGET).map \
          -nostartfiles

OBJS    = $(SRCS:.c=.o)

# ----------------------------------------------------------------
.PHONY: all clean flash

all: $(TARGET).elf $(TARGET).bin

$(TARGET).elf: $(OBJS)
	$(CC) $(LDFLAGS) $^ $(LIBS) -o $@
	$(SIZE) $@

$(TARGET).bin: $(TARGET).elf
	$(OBJCOPY) -O binary $< $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

flash: $(TARGET).elf
	openocd -f samd21J18_OOCD.cfg \
	        -c "program $(TARGET).elf verify reset exit"

clean:
	rm -f $(OBJS) $(TARGET).elf $(TARGET).bin $(TARGET).map