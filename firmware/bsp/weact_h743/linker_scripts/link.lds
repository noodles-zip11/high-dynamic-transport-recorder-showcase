MEMORY
{
    ROM (rx)      : ORIGIN = 0x08000000, LENGTH = 2048K
    RAM (rwx)     : ORIGIN = 0x24000000, LENGTH = 512K
    D2_SRAM1 (rw) : ORIGIN = 0x30000000, LENGTH = 128K
}

ENTRY(Reset_Handler)
_system_stack_size = 0x1000;
_minimum_heap_size = 0x4000;

SECTIONS
{
    .text :
    {
        . = ALIGN(4);
        _stext = .;
        KEEP(*(.isr_vector))
        *(.text)
        *(.text.*)
        *(.rodata)
        *(.rodata*)
        *(.glue_7)
        *(.glue_7t)
        *(.gnu.linkonce.t*)

        . = ALIGN(4);
        __fsymtab_start = .;
        KEEP(*(FSymTab))
        __fsymtab_end = .;

        . = ALIGN(4);
        __vsymtab_start = .;
        KEEP(*(VSymTab))
        __vsymtab_end = .;

        . = ALIGN(4);
        __rt_init_start = .;
        KEEP(*(SORT(.rti_fn*)))
        __rt_init_end = .;

        . = ALIGN(4);
        PROVIDE(__ctors_start__ = .);
        KEEP(*(SORT(.init_array.*)))
        KEEP(*(.init_array))
        PROVIDE(__ctors_end__ = .);

        . = ALIGN(4);
        _etext = .;
    } > ROM = 0

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
        *(.gnu.linkonce.d*)
        . = ALIGN(4);
        _edata = .;
    } > RAM AT > ROM

    .stack (NOLOAD) :
    {
        . = ALIGN(8);
        _sstack = .;
        . = . + _system_stack_size;
        . = ALIGN(8);
        _estack = .;
    } > RAM

    __bss_start = .;
    .bss (NOLOAD) :
    {
        . = ALIGN(4);
        _sbss = .;
        *(.bss)
        *(.bss.*)
        *(COMMON)
        *(.bss.init)
        . = ALIGN(4);
        _ebss = .;
    } > RAM
    __bss_end = .;

    .dma_buffer (NOLOAD) :
    {
        . = ALIGN(32);
        __dma_buffer_start = .;
        KEEP(*(.dma_buffer))
        KEEP(*(.dma_buffer.*))
        . = ALIGN(32);
        __dma_buffer_end = .;
    } > D2_SRAM1

    __dma_buffer_size = __dma_buffer_end - __dma_buffer_start;
    __d2_sram1_start = ORIGIN(D2_SRAM1);
    __d2_sram1_end = ORIGIN(D2_SRAM1) + LENGTH(D2_SRAM1);

    ASSERT((__dma_buffer_start & 31) == 0,
           "DMA buffer section start is not 32-byte aligned")
    ASSERT((__dma_buffer_end & 31) == 0,
           "DMA buffer section end is not 32-byte aligned")
    ASSERT(__dma_buffer_start >= __d2_sram1_start,
           "DMA buffer section starts before D2 SRAM1")
    ASSERT(__dma_buffer_end <= __d2_sram1_end,
           "DMA buffer section exceeds D2 SRAM1")

    __heap_start = ALIGN(__bss_end, 8);
    __heap_end = ORIGIN(RAM) + LENGTH(RAM);
    _end = __heap_start;

    ASSERT(__heap_end > __heap_start, "RAM layout leaves no heap")
    ASSERT((__heap_end - __heap_start) >= _minimum_heap_size,
           "RAM layout leaves less than the required 16 KiB heap")

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
