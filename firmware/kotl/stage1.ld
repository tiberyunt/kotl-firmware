OUTPUT_FORMAT("elf64-alpha")
ENTRY(_start)
PHDRS { flash PT_LOAD FLAGS(5); data PT_LOAD FLAGS(6); }
SECTIONS {
 . = 0;
 .text : { *(.text .text.*) } :flash
 .rodata : { *(.rodata .rodata.*) } :flash
 .data : { *(.data .data.*) *(.sdata .sdata.*) } :flash
 .got : { *(.got) *(.lit8) *(.lita) } :flash
 _gp = ALIGN(16) + 0x8000;
 . = 0x01000000;
 __bss_start = .;
 .bss (NOLOAD) : { *(.bss .bss.*) *(.sbss .sbss.*) *(COMMON) } :data
 . = ALIGN(16); __bss_end = .;
 /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
 ASSERT(__bss_end < 0x01080000, "stage-one state overlaps stack/DTB")
}
