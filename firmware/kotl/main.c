#include "device-path.h"
/* SPDX-License-Identifier: MIT */
#include "kotl.h"
#include "settings.h"
#include "storage.h"
extern const uint8_t demo_pe_start[], demo_pe_end[];
static EFI_STATUS boot(int disk)
{
    void *buffer = 0;
    EFI_HANDLE device = 0, image = 0;
    UINTN size = demo_pe_end - demo_pe_start;
    const void *source = demo_pe_start;
    EFI_STATUS status = EFI_SUCCESS;
    if (disk && boot_use_variables()) {
        EFI_STATUS ordered = boot_options_run();
        if (ordered != EFI_NOT_FOUND)
            return ordered;
    }
    if (disk) {
        puts_serial("Booting \\EFI\\BOOT\\BOOTAXP64.EFI from NVMe\n");
        status = boot_selected(&buffer, &size, &device);
        source = buffer;
    } else
        puts_serial("Booting embedded native EFI self-test\n");
    void *path = 0;
    if (!status && disk)
        status = device_path_file(u"\\EFI\\BOOT\\BOOTAXP64.EFI", &path);
    if (!status)
        status = boot_services.LoadImage(0, 0, path, (void *)source, size, &image);
    if (path)
        boot_services.FreePool(path);
    if (buffer)
        boot_services.FreePool(buffer);
    if (!status) {
        image_set_device(image, device);
        status = boot_services.StartImage(image, 0, 0);
    }
    puts_serial("EFI application returned: ");
    hex_serial(status);
    puts_serial("\n");
    if (image && boot_active) {
        EFI_STATUS unloaded = boot_services.UnloadImage(image);
        if (unloaded)
            return unloaded;
        puts_serial("EFI image unloaded\n");
    }
    return status;
}
static int equal(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}
static void monitor(void)
{
    char line[64];
    unsigned n = 0;
    puts_serial("Kotl EFI monitor. Commands: help, memmap, boot, disk, shutdown\nkotl> ");
    for (;;) {
        int c = getchar_serial();
        if (c < 0)
            continue;
        if (c == '\r' || c == '\n') {
            line[n] = 0;
            puts_serial("\n");
            if (equal(line, "boot"))
                boot(0);
            else if (equal(line, "disk"))
                boot(1);
            else if (equal(line, "memmap"))
                print_map();
            else if (equal(line, "shutdown"))
                halt();
            else if (equal(line, "help"))
                puts_serial("help memmap boot disk shutdown\n");
            else if (n)
                puts_serial("Unknown command\n");
            n = 0;
            puts_serial("kotl> ");
        } else if (c == 8 || c == 127) {
            if (n) {
                n--;
                puts_serial("\b \b");
            }
        } else if (c >= 32 && c < 127 && n + 1 < sizeof(line)) {
            line[n++] = c;
            putchar_serial(c);
        }
    }
}
void firmware_main(void)
{
    serial_init();
    puts_serial("Kotl native EFI experimental firmware\n");
    if (KOTL_READ_CSR(0) != 0x102 || !native_boot_info ||
        native_boot_info->magic != KOTL_BOOT_MAGIC)
        kotl_fatal();
    KSI_RESULT version = ksi_call(KSI_VERSION, 0);
    if (version.error || version.value != 0x10000 || ksi_call(KSI_PROBE, KSI_TIME).value != 1)
        kotl_fatal();
    puts_serial("PASS: K-level native firmware and resident KSI services\n");
    services_init();
    settings_load();
    display_init();
    if (native_boot_info->boot_mode == 0) {
        boot_manager();
        display_console(firmware_settings.graphics);
        if (!firmware_settings.graphics)
            puts_serial("Graphical text console: disabled; serial remains active\n");
    }
    if (native_boot_info->boot_mode == 2)
        monitor();
    EFI_STATUS status = boot(native_boot_info->boot_mode != 1);
    puts_serial(status ? "BOOT FAILED\n" : "EFI application finished\n");
    halt();
}
