# Makefile for SAMD21 FreeRTOS project
PROJECT = freertos_demo

# Toolchain
PREFIX = arm-none-eabi-
CC = $(PREFIX)gcc
OBJCOPY = $(PREFIX)objcopy
SIZE = $(PREFIX)size

# Directories
RTOS_DIR = ./freertos-kernel
RTOS_PORT_DIR = $(RTOS_DIR)/portable/GCC/ARM_CM0
RTOS_MEM_DIR = $(RTOS_DIR)/portable/MemMang
CMSIS_DIR = ../cmsis
DFP_DIR = ../Microchip.SAMD21_DFP.3.8.270/samd21a/include

APP_SRC = main.c \
          uart.c \
          startup.c

RTOS_SRC = $(RTOS_DIR)/tasks.c \
           $(RTOS_DIR)/queue.c \
           $(RTOS_DIR)/list.c \
           $(RTOS_DIR)/timers.c \
           $(RTOS_PORT_DIR)/port.c \
		   $(RTOS_PORT_DIR)/portasm.c \
           $(RTOS_MEM_DIR)/heap_4.c

SOURCES = $(APP_SRC) $(RTOS_SRC)

# Include paths
RTOS_INC = -I$(RTOS_DIR)/include \
           -I$(RTOS_PORT_DIR)
DFP_INC = -I$(DFP_DIR) -I$(CMSIS_DIR)
APP_INC = -I.  

INCLUDES = $(RTOS_INC) $(DFP_INC) $(APP_INC)
           

# Compiler flags
CPU_FLAGS = -mcpu=cortex-m0plus -mthumb
COMPILER_FLAGS = -g -O2 -Wall -Wextra 
CFLAGS =  $(CPU_FLAGS) $(COMPILER_FLAGS) \
         -ffunction-sections -fdata-sections \
         $(INCLUDES) \
         -D__SAMD21J18A__ \
         -DDEBUG

# Linker flags
LDFLAGS = $(CPU_FLAGS)  \
          -T samd21j18a.ld \
          -Wl,--gc-sections \
          -Wl,-Map=$(PROJECT).map \
          --specs=nosys.specs
# Add to LDFLAGS
LDFLAGS += -Wl,--undefined=vPortSVCHandler_C \
           -Wl,--undefined=vStartFirstTask \
           -Wl,--undefined=vRestoreContextOfFirstTask

# Objects
OBJECTS = $(SOURCES:.c=.o)

# Default target
all: $(PROJECT).elf $(PROJECT).bin

# Link
$(PROJECT).elf: $(OBJECTS)
	$(CC) $(LDFLAGS) $^ -o $@
	$(SIZE) $@

# Compile
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Generate binary
%.bin: %.elf
	$(OBJCOPY) -O binary $< $@

# Clean
clean:
	rm -f $(OBJECTS) $(PROJECT).elf $(PROJECT).bin $(PROJECT).map

# Flash with OpenOCD
flash: $(PROJECT).bin
	openocd -f samd21J18_OOCD.cfg -c "program $< 0x00000000 verify reset exit"

# Debug with GDB
debug: $(PROJECT).elf
	openocd -f samd21J18_OOCD.cfg &
	sleep 1
	$(PREFIX)gdb -ex "target remote localhost:3333" $<

.PHONY: all clean flash debug
