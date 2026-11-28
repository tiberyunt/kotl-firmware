/* SPDX-License-Identifier: MIT */
#include "device-path.h"
#include "kotl.h"
#include "menu.h"
#include "settings.h"
#include "storage.h"
#define MAX_BOOT 32
static struct {
    unsigned bdf, partition, controller_number;
    uint32_t nsid;
} entries[MAX_BOOT];
static unsigned count, selected;
static int discovered;
static int override;
int boot_use_variables(void)
{
    return !override;
}
static EFI_STATUS last_status;
static void decimal(unsigned value)
{
    char digits[12];
    unsigned n = 0;
    do {
        digits[n++] = '0' + value % 10;
        value /= 10;
    } while (value);
    while (n)
        putchar_serial(digits[--n]);
}
static EFI_STATUS select_entry(unsigned index)
{
    EFI_STATUS s = nvme_select(entries[index].bdf, entries[index].nsid);
    if (!s)
        s = storage_select_partition(entries[index].partition);
    return s;
}
static void discover(void)
{
    count = 0;
    unsigned controller_number = 0;
    last_status = EFI_NOT_FOUND;
    puts_serial("Scanning NVMe namespaces and GPT EFI System Partitions...\n");
    for (unsigned bdf = 8; bdf < 256; bdf++) {
        if ((pci_read32(bdf, 8) >> 8) != 0x010802)
            continue;
        controller_number++;
        EFI_STATUS s = nvme_select(bdf, 0);
        uint32_t namespaces[1024];
        unsigned n = nvme_namespaces(namespaces, 1024);
        if (s)
            last_status = s;
        for (unsigned ns = 0; ns < n; ns++) {
            s = nvme_select(bdf, namespaces[ns]);
            if (s) {
                last_status = s;
                continue;
            }
            unsigned partitions = 1;
            for (unsigned partition = 0; partition < partitions; partition++) {
                s = storage_select_partition(partition);
                void *buffer = 0;
                UINTN size = 0;
                EFI_HANDLE device = 0, image = 0;
                if (!s)
                    s = storage_boot_image(&buffer, &size, &device);
                unsigned found = storage_partition_count();
                if (found > partitions)
                    partitions = found;
                if (!s)
                    s = boot_services.LoadImage(0, 0, 0, buffer, size, &image);
                if (buffer)
                    boot_services.FreePool(buffer);
                if (image)
                    boot_services.UnloadImage(image);
                if (s) {
                    last_status = s;
                    continue;
                }
                if (count == MAX_BOOT) {
                    puts_serial("Boot candidate limit reached (32)\n");
                    continue;
                }
                entries[count].bdf = bdf;
                entries[count].controller_number = controller_number;
                entries[count].nsid = namespaces[ns];
                entries[count++].partition = partition;
            }
        }
    }
    selected = 0;
    discovered = 1;
    puts_serial("Detected ");
    decimal(count);
    puts_serial(" bootable EFI partition(s)\n");
}
#define KEY_UP (1U << 16)
#define KEY_DOWN (2U << 16)
#define KEY_RIGHT (3U << 16)
#define KEY_LEFT (4U << 16)
#define KEY_F9 (0x13U << 16)
#define KEY_F10 (0x14U << 16)
#define KEY_F12 (0x16U << 16)
#define KEY_ESC (0x17U << 16)
/* Bound escape decoding, including standalone Esc and serial F9/F10/F12. */
static uint32_t menu_key(void)
{
    static unsigned state, number;
    static uint64_t started;
    uint32_t key = platform_key();
    if (!key) {
        if (state && time_ns() - started > 50000000) {
            unsigned old = state;
            state = 0;
            return old == 1 ? KEY_ESC : 0;
        }
        return 0;
    }
    if (key >> 16) {
        state = 0;
        return key;
    }
    if (key == 27) {
        state = 1;
        number = 0;
        started = time_ns();
        return 0;
    }
    if (state == 1) {
        if (key == '[') {
            state = 2;
            return 0;
        }
        if (key == 'O') {
            state = 3;
            return 0;
        }
        state = 0;
        return KEY_ESC;
    }
    if (state == 2) {
        if (key >= '0' && key <= '9') {
            if (number > 99)
                state = 0;
            else
                number = number * 10 + key - '0';
            return 0;
        }
        state = 0;
        if (key == '~')
            return number == 20 ? KEY_F9 : number == 21 ? KEY_F10 : number == 24 ? KEY_F12 : 0;
        return key == 'A'   ? KEY_UP
               : key == 'B' ? KEY_DOWN
               : key == 'C' ? KEY_RIGHT
               : key == 'D' ? KEY_LEFT
                            : 0;
    }
    if (state == 3) {
        state = 0;
        return 0;
    }
    return key;
}
static void format_number(char *text, unsigned value)
{
    char reverse[12];
    unsigned n = 0;
    do {
        reverse[n++] = '0' + value % 10;
        value /= 10;
    } while (value);
    unsigned i = 0;
    while (n)
        text[i++] = reverse[--n];
    text[i] = 0;
}
static unsigned preferred(const FIRMWARE_SETTINGS *settings)
{
    for (unsigned i = 0; i < count; i++)
        if (entries[i].bdf == settings->preferred_bdf &&
            entries[i].nsid == settings->preferred_nsid &&
            entries[i].partition == settings->preferred_partition)
            return i;
    return 0;
}
static void set_preferred(FIRMWARE_SETTINGS *settings, unsigned i)
{
    if (i >= count)
        return;
    settings->preferred_bdf = entries[i].bdf;
    settings->preferred_nsid = entries[i].nsid;
    settings->preferred_partition = entries[i].partition;
}
static void candidate_label(char *text, unsigned i)
{
    /* Maximum numeric values fit this 48-byte label. */
    char number[12];
    unsigned n = 0;
    const char *p = "NVMe ";
    while (*p)
        text[n++] = *p++;
    format_number(number, entries[i].controller_number);
    p = number;
    while (*p)
        text[n++] = *p++;
    p = " / NS ";
    while (*p)
        text[n++] = *p++;
    format_number(number, entries[i].nsid);
    p = number;
    while (*p)
        text[n++] = *p++;
    p = " / ESP ";
    while (*p)
        text[n++] = *p++;
    format_number(number, entries[i].partition + 1);
    p = number;
    while (*p)
        text[n++] = *p++;
    text[n] = 0;
}
static void show_menu(void)
{
    menu_frame("                         KOTL EV9 - BOOT MENU (F12)", 4,
               " One-time selection. F10 opens firmware setup; S opens the monitor.");
    menu_help("Select an EFI loader.", "Enter boots this entry.", "R rescans storage.",
              "Order is not changed.");
    unsigned first = (selected / 7) * 7;
    for (unsigned i = first; i < count && i < first + 7; i++) {
        char label[48];
        candidate_label(label, i);
        unsigned y = 5 + (i - first) * 2;
        menu_text(2, y, i == selected ? UI_WHITE : UI_MUTED, i == selected ? UI_SELECTED : UI_BACKGROUND,
                  label, 49);
        menu_text(4, y + 1, UI_MUTED, UI_BACKGROUND, "\\EFI\\BOOT\\BOOTAXP64.EFI", 47);
    }
    if (!count) {
        menu_text(2, 6, UI_MUTED, UI_BACKGROUND, "No bootable EFI partitions.", 49);
        menu_text(2, 8, UI_MUTED, UI_BACKGROUND, "Attach a GPT/FAT32 NVMe disk, then press R.", 49);
    }
    menu_text(0, 23, UI_WHITE, UI_BACKGROUND, " Up/Down: Select   Enter: Boot   R: Rescan   S: Monitor",
              80);
    menu_text(0, 24, UI_WHITE, UI_BACKGROUND, " Enter: Boot   R: Rescan   F10: Setup   Esc: Default Boot",
              80);
    puts_serial("\nboot-settings> ");
}
static void setup_screen(unsigned tab, unsigned row, const FIRMWARE_SETTINGS *edit,
                         const char *status)
{
    int dirty = memcmp(edit, &firmware_settings, sizeof(*edit)) != 0;
    menu_frame("                         KOTL EV9 FIRMWARE SETUP (F10)", tab,
               status  ? status
               : dirty ? " Changes pending. F10 saves and exits; Esc discards them."
                       : " Configuration is saved to the firmware settings store.");
    char number[12], label[48];
    if (tab == 0) {
        menu_field(5, "Firmware", "Kotl EFI prototype", 0);
        menu_field(7, "Processor", "Alpha EV9", 0);
        format_number(number, *(volatile uint64_t *)(native_boot_info->syscon + 16));
        menu_field(9, "Hardware threads", number, 0);
        format_number(number, native_boot_info->ram_size / (1024 * 1024));
        menu_field(11, "Memory (MiB)", number, 0);
        menu_field(13, "Settings storage",
                   settings_persistent() ? "File backed" : "Until QEMU exits", 0);
        format_number(number, count);
        menu_field(15, "Bootable EFI partitions", number, 0);
        menu_help("System information.", "Use Left/Right to", "open another tab.",
                  "F12 is one-time boot.");
    } else if (tab == 1) {
        if (count)
            candidate_label(label, preferred(edit));
        else {
            const char *p = "No bootable media";
            unsigned i = 0;
            do {
                label[i] = p[i];
            } while (p[i++]);
        }
        menu_field(6, "Default boot entry", label, row == 0);
        format_number(number, edit->timeout);
        menu_field(9, "Startup delay (seconds)", number, row == 1);
        menu_field(12, "Rescan EFI partitions", "[ Enter ]", row == 2);
        if (row == 0)
            menu_help("Enter cycles boot", "entries. Saved order", "is used at startup.",
                      "F12 overrides once.");
        else if (row == 1)
            menu_help("Enter cycles 2, 5, 10", "seconds. F10 and F12", "are accepted during",
                      "the startup prompt.");
        else
            menu_help("Search NVMe disks and", "namespaces for valid", "Alpha EFI loaders in",
                      "GPT FAT32 partitions.");
    } else if (tab == 2) {
        menu_field(6, "Graphical text console", edit->graphics ? "Enabled" : "Disabled", row == 0);
        menu_field(9, "Graphics mode", "1024 x 768 / 32 bit", 0);
        menu_field(12, "Serial console", "Always available", 0);
        menu_help("Enter toggles graphics", "text after setup.", "Serial remains active.",
                  "EFI GOP stays present.");
    } else {
        static const char *actions[] = {"Save Changes and Boot", "Discard Changes and Boot",
                                        "Load Setup Defaults", "Open Serial Monitor", "Shut Down"};
        for (unsigned i = 0; i < 5; i++)
            menu_field(5 + i * 3, actions[i], "[ Enter ]", row == i);
        menu_help("Save applies settings", "and starts the default", "EFI entry. Discard",
                  "keeps saved settings.");
    }
    puts_serial("\nfirmware-setup> ");
}
static EFI_STATUS save_setup(const FIRMWARE_SETTINGS *edit)
{
    FIRMWARE_SETTINGS previous = firmware_settings;
    EFI_STATUS s = settings_save(edit);
    if (s)
        return s;
    int changed = edit->preferred_bdf != previous.preferred_bdf ||
                  edit->preferred_nsid != previous.preferred_nsid ||
                  edit->preferred_partition != previous.preferred_partition;
    if (!changed || !count)
        return EFI_SUCCESS;
    s = select_entry(preferred(edit));
    if (!s)
        s = storage_mount();
    void *file = 0;
    uint8_t path[128];
    if (!s)
        s = device_path_file(u"\\EFI\\BOOT\\BOOTAXP64.EFI", &file);
    if (!s) {
        UINTN n = 0;
        s = device_path_validate(file, EFI_DP_MAX, &n, 0);
        if (!s && n + 42 <= sizeof path) {
            memcpy(path, partition_path.bytes, 42);
            memcpy(path + 42, file, n);
            s = boot_options_set_default(path, u"Kotl EFI default loader");
        } else if (!s)
            s = EFI_OUT_OF_RESOURCES;
    }
    if (file)
        boot_services.FreePool(file);
    if (s) {
        EFI_STATUS rollback = settings_save(&previous);
        puts_serial(rollback ? "Setup rollback failed\n"
                             : "Standard boot selection failed; setup rolled back\n");
    }
    return s;
}
static void firmware_setup(void)
{
    FIRMWARE_SETTINGS edit = firmware_settings;
    unsigned tab = 0, row = 0;
    const char *status = 0;
    setup_screen(tab, row, &edit, status);
    for (;;) {
        uint32_t key = menu_key();
        if (!key)
            continue;
        status = 0;
        if (key == KEY_F9 || (tab == 3 && row == 2 && key == 13)) {
            settings_defaults(&edit);
            status = " Defaults loaded. F10 saves; Esc cancels.";
        } else if (key == KEY_F10 || (tab == 3 && row == 0 && key == 13)) {
            EFI_STATUS result = save_setup(&edit);
            if (!result) {
                puts_serial("\nFirmware settings saved\n");
                selected = preferred(&firmware_settings);
                break;
            }
            status = " Save failed. Saved settings are unchanged. Retry or Esc to discard.";
            puts_serial("\nFirmware settings save failed\n");
        } else if (key == KEY_ESC || (tab == 3 && row == 1 && key == 13)) {
            selected = preferred(&firmware_settings);
            puts_serial("\nFirmware settings discarded\n");
            break;
        } else if (tab == 3 && row == 3 && key == 13) {
            native_boot_info->boot_mode = 2;
            break;
        } else if (tab == 3 && row == 4 && key == 13)
            halt();
        else if (key == KEY_LEFT || key == KEY_RIGHT || key == 9) {
            tab = (tab + (key == KEY_LEFT ? 3 : 1)) % 4;
            row = 0;
        } else if (key == KEY_UP || key == KEY_DOWN) {
            unsigned rows = tab == 1 ? 3 : tab == 3 ? 5 : 1;
            row = key == KEY_UP ? (row ? row - 1 : rows - 1) : (row + 1 < rows ? row + 1 : 0);
        } else if (key == 13 && tab == 1) {
            if (row == 0 && count) {
                unsigned next = preferred(&edit) + 1;
                set_preferred(&edit, next < count ? next : 0);
            }
            if (row == 1)
                edit.timeout = edit.timeout == 2 ? 5 : edit.timeout == 5 ? 10 : 2;
            if (row == 2) {
                discover();
                status = " Storage scan complete.";
            }
        } else if (key == 13 && tab == 2 && row == 0)
            edit.graphics = !edit.graphics;
        else
            continue;
        setup_screen(tab, row, &edit, status);
    }
    menu_leave();
}
void boot_manager(void)
{
    unsigned enter = 0;
    if (firmware_settings.graphics)
        display_boot_logo();
    puts_serial("Press F12 for boot settings; F10 for firmware setup (");
    decimal(firmware_settings.timeout);
    puts_serial(" seconds)\n");
    uint64_t start = time_ns();
    while (time_ns() - start < firmware_settings.timeout * 1000000000ULL) {
        uint32_t key = menu_key();
        if (key == KEY_F12 || key == KEY_F10) {
            enter = key;
            break;
        }
    }
    discover();
    display_ui_leave();
    selected = preferred(&firmware_settings);
    uint32_t key = menu_key();
    if (key == KEY_F12 || key == KEY_F10)
        enter = key;
    if (enter == KEY_F10) {
        firmware_setup();
        return;
    }
    if (!enter)
        return;
    show_menu();
    for (;;) {
        key = menu_key();
        if (!key)
            continue;
        if (key == 13 && count) {
            override = 1;
            break;
        }
        if (key == KEY_ESC) {
            selected = preferred(&firmware_settings);
            break;
        }
        if (key == KEY_F10) {
            firmware_setup();
            return;
        }
        if (key == 's' || key == 'S') {
            native_boot_info->boot_mode = 2;
            break;
        }
        if (key == 'r' || key == 'R') {
            discover();
            selected = preferred(&firmware_settings);
            show_menu();
            continue;
        }
        if (key == KEY_UP && selected)
            selected--;
        else if (key == KEY_DOWN && selected + 1 < count)
            selected++;
        else if (key >= '1' && key <= '9' && key - '1' < count)
            selected = key - '1';
        else
            continue;
        show_menu();
    }
    menu_leave();
}
EFI_STATUS boot_selected(void **buffer, UINTN *size, EFI_HANDLE *device)
{
    if (!discovered)
        discover();
    if (!count)
        return last_status;
    EFI_STATUS s = select_entry(selected);
    if (!s)
        s = storage_boot_image(buffer, size, device);
    return s;
}

