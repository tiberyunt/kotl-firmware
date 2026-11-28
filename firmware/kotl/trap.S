/* SPDX-License-Identifier: MIT */
.text
.globl kotl_trap_entry
.ent kotl_trap_entry
kotl_trap_entry:
 br $27, 1f
1:
 ldgp $29, 0($27)
 lda $27, kotl_fatal
 jsr $26, ($27), kotl_fatal
 .long 0x47ff041f
.end kotl_trap_entry
.section .note.GNU-stack,"",@progbits
