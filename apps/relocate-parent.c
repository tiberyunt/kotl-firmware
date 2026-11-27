/* SPDX-License-Identifier: MIT */
#include "efi.h"
#include "reloc-child-meta.h"
extern const uint8_t reloc_child_start[], reloc_child_end[];
static EFI_BOOT_SERVICES *bs;
static EFI_SYSTEM_TABLE *st;
static volatile UINTN canary = 0x12345678;
static unsigned notifications;
static void parent_notify(EFI_EVENT event, void *context)
{
    (void)event;
    (void)context;
    notifications++;
}
static void text(const char *message)
{
    CHAR16 string[150];
    unsigned i = 0;
    while (message[i] && i < 149) {
        string[i] = message[i];
        i++;
    }
    string[i] = 0;
    st->ConOut->OutputString(st->ConOut, string);
}
static uint16_t read16(const uint8_t *p)
{
    return p[0] | ((uint16_t)p[1] << 8);
}
static uint32_t read32(const uint8_t *p)
{
    return read16(p) | ((uint32_t)read16(p + 2) << 16);
}
static uint64_t read64(const uint8_t *p)
{
    return read32(p) | ((uint64_t)read32(p + 4) << 32);
}
static void write16(uint8_t *p, uint16_t n)
{
    p[0] = n;
    p[1] = n >> 8;
}
static void write32(uint8_t *p, uint32_t n)
{
    write16(p, n);
    write16(p + 2, n >> 16);
}
static UINTN file_offset(uint8_t *data, UINTN rva)
{
    UINTN pe = read32(data + 60), table = pe + 24 + read16(data + pe + 20);
    for (unsigned i = 0; i < read16(data + pe + 6); i++) {
        uint8_t *s = data + table + i * 40;
        UINTN va = read32(s + 12), raw = read32(s + 16);
        if (rva >= va && rva - va < raw)
            return read32(s + 20) + rva - va;
    }
    return 0;
}
static UINTN free_pages(void)
{
    EFI_MEMORY_DESCRIPTOR map[128];
    UINTN size = sizeof(map), key, ds, pages = 0;
    uint32_t version;
    if (bs->GetMemoryMap(&size, map, &key, &ds, &version))
        return 0;
    if (ds < sizeof(map[0]))
        return 0;
    for (UINTN offset = 0; offset < size; offset += ds) {
        EFI_MEMORY_DESCRIPTOR *descriptor = (void *)((uint8_t *)map + offset);
        if (descriptor->Type == EfiConventionalMemory)
            pages += descriptor->NumberOfPages;
    }
    return pages;
}
#define CHECK(condition, message)                                                                  \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            text("FAIL: " message "\r\n");                                                         \
            return EFI_LOAD_ERROR;                                                                 \
        }                                                                                          \
        text("PASS: " message "\r\n");                                                             \
    } while (0)
