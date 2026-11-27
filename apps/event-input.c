/* SPDX-License-Identifier: MIT */
#include "efi.h"
#ifndef INPUT_KIND
#define INPUT_KIND 0
#endif
static EFI_SYSTEM_TABLE *table;
static void text(const char *message)
{
    CHAR16 string[100];
    unsigned i = 0;
    while (message[i] && i < 99) {
        string[i] = (uint8_t)message[i];
        i++;
    }
    string[i] = 0;
    table->ConOut->OutputString(table->ConOut, string);
}
#if INPUT_KIND == 4
static void abandoned_callback(EFI_EVENT event, void *context)
{
    (void)event;
    (void)context;
    text("FAIL: unloaded image event callback executed\r\n");
}
#endif
EFI_STATUS efi_main(EFI_HANDLE image, EFI_SYSTEM_TABLE *system)
{
    (void)image;
    table = system;
    EFI_BOOT_SERVICES *bs = table->BootServices;
    EFI_EVENT timer = 0;
#if INPUT_KIND == 4
    if (bs->CreateEvent(EVT_TIMER | EVT_NOTIFY_SIGNAL, TPL_CALLBACK, abandoned_callback, 0,
                        &timer) ||
        bs->SetTimer(timer, TimerPeriodic, 10000))
        return EFI_OUT_OF_RESOURCES;
    text("EVENT LEAK IMAGE RETURNED\r\n");
    return EFI_SUCCESS;
#else
    if (!table->ConIn->WaitForKey || bs->CreateEvent(EVT_TIMER, 0, 0, 0, &timer))
        return EFI_LOAD_ERROR;
    if (bs->SetTimer(timer, TimerRelative, INPUT_KIND ? 50000000 : 100000))
        return EFI_LOAD_ERROR;
    text("EVENT INPUT READY\r\n");
    EFI_EVENT events[] = {table->ConIn->WaitForKey, timer};
    UINTN index;
    if (bs->WaitForEvent(2, events, &index))
        return EFI_LOAD_ERROR;
#if INPUT_KIND == 0
    if (index != 1)
        return EFI_LOAD_ERROR;
    text("PASS: event timeout selected\r\n");
#else
    if (index != 0)
        return EFI_LOAD_ERROR;
    /* Rechecking readiness must preserve the unread keystroke. */
    if (bs->CheckEvent(table->ConIn->WaitForKey))
        return EFI_LOAD_ERROR;
    EFI_INPUT_KEY key;
    if (table->ConIn->ReadKeyStroke(table->ConIn, &key))
        return EFI_LOAD_ERROR;
#if INPUT_KIND == 2
    if (key.ScanCode != 0x14 || key.UnicodeChar)
        return EFI_LOAD_ERROR;
    text("PASS: WaitForKey delivers EFI F10 scan code\r\n");
#else
    if (key.ScanCode || key.UnicodeChar != 'x')
        return EFI_LOAD_ERROR;
    text("PASS: WaitForKey preserves serial input\r\n");
#endif
#if INPUT_KIND == 3
    if (bs->WaitForEvent(2, events, &index) || index ||
        table->ConIn->ReadKeyStroke(table->ConIn, &key) || key.ScanCode || key.UnicodeChar != 'y')
        return EFI_LOAD_ERROR;
    text("PASS: consecutive queued keys are preserved\r\n");
#endif
    if (bs->CheckEvent(table->ConIn->WaitForKey) != EFI_NOT_READY)
        return EFI_LOAD_ERROR;
    if (bs->SetTimer(timer, TimerCancel, 0))
        return EFI_LOAD_ERROR;
#endif
    if (bs->CloseEvent(timer))
        return EFI_LOAD_ERROR;
    text("EVENT INPUT TEST COMPLETE\r\n");
    EFI_MEMORY_DESCRIPTOR map[128];
    UINTN size = sizeof(map), map_key, ds;
    uint32_t version;
    if (bs->GetMemoryMap(&size, map, &map_key, &ds, &version) || bs->ExitBootServices(image, map_key))
        return EFI_LOAD_ERROR;
    table->RuntimeServices->ResetSystem(2, EFI_SUCCESS, 0, 0);
    return EFI_LOAD_ERROR;
#endif
}
