#!/usr/bin/env python3
"""Package static Alpha ELF as PE32+, optionally with checked base relocations."""
import argparse
import struct
from pathlib import Path

BASE = 0xfffffc0001000000
MASK64 = (1 << 64) - 1


def align(n, a):
    return (n + a - 1) & -a


def bounded(data, offset, size):
    if offset < 0 or size < 0 or offset > len(data) or size > len(data) - offset:
        raise ValueError('ELF structure extends beyond input')
    return data[offset:offset + size]


def unpack(fmt, data, offset):
    return struct.unpack(fmt, bounded(data, offset, struct.calcsize(fmt)))


def string(data, offset):
    if offset >= len(data) or b'\0' not in data[offset:]:
        raise ValueError('unterminated ELF string')
    return data[offset:data.index(0, offset)]


def base_fixups(data, base, end, raw):
    """Translate static ELF fixups, including linker-created Alpha GOT slots.

    Only one GP and non-TLS, statically resolved code are supported. Unchanged
    GP/PC-relative fixups remain in the code; absolute fields become PE fixups.
    No scanning for address-shaped integers is used.
    """
    shoff, = unpack('<Q', data, 40)
    shsize, count, names_index = unpack('<HHH', data, 58)
    if shsize != 64 or not 0 < count <= 4096 or names_index >= count:
        raise ValueError('relocatable conversion requires ELF section metadata')
    bounded(data, shoff, shsize * count)
    sections = [unpack('<IIQQQQIIQQ', data, shoff + i * shsize) for i in range(count)]
    names = bounded(data, sections[names_index][4], sections[names_index][5])
    symbols = {}
    gp = None
    got = None
    for section in sections:
        if section[2] & 0x400:
            raise ValueError('ELF TLS sections cannot be converted')
        if string(names, section[0]) == b'.got':
            if got is not None or section[1] != 1 or section[5] % 8 or section[5] > 65536:
                raise ValueError('unsupported Alpha GOT layout')
            got = section
        if section[1] != 2:  # SHT_SYMTAB
            continue
        if section[9] != 24 or section[5] % 24 or section[6] >= count:
            raise ValueError('invalid ELF symbol table')
        bounded(data, section[4], section[5])
        strs = sections[section[6]]
        strings = bounded(data, strs[4], strs[5])
        table = []
        for offset in range(0, section[5], 24):
            symbol = unpack('<IBBHQQ', data, section[4] + offset)
            table.append(symbol)
            string(strings, symbol[0])  # validate symbol-name bounds
        symbols[id(section)] = table

    def allocated(symbol):
        index = symbol[3]
        if 0 < index < count:
            if not sections[index][2] & 2:
                raise ValueError('loaded relocation references a non-allocated section')
            return True
        if index == 0xfff1 or (index == 0 and symbol[1] >> 4 == 2 and symbol[4] == 0):
            return False  # absolute symbols and unresolved weak zero do not move
        raise ValueError('undefined or unsupported ELF relocation symbol')

    def read_image(address, width):
        if address < base + 4096 or address + width > end:
            raise ValueError('relocation target outside image')
        return bounded(raw, address - base - 4096, width)

    # BFD's actual GP can differ from a linker-script _gp symbol because
    # GOT alignment is chosen independently. Decode the retained GPDISP pairs,
    # whose PV/RA base denotes the first instruction's own address in our ABI.
    for section in sections:
        if section[1] != 4 or section[7] >= count or not sections[section[7]][2] & 2:
            continue
        if section[9] != 24 or section[5] % 24:
            raise ValueError('invalid allocated RELA table')
        bounded(data, section[4], section[5])
        for offset in range(0, section[5], 24):
            address, info, addend = unpack('<QQq', data, section[4] + offset)
            if info & 0xffffffff != 6:
                continue
            if not 4 <= addend <= 256 or addend % 4:
                raise ValueError('unsupported Alpha GPDISP pair')
            high = int.from_bytes(read_image(address, 4), 'little')
            low = int.from_bytes(read_image(address + addend, 4), 'little')
            if high >> 26 != 9 or (high >> 21) & 31 != 29 or (high >> 16) & 31 not in (26, 27) or low >> 26 != 8 or (low >> 21) & 31 != 29 or (low >> 16) & 31 != 29:
                raise ValueError('nonstandard Alpha GP initialization')
            hi = struct.unpack('<h', struct.pack('<H', high & 0xffff))[0]
            lo = struct.unpack('<h', struct.pack('<H', low & 0xffff))[0]
            actual_gp = (address + (hi << 16) + lo) & MASK64
            if gp is not None and gp != actual_gp:
                raise ValueError('multiple Alpha GOT/GP domains are unsupported')
            gp = actual_gp

    fixes = {}
    classified_got = set()
    allocated_relas = 0

    def add(address, width):
        read_image(address, width)
        rva = address - base
        if rva in fixes and fixes[rva] != width:
            raise ValueError('overlapping ELF absolute fixups')
        fixes[rva] = width

    for section in sections:
        if section[1] not in (4, 9):
            continue
        if section[7] >= count:
            raise ValueError('invalid ELF relocation target section')
        target = sections[section[7]]
        if not target[2] & 2:
            continue  # debug metadata is not loaded
        allocated_relas += 1
        if section[1] != 4 or section[9] != 24 or section[5] % 24 or section[6] >= count:
            raise ValueError('only ELF64 RELA relocations are supported')
        table = symbols.get(id(sections[section[6]]))
        if table is None:
            raise ValueError('relocation has no static symbol table')
        bounded(data, section[4], section[5])
        for offset in range(0, section[5], 24):
            address, info, addend = unpack('<QQq', data, section[4] + offset)
            kind, index = info & 0xffffffff, info >> 32
            if index >= len(table):
                raise ValueError('relocation symbol index outside table')
            if kind == 0:
                continue
            if address < target[3] or address >= target[3] + target[5]:
                raise ValueError('ELF relocation outside its target section')
            symbol = table[index]
            if kind in (5, 6, 8):  # LITUSE, GPDISP, HINT contain no absolute field
                read_image(address, 4)
                continue
            moving = allocated(symbol)
            value = (symbol[4] + addend) & MASK64
            if kind in (1, 2):  # REFLONG / REFQUAD
                width = 4 if kind == 1 else 8
                actual = int.from_bytes(read_image(address, width), 'little')
                # Alpha BFD's retained STT_SECTION addends can omit the input
                # section's contribution when sections are merged by the script.
                # The linked field is authoritative; its allocated-symbol
                # provenance still establishes that it must move with the image.
                if symbol[1] & 15 != 3 and actual != value & ((1 << (width * 8)) - 1):
                    raise ValueError(f'resolved ELF absolute value at {address:#x} is {actual:#x}, expected {value:#x}')
                if moving:
                    add(address, width)
            elif kind == 4:  # LITERAL: load from a linker-generated GOT slot
                if gp is None:
                    raise ValueError('Alpha LITERAL relocation requires a proven GP initialization')
                instruction = int.from_bytes(read_image(address, 4), 'little')
                displacement = struct.unpack('<h', struct.pack('<H', instruction & 0xffff))[0]
                slot = (gp + displacement) & MASK64
                if (instruction >> 16) & 31 != 29:
                    raise ValueError('literal load does not use the single image GP')
                opcode = instruction >> 26
                if opcode == 8 and moving and (slot == value or symbol[1] & 15 == 3):
                    continue  # relaxed GP-relative LDA; no absolute GOT slot
                if opcode != 0x29 or got is None or slot < got[3] or slot + 8 > got[3] + got[5] or slot % 8:
                    raise ValueError('unsupported resolved Alpha literal instruction')
                got_value = int.from_bytes(read_image(slot, 8), 'little')
                if symbol[1] & 15 != 3 and got_value != value:
                    raise ValueError(f'GOT slot {slot:#x} contains {got_value:#x}, expected {value:#x}')
                classified_got.add(slot)
                if moving:
                    add(slot, 8)
            elif kind in (3, 7, 9, 10, 11, 17, 18, 19, 28):
                if not moving:
                    raise ValueError('relative relocation to an absolute symbol cannot be rebased')
                read_image(address, 2 if kind == 9 else 8 if kind == 11 else 4)
            else:
                raise ValueError(f'unsupported allocated Alpha ELF relocation {kind}')
    if not allocated_relas:
        raise ValueError('relocatable conversion requires linking with --emit-relocs')
    if got is not None:
        for slot in range(got[3], got[3] + got[5], 8):
            if slot not in classified_got and any(read_image(slot, 8)):
                raise ValueError('unclassified nonzero Alpha GOT slot')
    previous = 0
    blocks = {}
    for rva, width in sorted(fixes.items()):
        if rva < previous:
            raise ValueError('overlapping absolute ELF relocations')
        previous = rva + width
        blocks.setdefault(rva & ~4095, []).append(((3 if width == 4 else 10) << 12) | (rva & 4095))
    result = bytearray()
    for page, entries in blocks.items():
        if len(entries) % 2:
            entries.append(0)  # IMAGE_REL_BASED_ABSOLUTE padding
        result.extend(struct.pack('<II', page, 8 + 2 * len(entries)))
        result.extend(struct.pack('<' + 'H' * len(entries), *entries))
    return result


