/* Kotl adaptation of QEMU PALcode. SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef KOTL_PAL_PRIVATE_H
#define KOTL_PAL_PRIVATE_H
#include <stdint.h>
#include "efi-kotl-pal.h"
#include "../pal-layout.h"
#define PR_READ(c) ({register uint64_t x __asm__("$1");__asm__ volatile(".long 0x64200000|%1":"=r"(x):"i"(c):"memory");x;})
#define PR_WRITE(c,v) do{register uint64_t x __asm__("$1")=(v);__asm__ volatile(".long 0x74010000|%1"::"r"(x),"i"(c):"memory");}while(0)
typedef struct { int64_t error; uint64_t value; } PAL_KSI_RESULT;
PAL_KSI_RESULT pal_ksi(uint64_t function,uint64_t argument,const uint64_t *info);
/* Per-hart state is separate from the private 64 KiB trap stack. */
typedef struct { uint64_t sysval,usp,kgp,ipl,vpt; } PAL_STATE;
#endif
