/* Font atlas derived from Noto Sans Mono, SIL Open Font License 1.1.
 * See font/OFL.txt. Assembly wrapper SPDX-License-Identifier: MIT */
.section .rodata
.balign 8
.global kotl_font
kotl_font:
.incbin "font/ascii20.bin"
.section .note.GNU-stack,"",@progbits

.section .rodata
.balign 8
.global kotl_boot_logo
kotl_boot_logo:
.incbin "font/logo-gray.bin"
.section .rodata
.balign 8
.global kotl_sharp_fonts
kotl_sharp_fonts:
.incbin "font/sharp-fonts.rle"
