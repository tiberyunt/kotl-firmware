/* QEMU Emulation PALcode.

   Copyright (C) 2011 Richard Henderson

   This file is part of QEMU PALcode.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the text
   of the GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; see the file COPYING.  If not see
   <http://www.gnu.org/licenses/>.  */
/* Modified 2026-11-28 for Kotl EV9 F-level firmware and retained UEFI handoff. */

/* Kotl EV9 adaptation: no emulator-private IPRs or shadow registers.
 * The vector positions and IMB/UNIQUE routines derive from upstream pal.S.
 * Other routines are implemented by the F-level dispatcher below. */
.set noat
.text
.globl pal_start
pal_start:
 br $31,pal_reset
.org 0x100
 br $31,pal_trap
.org 0x200
 br $31,pal_trap
.org 0x300
 br $31,pal_trap
.org 0x400
 br $31,pal_trap
.org 0x2000
.rept 64
 br $31,pal_trap
 .space 60
.endr
.org 0x3000
.rept 64
 br $31,pal_unpriv
 .space 60
.endr
.org 0x4000
pal_unpriv:
 /* Dispatch IMB/RDUNIQUE/WRUNIQUE by FTVAL without losing caller R1. */
 .long 0x74010146 /* FSCRATCH6 = R1 */
 .long 0x64200103
 cmpeq $1,0x86,$1
 bne $1,CallPal_Imb
 .long 0x64200103
 cmpeq $1,0x9e,$1
 bne $1,CallPal_RdUnique
 .long 0x64200103
 cmpeq $1,0x9f,$1
 bne $1,CallPal_WrUnique
 .long 0x64200146
 br $31,pal_trap
CallPal_Imb:
 mb
 .long 0x60005400 /* EV9 ICSYNC; F keeps native assistance in full-PAL. */
 br $31,pal_simple_return
CallPal_RdUnique:
 .long 0x6400000d /* PR_READ R0,PUNIQUE replaces mfpr v0,qemu_unique */
 br $31,pal_simple_return
CallPal_WrUnique:
 .long 0x7410000d /* PR_WRITE PUNIQUE,R16 replaces mtpr a0,qemu_unique */
pal_simple_return:
 .long 0x64200101
 lda $1,4($1)
 .long 0x74010101
 .long 0x64200146
 .long 0x78000000
pal_reset:
 /* Establish a private per-hart F stack; PPI entry arguments are still intact. */
 ldq $30,48($16)
 sll $17,16,$1
 addq $30,$1,$30
 ldah $30,1($30)
 lda $30,-16($30)
 br $27,1f
1: ldgp $29,0($27)
 lda $27,pal_reset_c
 jsr $26,($27),pal_reset_c
 br $31,.
.globl pal_start_kernel
pal_start_kernel:
 /* R16 is the retained copied descriptor; establish roots and saved K state. */
 ldq $1,48($16)
 .long 0x7401000a
 ldq $1,56($16)
 .long 0x7401000b
 ldq $1,24($16)
 .long 0x74010101
 lda $1,0x100($31)
 .long 0x74010100
 clr $1
 .long 0x74010001
 .long 0x74010008
 ldq $30,32($16)
 ldq $29,40($16)
 mov $16,$28
 ldq $16,64($28)
 ldq $17,72($28)
 ldq $18,80($28)
 ldq $19,88($28)
 ldq $20,96($28)
 ldq $21,104($28)
 clr $0
 clr $1
 clr $2
 clr $3
 clr $4
 clr $5
 clr $6
 clr $7
 clr $8
 clr $9
 clr $10
 clr $11
 clr $12
 clr $13
 clr $14
 clr $15
 clr $22
 clr $23
 clr $24
 clr $25
 clr $26
 clr $27
 clr $28
 .long 0x78000000
pal_trap:
 .long 0x741e0140
 .long 0x74010141
 .long 0x64200000
 .long 0x67c0010e
 ldq $30,48($30)
 sll $1,16,$1
 addq $30,$1,$30
 ldah $30,1($30)
 lda $30,-288($30)
 .long 0x64200141
 stq $0,0($30)
 stq $1,8($30)
 stq $2,16($30)
 stq $3,24($30)
 stq $4,32($30)
 stq $5,40($30)
 stq $6,48($30)
 stq $7,56($30)
 stq $8,64($30)
 stq $9,72($30)
 stq $10,80($30)
 stq $11,88($30)
 stq $12,96($30)
 stq $13,104($30)
 stq $14,112($30)
 stq $15,120($30)
 stq $16,128($30)
 stq $17,136($30)
 stq $18,144($30)
 stq $19,152($30)
 stq $20,160($30)
 stq $21,168($30)
 stq $22,176($30)
 stq $23,184($30)
 stq $24,192($30)
 stq $25,200($30)
 stq $26,208($30)
 stq $27,216($30)
 stq $28,224($30)
 stq $29,232($30)
 .long 0x64200140
 stq $1,240($30)
 .long 0x64200101
 stq $1,248($30)
 .long 0x64200100
 stq $1,256($30)
 .long 0x64200102
 stq $1,264($30)
 .long 0x64200103
 stq $1,272($30)
 mov $30,$16
 br $27,1f
1: ldgp $29,0($27)
 lda $27,pal_dispatch
 jsr $26,($27),pal_dispatch
 ldq $1,248($30)
 .long 0x74010101
 ldq $1,256($30)
 .long 0x74010100
 ldq $0,0($30)
 ldq $1,8($30)
 ldq $2,16($30)
 ldq $3,24($30)
 ldq $4,32($30)
 ldq $5,40($30)
 ldq $6,48($30)
 ldq $7,56($30)
 ldq $8,64($30)
 ldq $9,72($30)
 ldq $10,80($30)
 ldq $11,88($30)
 ldq $12,96($30)
 ldq $13,104($30)
 ldq $14,112($30)
 ldq $15,120($30)
 ldq $16,128($30)
 ldq $17,136($30)
 ldq $18,144($30)
 ldq $19,152($30)
 ldq $20,160($30)
 ldq $21,168($30)
 ldq $22,176($30)
 ldq $23,184($30)
 ldq $24,192($30)
 ldq $25,200($30)
 ldq $26,208($30)
 ldq $27,216($30)
 ldq $28,224($30)
 ldq $29,232($30)
 ldq $30,240($30)
 .long 0x78000000
.globl pal_ksi
.ent pal_ksi
pal_ksi:
 lda $30,-32($30)
 stq $26,0($30)
 stq $29,8($30)
 stq $9,16($30)
 mov $16,$9
 mov $17,$16
 mov $18,$17
 ldq $27,96($19)
 jsr $26,($27),0
 ldq $29,8($30)
 stq $1,0($9)
 stq $0,8($9)
 mov $9,$0
 ldq $9,16($30)
 ldq $26,0($30)
 lda $30,32($30)
 ret $31,($26),1
.end pal_ksi
.section .note.GNU-stack,"",@progbits
