#include "device-path.h"
#include "storage.h"
extern const uint8_t demo_pe_start[], demo_pe_end[];
static int equal(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}
static EFI_STATUS boot_demo(int from_disk)
{
    EFI_HANDLE h = 0;
    puts_serial("Loading Alpha64 PE32+ EFI application\n");
    const uint8_t *source = external_image ? external_image : demo_pe_start;
    UINTN len = external_image ? external_image_size : (UINTN)(demo_pe_end - demo_pe_start);
    if (external_image)
        puts_serial("Using external EFI image from QEMU -initrd\n");
    void *disk_buffer = 0;
    EFI_HANDLE device = 0;
    EFI_STATUS s = EFI_SUCCESS;
    if (from_disk) {
        puts_serial("Booting \\EFI\\BOOT\\BOOTAXP64.EFI from disk\n");
        s = storage_boot_image(&disk_buffer, &len, &device);
        source = disk_buffer;
    }
    void *path = 0;
    if (!s && from_disk)
        s = device_path_file(u"\\EFI\\BOOT\\BOOTAXP64.EFI", &path);
    if (!s)
        s = boot_services.LoadImage(0, 0, path, (void *)source, len, &h);
    if (path)
        boot_services.FreePool(path);
    if (disk_buffer)
        boot_services.FreePool(disk_buffer);
    if (!s)
        image_set_device(h, device);
    if (!s)
        s = boot_services.StartImage(h, 0, 0);
    puts_serial("EFI application returned: ");
    hex_serial(s);
    puts_serial("\n");
    if (boot_active && h) {
        EFI_STATUS unloaded = boot_services.UnloadImage(h);
        if (unloaded)
            return unloaded;
        puts_serial("EFI image unloaded\n");
    }
    return s;
}
static void monitor(void)
{
    char line[64];
    UINTN n = 0;
    puts_serial("Alpha firmware monitor. Commands: help, memmap, boot, disk, shutdown\nalpha> ");
    for (;;) {
        int c = getchar_serial();
        if (c < 0)
            continue;
        if (c == '\r' || c == '\n') {
            line[n] = 0;
            puts_serial("\n");
            if (equal(line, "boot"))
                boot_demo(0);
            else if (equal(line, "disk"))
                boot_demo(1);
            else if (equal(line, "memmap"))
                print_map();
            else if (equal(line, "shutdown"))
                halt();
            else if (equal(line, "help"))
                puts_serial("help memmap boot shutdown (disk: boot from GPT/FAT32)\n");
            else if (n)
                puts_serial("Unknown command\n");
            n = 0;
            puts_serial("alpha> ");
        } else if (c == 8 || c == 127) {
            if (n) {
                n--;
                puts_serial("\b \b");
            }
        } else if (c >= 32 && c < 127 && n < sizeof(line) - 1) {
            line[n++] = c;
            putchar_serial(c);
        }
    }
}
void firmware_main(void)
{
    serial_init();
    puts_serial("\nAlpha EFI experimental firmware v0.1\nQEMU Clipper / Alpha AXP EV67 / native "
                "64-bit ABI\n");
    services_init();
    const char *cmd = (const char *)(ALPHA_KSEG + 0x200000 - 0x6000);
    if (equal(cmd, "shell"))
        monitor();
    EFI_STATUS s = boot_demo(equal(cmd, "disk"));
    puts_serial(s ? "BOOT FAILED\n" : "EFI application finished\n");
    halt();
}
