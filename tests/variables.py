#!/usr/bin/env python3
"""EFI variable persistence, validation, runtime access and failed-commit rollback."""
import hashlib
import os
from pathlib import Path
import selectors
import struct
import subprocess
import tempfile
import time
import zlib
import kotl_efi as efi

OUT=efi.OUT


def run(label,kind,path,required=()):
    image=efi.disk('variable-'+label,(OUT/('VAR'+kind+'.EFI')).read_bytes())
    return efi.run('variable-'+label,['-M','kotl-ev9,variables-file='+str(path),*efi.nvme(image)],
                   required=('VARIABLE TEST COMPLETE','ALPHA EFI SELFTEST COMPLETE',*required))


def crc(data):
    struct.pack_into('<I',data,20,0)
    struct.pack_into('<I',data,20,zlib.crc32(data))


def rollback(parent):
    path=parent/'store.bin';saved=path.read_bytes()
    held=parent.with_name(parent.name+'-held')
    image=efi.disk('variable-rollback',(OUT/'VARROLLBACK.EFI').read_bytes())
    cmd=[str(efi.QEMU),'-M','kotl-ev9,variables-file='+str(path),'-m','128M','-nographic',
         '-nic','none','-bios',str(OUT/'kotl-efi-flash.bin'),*efi.nvme(image)]
    proc=subprocess.Popen(cmd,cwd=efi.ROOT,stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    selector=selectors.DefaultSelector();selector.register(proc.stdout,selectors.EVENT_READ)
    data=bytearray();moved=False
    try:
        deadline=time.monotonic()+30
        while proc.poll() is None:
            if time.monotonic()>deadline:raise TimeoutError('variable rollback')
            for event,_ in selector.select(.1):data.extend(os.read(event.fileobj.fileno(),65536))
            if not moved and b'VARIABLE MUTATION READY' in data:
                # Remove the host directory only after firmware has loaded its
                # previous value. A regular-file guard makes the next commit fail.
                parent.rename(held);parent.write_text('guard');moved=True
                proc.stdin.write(b'x');proc.stdin.flush()
        data.extend(proc.stdout.read());text=data.decode(errors='replace')
        assert proc.returncode==0 and 'BOOT FAILED' not in text and 'FAIL:' not in text,text
        assert 'PASS: failed update preserves previous variable value' in text,text
        assert 'ALPHA EFI SELFTEST COMPLETE' in text,text
        assert moved and (held/'store.bin').read_bytes()==saved
        assert parent.read_text()=='guard'
        print('variable-rollback: PASS',flush=True)
    finally:
        if proc.poll() is None:proc.kill()
        proc.wait();selector.close();proc.stdin.close();proc.stdout.close()
        if moved:parent.unlink();held.rename(parent)
        (OUT/'test-variable-rollback.log').write_text(data.decode(errors='replace'))


with tempfile.TemporaryDirectory(prefix='alpha-vars-',dir=OUT) as temporary:
    directory=Path(temporary);parent=directory/'backing';parent.mkdir();path=parent/'store.bin'
    run('write','WRITE',path,('PASS: variable boot writes committed',))
    saved=path.read_bytes();assert len(saved)==65536
    check=bytearray(saved);stored_crc=struct.unpack_from('<I',check,20)[0]
    struct.pack_into('<I',check,20,0);assert zlib.crc32(check)==stored_crc
    assert struct.unpack_from('<I',saved,24)[0]==2,'volatile data was persisted'
    before=hashlib.sha256(saved).digest()
    run('restart-read','READ',path,('PASS: persistent variables restored; volatile data discarded',))
    assert hashlib.sha256(path.read_bytes()).digest()==before,'read-only boot wrote variable file'
    rollback(parent)
    run('delete','DELETE',path,('PASS: variable deletions committed',))
    assert struct.unpack_from('<I',path.read_bytes(),24)[0]==0
    run('deleted-restart','EMPTY',path)
    missing=directory/'missing'/'store.bin'
    run('failed-create','FAIL',missing,('PASS: failed create leaves variable store unchanged',))
    assert not missing.exists()
    mutations=[]
    data=bytearray(saved);data[-1]^=1;mutations.append(('crc',data))
    data=bytearray(saved);struct.pack_into('<I',data,64+20,2048);crc(data);mutations.append(('oversized-record',data))
    data=bytearray(saved);data[64+1184:64+2*1184]=data[64:64+1184];crc(data);mutations.append(('duplicate',data))
    data=bytearray(saved);data[64+2*1184]=42;crc(data);mutations.append(('dirty-free-slot',data))
    data=bytearray(saved);struct.pack_into('<I',data,64+24,65);crc(data);mutations.append(('name-length',data))
    mutations.append(('truncated',bytearray(saved[:100])))
    for label,data in mutations:
        damaged=directory/(label+'.bin');damaged.write_bytes(data)
        run('reject-'+label,'EMPTY',damaged,('EFI variables: empty or invalid persistent store',))
        assert damaged.read_bytes()==data,'invalid store was silently rewritten'
    full=efi.disk('variable-full-selftest',(OUT/'BOOTAXP64.EFI').read_bytes())
    efi.run('variable-full-selftest',['-M','kotl-ev9,variables-file='+str(directory/'selftest.bin'),*efi.nvme(full)],
            required=('nonvolatile variables commit and delete through persistent backend',
                      'runtime properties advertise only implemented services','ALPHA EFI SELFTEST COMPLETE'))
    print('All 13 variable persistence and recovery scenarios passed.',flush=True)
