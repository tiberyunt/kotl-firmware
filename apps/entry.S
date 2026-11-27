.text
.globl efi_entry
.ent efi_entry
efi_entry:
 br $27, 1f
1:
 ldgp $29, 0($27)
 lda $30, -16($30)
 stq $26, 0($30)
 lda $27, efi_main
 jsr $26, ($27), efi_main
 ldgp $29, 0($26)
 ldq $26, 0($30)
 lda $30, 16($30)
 ret $31, ($26), 1
.end efi_entry
