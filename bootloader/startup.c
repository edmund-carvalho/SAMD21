#include <stdint.h>

/* Symbols from linker script */
extern uint32_t _estack;
extern uint32_t _etext;
extern uint32_t _sdata;
extern uint32_t _edata;
extern uint32_t _sbss;
extern uint32_t _ebss;

extern int main(void);

/* Forward declarations */
void Reset_Handler(void);
void Default_Handler(void);

/* Weak aliases - override by defining the same function in user code */
void NMI_Handler(void)       __attribute__((weak, alias("Default_Handler")));
void HardFault_Handler(void) __attribute__((weak, alias("Default_Handler")));
void SVC_Handler(void)       __attribute__((weak, alias("Default_Handler")));
void PendSV_Handler(void)    __attribute__((weak, alias("Default_Handler")));
void SysTick_Handler(void)   __attribute__((weak, alias("Default_Handler")));

/* Cortex-M0+ vector table - placed at 0x00000000 by linker script */
__attribute__((section(".vectors"), used))
const void *vectors[] = {
    (void *)&_estack,           /* 0:  Initial stack pointer       */
    (void *)Reset_Handler,      /* 1:  Reset                       */
    (void *)NMI_Handler,        /* 2:  NMI                         */
    (void *)HardFault_Handler,  /* 3:  HardFault                   */
    (void *)0,                  /* 4:  Reserved (no MemManage M0+) */
    (void *)0,                  /* 5:  Reserved (no BusFault M0+)  */
    (void *)0,                  /* 6:  Reserved (no UsageFault M0+)*/
    (void *)0,                  /* 7:  Reserved                    */
    (void *)0,                  /* 8:  Reserved                    */
    (void *)0,                  /* 9:  Reserved                    */
    (void *)0,                  /* 10: Reserved                    */
    (void *)SVC_Handler,        /* 11: SVCall                      */
    (void *)0,                  /* 12: Reserved                    */
    (void *)0,                  /* 13: Reserved                    */
    (void *)PendSV_Handler,     /* 14: PendSV                      */
    (void *)SysTick_Handler,    /* 15: SysTick                     */
    /* SAMD21 external IRQs would start at index 16 */
};

void Reset_Handler(void)
{
    uint32_t *src, *dst;

    /* Copy .data initializers from FLASH to RAM */
    src = &_etext;
    dst = &_sdata;
    while (dst < &_edata) {
        *dst++ = *src++;
    }

    /* Zero-fill .bss */
    dst = &_sbss;
    while (dst < &_ebss) {
        *dst++ = 0;
    }

    main();

    /* Should never return */
    while (1);
}

void Default_Handler(void)
{
    while (1);
}