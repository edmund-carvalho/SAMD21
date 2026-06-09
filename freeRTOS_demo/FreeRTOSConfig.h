#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

/* ---------------------------------------------------------------
 * FreeRTOSConfig.h - SAMD21J18A, GCC/ARM_CM0 port
 *
 * Phase 1: 8 MHz (OSC8M undivided)
 * Phase 2: update configCPU_CLOCK_HZ to 48000000UL when DFLL48M
 *          is configured and update SERCOM3 BAUD register.
 * --------------------------------------------------------------- */

#define configENABLE_MPU 0
#define xPortSysTickHandler SysTick_Handler

/* ---- Scheduler ------------------------------------------------ */
#define configUSE_PREEMPTION                    1
#define configUSE_TIME_SLICING                  1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 0   /* M0+ has no CLZ */
#define configUSE_TICKLESS_IDLE                 0

/* ---- Clock ---------------------------------------------------- */
#define configCPU_CLOCK_HZ                      ( 8000000UL )
#define configTICK_RATE_HZ                      ( 1000 )        /* 1 ms tick */

/* ---- Tasks ---------------------------------------------------- */
#define configMAX_PRIORITIES                    ( 5 )
#define configMINIMAL_STACK_SIZE                ( 128 )         /* words */
#define configMAX_TASK_NAME_LEN                 ( 12 )
#define configIDLE_SHOULD_YIELD                 1

/* ---- Memory --------------------------------------------------- */
#define configSUPPORT_STATIC_ALLOCATION         0
#define configSUPPORT_DYNAMIC_ALLOCATION        1
#define configTOTAL_HEAP_SIZE                   ( 16 * 1024 )   /* 16 KB */
#define configAPPLICATION_ALLOCATED_HEAP        0

/* ---- Hooks ---------------------------------------------------- */
#define configUSE_IDLE_HOOK                     0
#define configUSE_TICK_HOOK                     0
#define configUSE_MALLOC_FAILED_HOOK            1   /* catch heap exhaustion  */
#define configCHECK_FOR_STACK_OVERFLOW          2   /* method 2: watermark    */

/* ---- Timers (disabled for phase 1) ---------------------------- */
#define configUSE_TIMERS                        0
#define configTIMER_TASK_PRIORITY               ( 3 )
#define configTIMER_QUEUE_LENGTH                ( 5 )
#define configTIMER_TASK_STACK_DEPTH            ( configMINIMAL_STACK_SIZE )

/* ---- Synchronisation primitives ------------------------------- */
#define configUSE_MUTEXES                       1
#define configUSE_RECURSIVE_MUTEXES             0
#define configUSE_COUNTING_SEMAPHORES           1
#define configUSE_QUEUE_SETS                    0
#define configQUEUE_REGISTRY_SIZE               0

/* ---- Trace and debug ------------------------------------------ */
#define configUSE_TRACE_FACILITY                0
#define configUSE_STATS_FORMATTING_FUNCTIONS    0
#define configGENERATE_RUN_TIME_STATS           0
#define configUSE_16_BIT_TICKS                  0   /* TickType_t = uint32_t */
#define configRECORD_STACK_HIGH_ADDRESS         0

/* ---- Assert --------------------------------------------------- */
#define configASSERT( x )  \
    do { if( ( x ) == 0 ) { taskDISABLE_INTERRUPTS(); for( ;; ); } } while(0)

/* ---- ARM Cortex-M0+ specific ---------------------------------- */
/* M0+ has no Basepri register - ARM_CM0 port disables all interrupts
 * during critical sections via CPSID/CPSIE. No configMAX_SYSCALL_
 * INTERRUPT_PRIORITY is needed or supported on this core. */

/* ---- Optional API includes ------------------------------------ */
#define INCLUDE_vTaskPrioritySet                1
#define INCLUDE_uxTaskPriorityGet               1
#define INCLUDE_vTaskDelete                     1
#define INCLUDE_vTaskSuspend                    1
#define INCLUDE_xResumeFromISR                  1
#define INCLUDE_vTaskDelayUntil                 1
#define INCLUDE_vTaskDelay                      1
#define INCLUDE_xTaskGetSchedulerState          1
#define INCLUDE_xTaskGetCurrentTaskHandle       1
#define INCLUDE_uxTaskGetStackHighWaterMark     1
#define INCLUDE_xTaskGetIdleTaskHandle          0
#define INCLUDE_eTaskGetState                   0
#define INCLUDE_xTimerPendFunctionCall          0
#define INCLUDE_xTaskAbortDelay                 0
#define INCLUDE_xTaskGetHandle                  0

#endif /* FREERTOS_CONFIG_H */
