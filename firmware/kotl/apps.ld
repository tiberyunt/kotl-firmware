OUTPUT_FORMAT("elf64-alpha")
ENTRY(efi_entry)
PHDRS { payload PT_LOAD FLAGS(7); }
SECTIONS {
 . = 0x101001000;
 .image : { *(.text .text.*) *(.rodata .rodata.*) *(.data .data.*) *(.sdata .sdata.*) } :payload
 _gp = ALIGN(16) + 0x8000;
 . = ALIGN(16);
 .bss : { *(.sbss .sbss.*) *(.bss .bss.*) *(COMMON) } :payload
 . = ALIGN(16);
 app_end = .;
 /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
