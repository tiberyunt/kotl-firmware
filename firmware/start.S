.set noreorder
.text
.globl _start
.ent _start
_start:
 br $27, 1f
1:
 ldgp $29, 0($27)
 lda $30, stack_top
 lda $1, __bss_start
 lda $2, __bss_end
2:
 cmpult $1, $2, $3
 beq $3, 3f
 stq $31, 0($1)
 addq $1, 8, $1
 br $31, 2b
3:
#ifdef KOTL_NATIVE
 lda $1, native_boot_info
 stq $16, 0($1)
 /* Stage one retains the F exception vector. */
#endif
 lda $27, firmware_main
 jsr $26, ($27), firmware_main
#ifdef KOTL_NATIVE
 br $31,.
#else
 call_pal 0
#endif
.end _start
.bss
.balign 16
stack_bottom:
.skip 65536
stack_top:

#ifdef KOTL_NATIVE
#include "kotl/ksi-call.S"
#endif

.section .note.GNU-stack,"",@progbits