/* Resolve GPT short paths before application execution. The current FAT driver
   owns one mounted volume; it must not invalidate an application's open files. */
static EFI_STATUS scan_paths(const void *path, EFI_HANDLE *image)
{
    if (!image && device_path_partition_matches(path))
        return EFI_SUCCESS;
    if (running_image)
        return EFI_UNSUPPORTED;
    for (unsigned bdf = 8; bdf < 256; bdf++) {
        if ((pci_read32(bdf, 8) >> 8) != 0x010802)
            continue;
        EFI_STATUS s = nvme_select(bdf, 0);
        if (s)
            continue;
        uint32_t nsids[1024];
        unsigned n = nvme_namespaces(nsids, 1024);
        for (unsigned ns = 0; ns < n; ns++) {
            if (nvme_select(bdf, nsids[ns]))
                continue;
            unsigned partitions = 1;
            for (unsigned p = 0; p < partitions; p++) {
                s = storage_select_partition(p);
                if (!s)
                    s = storage_mount();
                unsigned found = storage_partition_count();
                if (found > partitions)
                    partitions = found;
                if (!s) {
                    if (image)
                        s = boot_services.LoadImage(1, 0, (void *)path, 0, 0, image);
                    else
                        s = device_path_partition_matches(path) ? EFI_SUCCESS : EFI_NOT_FOUND;
                    if (!s)
                        return EFI_SUCCESS;
                }
            }
        }
    }
    return EFI_NOT_FOUND;
}

EFI_STATUS platform_partition_path(const void *path)
{
    return scan_paths(path, 0);
}
EFI_STATUS boot_option_load(void *path, EFI_HANDLE *image)
{
    const uint8_t *p = path;
    return p[0] == 4 && p[1] == 4 ? scan_paths(path, image)
                                  : boot_services.LoadImage(1, 0, path, 0, 0, image);
}
