#!/usr/bin/env python3
"""Real EFI event waits, serial/GUI keyboard readiness and unload cleanup."""
import argparse
import json
import os
from pathlib import Path
import selectors
import socket
import subprocess
import tempfile
import time
import kotl_efi as native

ROOT=native.ROOT
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--qemu',default=str(native.QEMU))
args=parser.parse_args()


def run(label,kind,machine='kotl-ev9',input_bytes=None,graphical_key=None,leak=False):
    is_native=machine=='kotl-ev9'
    directory=ROOT/'build/kotl-efi' if is_native else ROOT/'build'
    path=directory/('EVENT'+kind+'.EFI')
    with tempfile.TemporaryDirectory(prefix='alpha-events-') as temporary:
        qmp_path=Path(temporary)/'qmp'
        base=[args.qemu,'-M',machine+(',firmware-args=shell' if is_native and leak else ''),
              '-m','128M','-nographic','-nic','none']
        if is_native:
            image=native.disk('events-'+label,path.read_bytes())
            base+=['-bios',str(directory/'kotl-efi-flash.bin'),*native.nvme(image)]
        else:
            base+=['-kernel',str(ROOT/'build/alpha-efi.elf')]
            if leak:
                image=native.disk('events-'+label,path.read_bytes())
                base+=['-append','shell','-drive',f'if=ide,index=0,format=raw,file={image},snapshot=on']
            else:base+=['-initrd',str(path)]
        if graphical_key:base+=['-qmp',f'unix:{qmp_path},server=on,wait=off']
        proc=subprocess.Popen(base,cwd=ROOT,stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
        selection=selectors.DefaultSelector();selection.register(proc.stdout,selectors.EVENT_READ)
        output=bytearray();sent=False;prompts=0;command_count=0
        prompt=b'kotl> ' if is_native else b'alpha> '
        deadline=time.monotonic()+25
        sock=None
        try:
            if graphical_key:
                sock=socket.socket(socket.AF_UNIX);sock.settimeout(5)
                while not qmp_path.exists():
                    if proc.poll() is not None or time.monotonic()>deadline:raise TimeoutError(label)
                    time.sleep(.01)
                sock.connect(str(qmp_path));stream=sock.makefile('rwb',buffering=0)
                json.loads(stream.readline())
                def qmp(name,arguments=None):
                    stream.write(json.dumps({'execute':name,'arguments':arguments or {}}).encode()+b'\n')
                    while True:
                        response=json.loads(stream.readline())
                        assert 'error' not in response,response
                        if 'return' in response:return
                qmp('qmp_capabilities')
            while proc.poll() is None:
                if time.monotonic()>deadline:raise TimeoutError(label)
                for event,_ in selection.select(.1):output.extend(os.read(event.fileobj.fileno(),65536))
                if not sent and b'EVENT INPUT READY' in output:
                    if graphical_key:qmp('send-key',{'keys':[{'type':'qcode','data':'f10'}],'hold-time':20})
                    elif input_bytes:proc.stdin.write(input_bytes);proc.stdin.flush()
                    sent=True
                count=output.count(prompt)
                if leak and count>prompts:
                    # Forty returning images intentionally leave an armed callback.
                    # Unload must retire it before releasing executable memory.
                    proc.stdin.write(b'disk\r' if command_count<40 else b'boot\r');proc.stdin.flush()
                    command_count+=1;prompts=count
            output.extend(proc.stdout.read())
            text=output.decode(errors='replace')
            assert proc.returncode==0 and 'FAIL:' not in text and 'BOOT FAILED' not in text,text
            assert 'ALPHA EFI SELFTEST COMPLETE' in text,text
            if leak:assert text.count('EVENT LEAK IMAGE RETURNED')==40,text
            else:
                assert 'EVENT INPUT TEST COMPLETE' in text,text
                if kind=='TIMEOUT':assert 'PASS: event timeout selected' in text
                if kind in ('SERIAL','PAIR'):assert 'PASS: WaitForKey preserves serial input' in text
                if kind=='PAIR':assert 'PASS: consecutive queued keys are preserved' in text
                if kind=='KEY':assert 'PASS: WaitForKey delivers EFI F10 scan code' in text
            print(label+': PASS',flush=True)
        finally:
            if proc.poll() is None:proc.kill()
            proc.wait();selection.close()
            if sock:sock.close()
            proc.stdin.close();proc.stdout.close()
            (ROOT/'build'/('events-'+label+'.log')).write_text(output.decode(errors='replace'))


for machine in ('clipper','kotl-ev9'):
    run(machine+'-timeout','TIMEOUT',machine)
    run(machine+'-serial','SERIAL',machine,input_bytes=b'x')
    run(machine+'-queued-input','PAIR',machine,input_bytes=b'xy')
    run(machine+'-unload','LEAK',machine,leak=True)
run('native-f10','KEY',graphical_key=True)
print('All 9 event/input/unload integration scenarios passed.',flush=True)
