#include <stdint.h>
#include <stdio.h>
#include "sam.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "uart.h"
#include "core_cm0plus.h"  // Add this for SysTick_Config

/* ---------------------------------------------------------------
 * Pin assignments
 * --------------------------------------------------------------- */
#define LED0_PIN    30U     /* PB30, active low */
#define SW0_PIN     15U     /* PA15, active low */

/* ---------------------------------------------------------------
 * Button event queue
 * vButtonTask  →  [queue]  →  vUartTask
 * --------------------------------------------------------------- */
#define BUTTON_QUEUE_LEN    4U
static QueueHandle_t xButtonQueue;

/* ---------------------------------------------------------------
 * gpio_init - LED and button (no UART pins - uart_init handles those)
 * --------------------------------------------------------------- */
static void gpio_init(void)
{
    /* LED0: PB30 output, drive HIGH → LED off (active low) */
    PORT_REGS->GROUP[1].PORT_DIRSET          = (1UL << LED0_PIN);
    PORT_REGS->GROUP[1].PORT_OUTSET          = (1UL << LED0_PIN);

    /* SW0: PA15 input with internal pull-up */
    PORT_REGS->GROUP[0].PORT_DIRCLR          = (1UL << SW0_PIN);
    PORT_REGS->GROUP[0].PORT_PINCFG[SW0_PIN] = PORT_PINCFG_INEN_Msk |
                                                PORT_PINCFG_PULLEN_Msk;
    PORT_REGS->GROUP[0].PORT_OUTSET          = (1UL << SW0_PIN);
}

static inline int sw0_pressed(void)
{
    return !(PORT_REGS->GROUP[0].PORT_IN & (1UL << SW0_PIN));
}

/* ---------------------------------------------------------------
 * Task: vLedTask
 * Priority 1 - toggles LED0 every 500 ms
 * --------------------------------------------------------------- */
static void vLedTask(void *pvParam)
{
    (void)pvParam;
    for (;;) {
        PORT_REGS->GROUP[1].PORT_OUTTGL = (1UL << LED0_PIN);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

/* ---------------------------------------------------------------
 * Task: vUartTask
 * Priority 1 - prints uptime every second
 *              also drains button event queue and prints press count
 * --------------------------------------------------------------- */
static void vUartTask(void *pvParam)
{
    (void)pvParam;
    uint32_t press_count = 0;

    for (;;) {
        /* Drain all pending button events first */
        while (xQueueReceive(xButtonQueue, &press_count, 0) == pdTRUE) {
            printf("SW0 press #%lu\r\n", press_count);
        }

        printf("uptime: %lu ms  heap free: %u B\r\n",
               (unsigned long)xTaskGetTickCount(),
               (unsigned)xPortGetFreeHeapSize());

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

/* ---------------------------------------------------------------
 * Task: vButtonTask
 * Priority 2 - polls SW0 every 20 ms, edge-detects press,
 *              sends cumulative press count to xButtonQueue
 * --------------------------------------------------------------- */
static void vButtonTask(void *pvParam)
{
    (void)pvParam;
    uint32_t press_count = 0;
    uint8_t  last        = 0;

    for (;;) {
        uint8_t current = (uint8_t)sw0_pressed();

        if (current && !last) {         /* rising edge = new press */
            press_count++;
            xQueueSend(xButtonQueue, &press_count, 0);
        }
        last = current;
        vTaskDelay(pdMS_TO_TICKS(20));  /* 20 ms poll = built-in debounce */
    }
}

/* ---------------------------------------------------------------
 * FreeRTOS hooks
 * --------------------------------------------------------------- */
void vApplicationMallocFailedHook(void)
{
    /* Heap exhausted - print and halt.
     * Increase configTOTAL_HEAP_SIZE or reduce task stack sizes. */
    uart_puts("FATAL: malloc failed\r\n");
    taskDISABLE_INTERRUPTS();
    for (;;);
}

void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    /* Stack watermark crossed - print task name and halt.
     * Increase the stack size passed to xTaskCreate for this task. */
    (void)xTask;
    uart_puts("FATAL: stack overflow in ");
    uart_puts(pcTaskName);
    uart_puts("\r\n");
    taskDISABLE_INTERRUPTS();
    for (;;);
}

//EC why required ?
void configure_system_tick(void)
{
    // Reload value for 1ms tick (8 MHz / 1000 = 8000)
    if (SysTick_Config(8000000 / configTICK_RATE_HZ - 1)) {
        printf("SysTick config failed!\r\n");
        while(1);
    }
    // Set priority to lowest (FreeRTOS requirement)
    NVIC_SetPriority(SysTick_IRQn, (1UL << __NVIC_PRIO_BITS) - 1);
}

/* ---------------------------------------------------------------
 * main
 * --------------------------------------------------------------- */
int main(void)
{
    uart_init();    /* OSC8M → 8 MHz, SERCOM3 115200 8N1 - must be first */
    gpio_init();

    printf("FreeRTOS %s - SAMD21J18A - 8 MHz\r\n", tskKERNEL_VERSION_NUMBER);
    printf("heap: %u B  flash: 256 KB  ram: 32 KB\r\n",
           (unsigned)xPortGetFreeHeapSize());

    xButtonQueue = xQueueCreate(BUTTON_QUEUE_LEN, sizeof(uint32_t));
    configASSERT(xButtonQueue != NULL);

    xTaskCreate(vLedTask,    "LED",  configMINIMAL_STACK_SIZE,       NULL, 1, NULL);
    xTaskCreate(vUartTask,   "UART", configMINIMAL_STACK_SIZE * 2,   NULL, 1, NULL);
    xTaskCreate(vButtonTask, "BTN",  configMINIMAL_STACK_SIZE,       NULL, 2, NULL);

    /* Configure SysTick before starting scheduler */
    configure_system_tick();
    
    vTaskStartScheduler();  /* does not return */

    for (;;);
    return 0;
}