EFI_STATUS efi_main(EFI_HANDLE parent, EFI_SYSTEM_TABLE *table)
{
    st = table;
    bs = table->BootServices;
    UINTN bytes = reloc_child_end - reloc_child_start;
    uint8_t *copy = 0;
    CHECK(bs->AllocatePool(EfiLoaderData, bytes, (void **)&copy) == 0,
          "allocate mutable relocation source");
    bs->CopyMem(copy, reloc_child_start, bytes);
    UINTN pe = read32(copy + 60), opt = pe + 24, dir = opt + 112 + 40, rva = read32(copy + dir),
          directory = file_offset(copy, rva);
    UINTN original_size = read32(copy + dir + 4);
    uint64_t preferred = read64(copy + opt + 24);
    CHECK(CHILD_CROSS_RVA % 4096 == 4092, "fixture includes a page-crossing 64-bit relocation");
    CHECK(directory && original_size + 12 <= 512,
          "fixture has room for an additional relocation block");
    /* Add a HIGHLOW fixup in an out-of-order page block. DIR64 is generated
       from ELF; this explicitly tests the independent 32-bit loader path. */
    write32(copy + file_offset(copy, CHILD_LOW32_RVA), (uint32_t)(preferred + CHILD_DATUM_RVA));
    write32(copy + directory + original_size, CHILD_LOW32_RVA & ~4095U);
    write32(copy + directory + original_size + 4, 12);
    write16(copy + directory + original_size + 8, 0x3000 | (CHILD_LOW32_RVA & 4095));
    write16(copy + directory + original_size + 10, 0);
    write32(copy + dir + 4, original_size + 12);
    UINTN sections = opt + read16(copy + pe + 20);
    write32(copy + sections + 40 + 8, original_size + 12);
    EFI_HANDLE child = 0;
    EFI_LOADED_IMAGE_PROTOCOL *loaded = 0;
    EFI_GUID guid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
    UINTN baseline = free_pages();
    EFI_EVENT parent_event;
    CHECK(bs->CreateEvent(EVT_NOTIFY_SIGNAL, TPL_CALLBACK, parent_notify, 0, &parent_event) == 0,
          "create parent-owned event");
    CHECK(bs->LoadImage(0, (EFI_HANDLE)1, 0, copy, bytes, &child) == EFI_INVALID_PARAMETER,
          "invalid parent image rejected");
    uint16_t characteristics = read16(copy + pe + 22);
    write16(copy + pe + 22, characteristics | 1);
    write32(copy + dir, 0);
    write32(copy + dir + 4, 0);
    CHECK(bs->LoadImage(0, parent, 0, copy, bytes, &child) == EFI_NOT_FOUND &&
              free_pages() == baseline,
          "fixed images refuse an occupied preferred address without leaking pages");
    write16(copy + pe + 22, characteristics);
    write32(copy + dir, rva);
    write32(copy + dir + 4, original_size + 12);
    CHECK(bs->LoadImage(0, parent, 0, copy, bytes, &child) == 0 &&
              bs->HandleProtocol(child, &guid, (void **)&loaded) == 0 &&
              loaded->ParentHandle == parent && (UINTN)loaded->ImageBase != preferred,
          "occupied preferred base relocates child and preserves parent handle");
    CHECK(bs->StartImage(child, 0, 0) == 0 && canary == 0x12345678,
          "relocated child executes and returns to parent GP");
    CHECK(bs->StartImage(child, 0, 0) == EFI_INVALID_PARAMETER,
          "completed image cannot be started twice");
    EFI_HANDLE stale = child;
    CHECK(bs->UnloadImage(child) == 0 && bs->UnloadImage(stale) == EFI_INVALID_PARAMETER &&
              free_pages() == baseline,
          "unload releases relocated pages and invalidates image handle");
    bs->Stall(3000);
    CHECK(bs->SignalEvent(parent_event) == 0 && notifications == 1,
          "child unload retires its timers and preserves parent events");
    for (unsigned i = 0; i < 40; i++) {
        if (bs->LoadImage(0, parent, 0, copy, bytes, &child) || child == stale ||
            bs->StartImage(child, 0, 0) || bs->UnloadImage(child))
            return EFI_LOAD_ERROR;
        stale = child;
    }
    CHECK(free_pages() == baseline,
          "repeated relocation resets data and BSS without leaking pages");
    EFI_HANDLE children[16];
    unsigned count = 0;
    for (; count < 16; count++) {
        EFI_STATUS status = bs->LoadImage(0, parent, 0, copy, bytes, &children[count]);
        if (status == EFI_OUT_OF_RESOURCES)
            break;
        if (status)
            return EFI_LOAD_ERROR;
    }
    CHECK(count == 15, "multiple image capacity fails safely at the documented limit");
    for (unsigned i = 0; i < count; i++)
        if (bs->UnloadImage(children[i]))
            return EFI_LOAD_ERROR;
    CHECK(free_pages() == baseline, "all concurrently loaded images unload cleanly");
    const char *names[] = {"unaligned relocation page",
                           "zero block size",
                           "misaligned block size",
                           "oversized block",
                           "truncated directory",
                           "unsupported fixup type",
                           "header target",
                           "unmapped target",
                           "self-modifying relocation metadata",
                           "duplicate target",
                           "overlapping target",
                           "stripped flag with relocations",
                           "missing directory RVA"};
    for (unsigned test = 0; test < 13; test++) {
        bs->CopyMem(copy, reloc_child_start, bytes);
        EFI_STATUS expected = EFI_LOAD_ERROR;
        if (test == 0)
            write32(copy + directory, read32(copy + directory) + 1);
        if (test == 1)
            write32(copy + directory + 4, 0);
        if (test == 2)
            write32(copy + directory + 4, 10);
        if (test == 3)
            write32(copy + directory + 4, 0xfffffffc);
        if (test == 4)
            write32(copy + dir + 4, original_size - 1);
        if (test == 5) {
            write16(copy + directory + 8, 0xf000);
            expected = EFI_UNSUPPORTED;
        }
        if (test == 6) {
            write32(copy + directory, 0);
            write16(copy + directory + 8, 0xa080);
        }
        if (test == 7)
            write32(copy + directory, read32(copy + opt + 56));
        if (test == 8) {
            write32(copy + directory, rva);
            write16(copy + directory + 8, 0xa000);
        }
        if (test == 9 || test == 10) {
            /* Repeat the first block at the end of the directory. */
            UINTN block = read32(copy + directory + 4);
            if (original_size + block > 512)
                return EFI_LOAD_ERROR;
            bs->CopyMem(copy + directory + original_size, copy + directory, block);
            if (test == 10)
                write16(copy + directory + original_size + 8,
                        0x3000 | ((read16(copy + directory + 8) & 4095) + 4));
            write32(copy + dir + 4, original_size + block);
            write32(copy + sections + 40 + 8, original_size + block);
        }
        if (test == 11)
            write16(copy + pe + 22, read16(copy + pe + 22) | 1);
        if (test == 12)
            write32(copy + dir, 0);
        EFI_HANDLE bad = 0;
        if (bs->LoadImage(0, parent, 0, copy, bytes, &bad) != expected ||
            free_pages() != baseline) {
            text("FAIL: ");
            text(names[test]);
            text("\r\n");
            return EFI_LOAD_ERROR;
        }
        text("PASS: rejected ");
        text(names[test]);
        text("\r\n");
    }
    CHECK(bs->CloseEvent(parent_event) == 0, "close parent-owned event");
    CHECK(bs->FreePool(copy) == 0, "release relocation test source");
    text("ALPHA RELOCATION TEST COMPLETE\r\n");
    EFI_MEMORY_DESCRIPTOR map[128];
    UINTN size = sizeof(map), key, ds;
    uint32_t version;
    if (bs->GetMemoryMap(&size, map, &key, &ds, &version) || bs->ExitBootServices(parent, key))
        return EFI_LOAD_ERROR;
    table->RuntimeServices->ResetSystem(2, EFI_SUCCESS, 0, 0);
    return EFI_LOAD_ERROR;
}
