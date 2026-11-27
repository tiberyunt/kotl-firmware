/* Persistent boot-variable fixture, through real EFI services. SPDX-License-Identifier: MIT */
#include "efi-device-path.h"
#include "efi-storage.h"
EFI_STATUS efi_main(EFI_HANDLE image, EFI_SYSTEM_TABLE *st)
{
    EFI_GUID loaded_guid = EFI_LOADED_IMAGE_PROTOCOL_GUID,
             path_guid = EFI_DEVICE_PATH_PROTOCOL_GUID;
    EFI_GUID global = EFI_GLOBAL_VARIABLE_GUID;
    EFI_LOADED_IMAGE_PROTOCOL *loaded;
    void *device_path;
    EFI_BOOT_SERVICES *bs = st->BootServices;
    if (bs->HandleProtocol(image, &loaded_guid, (void **)&loaded) ||
        bs->HandleProtocol(loaded->DeviceHandle, &path_guid, &device_path))
        return EFI_LOAD_ERROR;
    for (unsigned id = 1; id <= 2; id++) {
        uint8_t option[256] = {1, 0, 0, 0};
        UINTN off = 6;
        const CHAR16 *description = u"Alpha boot test";
        for (UINTN i = 0;; i++) {
            option[off++] = description[i] & 255;
            option[off++] = description[i] >> 8;
            if (!description[i])
                break;
        }
        UINTN start = off;
        for (unsigned i = 0; i < 42; i++)
            option[off++] = ((uint8_t *)device_path)[i];
        const CHAR16 *file = id == 1 ? u"\\EFI\\BOOT\\RETURN.EFI" : u"\\EFI\\BOOT\\CHECK.EFI";
        UINTN units = 0;
        while (file[units])
            units++;
        UINTN bytes = 4 + (units + 1) * 2;
        option[off++] = 4;
        option[off++] = 4;
        option[off++] = bytes & 255;
        option[off++] = bytes >> 8;
        for (UINTN i = 0; i <= units; i++) {
            option[off++] = file[i] & 255;
            option[off++] = file[i] >> 8;
        }
        option[off++] = 0x7f;
        option[off++] = 0xff;
        option[off++] = 4;
        option[off++] = 0;
        UINTN path_bytes = off - start;
        option[4] = path_bytes & 255;
        option[5] = path_bytes >> 8;
        option[off++] = 0x41;
        option[off++] = 0x58;
        option[off++] = 0x50;
        if (st->RuntimeServices->SetVariable(id == 1 ? u"Boot0001" : u"Boot0002", &global, 7, off,
                                             option))
            return EFI_DEVICE_ERROR;
    }
    uint16_t order[] = {2}, next = 1;
    if (st->RuntimeServices->SetVariable(u"BootOrder", &global, 7, sizeof order, order) ||
        st->RuntimeServices->SetVariable(u"BootNext", &global, 7, 2, &next))
        return EFI_DEVICE_ERROR;
    st->ConOut->OutputString(st->ConOut, u"PASS: standard boot variables persisted\r\n");
    return EFI_SUCCESS;
}
