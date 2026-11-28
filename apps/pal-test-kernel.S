/* Kernel payload entered only through resident PAL reset. SPDX-License-Identifier: MIT */
.text
.globl pal_test_kernel,pal_test_kernel_end
pal_test_kernel:
#if TEST_BAD_MEMORY
 lda $1,1($31)
 sll $1,32,$1
#if TEST_BAD_MEMORY == 2
 ldah $1,0x20($1)
#else
 ldah $1,0x60($1)
#endif
 ldq $0,0($1) /* Native mapping exists, but PAL code must be F-only. */
#endif
 /* The handoff must preserve loader arguments and enter K. */
 lda $1,0x1234($31)
 cmpeq $16,$1,$1
 beq $1,bad
 implver $1
 lda $2,TEST_IMPL($31)
 cmpeq $1,$2,$1
 beq $1,bad
 lda $16,0x5678($31)
 call_pal 0x31 /* WRVAL */
 call_pal 0x32 /* RDVAL */
 lda $1,0x5678($31)
 cmpeq $0,$1,$1
 beq $1,bad
 lda $16,0x2345($31)
 call_pal 0x9f
 call_pal 0x9e
 lda $1,0x2345($31)
 cmpeq $0,$1,$1
 beq $1,bad
 call_pal 0x86
 lda $16,7($31)
 call_pal 0x35
 call_pal 0x36
 cmpeq $0,7,$1
 beq $1,bad
 clr $16
 call_pal 0x35
 cmpeq $0,7,$1
 beq $1,bad
 lda $16,0x3456($31)
 call_pal 0x38
 call_pal 0x3a
 lda $1,0x3456($31)
 cmpeq $0,$1,$1
 beq $1,bad
 lda $16,-2($31)
 call_pal 0x33
 call_pal 0x3c
 bne $0,bad
 br $9,good
 .asciz "KOTL PAL KERNEL HANDOFF PASS\r\n"
.balign 4
good:
 br $31,print
bad:
 br $9,print
 .asciz "KOTL PAL KERNEL HANDOFF FAIL\r\n"
.balign 4
print:
 ldbu $17,0($9)
 beq $17,done
 lda $16,0x100($31) /* Kotl CSERVE console extension */
 call_pal 0x09
 lda $9,1($9)
 br $31,print
done:
 call_pal 0
 br $31,.
pal_test_kernel_end:
.section .note.GNU-stack,"",@progbits