def convert(data, base=BASE, relocatable=False):
    if data[:6] != b'\x7fELF\x02\x01' or len(data) < 64:
        raise ValueError('expected little-endian ELF64')
    if unpack('<H', data, 16)[0] != 2 or unpack('<H', data, 18)[0] != 0x9026:
        raise ValueError('expected static ET_EXEC EM_ALPHA')
    if base < 0 or base % 4096 or base > MASK64 - 4 * 1024 * 1024:
        raise ValueError('invalid image base')
    entry, phoff = unpack('<QQ', data, 24)
    phsize, phnum = unpack('<HH', data, 54)
    if phsize != 56 or not 0 < phnum <= 64:
        raise ValueError('invalid ELF program headers')
    bounded(data, phoff, phsize * phnum)
    chunks = []
    entry_executable = False
    for i in range(phnum):
        kind, flags, off, va, pa, filesz, memsz, alignment = unpack('<IIQQQQQQ', data, phoff + i * phsize)
        if kind in (2, 3, 7):
            raise ValueError('dynamic linking and TLS are unsupported')
        if kind == 1 and memsz:
            if va < base + 4096 or va + memsz > base + 4 * 1024 * 1024 or filesz > memsz:
                raise ValueError('ELF segment outside application window')
            if any(va < oldva + oldsize and oldva < va + memsz for oldva, oldsize, _ in chunks):
                raise ValueError('overlapping ELF segments')
            chunks.append((va, memsz, bounded(data, off, filesz)))
            if flags & 1 and va <= entry < va + filesz:
                entry_executable = True
    if not chunks:
        raise ValueError('no loadable ELF segments')
    end = max(va + size for va, size, _ in chunks)
    raw = bytearray(align(end - base - 4096, 512))
    for va, _, payload in chunks:
        raw[va - base - 4096:va - base - 4096 + len(payload)] = payload
    if not entry_executable or not base + 4096 <= entry < end:
        raise ValueError('invalid ELF entry')
    relocations = base_fixups(data, base, end, raw) if relocatable else bytearray()
    reloc_rva = align(end - base, 4096)
    image_size = align(reloc_rva + len(relocations), 4096) if relocations else align(end - base, 4096)
    if image_size > 4 * 1024 * 1024:
        raise ValueError('relocation section exceeds application window')
    hdr = bytearray(512)
    hdr[:2] = b'MZ'
    struct.pack_into('<I', hdr, 60, 0x80)
    hdr[0x80:0x84] = b'PE\0\0'
    struct.pack_into('<HHIIIHH', hdr, 0x84, 0x284, 2 if relocations else 1, 0, 0, 0, 240, 0x222 if relocatable else 0x223)
    opt = 0x98
    relocation_raw = relocations + bytes(align(len(relocations), 512) - len(relocations))
    struct.pack_into('<HBBIIIIIQII', hdr, opt, 0x20b, 0, 1, len(raw), len(relocation_raw), 0, entry - base, 4096, base, 4096, 512)
    struct.pack_into('<HHHHHH', hdr, opt + 40, 0, 0, 0, 1, 2, 100)
    struct.pack_into('<III', hdr, opt + 52, 0, image_size, 512)
    struct.pack_into('<HH', hdr, opt + 68, 10, 0)
    struct.pack_into('<QQQQII', hdr, opt + 72, 65536, 4096, 65536, 4096, 0, 16)
    sec = opt + 240
    struct.pack_into('<8sIIIIIIHHI', hdr, sec, b'.image\0\0', end - base - 4096, 4096, len(raw), 512, 0, 0, 0, 0, 0xe0000020)
    if relocations:
        struct.pack_into('<II', hdr, opt + 112 + 5 * 8, reloc_rva, len(relocations))
        struct.pack_into('<8sIIIIIIHHI', hdr, sec + 40, b'.reloc\0\0', len(relocations), reloc_rva, len(relocation_raw), 512 + len(raw), 0, 0, 0, 0, 0x42000040)
    return bytes(hdr + raw + relocation_raw)


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('input', type=Path)
    p.add_argument('output', type=Path)
    p.add_argument('--base', type=lambda value: int(value, 0), default=BASE)
    p.add_argument('--relocatable', action='store_true', help='requires ELF linked with --emit-relocs')
    a = p.parse_args()
    a.output.write_bytes(convert(a.input.read_bytes(), a.base, a.relocatable))
