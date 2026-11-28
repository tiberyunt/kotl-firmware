OUTPUT_FORMAT("elf64-alpha")
ENTRY(pal_start)
SECTIONS {
 . = 0x100600000;
 .text : { *(.text .text.*) }
 .rodata : { *(.rodata .rodata.*) }
 .data : { *(.data .data.*) *(.sdata .sdata.*) }
 .got : { *(.got) *(.lit8) *(.lita) }
 _gp = ALIGN(16) + 0x8000;
 .bss : { *(.bss .bss.*) *(.sbss .sbss.*) *(COMMON) }
 ASSERT(SIZEOF(.bss) == 0, "PAL state must live in per-hart scratch")
 ASSERT(. <= 0x100610000, "PAL exceeds 64 KiB code region")
 /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
