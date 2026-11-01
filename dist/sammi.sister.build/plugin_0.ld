SECTIONS
{
    . = 0x01E80000;
    .image : { *(.text .text.*) *(.rodata .rodata.*) *(.data .data.*) }
    .zero (NOLOAD) : { *(.bss .bss.*) *(COMMON) *(.far) }
    /DISCARD/ : { *(.comment) *(.eh_frame) *(.note.*) *(.debug*) }
}
