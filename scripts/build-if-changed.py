#!/usr/bin/env python3
"""Run a build only when its declared inputs or required output change."""
import argparse
import hashlib
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output', required=True, type=Path)
parser.add_argument('--input', action='append', default=[], type=Path)
parser.add_argument('command', nargs=argparse.REMAINDER)
args = parser.parse_args()
command = args.command[1:] if args.command[:1] == ['--'] else args.command
checksum = hashlib.sha256(repr(command).encode())
for source in sorted(set(args.input)):
    files = sorted(source.rglob('*')) if source.is_dir() else [source]
    for file in files:
        if file.is_file() and '__pycache__' not in file.parts:
            checksum.update(str(file).encode())
            checksum.update(file.read_bytes())
stamp = args.output.with_name(args.output.name + '.inputs-sha256')
digest = checksum.hexdigest()
if args.output.exists() and stamp.exists() and stamp.read_text() == digest:
    raise SystemExit(0)
subprocess.run(command, check=True)
if not args.output.exists():
    raise SystemExit(f'build did not produce {args.output}')
stamp.write_text(digest)
