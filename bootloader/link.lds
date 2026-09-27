MEMORY
{
    ROM (rx)  : ORIGIN = 0x08000000, LENGTH = 128K
    RAM (rwx) : ORIGIN = 0x24000000, LENGTH = 512K
}

ENTRY(Reset_Handler)

_bootloader_stack_size = 0x1000;

SECTIONS
{
    .text :
    {
        . = ALIGN(4);
        KEEP(*(.isr_vector))
        *(.text.Reset_Handler)
        *(.text)
        *(.text.*)
        *(.rodata)
        *(.rodata.*)
        *(.glue_7)
        *(.glue_7t)
        *(.init)
        *(.fini)
        . = ALIGN(4);
        PROVIDE_HIDDEN(__preinit_array_start = .);
        KEEP(*(.preinit_array))
        PROVIDE_HIDDEN(__preinit_array_end = .);
        PROVIDE_HIDDEN(__init_array_start = .);
        KEEP(*(SORT(.init_array.*)))
        KEEP(*(.init_array))
        PROVIDE_HIDDEN(__init_array_end = .);
        PROVIDE_HIDDEN(__fini_array_start = .);
        KEEP(*(SORT(.fini_array.*)))
        KEEP(*(.fini_array))
        PROVIDE_HIDDEN(__fini_array_end = .);
        . = ALIGN(4);
        _etext = .;
    } > ROM

    .ARM.extab :
    {
        *(.ARM.extab* .gnu.linkonce.armextab.*)
    } > ROM

    __exidx_start = .;
    .ARM.exidx :
    {
        *(.ARM.exidx* .gnu.linkonce.armexidx.*)
    } > ROM
    __exidx_end = .;

    _sidata = LOADADDR(.data);
    .data :
    {
        . = ALIGN(4);
        _sdata = .;
        *(.data)
        *(.data.*)
        . = ALIGN(4);
        _edata = .;
    } > RAM AT > ROM

    .bss (NOLOAD) :
    {
        . = ALIGN(4);
        _sbss = .;
        *(.bss)
        *(.bss.*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
    } > RAM

    /* Keep the Bootloader stack away from the low AXI-SRAM region used by
     * the ST-Link flash loader during SWD programming. */
    .stack ORIGIN(RAM) + LENGTH(RAM) - _bootloader_stack_size (NOLOAD) :
    {
        . = ALIGN(8);
        _sstack = .;
        . = . + _bootloader_stack_size;
        . = ALIGN(8);
        _estack = .;
    } > RAM

    __bootloader_rom_end = LOADADDR(.data) + SIZEOF(.data);
    ASSERT(__bootloader_rom_end <= ORIGIN(ROM) + LENGTH(ROM),
           "Bootloader exceeds its 128 KiB Flash reservation")
    ASSERT(ORIGIN(ROM) == 0x08000000, "Bootloader ROM origin changed")
    ASSERT(LENGTH(ROM) == 128K, "Bootloader ROM length changed")

    /DISCARD/ :
    {
        *(.note.GNU-stack)
    }

    .ARM.attributes 0 : { *(.ARM.attributes) }
    .comment 0 : { *(.comment) }
    .debug_info 0 : { *(.debug_info .gnu.linkonce.wi.*) }
    .debug_abbrev 0 : { *(.debug_abbrev) }
    .debug_aranges 0 : { *(.debug_aranges) }
    .debug_line 0 : { *(.debug_line .debug_line.*) }
    .debug_frame 0 : { *(.debug_frame) }
    .debug_str 0 : { *(.debug_str) }
    .debug_loc 0 : { *(.debug_loc) }
    .debug_ranges 0 : { *(.debug_ranges) }
}
