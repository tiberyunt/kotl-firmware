/* Integer-only Alpha firmware exception boundary. SPDX-License-Identifier: MIT */
.text
.globl image_checkpoint
.ent image_checkpoint
image_checkpoint:
 stq $9,0($16)
 stq $10,8($16)
 stq $11,16($16)
 stq $12,24($16)
 stq $13,32($16)
 stq $14,40($16)
 stq $15,48($16)
 stq $30,56($16)
 stq $26,64($16)
 stq $29,72($16)
 clr $0
 ret $31,($26),1
.end image_checkpoint
.globl image_resume
.ent image_resume
image_resume:
 ldq $9,0($16)
 ldq $10,8($16)
 ldq $11,16($16)
 ldq $12,24($16)
 ldq $13,32($16)
 ldq $14,40($16)
 ldq $15,48($16)
 ldq $30,56($16)
 ldq $26,64($16)
 ldq $29,72($16)
 lda $0,1($31)
 ret $31,($26),1
.end image_resume
.section .note.GNU-stack,"",@progbits
