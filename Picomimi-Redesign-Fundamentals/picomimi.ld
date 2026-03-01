/**
 * Picomimi Linker Script
 * 
 * For RP2040 (264KB SRAM, 2MB Flash)
 */

MEMORY
{
    FLASH(rx)  : ORIGIN = 0x10000000, LENGTH = 2048K
    SRAM(rwx)  : ORIGIN = 0x20000000, LENGTH = 264K
}

ENTRY(Reset_Handler)

SECTIONS
{
    /* Boot2 stage (RP2040 specific) */
    .boot2 : {
        __boot2_start = .;
        KEEP(*(.boot2))
        __boot2_end = .;
    } > FLASH

    /* Vector table */
    .vectors : {
        . = ALIGN(256);
        KEEP(*(.vectors))
    } > FLASH

    /* Code */
    .text : {
        . = ALIGN(4);
        *(.text*)
        *(.rodata*)
        . = ALIGN(4);
    } > FLASH

    /* ARM exception tables */
    .ARM.extab : {
        *(.ARM.extab*)
    } > FLASH

    .ARM.exidx : {
        __exidx_start = .;
        *(.ARM.exidx*)
        __exidx_end = .;
    } > FLASH

    /* Initialized data (copied to SRAM) */
    .data : {
        . = ALIGN(4);
        __data_start = .;
        *(.data*)
        . = ALIGN(4);
        __data_end = .;
    } > SRAM AT > FLASH

    __data_load = LOADADDR(.data);

    /* Uninitialized data */
    .bss : {
        . = ALIGN(4);
        __bss_start = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        __bss_end = .;
    } > SRAM

    /* Heap */
    .heap : {
        . = ALIGN(8);
        __heap_start = .;
        . = . + 128K;
        __heap_end = .;
    } > SRAM

    /* Stack for Core0 */
    .stack0 : {
        . = ALIGN(8);
        __stack0_bottom = .;
        . = . + 4K;
        __stack_top = .;
    } > SRAM

    /* Stack for Core1 */
    .stack1 : {
        . = ALIGN(8);
        __stack1_bottom = .;
        . = . + 4K;
        __core1_stack_top = .;
    } > SRAM

    /* Scratch memory for fast operations */
    .scratch_x : {
        *(.scratch_x*)
    } > SRAM

    .scratch_y : {
        *(.scratch_y*)
    } > SRAM

    /* End marker */
    __end = .;
}

/* Assertions */
ASSERT(__stack_top <= ORIGIN(SRAM) + LENGTH(SRAM), "Stack overflow!")
