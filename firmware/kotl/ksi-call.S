/* C ABI (hidden result pointer) -> raw retained F KSI. SPDX-License-Identifier: MIT */
.text
.globl kotl_ksi_call
.ent kotl_ksi_call
kotl_ksi_call:
 lda $30,-32($30)
 stq $26,0($30)
 stq $29,8($30)
 stq $9,16($30)
 mov $16,$9
 mov $17,$16
 mov $18,$17
 ldah $1,0x100($31)
 ldq $27,128($1)
 jsr $26,($27),0
 ldq $29,8($30)
 stq $1,0($9)
 stq $0,8($9)
 mov $9,$0
 ldq $9,16($30)
 ldq $26,0($30)
 lda $30,32($30)
 ret $31,($26),1
.end kotl_ksi_call
