/* bfin-elf-run memory: SDRAM from 0x1000, L1 data bank A at 0xFF800000 (the BF52x's own, where Opus's fast memory is).
 * Copyright (C) 2026 SammiBabe666. GPL-3.0-or-later with the OP-1 firmware permission (NOTICE). */
ENTRY(__start)
MEMORY { ram (rwx) : ORIGIN = 0x00001000, LENGTH = 0x00FF0000
         l1d (rw)  : ORIGIN = 0xFF800000, LENGTH = 0x00004000 }
SECTIONS
{
  .text : { *(.text.start) *(.text .text.*) } > ram
  .rodata : { *(.rodata .rodata.*) } > ram
  .data : { *(.data .data.*) } > ram
  .bss (NOLOAD) : { __bss_start = .; *(.bss .bss.* COMMON) __bss_end = .; } > ram
  .l1 (NOLOAD) : { *(.l1) } > l1d
  __stack_top = ORIGIN(ram) + LENGTH(ram) - 64;
}
