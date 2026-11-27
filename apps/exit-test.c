/* Exercise EFI Exit from a nested C frame. SPDX-License-Identifier: MIT */
#include "efi.h"
static EFI_STATUS nested(EFI_HANDLE image, EFI_SYSTEM_TABLE *st)
{
    CHAR16 data[] = u"exit-ok";
    EFI_STATUS s = st->BootServices->Exit((void *)1, EFI_LOAD_ERROR, 0, 0);
    if (s != EFI_INVALID_PARAMETER)
        return EFI_LOAD_ERROR;
    s = st->BootServices->Exit(image, EFI_LOAD_ERROR, sizeof data, data);
    (void)s;
    st->ConOut->OutputString(st->ConOut, u"FAIL: Exit returned to the application\r\n");
    return EFI_LOAD_ERROR;
}
EFI_STATUS efi_main(EFI_HANDLE image, EFI_SYSTEM_TABLE *st)
{
    return nested(image, st);
}
