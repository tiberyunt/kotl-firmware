#include "efi.h"
EFI_STATUS efi_main(EFI_HANDLE image, EFI_SYSTEM_TABLE *table)
{
    static const CHAR16 message[] = {'K', 'O', 'T', 'L', ' ', 'R', 'E',  'S',  'E',
                                     'T', ' ', 'T', 'E', 'S', 'T', '\r', '\n', 0};
    table->ConOut->OutputString(table->ConOut, message);
    EFI_MEMORY_DESCRIPTOR map[128];
    UINTN size = sizeof(map), key, ds;
    uint32_t version;
    EFI_STATUS status = table->BootServices->GetMemoryMap(&size, map, &key, &ds, &version);
    if (status)
        return status;
    status = table->BootServices->ExitBootServices(image, key);
    if (status)
        return status;
    table->RuntimeServices->ResetSystem(0, EFI_SUCCESS, 0, 0);
    return EFI_LOAD_ERROR;
}
