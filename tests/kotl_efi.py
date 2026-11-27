#!/usr/bin/env python3
"""Boot the native EV9 EFI firmware through real QEMU NVMe and KSI paths."""
import hashlib
import importlib.util
import os
from pathlib import Path
import selectors
import struct
import subprocess
import time

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / 'build/kotl-efi'
spec = importlib.util.spec_from_file_location('esp', ROOT / 'scripts/make-esp.py')
esp = importlib.util.module_from_spec(spec)
spec.loader.exec_module(esp)
QEMU = ROOT / 'build/qemu-kotl/qemu-system-alpha'
passed = 0


def disk(name, data):
    path = OUT / (name + '-' + str(os.getpid()) + '.img')
    esp.build(path, data)
    return path


def nvme(path, slot=1, namespace=None, sector=512):
    args = ['-drive', f'if=none,id=d0,format=raw,file={path},snapshot=on']
    if namespace is None:
        return args + ['-device', f'nvme,drive=d0,serial=KOTLEFI,addr={slot}']
    return args + ['-device', f'nvme,id=n0,serial=KOTLEFI,addr={slot}',
                   '-device', f'nvme-ns,drive=d0,bus=n0,nsid={namespace},logical_block_size={sector},physical_block_size={sector}']


def run(name, extra=(), mode='', ram='128M', smp='1', required=(), error=None,
        commands=(), reset=False):
    global passed
    machine = 'kotl-ev9' + (',firmware-args=' + mode if mode else '')
    cmd = [str(QEMU), '-M', machine, '-m', ram, '-smp', smp, '-nographic',
           '-nic', 'none', '-bios', str(OUT / 'kotl-efi-flash.bin'), *extra]
    proc = subprocess.Popen(cmd, cwd=ROOT, stdin=subprocess.PIPE,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    data = bytearray()
    pending = list(commands)
    prompts = 0
    deliberate_reset_stop = False
    selector = selectors.DefaultSelector()
    selector.register(proc.stdout, selectors.EVENT_READ)
    deadline = time.monotonic() + 35
    try:
        while proc.poll() is None:
            if time.monotonic() > deadline:
                raise TimeoutError(name)
            for event, _ in selector.select(.1):
                chunk = os.read(event.fileobj.fileno(), 65536)
                if not chunk:
                    continue
                data.extend(chunk)
                seen = data.count(b'kotl> ')
                if pending and seen > prompts:
                    proc.stdin.write(pending.pop(0).encode() + b'\r')
                    proc.stdin.flush()
                    prompts = seen
                if reset and data.count(b'KOTL RESET TEST') >= 2:
                    deliberate_reset_stop = True
                    proc.terminate()
        data.extend(proc.stdout.read())
    finally:
        if proc.poll() is None:
            proc.kill()
        proc.wait()
        selector.close()
        proc.stdin.close()
        proc.stdout.close()
        output = data.decode(errors='replace')
        (OUT / ('test-' + name + '.log')).write_text(output)
    assert not proc.returncode or deliberate_reset_stop, (name, proc.returncode, output)
    assert 'FAIL:' not in output and 'trap outside KSI' not in output, (name, output)
    for text in required:
        assert text in output, (name, text, output)
    if error:
        assert 'BOOT FAILED' in output and error in output, (name, output)
        assert 'ALPHA EFI SELFTEST COMPLETE' not in output, (name, output)
    else:
        assert 'BOOT FAILED' not in output, (name, output)
    if reset:
        assert deliberate_reset_stop and data.count(b'Kotl native EFI experimental firmware') >= 2
        assert 'EFI runtime reset requested' in output
    passed += 1
    print(f'{name}: PASS ({output.count("PASS:")} guest assertions)', flush=True)
    return output


def main():
    complete = ('ALPHA EFI SELFTEST COMPLETE', 'PASS: ExitBootServices and runtime shutdown',
                'KSI: native model selected; runtime SYSTEM_OFF')
    for ram in ('64M', '128M', '256M'):
        run('embedded-' + ram, mode='selftest', ram=ram, required=complete)
    application = (OUT / 'BOOTAXP64.EFI').read_bytes()
    image = disk('esp', application)
    before = hashlib.sha256(image.read_bytes()).digest()
    for ram in ('64M', '128M', '256M'):
        run('nvme-' + ram, nvme(image), ram=ram,
            required=complete + ('NVMe quiesced; IOMMU returned to default deny',))
    run('eight-harts', nvme(image), smp='8,sockets=1,cores=4,threads=2', required=complete)
    run('pci-slot', ['-device', 'qemu-xhci,addr=1', *nvme(image, slot=7)], required=complete)
    run('namespace-seven', nvme(image, namespace=7), required=complete)
    run('no-nvme', error='EFI application returned: 0x800000000000000e')
    run('no-namespace', ['-device', 'nvme,serial=EMPTY'], error='EFI application returned: 0x800000000000000c')
    run('sector-4096', nvme(image, namespace=1, sector=4096), error='EFI application returned: 0x8000000000000003')
    padded = disk('padded', application + bytes(256 * 1024))
    run('queue-wrap-fragmented', nvme(padded), required=complete)
    hello = disk('hello', (OUT / 'HELLOAXP64.EFI').read_bytes())
    output = run('reload', nvme(hello), mode='shell', commands=('disk', 'disk', 'shutdown'),
                 required=('Hello from Alpha EFI!', 'EFI image unloaded'))
    assert output.count('Hello from Alpha EFI!') == 2 and output.count('EFI image unloaded') == 2
    wrong = bytearray(application)
    pe = struct.unpack_from('<I', wrong, 60)[0]
    struct.pack_into('<H', wrong, pe + 4, 0x8664)
    run('wrong-machine', nvme(disk('wrong-machine', wrong)), error='EFI application returned: 0x8000000000000003')
    corrupt = disk('corrupt-gpt', application)
    with corrupt.open('r+b') as file:
        file.seek(512)
        file.write(b'BAD PART')
    run('bad-gpt', nvme(corrupt), error='EFI application returned:')
    run('runtime-reset', nvme(disk('reset', (OUT / 'RESETAXP64.EFI').read_bytes())), reset=True)
    assert hashlib.sha256(image.read_bytes()).digest() == before, 'source disk modified'
    print(f'All {passed} native EFI scenarios passed; source disk unchanged.', flush=True)


if __name__ == "__main__":
    main()
