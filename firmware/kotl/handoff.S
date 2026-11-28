/* Final transfer from retained F firmware to PPI reset. SPDX-License-Identifier: MIT */
.text
.globl kotl_enter_pal
.ent kotl_enter_pal
kotl_enter_pal:
 mov $16,$2
 mov $17,$3
 ldq $1,32($2)
 .long 0x74010109
 mov $2,$1
 .long 0x7401010e
 lda $1,1($31)
 .long 0x74010122
 clr $1
 .long 0x7401010f
 mov $3,$1
 .long 0x74010108
 ldq $1,32($2)
 .long 0x74010101
 lda $1,0x300($31)
 .long 0x74010100
 mov $2,$16
 clr $17
 clr $18
.irp n,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,19,20,21,22,23,24,25,26,27,28,29,30
 clr $\n
.endr
 .long 0x78000000
.end kotl_enter_pal
.section .note.GNU-stack,"",@progbits
