#!/usr/bin/env python3
"""Exercise graphical EFI, real QMP keyboard input and ESP boot discovery."""
import hashlib
import json
import os
from pathlib import Path
import selectors
import socket
import struct
import subprocess
import tempfile
import time
import uuid
import zlib
import kotl_efi as efi

OUT = efi.OUT


def multi_esp(name, first, second):
    """Build two non-overlapping FAT32 ESPs with independent GPT entries/CRCs."""
    a = efi.disk(name + '-a', first)
    b = efi.disk(name + '-b', second)
    path = OUT / (name + '.img')
    total = 294912
    starts = (2048, 139264)
    entries = bytearray(16384)
    with path.open('wb') as image:
        image.truncate(total * 512)
        mbr = bytearray(512)
        mbr[450] = 0xee
        struct.pack_into('<II', mbr, 454, 1, total - 1)
        mbr[510:] = b'\x55\xaa'
        image.write(mbr)
        for i, source in enumerate((a, b)):
            entry = memoryview(entries)[i*128:(i+1)*128]
            entry[:16] = uuid.UUID('c12a7328-f81f-11d2-ba4b-00a0c93ec93b').bytes_le
            entry[16:32] = uuid.uuid4().bytes_le
            struct.pack_into('<QQ', entry, 32, starts[i], starts[i]+131072-1)
            with source.open('rb') as volume:
                volume.seek(2048*512)
                image.seek(starts[i]*512)
                for _ in range(64):
                    image.write(volume.read(1024*1024))
        def header(current, backup, table):
            data = bytearray(512)
            struct.pack_into('<8sIIIIQQQQ16sQIII', data, 0, b'EFI PART', 0x10000,
                92, 0, 0, current, backup, 34, total-34, uuid.uuid4().bytes_le,
                table, 128, 128, zlib.crc32(entries))
            struct.pack_into('<I', data, 16, zlib.crc32(data[:92]))
            return data
        for lba, data in ((1, header(1,total-1,2)), (2,entries),
                          (total-33, entries), (total-1,header(total-1,1,total-33))):
            image.seek(lba*512)
            image.write(data)
    return path


