#!/usr/bin/env python3
"""Emit named RVAs for the relocation integration test (not used by the loader)."""
import struct
import sys
from pathlib import Path

elf=Path(sys.argv[1]).read_bytes()
base=int(sys.argv[3],0)
shoff=struct.unpack_from('<Q',elf,40)[0]
shsize,count=struct.unpack_from('<HH',elf,58)
sections=[struct.unpack_from('<IIQQQQIIQQ',elf,shoff+i*shsize) for i in range(count)]
needed={'reloc_datum':'CHILD_DATUM_RVA','reloc_low32':'CHILD_LOW32_RVA','reloc_cross_pointer':'CHILD_CROSS_RVA'}
result={}
for section in sections:
    if section[1]!=2:continue
    strings=sections[section[6]]
    data=elf[strings[4]:strings[4]+strings[5]]
    for offset in range(0,section[5],24):
        name,info,other,index,value,size=struct.unpack_from('<IBBHQQ',elf,section[4]+offset)
        name=data[name:data.index(0,name)].decode()
        if name in needed:result[needed[name]]=value-base
if len(result)!=len(needed):raise ValueError('test symbols missing')
Path(sys.argv[2]).write_text('/* Generated relocation test metadata. */\n'+''.join(f'#define {key} {value}U\n' for key,value in sorted(result.items())))
