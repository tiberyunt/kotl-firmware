#!/usr/bin/env python3
"""Create a sparse GPT disk with a FAT32 ESP and fragmented Alpha EFI application."""
import argparse
import struct
import uuid
import zlib
from pathlib import Path

SECTOR = 512
DISK_SECTORS = 196608
ESP_START = 2048
ESP_SECTORS = 131072
RESERVED = 32
FAT_SECTORS = 1024
DATA_SECTOR = RESERVED+2*FAT_SECTORS
APP_CLUSTER = 5

def short_entry(name, cluster, size=0, attributes=0x20):
    entry = bytearray(32)
    entry[:11] = name
    entry[11] = attributes
    struct.pack_into('<H', entry, 20, cluster >> 16)
    struct.pack_into('<HI', entry, 26, cluster & 0xffff, size)
    return entry

def lfn_entries(name, short):
    checksum = 0
    for byte in short:
        checksum = (((checksum & 1) << 7)+(checksum >> 1)+byte) & 255
    units = list(struct.unpack('<'+'H'*len(name), name.encode('utf-16le')))
    units.append(0)
    while len(units) % 13:
        units.append(0xffff)
    entries = []
    for index in reversed(range(len(units)//13)):
        entry = bytearray(32)
        entry[0] = index+1 | (0x40 if index == len(units)//13-1 else 0)
        entry[11] = 0x0f
        entry[13] = checksum
        for offset, value in zip((1,3,5,7,9,14,16,18,20,22,24,28,30), units[index*13:index*13+13]):
            struct.pack_into('<H', entry, offset, value)
        entries.append(entry)
    return b''.join(entries)

def build(path, application, boot_files=None):
    if not application or len(application)>4*1024*1024:
        raise ValueError('application must contain 1 byte through 4 MiB')
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open('wb') as image:
        image.truncate(DISK_SECTORS*SECTOR)
        def write_sector(lba, data):
            image.seek(lba*SECTOR)
            image.write(data)
        mbr = bytearray(SECTOR)
        mbr[450] = 0xee
        struct.pack_into('<II', mbr, 454, 1, DISK_SECTORS-1)
        mbr[510:] = b'\x55\xaa'
        write_sector(0, mbr)
        entries = bytearray(128*128)
        entries[:16] = uuid.UUID('c12a7328-f81f-11d2-ba4b-00a0c93ec93b').bytes_le
        entries[16:32] = uuid.UUID('4cd725e0-735f-426f-a2c8-e5a553df51f9').bytes_le
        struct.pack_into('<QQQ', entries, 32, ESP_START, ESP_START+ESP_SECTORS-1, 0)
        label = 'Alpha EFI System'.encode('utf-16le')
        entries[56:56+len(label)] = label
        entries_crc = zlib.crc32(entries)
        def gpt_header(current, backup, table):
            header = bytearray(SECTOR)
            struct.pack_into('<8sIIIIQQQQ16sQIII', header, 0, b'EFI PART', 0x10000,
                92, 0, 0, current, backup, 34, DISK_SECTORS-34,
                uuid.UUID('4bc79aef-8d28-4489-83dd-63dbf5ed36cf').bytes_le,
                table, 128, 128, entries_crc)
            struct.pack_into('<I', header, 16, zlib.crc32(header[:92]))
            return header
        write_sector(1, gpt_header(1, DISK_SECTORS-1, 2))
        write_sector(2, entries)
        write_sector(DISK_SECTORS-33, entries)
        write_sector(DISK_SECTORS-1, gpt_header(DISK_SECTORS-1, 1, DISK_SECTORS-33))
        boot = bytearray(SECTOR)
        boot[:3] = b'\xeb\x58\x90'
        boot[3:11] = b'ALPHAEFI'
        struct.pack_into('<HBHBHHBHHHII', boot, 11, 512, 1, RESERVED, 2, 0, 0, 0xf8,
                         0, 63, 255, ESP_START, ESP_SECTORS)
        struct.pack_into('<IHHIHH', boot, 36, FAT_SECTORS, 0, 0, 2, 1, 6)
        boot[64] = 0x80
        boot[66] = 0x29
        struct.pack_into('<I', boot, 67, 0xa1fa2026)
        boot[71:82] = b'ALPHA EFI  '
        boot[82:90] = b'FAT32   '
        boot[510:] = b'\x55\xaa'
        write_sector(ESP_START, boot)
        write_sector(ESP_START+6, boot)
        fsinfo = bytearray(SECTOR)
        struct.pack_into('<I', fsinfo, 0, 0x41615252)
        struct.pack_into('<IIII', fsinfo, 484, 0x61417272, 0xffffffff, 0xffffffff, 0)
        struct.pack_into('<I', fsinfo, 508, 0xaa550000)
        write_sector(ESP_START+1, fsinfo)
        write_sector(ESP_START+7, fsinfo)
        fat = bytearray(FAT_SECTORS*SECTOR)
        def set_fat(cluster, value):
            struct.pack_into('<I', fat, cluster*4, value)
        set_fat(0, 0x0ffffff8)
        set_fat(1, 0xffffffff)
        for cluster in (2,3,4):
            set_fat(cluster, 0x0fffffff)
        def cluster_write(cluster, data):
            if len(data)>512:
                raise ValueError('cluster overflow')
            write_sector(ESP_START+DATA_SECTOR+cluster-2, data.ljust(512,b'\0'))
        def file_write(data, start):
            clusters = list(range(start, start+2*((len(data)+511)//512), 2))
            for i, cluster in enumerate(clusters):
                set_fat(cluster, clusters[i+1] if i+1<len(clusters) else 0x0fffffff)
                cluster_write(cluster, data[i*512:(i+1)*512])
            return clusters[-1]+2
        next_cluster = file_write(application, APP_CLUSTER)
        pattern = bytes((i*37+11)&255 for i in range(2049))
        pattern_cluster = next_cluster
        next_cluster = file_write(pattern, next_cluster)
        extra_entries = bytearray()
        for name, data in (boot_files or {}).items():
            parts = name.upper().split('.')
            if (len(parts) != 2 or not 1 <= len(parts[0]) <= 8 or not 1 <= len(parts[1]) <= 3
                    or any(c not in 'ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-' for c in ''.join(parts))):
                raise ValueError('extra boot file must use a simple 8.3 name')
            if not data or next_cluster + 2*((len(data)+511)//512) >= ESP_SECTORS-DATA_SECTOR:
                raise ValueError('empty extra boot file or ESP capacity exceeded')
            extra_entries += short_entry((parts[0].ljust(8)+parts[1].ljust(3)).encode(),next_cluster,len(data))
            next_cluster = file_write(data, next_cluster)
        if len(extra_entries) > 320:
            raise ValueError('too many extra boot files')
        cluster_write(2, short_entry(b'EFI        ',3,attributes=0x10)+short_entry(b'PATTERN BIN',pattern_cluster,len(pattern)))
        cluster_write(3, short_entry(b'.          ',3,attributes=0x10)+short_entry(b'..         ',0,attributes=0x10)+short_entry(b'BOOT       ',4,attributes=0x10))
        short = b'BOOTAX~1EFI'
        cluster_write(4, short_entry(b'.          ',4,attributes=0x10)+short_entry(b'..         ',3,attributes=0x10)+lfn_entries('BOOTAXP64.EFI',short)+short_entry(short,APP_CLUSTER,len(application))+extra_entries)
        write_sector(ESP_START+RESERVED, fat)
        write_sector(ESP_START+RESERVED+FAT_SECTORS, fat)
        extra_used = sum((len(data)+511)//512 for data in (boot_files or {}).values())
        used = extra_used+3+(len(application)+511)//512+(len(pattern)+511)//512
        struct.pack_into('<II', fsinfo, 488, ESP_SECTORS-DATA_SECTOR-used, 6)
        write_sector(ESP_START+1, fsinfo)
        write_sector(ESP_START+7, fsinfo)

if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('application', type=Path)
    p.add_argument('output', type=Path)
    args = p.parse_args()
    build(args.output, args.application.read_bytes())
    print(f'{args.output}: GPT + FAT32 ESP, \\EFI\\BOOT\\BOOTAXP64.EFI')
