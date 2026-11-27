/* SPDX-License-Identifier: MIT */
.section .data.cross,"aw",@progbits
.balign 4096
.zero 4092
.align 0
.global reloc_cross_pointer
reloc_cross_pointer:
.quad reloc_datum
.section .note.GNU-stack,"",@progbits
