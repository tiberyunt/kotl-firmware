OUTPUT_FORMAT("elf64-alpha")
ENTRY(_start)
PHDRS { payload PT_LOAD FLAGS(7); }
SECTIONS {
 . = 0xfffffc0000200000;
 .text : { *(.text .text.*) } :payload
 .rodata : { *(.rodata .rodata.*) } :payload
 .data : { *(.data .data.*) } :payload
 . = ALIGN(16);
 .sdata : { *(.sdata .sdata.*) } :payload
 _gp = ALIGN(16) + 0x8000;
 . = ALIGN(16);
 __bss_start = .;
 .bss : { *(.sbss .sbss.*) *(.bss .bss.*) *(COMMON) } :payload
 . = ALIGN(16);
 __bss_end = .;
 /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
 ASSERT(__bss_end < 0xfffffc0000400000, "firmware exceeds reserved range")
}
