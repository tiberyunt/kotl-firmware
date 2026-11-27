#!/usr/bin/env python3
"""Alpha PE relocation: real execution, nested images and malformed metadata."""
import importlib.util
from pathlib import Path
import struct
import subprocess
import sys
import kotl_efi as native

ROOT=native.ROOT
spec=importlib.util.spec_from_file_location('pe_converter',ROOT/'scripts/elf-to-pe.py')
converter=importlib.util.module_from_spec(spec);spec.loader.exec_module(converter)


def run(machine,label,image,required=(),ram='128M',smp='1',display=False):
    if machine=='kotl-ev9':
        disk=native.disk('reloc-'+label,image)
        args=native.nvme(disk)
        if display:args=['-device','bochs-display,addr=2',*args]
        return native.run('reloc-'+label,args,ram=ram,smp=smp,required=required)
    path=ROOT/'build'/('reloc-'+label+'.EFI');path.write_bytes(image)
    command=[str(native.QEMU),'-M','clipper','-m',ram,'-nographic','-nic','none',
             '-kernel',str(ROOT/'build/alpha-efi.elf'),'-initrd',str(path)]
    result=subprocess.run(command,cwd=ROOT,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=25)
    text=result.stdout.decode(errors='replace');(ROOT/'build'/('reloc-'+label+'.log')).write_text(text)
    assert result.returncode==0 and 'FAIL:' not in text and 'BOOT FAILED' not in text,text
    for marker in required:assert marker in text,(marker,text)
    print('reloc-'+label+': PASS',flush=True)
    return text


def sections(image):
    pe=struct.unpack_from('<I',image,60)[0];opt=pe+24
    table=opt+struct.unpack_from('<H',image,pe+20)[0]
    count=struct.unpack_from('<H',image,pe+6)[0]
    return opt,[struct.unpack_from('<8sIIIIIIHHI',image,table+i*40) for i in range(count)]


def raw_offset(tables,rva,width=1):
    for section in tables:
        va,raw,offset=section[2:5]
        if va<=rva and rva+width<=va+raw:return offset+rva-va
    raise AssertionError(('unmapped fixture RVA',rva))


def rebase(data,base):
    result=bytearray(data);opt,table=sections(result)
    old=struct.unpack_from('<Q',result,opt+24)[0]
    rva,size=struct.unpack_from('<II',result,opt+112+40)
    start=raw_offset(table,rva,size);end=start+size
    while start<end:
        page,block=struct.unpack_from('<II',result,start)
        for i in range(start+8,start+block,2):
            word=struct.unpack_from('<H',result,i)[0];kind=word>>12
            if not kind:continue
            width=8 if kind==10 else 4
            offset=raw_offset(table,page+(word&4095),width)
            value=int.from_bytes(result[offset:offset+width],'little')
            result[offset:offset+width]=((value+base-old)&((1<<(width*8))-1)).to_bytes(width,'little')
        start+=block
    struct.pack_into('<Q',result,opt+24,base)
    return result


# Parent occupies the child's preferred base. Its guest assertions test both
# fixup widths, unaligned/cross-page pointers, negative blocks and cleanup.
for machine in ('clipper','kotl-ev9'):
    out=ROOT/'build/kotl-efi' if machine=='kotl-ev9' else ROOT/'build'
    parent=(out/'RELOCPARENT.EFI').read_bytes();child=(out/'RELOCCHILD.EFI').read_bytes()
    for ram in ('64M','128M','256M'):
        run(machine,machine+'-nested-'+ram,parent,ram=ram,
            required=('ALPHA RELOCATION TEST COMPLETE','child unload retires its timers and preserves parent events',
                      'PASS: rejected overlapping target','PASS: multiple image capacity fails safely'))
    for label,base in (('zero',0),('below-ram',0x200000),('high',0xfffffe0001000000)):
        run(machine,machine+'-base-'+label,rebase(child,base),
            required=('PASS: relocated Alpha pointers, GOT, and BSS',))
run('kotl-ev9','native-smp8',(ROOT/'build/kotl-efi/RELOCPARENT.EFI').read_bytes(),
    smp='8,sockets=1,cores=4,threads=2',required=('ALPHA RELOCATION TEST COMPLETE',))
run('kotl-ev9','native-display',(ROOT/'build/kotl-efi/RELOCPARENT.EFI').read_bytes(),display=True,
    required=('EFI GOP installed','ALPHA RELOCATION TEST COMPLETE'))

# Independent converter rejection tests use the allocated relocation table,
# rather than guessing offsets in the instruction stream.
elf=(ROOT/'build/relocate-child.elf').read_bytes()
shoff=struct.unpack_from('<Q',elf,40)[0];shsize,count=struct.unpack_from('<HH',elf,58)
headers=[struct.unpack_from('<IIQQQQIIQQ',elf,shoff+i*shsize) for i in range(count)]
relocation=next(section for section in headers if section[1]==4 and headers[section[7]][2]&2)
mutations=[]
data=bytearray(elf);struct.pack_into('<Q',data,relocation[4],0);mutations.append(('target',data))
data=bytearray(elf);info=struct.unpack_from('<Q',data,relocation[4]+8)[0]
struct.pack_into('<Q',data,relocation[4]+8,(info&~0xffffffff)|29);mutations.append(('TLS',data))
data=bytearray(elf);struct.pack_into('<Q',data,relocation[4]+8,0xffffffff00000002);mutations.append(('symbol',data))
data=bytearray(elf);struct.pack_into('<Q',data,40,len(data)+64);mutations.append(('section-bounds',data))
data=bytearray(elf);struct.pack_into('<Q',data,40,0);struct.pack_into('<H',data,60,0);mutations.append(('missing-retained-relocs',data))
for label,data in mutations:
    try:converter.convert(data,relocatable=True)
    except (ValueError,struct.error):print('converter-reject-'+label+': PASS',flush=True)
    else:raise AssertionError('accepted invalid ELF: '+label)
print('All 14 relocation emulator scenarios and 5 converter rejection checks passed.',flush=True)
