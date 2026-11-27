/* SPDX-License-Identifier: MIT */
#include "efi.h"
UINTN reloc_datum = 0x13579bdf2468ace0ULL;
volatile uint32_t reloc_low32 = 0x50505050;
static volatile UINTN zeroed;
/* This address-shaped integer has no ELF relocation and must remain unchanged. */
static volatile UINTN constant = ALPHA_KSEG + EFI_RAM_BASE + 0x1001000;
static uint8_t *volatile interior = (uint8_t *)&reloc_datum + 3;
static UINTN *volatile pointer = &reloc_datum;
static EFI_STATUS probe(void)
{
    return reloc_datum == 0x13579bdf2468ace0ULL ? 0 : EFI_LOAD_ERROR;
}
static EFI_STATUS (*volatile function_pointer)(void) = probe;
static EFI_SYSTEM_TABLE *child_table;
static unsigned notifications;
static void relocated_notify(EFI_EVENT event, void *context)
{
    (void)event;
    (void)context;
    if (pointer == &reloc_datum && *pointer == 0x13579bdf2468ace0ULL)
        notifications++;
}
static const CHAR16 bad_message[] = {'F', 'A', 'I', 'L', ':', ' ', 'u', 'n',  'l',  'o', 'a',
                                     'd', 'e', 'd', ' ', 'c', 'h', 'i', 'l',  'd',  ' ', 'c',
                                     'a', 'l', 'l', 'b', 'a', 'c', 'k', '\r', '\n', 0};
static void abandoned(EFI_EVENT event, void *context)
{
    (void)event;
    (void)context;
    child_table->ConOut->OutputString(child_table->ConOut, bad_message);
}
static struct __attribute__((packed)) {
    uint8_t tag;
    UINTN *pointer;
} volatile packed = {0x5a, &reloc_datum};
extern struct __attribute__((packed)) {
    UINTN *value;
} reloc_cross_pointer;
static const CHAR16 message[] = {'P', 'A', 'S', 'S', ':', ' ', 'r', 'e', 'l',  'o',  'c', 'a',
                                 't', 'e', 'd', ' ', 'A', 'l', 'p', 'h', 'a',  ' ',  'p', 'o',
                                 'i', 'n', 't', 'e', 'r', 's', ',', ' ', 'G',  'O',  'T', ',',
                                 ' ', 'a', 'n', 'd', ' ', 'B', 'S', 'S', '\r', '\n', 0};
static const CHAR16 *volatile message_pointer = message;
EFI_STATUS efi_main(EFI_HANDLE image, EFI_SYSTEM_TABLE *table)
{
    if (constant != ALPHA_KSEG + EFI_RAM_BASE + 0x1001000 ||
        interior != (uint8_t *)&reloc_datum + 3)
        return EFI_LOAD_ERROR;
    if (zeroed || pointer != &reloc_datum || packed.tag != 0x5a || packed.pointer != &reloc_datum ||
        reloc_cross_pointer.value != &reloc_datum || message_pointer != message ||
        function_pointer())
        return EFI_LOAD_ERROR;
    if (reloc_low32 != 0x50505050 && reloc_low32 != (uint32_t)(UINTN)&reloc_datum)
        return EFI_LOAD_ERROR;
    EFI_GUID guid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
    EFI_LOADED_IMAGE_PROTOCOL *loaded = 0;
    if (table->BootServices->HandleProtocol(image, &guid, (void **)&loaded) ||
        (UINTN)&reloc_datum < (UINTN)loaded->ImageBase ||
        (UINTN)&reloc_datum - (UINTN)loaded->ImageBase >= loaded->ImageSize)
        return EFI_LOAD_ERROR;
    if (table->BootServices->UnloadImage(image) != EFI_INVALID_PARAMETER)
        return EFI_LOAD_ERROR;
    if (loaded->ParentHandle &&
        table->BootServices->UnloadImage(loaded->ParentHandle) != EFI_INVALID_PARAMETER)
        return EFI_LOAD_ERROR;
    child_table = table;
    EFI_EVENT notified;
    if (table->BootServices->CreateEvent(EVT_NOTIFY_SIGNAL, TPL_CALLBACK, relocated_notify, 0,
                                         &notified) ||
        table->BootServices->SignalEvent(notified) || notifications != 1 ||
        table->BootServices->CloseEvent(notified))
        return EFI_LOAD_ERROR;
    EFI_EVENT timer;
    if (table->BootServices->CreateEvent(EVT_TIMER | EVT_NOTIFY_SIGNAL, TPL_CALLBACK, abandoned, 0,
                                         &timer) ||
        table->BootServices->SetTimer(timer, TimerRelative, 10000))
        return EFI_LOAD_ERROR;
    zeroed = 1;
    reloc_datum = 0;
    return table->ConOut->OutputString(table->ConOut, message_pointer);
}
