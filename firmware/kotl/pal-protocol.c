/* Resident PAL selection and retained UEFI handoff. SPDX-License-Identifier: MIT */
#include "kotl.h"
#include "services.h"
#include "efi-kotl-pal.h"
#include "pal-layout.h"
#include "resident-pal-meta.h"
extern const uint8_t resident_pal_start[],resident_pal_end[];
extern void kotl_enter_pal(uint64_t,uint64_t) __attribute__((noreturn));
static uint64_t selected_model;
static uint32_t prepared_crc;
static int pal_handle;
static EFI_KOTL_PAL_PROTOCOL protocol;
#define CONTEXT ((KOTL_OS_HANDOFF *)(PAL_INFO+0x800))
static uint64_t descriptor(uint64_t base,unsigned size,uint64_t flags)
{
    return base|size|(1ULL<<15)|(1ULL<<11)|flags;
}
static EFI_STATUS get_info(EFI_KOTL_PAL_PROTOCOL *self,KOTL_PAL_INFO *out)
{
    if(!boot_active)return EFI_UNSUPPORTED;
    if(self!=&protocol || !out)return EFI_INVALID_PARAMETER;
    *out=(KOTL_PAL_INFO){1,0x10000,0x80000000,KOTL_RESIDENT_PAL,6,1};
    return EFI_SUCCESS;
}
static EFI_STATUS stage(EFI_KOTL_PAL_PROTOCOL *self,const void *image,UINTN size,uint64_t *id)
{
    if(!boot_active)return EFI_UNSUPPORTED;
    if(self!=&protocol || !image || !size || !id)return EFI_INVALID_PARAMETER;
    /* External image hashing/signature/SVN staging is deliberately unavailable.
     * The compiled-in resident image is covered by the firmware trust boundary. */
    return EFI_UNSUPPORTED;
}
static int root_valid(uint64_t root,uint64_t model)
{
    unsigned selector=root&3;
    unsigned granule=selector==1?12:selector==2?13:14;
    int legacy=(root&4)!=0;
    if(selector==3 || (legacy && selector!=2) || (model==2 && !legacy))return 0;
    uint64_t alignment=legacy?4096:1ULL<<granule;
    uint64_t base=root&~7ULL;
    return !(base&(alignment-1)) && base>=KOTL_RAM+0x800000 &&
        base<native_boot_info->ram_base+native_boot_info->ram_size &&
        alignment<=native_boot_info->ram_base+native_boot_info->ram_size-base;
}
static int canonical(uint64_t address,uint64_t model)
{
    unsigned sign=model==2?42:47;
    return (int64_t)address>>sign==(int64_t)address>>63;
}
static EFI_STATUS enter(void *context)
{
    if(boot_active)return EFI_NOT_READY;
    if(context!=CONTEXT || !selected_model ||
       crc32((void *)PAL_INFO,65536)!=prepared_crc ||
       memcmp((void *)PAL_BASE,resident_pal_start,resident_pal_end-resident_pal_start)) {
        puts_serial("FAIL: invalid retained PAL handoff context\n");
        halt();
    }
    /* No allocation or Boot Services calls beyond this point. */
    uint64_t harts=*(volatile uint64_t *)(native_boot_info->syscon+16);
    for(unsigned i=0;i<harts;i++) {
        *(volatile uint64_t *)(native_boot_info->timer+0x100+i*8)=UINT64_MAX;
        *(volatile uint64_t *)(native_boot_info->timer+0x1000+i*8)=0;
        for(unsigned word=0;word<16;word++)
            *(volatile uint64_t *)(native_boot_info->kaia+0x10200+i*0x1000+word*8)=0;
    }
    KOTL_WRITE_CSR(8,0);KOTL_WRITE_CSR(9,0);
    unsigned ram_shift=0;
    while((1ULL<<ram_shift)<native_boot_info->ram_size)ram_shift++;
    /* Consolidate the low controller apertures, freeing descriptors for
     * retained EFI code/data and private IOMMU metadata. */
    KOTL_WRITE_CSR(0x115,descriptor(0x10000000,31,(3ULL<<6)|(1ULL<<14)|(1ULL<<10)));
    KOTL_WRITE_CSR(0x116,descriptor(KOTL_FW_BASE,21,(7ULL<<6)|(1ULL<<12)|(1ULL<<13)));
    KOTL_WRITE_CSR(0x117,descriptor(KOTL_RAM,21,(3ULL<<6)|(1ULL<<12)|(1ULL<<13)));
    KOTL_WRITE_CSR(0x118,0);KOTL_WRITE_CSR(0x119,0);
    KOTL_WRITE_CSR(0x11a,descriptor(0x80000000,31,(3ULL<<6)|(1ULL<<14)|(1ULL<<10)));
    KOTL_WRITE_CSR(0x11c,descriptor(0x10000000000ULL,40,(3ULL<<6)|(1ULL<<14)|(1ULL<<10)));
    KOTL_WRITE_CSR(0x11d,descriptor(PAL_BASE,16,(5ULL<<6)|(1ULL<<12)));
    KOTL_WRITE_CSR(0x11e,descriptor(PAL_DATA,20,(3ULL<<6)|(1ULL<<12)|(1ULL<<13)));
    KOTL_WRITE_CSR(0x11f,descriptor(PAL_INFO,16,(1ULL<<6)|(1ULL<<12)));
    KOTL_WRITE_CSR(0x11b,descriptor(KOTL_RAM,ram_shift,(7ULL<<6)|(1ULL<<12)|
                                  (1ULL<<13)|(1ULL<<10)|(1ULL<<9)));
    kotl_pal_fdt_state(selected_model,1);
    __asm__ volatile("mb; .long 0x60005400":::"memory");
    kotl_enter_pal(PAL_INFO,selected_model);
}
static EFI_STATUS activate(EFI_KOTL_PAL_PROTOCOL *self,uint64_t image,uint64_t model,
                          const KOTL_OS_HANDOFF *handoff,UINTN size,
                          KOTL_PAL_ENTER *entry,void **context)
{
    if(!boot_active)return EFI_UNSUPPORTED;
    if(selected_model)return EFIERR(20); /* EFI_ALREADY_STARTED */
    if(self!=&protocol || image!=KOTL_RESIDENT_PAL || (model!=1 && model!=2) ||
       !handoff || size!=sizeof(*handoff) || !entry || !context)return EFI_INVALID_PARAMETER;
    KOTL_OS_HANDOFF copy=*handoff;
    if(copy.magic!=KOTL_HANDOFF_MAGIC || copy.version!=1 || copy.size!=sizeof(copy) ||
       !copy.entry || (copy.entry&3) || !copy.stack || (copy.stack&15) ||
       ((copy.root_u&7)!=(copy.root_k&7)) ||
       !canonical(copy.entry,(copy.root_u&4)?2:model) ||
       !canonical(copy.stack,(copy.root_u&4)?2:model) ||
       (copy.gp && !canonical(copy.gp,(copy.root_u&4)?2:model)) ||
       !root_valid(copy.root_u,model) || !root_valid(copy.root_k,model))return EFI_INVALID_PARAMETER;
    UINTN bytes=resident_pal_end-resident_pal_start;
    if(!bytes || bytes>65536)return EFI_LOAD_ERROR;
    memset((void *)PAL_BASE,0,65536);
    memcpy((void *)PAL_BASE,resident_pal_start,bytes);
    if(memcmp((void *)PAL_BASE,resident_pal_start,bytes))return EFI_LOAD_ERROR;
    memset((void *)PAL_DATA,0,0x100000);
    memset((void *)PAL_INFO,0,65536);
    uint64_t *p=(void *)PAL_INFO;
    p[0]=(0x100ULL<<32)|0x10000;p[1]=KOTL_READ_CSR(0x10a);
    uint64_t harts=native_boot_info->hart_count;
    uint64_t stride=model==2?native_boot_info->threads:1;
    p[2]=harts/stride;p[3]=PAL_INFO+0x100;
    for(unsigned i=0;i<p[2];i++)p[32+i]=i*stride;
    p[4]=PAL_BASE;p[5]=65536;p[6]=PAL_DATA;p[7]=65536;
    p[8]=native_boot_info->kaia;p[9]=native_boot_info->timer;p[10]=native_boot_info->uart;
    p[11]=PAL_INFO+0x200;p[12]=native_boot_info->ksi_entry;
    p[14]=native_boot_info->dtb;p[16]=(uint64_t)CONTEXT;
    uint32_t *identity=(void *)(PAL_INFO+0x180);
    identity[0]=0x80000000;identity[1]=0;
    *(uint64_t *)(identity+2)=1;identity[4]=0x10000;identity[5]=0x62;
    memcpy(identity+6,resident_pal_digest,48);
    uint64_t *map=(void *)(PAL_INFO+0x200);
    map[0]=2;map[1]=KOTL_RAM;map[2]=0x800000;map[3]=1;
    map[5]=KOTL_RAM+0x800000;map[6]=native_boot_info->ram_size-0x800000;
    *CONTEXT=copy;
    prepared_crc=crc32((void *)PAL_INFO,65536);
    /* All fallible work completed. Lock the pending selection, leaving FMODEL=0. */
    selected_model=model;
    kotl_pal_fdt_state(model,0);
    *entry=enter;*context=CONTEXT;
    puts_serial("PAL handoff prepared; FMODEL remains firmware\n");
    return EFI_SUCCESS;
}
void kotl_pal_init(void)
{
    if(!kotl_pal_fdt_init())kotl_fatal();
    static const EFI_GUID guid=EFI_KOTL_PAL_PROTOCOL_GUID;
    protocol=(EFI_KOTL_PAL_PROTOCOL){1,get_info,stage,activate};
    if(protocol_publish(&pal_handle,&guid,&protocol))kotl_fatal();
    puts_serial("Kotl resident PAL: QEMU-derived, Permissive development policy\n");
}
