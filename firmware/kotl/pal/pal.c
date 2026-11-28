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

#include "pal.h"
static const uint64_t *info(void) { return (const uint64_t *)PR_READ(0x10e); }
static PAL_STATE *state(void)
{
    const uint64_t *p=info();
    return (PAL_STATE *)(p[6]+PR_READ(0)*p[7]);
}
static void putc(char c) { pal_ksi(4,(unsigned char)c,info()); }
static void message(const char *p) { while(*p)putc(*p++); }
static void hexadecimal(uint64_t value)
{
    for(int bit=60;bit>=0;bit-=4)putc("0123456789abcdef"[(value>>bit)&15]);
}
static void fatal(void) __attribute__((noreturn));
static void fatal(void)
{
    message("Kotl PAL: unsupported or fatal exception cause=");hexadecimal(PR_READ(0x102));
    message(" pc=");hexadecimal(PR_READ(0x101));
    message(" value=");hexadecimal(PR_READ(0x103));message("\r\n");
    pal_ksi(3,0,info());
    for(;;){}
}
extern void pal_start_kernel(const KOTL_OS_HANDOFF *) __attribute__((noreturn));
void pal_reset_c(const uint64_t *p,uint64_t hart,uint64_t reason)
{
    if(p!=info() || p[0]!=(0x100ULL<<32|0x10000) || p[7]!=65536)fatal();
    if(hart || reason) {
        /* Secondary scheduling/OSF HWRPB restart is not implemented yet. */
        for(;;){}
    }
    const KOTL_OS_HANDOFF *handoff=(void *)p[16];
    if(!handoff || handoff->magic!=KOTL_HANDOFF_MAGIC || handoff->size!=sizeof(*handoff))fatal();
    message("Kotl PAL: QEMU-derived personality entering kernel\r\n");
    pal_start_kernel(handoff);
}
void pal_dispatch(uint64_t *r)
{
    uint64_t cause=r[33],call=r[34],saved=r[32];
    PAL_STATE *s=state();
    if(cause!=11 || ((saved>>8)&3)==3)fatal();
    r[31]+=4;
    switch(call) {
    case 0: pal_ksi(2,0,info());for(;;){}
    case 0x09: /* QEMU CSERVE selectors; Kotl console extension starts at 0x100. */
        switch(r[16]) {
        case 3:r[0]=pal_ksi(17,0,info()).value;break;
        case 7: { uint64_t t=pal_ksi(6,0,info()).value;r[0]=(t/3)*125+(t%3)*125/3;break; }
        case 0x100: r[0]=pal_ksi(4,r[17],info()).value;break;
        case 0x101: { PAL_KSI_RESULT v=pal_ksi(5,0,info());r[0]=v.error?(uint64_t)-1:v.value;break; }
        default:r[0]=(uint64_t)-2;break;
        }
        break;
    case 0x2b: PR_WRITE(1,(PR_READ(1)&~24ULL)|((r[16]&1)?8:0));break;
    case 0x2d:s->vpt=r[16];break;
    case 0x31:s->sysval=r[16];break;
    case 0x32:r[0]=s->sysval;break;
    case 0x33:if((int64_t)r[16]<0)PR_WRITE(0x1a,0);else PR_WRITE(0x18,r[17]);break;
    case 0x35:r[0]=s->ipl;s->ipl=r[16]&7;r[32]=(saved&~1ULL)|(s->ipl==0);break;
    case 0x36:r[0]=s->ipl|(((saved>>8)&3)==0?8:0);break;
    case 0x37:s->kgp=r[16];break;
    case 0x38:s->usp=r[16];break;
    case 0x3a:r[0]=s->usp;break;
    case 0x3c:r[0]=PR_READ(0);break;
    default:fatal();
    }
}
