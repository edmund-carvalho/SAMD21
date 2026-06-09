# FreeRTOS on SAMD21J18A (Explained Pro Board)

A minimal FreeRTOS port for the SAMD21J18A microcontroller running at 8 MHz, with UART, GPIO, and a button‑press queue demo. The system runs three tasks (LED toggling, UART reporting, button polling) and uses FreeRTOS V11.1.0+.

## Hardware

- Board: SAM D21 Xplained Pro (or compatible with SAMD21J18A)
- LED: PB30 (active low)
- Button: PA15 (active low, internal pull‑up)
- UART: SERCOM3 on PA22 (TX) and PA23 (RX) – 115200 8N1

## Clock Configuration

The system clock is set to 8 MHz using the internal OSC8M (no prescaler).  
This clock drives the CPU, SysTick, and the SERCOM3 UART.

- `configCPU_CLOCK_HZ = 8000000UL`
- SysTick reload for 1 ms tick: `8000000 / 1000 - 1 = 7999`
- UART baud rate: 115200 with 0.003% error

## Project Structure

```
freeRTOS_demo/
├── FreeRTOSConfig.h           # FreeRTOS configuration
├── Makefile                   # Build script
├── main.c                     # Application tasks and initialisation
├── startup.c                  # Reset handler, vector table, .data/.bss init
├── samd21j18a.ld              # Linker script
├── samd21J18_OOCD.cfg         # OpenOCD configuration
├── uart.c                     # SERCOM3 USART driver + printf retarget
├── uart.h                     # UART API
└── freertos-kernel/           # FreeRTOS source (copied from GitHub)
    ├── include/
    ├── portable/GCC/ARM_CM0/
    ├── portable/MemMang/heap_4.c
    ├── tasks.c
    ├── queue.c
    ├── list.c
    └── timers.c
```

## FreeRTOS Configuration Highlights

- Preemptive scheduler with time slicing
- 5 priority levels
- 16 KB total heap size (`heap_4`)
- Stack overflow checking (method 2)
- `malloc` failed hook enabled
- No software timers (disabled for phase 1)
- Mutexes and counting semaphores enabled
- ARM Cortex‑M0+ specific: no `BASEPRI` register, so `configKERNEL_INTERRUPT_PRIORITY` and `configMAX_SYSCALL_INTERRUPT_PRIORITY` are not used

## Building and Flashing

1. Ensure an ARM GCC toolchain (e.g., `arm-none-eabi-gcc`) and OpenOCD are installed.
2. Clone the FreeRTOS kernel (or use the provided setup script – see `a.log`):
   ```bash
   git clone --depth=1 https://github.com/FreeRTOS/FreeRTOS-Kernel.git /tmp/fk
   mkdir -p freertos-kernel/portable/GCC/ARM_CM0 freertos-kernel/portable/MemMang freertos-kernel/include
   cp /tmp/fk/tasks.c /tmp/fk/queue.c /tmp/fk/list.c /tmp/fk/timers.c freertos-kernel/
   cp /tmp/fk/include/*.h freertos-kernel/include/
   cp /tmp/fk/portable/GCC/ARM_CM0/* freertos-kernel/portable/GCC/ARM_CM0/
   cp /tmp/fk/portable/MemMang/heap_4.c freertos-kernel/portable/MemMang/
   rm -rf /tmp/fk
   ```
3. Copy the project files (`FreeRTOSConfig.h`, `main.c`, `startup.c`, `uart.c`, `uart.h`, linker script, OpenOCD config) into the same directory.
4. Build with `make`.
5. Flash using OpenOCD:
   ```bash
   openocd -f samd21J18_OOCD.cfg -c "program your_firmware.elf verify reset exit"
   ```
6. Added `#define xPortSysTickHandler SysTick_Handler` to `FreeRTOSConfig.h` to connect SysTick to the FreeRTOS tick handler.
7. Added `/DISCARD/` section in the linker script to suppress `.ARM.exidx`, `.init`, `.fini`.
8. Implemented `configure_system_tick()` in `main.c` to set up SysTick before starting the scheduler.

## Application Behaviour

After reset, the system:

- Initialises UART (8 MHz, 115200)
- Initialises LED and button GPIO
- Creates a queue for button events (depth 4, each element a `uint32_t`)
- Creates three tasks:
  - **LED task** (priority 1): toggles the LED every 500 ms
  - **UART task** (priority 1): prints uptime and free heap every second; also drains the button queue and prints press counts
  - **Button task** (priority 2): polls the button every 20 ms, detects rising edges, and sends the cumulative press count to the queue
- Configures SysTick for 1 ms tick (lowest interrupt priority)
- Starts the FreeRTOS scheduler

Expected serial output (115200 8N1):

```
FreeRTOS V11.1.0+ - SAMD21J18A - 8 MHz
heap: 0 B  flash: 256 KB  ram: 32 KB
uptime: 1003 ms  heap free: 13336 B
uptime: 2006 ms  heap free: 13336 B
...
SW0 press #1
uptime: 8025 ms  heap free: 13336 B
...
```

Note: The first `heap: 0 B` is printed before any FreeRTOS allocations – it is harmless; subsequent calls show the correct free heap size.


## Important Notes

- The ARM Cortex‑M0+ port **does not** initialise SysTick automatically – you must call `SysTick_Config()` and set the priority before `vTaskStartScheduler()`.
- The macros `configKERNEL_INTERRUPT_PRIORITY` and `configMAX_SYSCALL_INTERRUPT_PRIORITY` are **not used** on M0+ because there is no `BASEPRI` register.
- The `_write` syscall retargets `printf` to UART and expands `\n` to `\r\n`.
- The `_sbrk` implementation uses the linker symbol `_end` – this provides a heap for newlib separate from FreeRTOS’s heap.

## Troubleshooting

| Symptom | Likely Fix |
|---------|-------------|
| No output on serial | Check that `uart_init()` is called first (it sets the clock). Verify terminal settings: 115200 8N1. |
| System hangs, LED does not toggle | Ensure `xPortSysTickHandler` is defined in `FreeRTOSConfig.h` and SysTick is configured before `vTaskStartScheduler()`. |
| `heap: 0 B` always | Move the `printf` of free heap size after the first `xQueueCreate` or `xTaskCreate` – the heap statistics are updated only after allocations. |
| Stack overflow reported | Increase the stack size passed to `xTaskCreate` (e.g., `configMINIMAL_STACK_SIZE * 2`). |

## Future Enhancements

- Switch to 48 MHz using DFLL48M (update `configCPU_CLOCK_HZ`, SysTick reload, and UART baud).
- Add a mutex to make UART output reentrant.
- Enable tickless idle for low power.
- Use software timers instead of task delays where appropriate.

## License

FreeRTOS is licensed under the MIT License. The demo application code and drivers are provided under the same terms (or choose your own open‑source license).