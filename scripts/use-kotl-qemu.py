#!/usr/bin/env python3
"""Make existing test harnesses use a separately built companion emulator."""
from pathlib import Path
import argparse
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('emulator', type=Path)
a = p.parse_args()
binary = a.emulator.expanduser().resolve()
if not binary.is_file():
    p.error(f'Build the companion kotl-qemu repo first, or set KOTL_QEMU: {binary}')
target = Path(__file__).resolve().parent.parent / 'build/qemu-kotl/qemu-system-alpha'
target.parent.mkdir(parents=True, exist_ok=True)
if target.is_symlink():
    if target.resolve() != binary:
        target.unlink()
        target.symlink_to(binary)
elif target.exists():
    if not target.samefile(binary):
        p.error(f'Refusing to replace an existing emulator: {target}')
else:
    target.symlink_to(binary)
