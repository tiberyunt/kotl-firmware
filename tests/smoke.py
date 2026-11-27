#!/usr/bin/env python3
"""Run native Alpha firmware tests in the real QEMU system emulator."""
import argparse
import subprocess
import selectors
import os
import time
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--qemu', default='qemu-system-alpha')
a = p.parse_args()

def run(label, args=(), commands=None, required=(), ram='128M', expected_error=None):
    cmd = [a.qemu, '-M', 'clipper', '-cpu', 'ev67', '-m', ram, '-smp', '1',
           '-nographic', '-nic', 'none', '-kernel', 'build/alpha-efi.elf', *args]
    proc = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, cwd=ROOT)
    output_bytes = bytearray()
    pending = commands.split(b'\r')[:-1] if commands else []
    prompts = 0
    selector = selectors.DefaultSelector()
    selector.register(proc.stdout, selectors.EVENT_READ)
    deadline = time.monotonic()+30
    try:
        while True:
            if time.monotonic() > deadline:
                raise TimeoutError('emulator timed out')
            for event, _ in selector.select(0.25):
                chunk = os.read(event.fileobj.fileno(), 8192)
                if not chunk:
                    proc.wait(timeout=2)
                    break
                output_bytes.extend(chunk)
                seen = output_bytes.count(b'alpha> ')
                if pending and seen > prompts:
                    proc.stdin.write(pending.pop(0)+b'\r')
                    proc.stdin.flush()
                    prompts = seen
            if proc.poll() is not None:
                output_bytes.extend(proc.stdout.read())
                break
    except (TimeoutError, subprocess.TimeoutExpired) as e:
        proc.kill()
        proc.wait()
        output_bytes.extend(proc.stdout.read())
        output = output_bytes.decode(errors='replace')
        (ROOT/'build'/f'serial-{label}.log').write_text(output)
        raise SystemExit(f'{label}: {e}\n{output}')
    finally:
        selector.close()
        proc.stdin.close()
        proc.stdout.close()
    output = output_bytes.decode(errors='replace')
    (ROOT/'build'/f'serial-{label}.log').write_text(output)
    bad = proc.returncode or 'FAIL:' in output or any(s not in output for s in required)
    if expected_error:
        bad = bad or expected_error not in output or 'BOOT FAILED' not in output or 'ALPHA EFI SELFTEST COMPLETE' in output
    else:
        bad = bad or 'BOOT FAILED' in output
    if bad:
        raise SystemExit(f'{label}: failed (QEMU exit {proc.returncode})\n{output}')
    print(f'{label}: PASS ({output.count("PASS:")} guest assertions)')
    return output

for ram in ('64M', '128M', '256M'):
    run(ram, ram=ram, required=('ALPHA EFI SELFTEST COMPLETE',
        'PASS: EFI table signatures and CRC32', 'PASS: LoadedImage protocol',
        'PASS: allocated RAM read/write', 'PASS: stale ExitBootServices key rejected',
        'PASS: ExitBootServices and runtime shutdown'))
run('monitor', ['-append', 'shell'], b'help\rmemmap\rshutdown\r',
    required=('Alpha firmware monitor.', 'help memmap boot shutdown', 'type=0x'))
run('monitor-boot', ['-append', 'shell'], b'boot\r', required=('ALPHA EFI SELFTEST COMPLETE',))

run('external-image', ['-initrd', 'build/BOOTAXP64.EFI'],
    required=('Using external EFI image from QEMU -initrd', 'ALPHA EFI SELFTEST COMPLETE'))

run('hello-image', ['-initrd', 'build/HELLOAXP64.EFI'],
    required=('Hello from Alpha EFI!', 'EFI image unloaded', 'EFI application finished'))
# Exercise the guest loader with independently mutated external files.
original = (ROOT/'build/HELLOAXP64.EFI').read_bytes()
pe = struct.unpack_from('<I', original, 60)[0]
opt = pe+24
sec = opt+240
mutations = [
    ('machine', pe+4, '<H', 0x8664, 3),
    ('raw-bounds', sec+20, '<I', 0xfffffe00, 1),
    ('entry-bounds', opt+16, '<I', 0xffffffff, 1),
    ('section-bounds', sec+12, '<I', 0xfffff000, 1),
    ('image-size', opt+56, '<I', 0xfffff000, 1),
    ('header-size', opt+60, '<I', 0xfffff000, 1),
    ('base-alignment', opt+24, '<Q', 0xfffffc0001000001, 1),
    ('nonexecutable', sec+36, '<I', 0xc0000040, 1),
    ('data-directory', opt+112, '<Q', 0x1000, 3),
]
for label, offset, fmt, value, error in mutations:
    data = bytearray(original)
    struct.pack_into(fmt, data, offset, value)
    path = ROOT/'build'/f'invalid-{label}.EFI'
    path.write_bytes(data)
    run('reject-'+label, ['-initrd', str(path)],
        expected_error=f'EFI application returned: 0x800000000000000{error}')
path = ROOT/'build/invalid-truncated.EFI'
path.write_bytes(original[:64])
run('reject-truncated', ['-initrd', str(path)],
    expected_error='EFI application returned: 0x8000000000000001')
data = bytearray(original)
struct.pack_into('<H', data, pe+6, 2)
data[sec+40:sec+80] = data[sec:sec+40]
path = ROOT/'build/invalid-overlap.EFI'
path.write_bytes(data)
run('reject-overlap', ['-initrd', str(path)],
    expected_error='EFI application returned: 0x8000000000000001')

