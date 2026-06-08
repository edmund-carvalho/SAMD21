MEMORY
{
    FLASH (rx)  : ORIGIN = 0x00000000, LENGTH = 256K
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 32K
}

/* Initial stack pointer: top of RAM */
_estack = ORIGIN(RAM) + LENGTH(RAM);

SECTIONS
{
    .text :
    {
        KEEP(*(.vectors))   /* Vector table must be first */
        *(.text*)
        *(.rodata*)
        . = ALIGN(4);
        _etext = .;         /* End of FLASH content (LMA for .data) */
    } > FLASH

    .data : AT(_etext)      /* LMA = end of .text in FLASH, VMA = RAM */
    {
        . = ALIGN(4);
        _sdata = .;
        *(.data*)
        . = ALIGN(4);
        _edata = .;
    } > RAM

    .bss :
    {
        . = ALIGN(4);
        _sbss = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
    } > RAM
}