def ui(name, extra=(), keys=(), screenshot=False, serial_f12=False, empty=False,
       setup=False, settings_file=None, actions=None, required=(), complete=True, splash=None,
       display="bochs-display,addr=2"):
    with tempfile.TemporaryDirectory(prefix='kotl-ui-') as directory:
        qmp_path = str(Path(directory)/'qmp')
        machine='kotl-ev9'+(',settings-file='+str(settings_file) if settings_file else '')
        cmd = [str(efi.QEMU), '-M', machine, '-m', '128M', '-display', 'none',
               '-serial', 'stdio', '-monitor', 'none', '-nic', 'none', '-bios',
               str(OUT/'kotl-efi-flash.bin'), '-device', display,
               '-qmp', 'unix:' + qmp_path + ',server=on,wait=off', *extra]
        proc = subprocess.Popen(cmd, cwd=efi.ROOT, stdin=subprocess.PIPE,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        sock = socket.socket(socket.AF_UNIX)
        sock.settimeout(5)
        data = bytearray()
        sel = selectors.DefaultSelector()
        sel.register(proc.stdout, selectors.EVENT_READ)
        try:
            deadline = time.monotonic()+40
            while not Path(qmp_path).exists():
                if proc.poll() is not None: raise RuntimeError(proc.stdout.read().decode())
                if time.monotonic()>deadline: raise TimeoutError('QMP startup')
                time.sleep(.01)
            sock.connect(qmp_path)
            stream = sock.makefile('rwb', buffering=0)
            json.loads(stream.readline())
            def command(execute, arguments=None):
                stream.write(json.dumps({'execute':execute,'arguments':arguments or {}}).encode()+b'\n')
                while True:
                    response=json.loads(stream.readline())
                    if 'error' in response: raise RuntimeError(response)
                    if 'return' in response:return response['return']
            command('qmp_capabilities')
            def key(name):
                command('send-key', {'keys':[{'type':'qcode','data':name}], 'hold-time':20})
                # QEMU schedules key-up; allow it before pressing another key.
                time.sleep(.04)
            def read_until(marker):
                while marker not in data:
                    if proc.poll() is not None: raise RuntimeError(data.decode(errors='replace'))
                    if time.monotonic()>deadline: raise TimeoutError(marker)
                    for event,_ in sel.select(.1):
                        data.extend(os.read(event.fileobj.fileno(),65536))
            read_until(b'Press F12 for boot settings')
            if splash:
                command('screendump', {'filename': str(splash)})
            if serial_f12:
                proc.stdin.write(b'\x1b[21~' if setup else b'\x1b[24~');proc.stdin.flush()
            else: key('f10' if setup else 'f12')
            read_until(b'firmware-setup> ' if setup else b'boot-settings> ')
            if screenshot:
                time.sleep(.2)
                command('screendump', {'filename':str(OUT/'boot-settings.ppm')})
                screenshot_data=(OUT/'boot-settings.ppm').read_bytes()
                magic, dimensions, maximum, rgb=screenshot_data.split(b'\n',3)
                assert (magic,dimensions,maximum)==(b'P6',b'1024 768',b'255')
                # Antialiasing must reach the actual virtual display, not just the font atlas.
                assert len(set(zip(rgb[0::3],rgb[1::3],rgb[2::3])))>32
            if actions:
                actions(key,read_until,command,data,proc)
            elif empty:
                assert b'No bootable EFI partitions.' in data
                key('s')
                read_until(b'kotl> ')
                proc.stdin.write(b'shutdown\r');proc.stdin.flush()
            else:
                for name_key in keys:key(name_key)
                key('ret')
            while proc.poll() is None:
                if time.monotonic()>deadline: raise TimeoutError('guest finish')
                for event,_ in sel.select(.1):data.extend(os.read(event.fileobj.fileno(),65536))
            data.extend(proc.stdout.read())
            output=data.decode(errors='replace')
            assert proc.returncode==0 and 'FAIL:' not in output and 'BOOT FAILED' not in output,output
            assert 'EFI GOP installed' in output,output
            if not empty and complete: assert 'ALPHA EFI SELFTEST COMPLETE' in output,output
            for text in required: assert text in output,(text,output)
            print(f'{name}: PASS',flush=True)
            return output
        finally:
            if proc.poll() is None:proc.kill()
            proc.wait()
            sock.close();sel.close();proc.stdin.close();proc.stdout.close()
            (OUT/('test-'+name+'.log')).write_text(data.decode(errors='replace'))


if __name__ == '__main__':
    application=(OUT/'BOOTAXP64.EFI').read_bytes()
    hello=(OUT/'HELLOAXP64.EFI').read_bytes()
    image=efi.disk('ui-esp',application)
    complete=('ALPHA EFI SELFTEST COMPLETE','GOP fill and video-to-buffer BLT',
              'GOP buffer-to-video and overlapping video BLT','GOP physical framebuffer is directly writable')
    efi.run('gop', ['-device','bochs-display,addr=2',*efi.nvme(image)],required=complete)
    ui('f12-qmp',efi.nvme(image),screenshot=True)
    ui('f12-serial',efi.nvme(image),serial_f12=True)
    ui('empty-menu',empty=True)
    two=multi_esp('two-esps',hello,application)
    output=ui('select-second-esp',efi.nvme(two),keys=('down',))
    assert 'Detected 2 bootable EFI partition(s)' in output and 'Hello from Alpha EFI!' not in output
    broken=multi_esp('skip-bad-esp',b'invalid EFI image',application)
    efi.run('auto-second-esp',efi.nvme(broken),required=('ALPHA EFI SELFTEST COMPLETE','Detected 1 bootable EFI partition(s)'))
    # The first controller has no bootable PE; discovery must continue to the next.
    invalid=efi.disk('invalid-controller',b'not PE')
    args=[*efi.nvme(invalid),'-drive',f'if=none,id=d1,format=raw,file={image},snapshot=on',
          '-device','nvme,drive=d1,serial=SECOND,addr=3']
    efi.run('auto-second-controller',args,required=('ALPHA EFI SELFTEST COMPLETE',))
    # Two namespace IDs under one controller, only the second contains a valid PE.
    args=['-drive',f'if=none,id=d0,format=raw,file={invalid},snapshot=on',
          '-drive',f'if=none,id=d1,format=raw,file={image},snapshot=on',
          '-device','nvme,id=n0,serial=MULTINS,addr=1',
          '-device','nvme-ns,drive=d0,bus=n0,nsid=1',
          '-device','nvme-ns,drive=d1,bus=n0,nsid=7']
    efi.run('auto-second-namespace',args,required=('ALPHA EFI SELFTEST COMPLETE','Detected 1 bootable EFI partition(s)'))
    print('All 8 display, keyboard and discovery scenarios passed.',flush=True)
