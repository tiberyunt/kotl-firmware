.set noat
/* Alpha GCC divide ABI: operands r24/r25, result r27, return via r23.
 * Original implementation. SPDX-License-Identifier: BSD-2-Clause-Patent */
.text
.macro divide name,bits,signed,remainder
.globl \name
.ent \name
\name:
 lda $30,-80($30)
 stq $0,0($30)
 stq $1,8($30)
 stq $2,16($30)
 stq $3,24($30)
 stq $4,32($30)
 stq $5,40($30)
 stq $6,48($30)
 stq $25,56($30)
 stq $23,64($30)
 mov $24,$0
 .if \bits == 32
  .if \signed
   addl $0,$31,$0
   addl $25,$31,$25
  .else
   zapnot $0,15,$0
   zapnot $25,15,$25
  .endif
 .endif
 .if \signed
  cmplt $0,$31,$4
  cmplt $25,$31,$5
  negq $0,$6
  cmovne $4,$6,$0
  negq $25,$6
  cmovne $5,$6,$25
  xor $4,$5,$5
 .endif
 bne $25,1f
 call_pal 0xaa
1:
 clr $27
 clr $28
 lda $23,64($31)
2:
 srl $0,63,$1
 srl $28,63,$2
 sll $28,1,$28
 bis $28,$1,$28
 sll $0,1,$0
 sll $27,1,$27
 cmpult $28,$25,$3
 cmpeq $3,$31,$3
 bis $3,$2,$3
 subq $28,$25,$2
 cmovne $3,$2,$28
 bis $27,$3,$27
 subq $23,1,$23
 bne $23,2b
 .if \remainder
  mov $28,$27
 .endif
 .if \signed
  negq $27,$6
  .if \remainder
   cmovne $4,$6,$27
  .else
   cmovne $5,$6,$27
  .endif
 .endif
 .if \bits == 32
  addl $27,$31,$27
 .endif
 ldq $0,0($30)
 ldq $1,8($30)
 ldq $2,16($30)
 ldq $3,24($30)
 ldq $4,32($30)
 ldq $5,40($30)
 ldq $6,48($30)
 ldq $25,56($30)
 ldq $23,64($30)
 lda $30,80($30)
 ret $31,($23),1
.end \name
.endm
divide __divqu,64,0,0
divide __remqu,64,0,1
divide __divq,64,1,0
divide __remq,64,1,1
divide __divlu,32,0,0
divide __remlu,32,0,1
divide __divl,32,1,0
divide __reml,32,1,1
.section .note.GNU-stack,"",@progbits
