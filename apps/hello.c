#include "efi.h"
/* This entry point uses the native Alpha calling convention from entry.S. */
static UINTN initialized = 7;
static volatile UINTN zeroed;
EFI_STATUS efi_main(EFI_HANDLE image, EFI_SYSTEM_TABLE *table)
{
    (void)image;
    if (initialized != 7 || zeroed != 0)
        return EFI_LOAD_ERROR;
    zeroed = 1;
    static const CHAR16 message[] = {'H', 'e', 'l', 'l', 'o', ' ', 'f', 'r', 'o', 'm',  ' ',  'A',
                                     'l', 'p', 'h', 'a', ' ', 'E', 'F', 'I', '!', '\r', '\n', 0};
    return table->ConOut->OutputString(table->ConOut, message);
}
