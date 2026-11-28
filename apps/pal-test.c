/* Real UEFI -> retained Enter -> PAL -> K regression. SPDX-License-Identifier: MIT */
#include "efi-kotl-pal.h"
#include "kotl/kotl.h"
#ifndef TEST_GRANULE
#define TEST_GRANULE 14
#endif
extern const unsigned char pal_test_kernel[],pal_test_kernel_end[];
extern void *memcpy(void *,const void *,size_t);
extern void *memset(void *,int,size_t);
static EFI_SYSTEM_TABLE *table;
static void require(int condition,const CHAR16 *message)
{
    if(condition)return;
    table->ConOut->OutputString(table->ConOut,u"FAIL: PAL loader ");
    table->ConOut->OutputString(table->ConOut,(CHAR16 *)message);
    table->RuntimeServices->ResetSystem(2,EFI_LOAD_ERROR,0,0);
    for(;;){}
}
EFI_STATUS efi_main(EFI_HANDLE image,EFI_SYSTEM_TABLE *st)
{
    table=st;
    EFI_BOOT_SERVICES *bs=st->BootServices;
    EFI_GUID guid=EFI_KOTL_PAL_PROTOCOL_GUID;
    EFI_KOTL_PAL_PROTOCOL *pal=0;
    require(bs->LocateProtocol(&guid,0,(void **)&pal)==0,u"protocol discovery\r\n");
    KOTL_PAL_INFO info;
    require(pal->GetInfo(pal,&info)==0 && info.ppi==0x10000 && info.resident_image==1,
            u"resident image discovery\r\n");
    uint64_t id=0xdead;
    require(pal->Stage(pal,"not an image",12,&id)==EFI_UNSUPPORTED && id==0xdead,
            u"external Stage must not silently accept unverified content\r\n");
    EFI_PHYSICAL_ADDRESS code=KOTL_RAM+0x1800000,allocation=0;
    require(bs->AllocatePages(AllocateAddress,EfiLoaderCode,16,&code)==0,u"kernel allocation\r\n");
    require(bs->AllocatePages(AllocateAnyPages,EfiLoaderData,32,&allocation)==0,u"page tables\r\n");
    uint64_t base=(allocation+16383)&~16383ULL;
    memset((void *)base,0,0x10000);
    uint64_t *root=(void *)base,*middle=(void *)(base+0x4000),*leaf=(void *)(base+0x8000);
#if TEST_MODEL == 2
    root[(code>>33)&511]=((base+0x4000)>>13)<<32|0x101;
    middle[(code>>23)&1023]=((base+0x8000)>>13)<<32|0x101;
    leaf[(code>>13)&1023]=(code>>13)<<32|0x1101;
    leaf[((code+8192)>>13)&1023]=((code+8192)>>13)<<32|0x1101;
    uint64_t root_u=base|6,root_k=(base+4096)|6;
#elif TEST_GRANULE == 12
    uint64_t *l2=(void *)(base+0x1000),*l1=(void *)(base+0x2000),*l0=(void *)(base+0x3000);
    root[(code>>39)&255]=(base+0x1000)|1;
    l2[(code>>30)&511]=(base+0x2000)|1;
    l1[(code>>21)&511]=(base+0x3000)|1;
    for(unsigned i=0;i<4;i++)l0[((code>>12)+i)&511]=(code+i*4096)|15;
    uint64_t root_u=base|1,root_k=base|1;
    (void)middle;(void)leaf;
#else
    root[(code>>36)&2047]=(base+0x4000)|1;
    middle[(code>>25)&2047]=(code&~0x1ffffffULL)|15;
    uint64_t root_u=base,root_k=base;
    (void)leaf;
#endif
    memcpy((void *)code,pal_test_kernel,pal_test_kernel_end-pal_test_kernel);
    __asm__ volatile("mb;call_pal 0x86":::"memory");
    KOTL_OS_HANDOFF handoff={KOTL_HANDOFF_MAGIC,1,sizeof(handoff),code,code+0x4000,0,
                             root_u,root_k,{0x1234,0,0,0,0,0}};
    KOTL_PAL_ENTER enter=0;
    void *context=0;
    handoff.magic=0;
    require(pal->Activate(pal,1,TEST_MODEL,&handoff,sizeof(handoff),&enter,&context)==EFI_INVALID_PARAMETER,
            u"bad descriptor rejected before selection\r\n");
    require(KOTL_READ_CSR(0x108)==0 && !context && !enter,u"failed selection atomicity\r\n");
    handoff.magic=KOTL_HANDOFF_MAGIC;
    require(pal->Activate(pal,1,TEST_MODEL,&handoff,sizeof(handoff),&enter,&context)==0,
            u"Activate\r\n");
    require(KOTL_READ_CSR(0x108)==0 && enter(context)==EFI_NOT_READY && KOTL_READ_CSR(0x108)==0,
            u"Enter before exit must preserve firmware mode\r\n");
    require(pal->Activate(pal,1,TEST_MODEL,&handoff,sizeof(handoff),&enter,&context)==EFIERR(20),
            u"selection is one-shot\r\n");
    handoff.entry=0; /* The retained copy must remain intact. */
    EFI_MEMORY_DESCRIPTOR map[128];
    UINTN bytes=sizeof(map),key,descriptor_size;
    uint32_t version;
    require(bs->GetMemoryMap(&bytes,map,&key,&descriptor_size,&version)==0,u"map\r\n");
    int retained=0;
    for(UINTN at=0;at<bytes;at+=descriptor_size) {
        EFI_MEMORY_DESCRIPTOR *d=(void *)((uint8_t *)map+at);
        if(d->Type==EfiReservedMemoryType && d->PhysicalStart<=KOTL_RAM+0x600000 &&
           d->PhysicalStart+d->NumberOfPages*4096>=KOTL_RAM+0x800000)retained=1;
    }
    require(retained,u"PAL code, context and stacks must remain reserved\r\n");
    EFI_PHYSICAL_ADDRESS extra=0;
    require(bs->AllocatePages(AllocateAnyPages,EfiLoaderData,1,&extra)==0,u"stale key allocation\r\n");
    require(bs->ExitBootServices(image,key)==EFI_INVALID_PARAMETER && KOTL_READ_CSR(0x108)==0,
            u"failed exit preserves pending selection\r\n");
    table->ConOut->OutputString(table->ConOut,u"PASS: PAL selection, copied descriptor and stale-key retry\r\n");
    bytes=sizeof(map);
    require(bs->GetMemoryMap(&bytes,map,&key,&descriptor_size,&version)==0,u"final map\r\n");
    require(bs->ExitBootServices(image,key)==0,u"final exit\r\n");
#if TEST_BAD_CONTEXT
    ((KOTL_OS_HANDOFF *)context)->entry^=4;
#endif
    return enter(context);
}
