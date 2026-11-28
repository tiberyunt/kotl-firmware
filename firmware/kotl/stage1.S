/* SPDX-License-Identifier: MIT */
.text
.globl _start
.ent _start
_start:
 br $27, 1f
1:
 ldgp $29, 0($27)
 lda $30, 0x10e
 sll $30, 16, $30
 bne $16, secondary_stop
 lda $1, __bss_start
 lda $2, __bss_end
2:
 cmpult $1, $2, $3
 beq $3, 3f
 stq $31, 0($1)
 addq $1, 8, $1
 br $31, 2b
3:
 mov $17, $16
 lda $27, stage1_main
 jsr $26, ($27), stage1_main
secondary_stop:
 br $31,.
 br $31, secondary_stop
.end _start
.globl stage1_trap
.ent stage1_trap
stage1_trap:
 .set noat
 /* Save the caller's possibly virtual K stack before entering F memory.
    Each hart owns 16 KiB below 0x010f0000; no K pointer is dereferenced. */
 .long 0x741e0140
 .long 0x67c00000
 sll $30,14,$30
 negq $30,$30
 ldah $30,0x10f($30)
 lda $30,-256($30)
 stq $0, 0($30)
 stq $1, 8($30)
 stq $2, 16($30)
 stq $3, 24($30)
 stq $4, 32($30)
 stq $5, 40($30)
 stq $6, 48($30)
 stq $7, 56($30)
 stq $8, 64($30)
 stq $9, 72($30)
 stq $10, 80($30)
 stq $11, 88($30)
 stq $12, 96($30)
 stq $13, 104($30)
 stq $14, 112($30)
 stq $15, 120($30)
 stq $16, 128($30)
 stq $17, 136($30)
 stq $18, 144($30)
 stq $19, 152($30)
 stq $20, 160($30)
 stq $21, 168($30)
 stq $22, 176($30)
 stq $23, 184($30)
 stq $24, 192($30)
 stq $25, 200($30)
 stq $26, 208($30)
 stq $27, 216($30)
 stq $28, 224($30)
 stq $29, 232($30)
 .long 0x64200140
 stq $1, 240($30)
 mov $30, $16
 br $27, 1f
1:
 ldgp $29, 0($27)
 lda $27, stage1_exception
 jsr $26, ($27), stage1_exception
 ldq $0, 0($30)
 ldq $1, 8($30)
 ldq $2, 16($30)
 ldq $3, 24($30)
 ldq $4, 32($30)
 ldq $5, 40($30)
 ldq $6, 48($30)
 ldq $7, 56($30)
 ldq $8, 64($30)
 ldq $9, 72($30)
 ldq $10, 80($30)
 ldq $11, 88($30)
 ldq $12, 96($30)
 ldq $13, 104($30)
 ldq $14, 112($30)
 ldq $15, 120($30)
 ldq $16, 128($30)
 ldq $17, 136($30)
 ldq $18, 144($30)
 ldq $19, 152($30)
 ldq $20, 160($30)
 ldq $21, 168($30)
 ldq $22, 176($30)
 ldq $23, 184($30)
 ldq $24, 192($30)
 ldq $25, 200($30)
 ldq $26, 208($30)
 ldq $27, 216($30)
 ldq $28, 224($30)
 ldq $29, 232($30)
 ldq $30, 240($30)
 .long 0x78000000
 .set at
.end stage1_trap
.globl stage1_ksi
.ent stage1_ksi
stage1_ksi:
 .set noat
 lda $30,-256($30)
 stq $0, 0($30)
 stq $1, 8($30)
 stq $2, 16($30)
 stq $3, 24($30)
 stq $4, 32($30)
 stq $5, 40($30)
 stq $6, 48($30)
 stq $7, 56($30)
 stq $8, 64($30)
 stq $9, 72($30)
 stq $10, 80($30)
 stq $11, 88($30)
 stq $12, 96($30)
 stq $13, 104($30)
 stq $14, 112($30)
 stq $15, 120($30)
 stq $16, 128($30)
 stq $17, 136($30)
 stq $18, 144($30)
 stq $19, 152($30)
 stq $20, 160($30)
 stq $21, 168($30)
 stq $22, 176($30)
 stq $23, 184($30)
 stq $24, 192($30)
 stq $25, 200($30)
 stq $26, 208($30)
 stq $27, 216($30)
 stq $28, 224($30)
 stq $29, 232($30)
 lda $1,256($30)
 stq $1,240($30)
 mov $30, $16
 br $27, 1f
1:
 ldgp $29, 0($27)
 lda $27, stage1_service
 jsr $26, ($27), stage1_service
 ldq $0, 0($30)
 ldq $1, 8($30)
 ldq $2, 16($30)
 ldq $3, 24($30)
 ldq $4, 32($30)
 ldq $5, 40($30)
 ldq $6, 48($30)
 ldq $7, 56($30)
 ldq $8, 64($30)
 ldq $9, 72($30)
 ldq $10, 80($30)
 ldq $11, 88($30)
 ldq $12, 96($30)
 ldq $13, 104($30)
 ldq $14, 112($30)
 ldq $15, 120($30)
 ldq $16, 128($30)
 ldq $17, 136($30)
 ldq $18, 144($30)
 ldq $19, 152($30)
 ldq $20, 160($30)
 ldq $21, 168($30)
 ldq $22, 176($30)
 ldq $23, 184($30)
 ldq $24, 192($30)
 ldq $25, 200($30)
 ldq $26, 208($30)
 ldq $27, 216($30)
 ldq $28, 224($30)
 ldq $29, 232($30)
 ldq $30, 240($30)
 ret $31,($26),1
 .set at
.end stage1_ksi
.section .note.GNU-stack,"",@progbits
