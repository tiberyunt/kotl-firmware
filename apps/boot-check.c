/* Boot manager and device-path guest assertions. SPDX-License-Identifier: MIT */
#include "efi-device-path.h"
#include "efi-storage.h"
static EFI_SYSTEM_TABLE *st;
static void check(int ok, const CHAR16 *name)
{
    st->ConOut->OutputString(st->ConOut, ok ? u"PASS: " : u"FAIL: ");
    st->ConOut->OutputString(st->ConOut, name);
    st->ConOut->OutputString(st->ConOut, u"\r\n");
    if (!ok)
        st->RuntimeServices->ResetSystem(2, EFI_LOAD_ERROR, 0, 0);
}
EFI_STATUS efi_main(EFI_HANDLE image, EFI_SYSTEM_TABLE *table)
{
    st = table;
    EFI_GUID global = EFI_GLOBAL_VARIABLE_GUID, loaded_guid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
    EFI_GUID dp_guid = EFI_LOADED_IMAGE_DEVICE_PATH_PROTOCOL_GUID,
             fs_guid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
    EFI_LOADED_IMAGE_PROTOCOL *loaded = 0;
    void *full = 0;
    UINTN size = 2;
    uint16_t current = 0;
    uint32_t attributes = 0;
    check(st->RuntimeServices->GetVariable(u"BootCurrent", &global, &attributes, &size, &current) ==
                  0 &&
              current == 2 && attributes == 6,
          u"BootCurrent identifies the running boot option");
    size = 2;
    check(st->RuntimeServices->GetVariable(u"BootNext", &global, 0, &size, &current) ==
              EFI_NOT_FOUND,
          u"BootNext was deleted before execution");
    check(!st->BootServices->HandleProtocol(image, &loaded_guid, (void **)&loaded) &&
              loaded->LoadOptionsSize == 3 && ((uint8_t *)loaded->LoadOptions)[0] == 0x41 &&
              ((uint8_t *)loaded->LoadOptions)[2] == 0x50,
          u"EFI load option optional data delivered");
    check(!st->BootServices->HandleProtocol(image, &dp_guid, &full) && ((uint8_t *)full)[0] == 4 &&
              ((uint8_t *)full)[1] == 1 && loaded->FilePath &&
              ((uint8_t *)loaded->FilePath)[1] == 4,
          u"loaded image owns full and relative device paths");
    void *tail = full;
    EFI_HANDLE found = 0;
    check(!st->BootServices->LocateDevicePath(&fs_guid, &tail, &found) &&
              found == loaded->DeviceHandle && tail == (uint8_t *)full + 42,
          u"LocateDevicePath resolves partition and advances the path");
    UINTN n = 0;
    check(st->BootServices->LocateHandle(ByProtocol, &fs_guid, 0, &n, 0) == EFI_BUFFER_TOO_SMALL &&
              n >= sizeof(EFI_HANDLE),
          u"LocateHandle supports buffer sizing");
    EFI_HANDLE child = 0;
    /* Reuse the published partition node and build another file path. */
    uint8_t path[128];
    for (unsigned i = 0; i < 42; i++)
        path[i] = ((uint8_t *)full)[i];
    const CHAR16 *name = u"\\EFI\\BOOT\\RETURN.EFI";
    unsigned units = 0;
    while (name[units])
        units++;
    unsigned bytes = 4 + (units + 1) * 2, off = 42;
    path[off++] = 4;
    path[off++] = 4;
    path[off++] = bytes & 255;
    path[off++] = bytes >> 8;
    for (unsigned i = 0; i <= units; i++) {
        path[off++] = name[i] & 255;
        path[off++] = name[i] >> 8;
    }
    path[off++] = 0x7f;
    path[off++] = 0xff;
    path[off++] = 4;
    path[off++] = 0;
    check(!st->BootServices->LoadImage(0, image, path, 0, 0, &child),
          u"LoadImage reads a child through a GPT file device path");
    for (unsigned i = 0; i < off; i++)
        path[i] = 0;
    check(!st->BootServices->StartImage(child, 0, 0) && !st->BootServices->UnloadImage(child),
          u"nested path-loaded image runs after caller path is overwritten");
    path[0] = 4;
    path[1] = 4;
    path[2] = 0;
    path[3] = 0;
    check(st->BootServices->LoadImage(0, image, path, 0, 0, &child) == EFI_INVALID_PARAMETER,
          u"malformed device-path node rejected");
    /* File-only paths resolve against the parent's mounted filesystem. */
    name = u"\\EFI\\BOOT\\EXIT.EFI";
    units = 0;
    while (name[units])
        units++;
    bytes = 4 + (units + 1) * 2;
    off = 0;
    path[off++] = 4;
    path[off++] = 4;
    path[off++] = bytes & 255;
    path[off++] = bytes >> 8;
    for (unsigned i = 0; i <= units; i++) {
        path[off++] = name[i] & 255;
        path[off++] = name[i] >> 8;
    }
    path[off++] = 0x7f;
    path[off++] = 0xff;
    path[off++] = 4;
    path[off++] = 0;
    check(!st->BootServices->LoadImage(0, image, path, 0, 0, &child),
          u"FilePath-only LoadImage resolves on the boot volume");
    UINTN exit_bytes = 0;
    CHAR16 *exit_data = 0;
    check(st->BootServices->StartImage(child, &exit_bytes, &exit_data) == EFI_LOAD_ERROR &&
              exit_bytes == 16 && exit_data && exit_data[0] == 'e' && exit_data[6] == 'k',
          u"EFI Exit unwinds to StartImage with copied status and data");
    check(!st->BootServices->FreePool(exit_data) && !st->BootServices->UnloadImage(child),
          u"Exit data ownership and image cleanup are preserved");
    st->ConOut->OutputString(st->ConOut, u"BOOT OPTION TEST COMPLETE\r\n");
    st->RuntimeServices->ResetSystem(2, EFI_SUCCESS, 0, 0);
    return EFI_SUCCESS;
}