output = run('reload-image', ['-append', 'shell', '-initrd', 'build/HELLOAXP64.EFI'],
             b'boot\rboot\rshutdown\r',
             required=('Hello from Alpha EFI!', 'EFI image unloaded'))
assert output.count('Hello from Alpha EFI!') == 2, 'image data/BSS failed on reload'
assert output.count('EFI image unloaded') == 2, 'image unload failed on reload'

# Storage tests use real ATA PIO reads from sparse GPT/FAT32 disk images.
import importlib.util
spec = importlib.util.spec_from_file_location('make_esp', ROOT/'scripts/make-esp.py')
esp = importlib.util.module_from_spec(spec)
spec.loader.exec_module(esp)

def disk_args(path):
    return ['-append', 'disk', '-drive',
            f'if=ide,index=0,format=raw,file={path},snapshot=on']

valid_disk = ROOT/'build/storage-test.img'
esp.build(valid_disk, (ROOT/'build/BOOTAXP64.EFI').read_bytes())
for ram in ('64M','128M','256M'):
    run('disk-'+ram, disk_args(valid_disk), ram=ram,
        required=('Mounted GPT EFI System Partition', 'PASS: ESP Block IO protocol',
                  'PASS: fragmented file contents', 'PASS: VFAT long filename enumeration',
                  'PASS: Simple File System protocol', 'ALPHA EFI SELFTEST COMPLETE'))
hello_disk = ROOT/'build/storage-hello.img'
esp.build(hello_disk, (ROOT/'build/HELLOAXP64.EFI').read_bytes())
output = run('disk-reload', ['-append','shell','-drive',
    f'if=ide,index=0,format=raw,file={hello_disk},snapshot=on'],
    b'disk\rdisk\rshutdown\r', required=('Hello from Alpha EFI!', 'EFI image unloaded'))
assert output.count('Hello from Alpha EFI!') == 2, 'disk image reload failed'

run('disk-absent', ['-append','disk'], expected_error='EFI application returned: 0x800000000000000c')

def patch_file(path, offset, fmt, value):
    with path.open('r+b') as image:
        image.seek(offset)
        image.write(struct.pack(fmt,value))

def repair_gpt(path):
    import zlib
    with path.open('r+b') as image:
        image.seek(1024)
        entries = image.read(128*128)
        image.seek(512)
        header = bytearray(image.read(512))
        struct.pack_into('<I',header,88,zlib.crc32(entries))
        struct.pack_into('<I',header,16,0)
        struct.pack_into('<I',header,16,zlib.crc32(header[:92]))
        image.seek(512)
        image.write(header)

fat_offset = (esp.ESP_START+esp.RESERVED)*512
boot_offset = esp.ESP_START*512
bootdir_offset = (esp.ESP_START+esp.DATA_SECTOR+4-2)*512
cases = [
    ('gpt-header-crc', 512+16, '<I', 0, 10, False),
    ('gpt-table-crc', 1024+16, '<I', 0, 10, False),
    ('gpt-partition-bounds', 1024+32, '<Q', 1, 10, True),
    ('gpt-no-esp', 1024, '<I', 0, 14, True),
    ('fat-sector-size', boot_offset+11, '<H', 4096, 10, False),
    ('fat-cluster-zero', boot_offset+13, '<B', 0, 10, False),
    ('fat-cluster-nonpower', boot_offset+13, '<B', 3, 10, False),
    ('fat-size-overflow', boot_offset+36, '<I', 0xffffffff, 10, False),
    ('fat-invalid-root', boot_offset+44, '<I', 0x0ffffff0, 10, False),
    ('fat-file-cycle', fat_offset+esp.APP_CLUSTER*4, '<I', esp.APP_CLUSTER, 10, False),
    ('fat-file-truncated', fat_offset+esp.APP_CLUSTER*4, '<I', 0x0fffffff, 10, False),
    ('fat-missing-boot', bootdir_offset+4*32, '<B', 0xe5, 14, False),
    ('fat-invalid-lfn', bootdir_offset+2*32+13, '<B', 0, 14, False),
]
for label, offset, fmt, value, error, fix_gpt in cases:
    path = ROOT/'build'/f'storage-invalid-{label}.img'
    esp.build(path, (ROOT/'build/HELLOAXP64.EFI').read_bytes())
    patch_file(path,offset,fmt,value)
    if fix_gpt:
        repair_gpt(path)
    run('reject-'+label, disk_args(path),
        expected_error=f'EFI application returned: 0x{(1<<63)|error:016x}')

# Disable FAT mirroring and select FAT 1, then corrupt FAT 0 to prove selection.
path = ROOT/'build/storage-active-fat.img'
esp.build(path, (ROOT/'build/BOOTAXP64.EFI').read_bytes())
patch_file(path,boot_offset+40,'<H',0x81)
patch_file(path,fat_offset+esp.APP_CLUSTER*4,'<I',0)
run('active-second-fat',disk_args(path),required=('ALPHA EFI SELFTEST COMPLETE',))

# Force PALcode to assign different IDE BARs by placing a NIC before the IDE controller.
run('disk-pci-bars',disk_args(valid_disk)+['-nic','user,model=ne2k_pci,restrict=on'],
    required=('Mounted GPT EFI System Partition','ALPHA EFI SELFTEST COMPLETE'))
