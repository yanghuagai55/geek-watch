ENTRY(Reset_Handler)

MEMORY
{
    FLASH (rx) : ORIGIN = 0x08000000, LENGTH = 64K
    RAM (rwx)  : ORIGIN = 0x20000000, LENGTH = 20K
}

_estack = ORIGIN(RAM) + LENGTH(RAM);
_stack_size = 2K;
_stack_limit = _estack - _stack_size;
_heap_size = 256;
__ui_static_budget = 10K;
__portable_ram_size = 16K;
__future_ram_reserve = 4K;

SECTIONS
{
    .isr_vector :
    {
        KEEP(*(.isr_vector))
    } > FLASH

    .text :
    {
        *(.text*)
        *(.rodata*)
        . = ALIGN(4);
        _etext = .;
    } > FLASH

    .ARM.extab : { *(.ARM.extab*) } > FLASH
    .ARM.exidx :
    {
        __exidx_start = .;
        *(.ARM.exidx*)
        __exidx_end = .;
    } > FLASH

    _sidata = LOADADDR(.data);
    .data :
    {
        . = ALIGN(4);
        _sdata = .;
        __ui_data_start = .;
        *libui.a:*(.data*)
        __ui_data_end = .;
        *(.data*)
        . = ALIGN(4);
        _edata = .;
    } > RAM AT > FLASH

    .bss (NOLOAD) :
    {
        . = ALIGN(8);
        _sbss = .;
        __ui_bss_start = .;
        *libui.a:*(.bss* COMMON)
        . = ALIGN(8);
        __ui_bss_end = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(8);
        _ebss = .;
    } > RAM

    .heap (NOLOAD) :
    {
        . = ALIGN(8);
        _end = .;
        . += _heap_size;
        _heap_end = .;
    } > RAM

    /* Reserve the actual descending stack, rather than a token after BSS. */
    .stack _stack_limit (NOLOAD) :
    {
        . += _stack_size;
    } > RAM

    __ui_ram_used = (__ui_data_end - __ui_data_start) +
                    (__ui_bss_end - __ui_bss_start);
    __ram_plan_used = _heap_end - ORIGIN(RAM) + _stack_size;
    __flash_used = LOADADDR(.data) + SIZEOF(.data) - ORIGIN(FLASH);

    ASSERT(__ui_ram_used > 0, "UI archive RAM accounting matched no sections")
    ASSERT(__ui_ram_used <= __ui_static_budget, "UI static RAM exceeds 10 KiB")
    ASSERT(__ram_plan_used <= __portable_ram_size - __future_ram_reserve, "Firmware must leave 4 KiB free within a 16 KiB RAM target")
    ASSERT(_heap_end <= _stack_limit, "F103 heap collides with reserved 2 KiB stack")
    ASSERT(__flash_used <= LENGTH(FLASH), "Firmware exceeds 64 KiB Flash")
}
