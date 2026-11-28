#!/usr/bin/env python3
"""Build the resident KPAL metadata from the exact linked PAL payload."""
import hashlib
from pathlib import Path
import struct
import sys
payload=Path(sys.argv[1]).read_bytes()
header=bytearray(192)
struct.pack_into('<4sHHIIQIIQQQQIII',header,0,b'KPAL',1,192,0x80000000,0x10000,
                 1,0,0x62,0,65536,65536,65536,192,len(payload),1)
assert 0<len(payload)<=65536
digest=hashlib.sha384(header+payload).digest()
header[76:124]=digest
Path(sys.argv[2]).write_bytes(header+payload)
Path(sys.argv[3]).write_text('/* Generated resident KPAL digest. */\nstatic const unsigned char resident_pal_digest[48] = {'+
                           ','.join(f'0x{x:02x}' for x in digest)+'};\n')
