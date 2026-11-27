/* SPDX-License-Identifier: MIT */
#include "efi-storage.h"
#ifndef VARIABLE_KIND
#define VARIABLE_KIND 0
#endif
static EFI_SYSTEM_TABLE *st;
static const EFI_GUID guid = {0x11779933, 0x1844, 0x4422, {1, 3, 5, 7, 9, 11, 13, 15}};
static const CHAR16 name[] = {'P', 'e', 'r', 's', 'i', 's', 't', 'e', 'n', 't', 0};
static const CHAR16 boot[] = {'B', 'o', 'o', 't', 'N', 'v', 0};
static const CHAR16 transient[] = {'V', 'o', 'l', 'a', 't', 'i', 'l', 'e', 0};
static void text(const char *s)
{
    CHAR16 string[100];
    unsigned i = 0;
    while (s[i] && i < 99) {
        string[i] = s[i];
        i++;
    }
    string[i] = 0;
    st->ConOut->OutputString(st->ConOut, string);
}
static int equals(const char *buffer, const char *wanted, UINTN size)
{
    for (UINTN i = 0; i < size; i++)
        if (buffer[i] != wanted[i])
            return 0;
    return 1;
}
EFI_STATUS efi_main(EFI_HANDLE image, EFI_SYSTEM_TABLE *table)
{
    st = table;
    EFI_RUNTIME_SERVICES *rt = st->RuntimeServices;
    EFI_BOOT_SERVICES *bs = st->BootServices;
    char buffer[32];
    UINTN size = sizeof(buffer);
    EFI_STATUS status;
    (void)status;
    (void)boot;
    (void)transient;
    (void)equals;
#if VARIABLE_KIND == 0
    if (rt->SetVariable(name, &guid, 7, 6, "before") ||
        rt->SetVariable(boot, &guid, 3, 6, "hidden") ||
        rt->SetVariable(transient, &guid, 6, 4, "lost"))
        return EFI_LOAD_ERROR;
    text("PASS: variable boot writes committed\r\n");
#elif VARIABLE_KIND == 1
    if (rt->GetVariable(name, &guid, 0, &size, buffer) || size != 8 ||
        !equals(buffer, "runtime!", 8))
        return EFI_LOAD_ERROR;
    size = sizeof(buffer);
    if (rt->GetVariable(transient, &guid, 0, &size, buffer) != EFI_NOT_FOUND)
        return EFI_LOAD_ERROR;
    size = sizeof(buffer);
    if (rt->GetVariable(boot, &guid, 0, &size, buffer) || size != 6)
        return EFI_LOAD_ERROR;
    text("PASS: persistent variables restored; volatile data discarded\r\n");
#elif VARIABLE_KIND == 2
    if (rt->SetVariable(name, &guid, 0, 0, 0) || rt->SetVariable(boot, &guid, 0, 0, 0))
        return EFI_LOAD_ERROR;
    text("PASS: variable deletions committed\r\n");
#elif VARIABLE_KIND == 3
    status = rt->SetVariable(name, &guid, 7, 6, "before");
    if (status != EFI_DEVICE_ERROR ||
        rt->GetVariable(name, &guid, 0, &size, buffer) != EFI_NOT_FOUND)
        return EFI_LOAD_ERROR;
    text("PASS: failed create leaves variable store unchanged\r\n");
#elif VARIABLE_KIND == 4
    if (rt->GetVariable(name, &guid, 0, &size, buffer) != EFI_NOT_FOUND)
        return EFI_LOAD_ERROR;
    text("PASS: empty or rejected store exposes no stale variables\r\n");
#elif VARIABLE_KIND == 5
    if (rt->GetVariable(name, &guid, 0, &size, buffer) || size != 8 ||
        !equals(buffer, "runtime!", 8))
        return EFI_LOAD_ERROR;
    text("VARIABLE MUTATION READY\r\n");
    UINTN index;
    EFI_INPUT_KEY key;
    if (bs->WaitForEvent(1, &st->ConIn->WaitForKey, &index) ||
        st->ConIn->ReadKeyStroke(st->ConIn, &key))
        return EFI_LOAD_ERROR;
    status = rt->SetVariable(name, &guid, 7, 7, "changed");
    size = sizeof(buffer);
    if (status != EFI_DEVICE_ERROR || rt->GetVariable(name, &guid, 0, &size, buffer) || size != 8 ||
        !equals(buffer, "runtime!", 8))
        return EFI_LOAD_ERROR;
    text("PASS: failed update preserves previous variable value\r\n");
#endif
    text("VARIABLE TEST COMPLETE\r\n");
    EFI_MEMORY_DESCRIPTOR map[128];
    UINTN bytes = sizeof(map), map_key, ds;
    uint32_t version;
    if (bs->GetMemoryMap(&bytes, map, &map_key, &ds, &version) ||
        bs->ExitBootServices(image, map_key))
        return EFI_LOAD_ERROR;
#if VARIABLE_KIND == 0
    if (rt->SetVariable(name, &guid, 7, 7, "runtime") ||
        rt->SetVariable(name, &guid, 7 | EFI_VARIABLE_APPEND_WRITE, 1, "!"))
        return EFI_LOAD_ERROR;
    size = sizeof(buffer);
    if (rt->GetVariable(name, &guid, 0, &size, buffer) || size != 8 ||
        !equals(buffer, "runtime!", 8))
        return EFI_LOAD_ERROR;
    size = sizeof(buffer);
    if (rt->GetVariable(boot, &guid, 0, &size, buffer) != EFI_NOT_FOUND)
        return EFI_LOAD_ERROR;
    if (rt->SetVariable(transient, &guid, 6, 1, "x") != EFI_WRITE_PROTECTED)
        return EFI_LOAD_ERROR;
#elif VARIABLE_KIND == 1 || VARIABLE_KIND == 5
    size = sizeof(buffer);
    if (rt->GetVariable(name, &guid, 0, &size, buffer) || size != 8 ||
        !equals(buffer, "runtime!", 8))
        return EFI_LOAD_ERROR;
#elif VARIABLE_KIND == 2 || VARIABLE_KIND == 3 || VARIABLE_KIND == 4
    size = sizeof(buffer);
    if (rt->GetVariable(name, &guid, 0, &size, buffer) != EFI_NOT_FOUND)
        return EFI_LOAD_ERROR;
#endif
    rt->ResetSystem(2, EFI_SUCCESS, 0, 0);
    return EFI_LOAD_ERROR;
}
