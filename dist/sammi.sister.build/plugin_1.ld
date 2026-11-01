SECTIONS
{
    . = 0x01EC1240;
    .image : { *(.text .text.*) *(.rodata .rodata.*) *(.data .data.*) }
    .zero (NOLOAD) : { *(.bss .bss.*) *(COMMON) *(.far) }
    /DISCARD/ : { *(.comment) *(.eh_frame) *(.note.*) *(.debug*) }
}
