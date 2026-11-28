#!/usr/bin/env python3
"""Real UEFI selection/exit/Enter/PAL/kernel transfers on the revised emulator."""
from pathlib import Path
import importlib.util
import subprocess

ROOT=Path(__file__).resolve().parent.parent
OUT=ROOT/'build/kotl-efi'
spec=importlib.util.spec_from_file_location('esp',ROOT/'scripts/make-esp.py')
esp=importlib.util.module_from_spec(spec);spec.loader.exec_module(esp)
for name,app,part,smp,bad in [
    ('native-4k','PAL4K.EFI','21564','4,sockets=1,cores=4,threads=1',False),
    ('native','PALNATIVE.EFI','21564','4,sockets=1,cores=4,threads=1',False),
    ('full','PALFULL.EFI','21564','4,sockets=1,cores=4,threads=1',False),
    ('full-smt-exclusion','PALFULL.EFI','z1-lite','8,sockets=1,cores=4,threads=2',False),
    ('protected-efi-read','PALFWPROTECT.EFI','21564','4,sockets=1,cores=4,threads=1','protected'),
    ('protected-code-read','PALPROTECT.EFI','21564','4,sockets=1,cores=4,threads=1','protected'),
    ('invalid-retained-context','PALBAD.EFI','21564','4,sockets=1,cores=4,threads=1',True),
]:
    disk=OUT/('pal-test-'+name+'.img');esp.build(disk,(OUT/app).read_bytes())
    command=[str(ROOT/'build/qemu-kotl/qemu-system-alpha'),'-M',f'kotl-ev9,part={part}',
             '-smp',smp,'-m','128M','-display','none','-serial','stdio','-monitor','none',
             '-nic','none','-no-reboot','-bios',str(OUT/'kotl-efi-flash.bin'),
             '-drive',f'if=none,id=d0,format=raw,file={disk},snapshot=on',
             '-device','nvme,drive=d0,serial=KOTLPAL,addr=1']
    try: result=subprocess.run(command,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=12)
    except subprocess.TimeoutExpired as e:
        (OUT/('pal-test-'+name+'.log')).write_bytes(e.stdout or b'');raise
    text=result.stdout.decode(errors='replace')
    (OUT/('pal-test-'+name+'.log')).write_text(text)
    assert result.returncode==0,(name,text)
    assert 'PASS: PAL selection, copied descriptor and stale-key retry' in text,(name,text)
    if bad=='protected':
        assert 'fatal exception cause=0000000000000004' in text and 'KERNEL HANDOFF PASS' not in text,(name,text)
    elif bad:
        assert 'FAIL: invalid retained PAL handoff context' in text and 'entering kernel' not in text,(name,text)
    else:
        assert 'KOTL PAL KERNEL HANDOFF PASS' in text and 'FAIL' not in text,(name,text)
    print(name+': PASS',flush=True)
