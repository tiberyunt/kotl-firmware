/* Fixed-size resident/pending/active PAL discovery. SPDX-License-Identifier: MIT */
#include "kotl.h"
#include "platform.h"
static uint8_t *pending,*active;
static uint32_t read32(const uint8_t *p)
{
    return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];
}
static void write32(uint8_t *p,uint32_t n)
{
    p[0]=n>>24;p[1]=n>>16;p[2]=n>>8;p[3]=n;
}
static unsigned string_size(const char *s)
{
    unsigned n=1;while(*s++)n++;return n;
}
static unsigned word(uint8_t *b,unsigned at,uint32_t n)
{
    write32(b+at,n);return at+4;
}
static unsigned node(uint8_t *b,unsigned at,const char *name)
{
    at=word(b,at,1);
    unsigned n=string_size(name);memcpy(b+at,name,n);
    return (at+n+3)&~3U;
}
static unsigned property(uint8_t *b,unsigned at,uint32_t name,const void *value,unsigned n)
{
    at=word(b,at,3);at=word(b,at,n);at=word(b,at,name);
    memcpy(b+at,value,n);return (at+n+3)&~3U;
}
void kotl_pal_fdt_state(unsigned model,int committed)
{
    if(committed && active)write32(active,model);
    else if(pending)write32(pending,model);
}
int kotl_pal_fdt_init(void)
{
    uint8_t *dtb=(void *)native_boot_info->dtb;
    unsigned total=read32(dtb+4),structure=read32(dtb+8),strings=read32(dtb+12);
    unsigned nstrings=read32(dtb+32),nstructure=read32(dtb+36),insert=0,depth=0;
    unsigned at=structure;
    /* The parser already validated this packed QEMU DTB. Locate root END_NODE. */
    while(at<structure+nstructure) {
        unsigned tag=read32(dtb+at);at+=4;
        if(tag==1) {
            while(at<total && dtb[at])at++;
            at=(at+4)&~3U;depth++;
        } else if(tag==2) {
            if(depth==1){insert=at-4;break;}
            if(!depth)return 0;
            depth--;
        } else if(tag==3) {
            unsigned n=read32(dtb+at);at+=8+((n+3)&~3U);
        } else if(tag!=4)return 0;
    }
    if(!insert || strings<structure+nstructure || strings+nstrings!=total)return 0;
    uint8_t blob[512]={0},names[256]={0},value[4];
    unsigned length=0,nnames=0,pending_offset=0,active_offset=0;
    length=node(blob,length,"firmware");length=node(blob,length,"kotl-pal");
    const char *keys[]={"compatible","kotl,policy","kotl,ppi-version","kotl,resident-image",
                        "kotl,personality","kotl,pending-model","kotl,active-model"};
    const char *compat="kotl,resident-pal-draft01",*policy="permissive-resident-dev";
    for(unsigned i=0;i<7;i++) {
        unsigned name=nstrings+nnames,n=string_size(keys[i]);
        memcpy(names+nnames,keys[i],n);nnames+=n;
        if(i<2) {
            const char *text=i?policy:compat;
            length=property(blob,length,name,text,string_size(text));
        } else {
            write32(value,i==2?0x10000:i==3?1:i==4?0x80000000:0);
            if(i==5)pending_offset=length+12;
            if(i==6)active_offset=length+12;
            length=property(blob,length,name,value,4);
        }
    }
    length=word(blob,length,2);length=word(blob,length,2);
    if(total+length+nnames>0x100000)return 0;
    /* Move backwards within the already reserved SRAM window; never allocate. */
    for(unsigned i=total;i>insert;i--)dtb[i+length-1]=dtb[i-1];
    memcpy(dtb+insert,blob,length);
    memcpy(dtb+strings+length+nstrings,names,nnames);
    write32(dtb+4,total+length+nnames);write32(dtb+12,strings+length);
    write32(dtb+32,nstrings+nnames);write32(dtb+36,nstructure+length);
    native_boot_info->dtb_size=total+length+nnames;
    pending=dtb+insert+pending_offset;active=dtb+insert+active_offset;
    return 1;
}
