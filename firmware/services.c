#include "services.h"
#ifdef KOTL_NATIVE
#include "kotl/kotl.h"
#endif
#define MAX_PAGES (1024 * 1024 * 1024 / EFI_PAGE_SIZE)
static uint8_t page_types[MAX_PAGES];
static uint8_t allocated[MAX_PAGES];
static UINTN total_pages;
static UINTN memory_base = EFI_RAM_BASE;
UINTN map_key = 1;
int boot_active = 1;
EFI_HANDLE running_image;
const uint8_t *external_image;
UINTN external_image_size;
EFI_SYSTEM_TABLE system_table;
EFI_BOOT_SERVICES boot_services;
static EFI_RUNTIME_SERVICES runtime_services;
static EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL console_out;
static EFI_SIMPLE_TEXT_INPUT_PROTOCOL console_in;
static typeof(*console_out.Mode) text_mode = {1, 0, 7, 0, 0, 1};
static EFI_STATUS unsupported(void)
{
    return EFI_UNSUPPORTED;
}
static void map_changed(void)
{
    map_key++;
    static const EFI_GUID group = EFI_EVENT_GROUP_MEMORY_MAP_CHANGE;
    events_signal_group(&group);
}
static void table_crc(EFI_TABLE_HEADER *h)
{
    h->CRC32 = 0;
    h->CRC32 = crc32(h, h->HeaderSize);
}
static EFI_CONFIGURATION_TABLE configuration_tables[8];
static EFI_RT_PROPERTIES_TABLE runtime_properties;
static EFI_STATUS install_configuration_table(EFI_GUID *guid, void *table)
{
    if (!boot_active || !guid)
        return EFI_INVALID_PARAMETER;
    UINTN i = 0;
    while (i < system_table.NumberOfTableEntries &&
           memcmp(guid, &configuration_tables[i].VendorGuid, sizeof(*guid)))
        i++;
    if (i == system_table.NumberOfTableEntries) {
        if (!table)
            return EFI_NOT_FOUND;
        if (i == 8)
            return EFI_OUT_OF_RESOURCES;
        system_table.NumberOfTableEntries++;
    } else if (!table) {
        for (UINTN j = i + 1; j < system_table.NumberOfTableEntries; j++)
            configuration_tables[j - 1] = configuration_tables[j];
        system_table.NumberOfTableEntries--;
        table_crc(&system_table.Hdr);
        events_signal_group(guid);
        return EFI_SUCCESS;
    }
    configuration_tables[i] = (EFI_CONFIGURATION_TABLE){*guid, table};
    table_crc(&system_table.Hdr);
    events_signal_group(guid);
    return EFI_SUCCESS;
}
static EFI_STATUS alloc_pages(uint32_t how, uint32_t type, UINTN n, EFI_PHYSICAL_ADDRESS *pa)
{
    if (events_exiting())
        return EFI_UNSUPPORTED;
    if (!boot_active || !pa || !n || how > 2 || type == EfiConventionalMemory ||
        type == EfiReservedMemoryType || type > 10)
        return EFI_INVALID_PARAMETER;
    if (n > total_pages)
        return EFI_OUT_OF_RESOURCES;
    UINTN first = 0, limit = total_pages;
    if (how == AllocateAddress) {
        if (*pa % EFI_PAGE_SIZE)
            return EFI_NOT_FOUND;
        if (*pa < memory_base)
            return EFI_NOT_FOUND;
        first = (*pa - memory_base) / EFI_PAGE_SIZE;
        if (first >= limit || n > limit - first)
            return EFI_NOT_FOUND;
        limit = first + n;
    }
    if (how == AllocateMaxAddress && *pa < memory_base)
        return EFI_OUT_OF_RESOURCES;
    if (how == AllocateMaxAddress && (*pa - memory_base) / EFI_PAGE_SIZE < limit - 1)
        limit = (*pa - memory_base) / EFI_PAGE_SIZE + 1;
    for (UINTN p = first; p < limit && n <= limit - p; p++) {
        UINTN i = 0;
        while (i < n && page_types[p + i] == EfiConventionalMemory)
            i++;
        if (i == n) {
            for (i = 0; i < n; i++) {
                page_types[p + i] = (uint8_t)type;
                allocated[p + i] = 1;
            }
            *pa = memory_base + p * EFI_PAGE_SIZE;
            map_changed();
            return EFI_SUCCESS;
        }
        p += i;
    }
    return how == AllocateAddress ? EFI_NOT_FOUND : EFI_OUT_OF_RESOURCES;
}
static EFI_STATUS free_pages(EFI_PHYSICAL_ADDRESS pa, UINTN n)
{
    if (events_exiting())
        return EFI_UNSUPPORTED;
    UINTN p = (pa - memory_base) / EFI_PAGE_SIZE;
    if (!boot_active || pa < memory_base || pa % EFI_PAGE_SIZE || !n || p >= total_pages ||
        n > total_pages - p)
        return EFI_INVALID_PARAMETER;
    for (UINTN i = 0; i < n; i++)
        if (!allocated[p + i])
            return EFI_INVALID_PARAMETER;
    for (UINTN i = 0; i < n; i++) {
        page_types[p + i] = EfiConventionalMemory;
        allocated[p + i] = 0;
    }
    map_changed();
    return EFI_SUCCESS;
}
#ifdef KOTL_NATIVE
static const EFI_MEMORY_DESCRIPTOR native_regions[] = {
    {EfiRuntimeServicesCode, 0, 0, 0, 0x1000000 / 4096, EFI_MEMORY_WB | (1ULL << 63)},
    {EfiRuntimeServicesData, 0, 0x1000000, 0, 0x200000 / 4096, EFI_MEMORY_WB | (1ULL << 63)},
    {11, 0, 0x2000000, 0, 0x3000000 / 4096, 1 | (1ULL << 63)},
    {11, 0, 0x10000000, 0, 0x40000 / 4096, 1 | (1ULL << 63)},
    {11, 0, 0x30000000, 0, 0x1000000 / 4096, 1},
    {11, 0, 0x40000000, 0, 0x10000000 / 4096, 1},
    {11, 0, 0x80000000, 0, 0x80000000ULL / 4096, 1},
};
#define PREFIX_REGIONS (sizeof(native_regions) / sizeof(native_regions[0]))
#else
#define PREFIX_REGIONS 0
#endif
static EFI_STATUS get_map(UINTN *size, EFI_MEMORY_DESCRIPTOR *map, UINTN *key, UINTN *ds,
                          uint32_t *ver)
{
    if (!boot_active || !size || !key || !ds || !ver)
        return EFI_INVALID_PARAMETER;
    UINTN count = 0;
    for (UINTN p = 0; p < total_pages;) {
        uint8_t t = page_types[p++];
        while (p < total_pages && page_types[p] == t)
            p++;
        count++;
    }
    UINTN need = (PREFIX_REGIONS + count) * sizeof(*map);
    *ds = sizeof(*map);
    *ver = 1;
    if (*size < need) {
        *size = need;
        return EFI_BUFFER_TOO_SMALL;
    }
    if (!map)
        return EFI_INVALID_PARAMETER;
    *size = need;
    *key = map_key;
#ifdef KOTL_NATIVE
    memcpy(map, native_regions, sizeof(native_regions));
#endif
    count = PREFIX_REGIONS;
    for (UINTN p = 0; p < total_pages;) {
        UINTN start = p;
        uint8_t t = page_types[p++];
        while (p < total_pages && page_types[p] == t)
            p++;
        map[count++] = (EFI_MEMORY_DESCRIPTOR){
            t,
            0,
            memory_base + start * EFI_PAGE_SIZE,
            0,
            p - start,
            EFI_MEMORY_WB |
                ((t == EfiRuntimeServicesCode || t == EfiRuntimeServicesData) ? (1ULL << 63) : 0)};
    }
    return EFI_SUCCESS;
}
typedef struct {
    uint64_t magic, pages;
} POOL;
#define POOL_MAGIC 0x414c504841504f4fULL
static EFI_STATUS alloc_pool(uint32_t type, UINTN size, void **out)
{
    if (!out || size > UINT64_MAX - sizeof(POOL) - 4095)
        return EFI_INVALID_PARAMETER;
    /* Pool allocations carry metadata, and must stay in reclaimable data memory. */
    if (type != EfiLoaderData && type != EfiBootServicesData)
        return EFI_UNSUPPORTED;
    UINTN n = (size + sizeof(POOL) + 4095) / 4096;
    EFI_PHYSICAL_ADDRESS pa = 0;
    EFI_STATUS s = alloc_pages(AllocateAnyPages, type, n, &pa);
    if (s)
        return s;
    POOL *p = (POOL *)(ALPHA_KSEG + pa);
    p->magic = POOL_MAGIC;
    p->pages = n;
    *out = p + 1;
    return EFI_SUCCESS;
}
static EFI_STATUS free_pool(void *buf)
{
    UINTN v = (UINTN)buf;
    if (!boot_active || v < ALPHA_KSEG + memory_base + EFI_ALLOC_START + sizeof(POOL) ||
        v >= ALPHA_KSEG + memory_base + total_pages * 4096 || ((v - sizeof(POOL)) & 4095))
        return EFI_INVALID_PARAMETER;
    POOL *p = (POOL *)buf - 1;
    if (p->magic != POOL_MAGIC)
        return EFI_INVALID_PARAMETER;
    UINTN n = p->pages;
    p->magic = 0;
    EFI_STATUS s = free_pages((UINTN)p - ALPHA_KSEG, n);
    if (s)
        p->magic = POOL_MAGIC;
    return s;
}
static EFI_STATUS calc_crc(void *p, UINTN n, uint32_t *out)
{
    if (!boot_active || !p || !n || !out)
        return EFI_INVALID_PARAMETER;
    *out = crc32(p, n);
    return EFI_SUCCESS;
}
static void copy_mem(void *d, const void *s, UINTN n)
{ /* CopyMem permits overlap. */
    uint8_t *a = d;
    const uint8_t *b = s;
    if ((UINTN)a > (UINTN)b) {
        while (n) {
            n--;
            a[n] = b[n];
        }
    } else
        memcpy(d, s, n);
}
static void set_mem(void *d, UINTN n, uint8_t v)
{
    memset(d, v, n);
}
static EFI_STATUS clear(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *p);
static EFI_STATUS attribute(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *p, UINTN a);
static char console_ascii(CHAR16 c)
{
    if (c < 128)
        return c;
    if (c == 0x2500)
        return '-';
    if (c == 0x2502)
        return '|';
    if (c >= 0x250c && c <= 0x253c)
        return '+';
    if (c == 0x2190)
        return '<';
    if (c == 0x2192)
        return '>';
    if (c == 0x2191 || c == 0x25b2)
        return '^';
    if (c == 0x2193 || c == 0x25bc)
        return 'v';
    return '?';
}
static EFI_STATUS output(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *p, const CHAR16 *s)
{
    (void)p;
    if (!boot_active || !s)
        return EFI_INVALID_PARAMETER;
    while (*s) {
        char c = console_ascii(*s++);
        putchar_serial(c);
        if (c == '\r')
            text_mode.CursorColumn = 0;
        else if (c == '\n') {
            if (text_mode.CursorRow < 24)
                text_mode.CursorRow++;
            text_mode.CursorColumn = 0;
        } else if (c == '\b') {
            if (text_mode.CursorColumn)
                text_mode.CursorColumn--;
        } else if ((unsigned char)c >= 32 && ++text_mode.CursorColumn == 80) {
            text_mode.CursorColumn = 0;
            if (text_mode.CursorRow < 24)
                text_mode.CursorRow++;
        }
    }
    return EFI_SUCCESS;
}
static EFI_STATUS text_reset(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *p, uint8_t ext)
{
    (void)ext;
    if (!boot_active)
        return EFI_UNSUPPORTED;
    attribute(p, 7);
    return clear(p);
}
static EFI_STATUS text_test(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *p, const CHAR16 *s)
{
    (void)p;
    if (!s)
        return EFI_INVALID_PARAMETER;
    while (*s) {
        if (*s > 127 && console_ascii(*s) == '?')
            return EFI_UNSUPPORTED;
        s++;
    }
    return EFI_SUCCESS;
}
static EFI_STATUS query_mode(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *p, UINTN mode, UINTN *cols,
                             UINTN *rows)
{
    (void)p;
    if (mode)
        return EFI_UNSUPPORTED;
    if (!cols || !rows)
        return EFI_INVALID_PARAMETER;
    *cols = 80;
    *rows = 25;
    return EFI_SUCCESS;
}
static EFI_STATUS set_mode(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *p, UINTN mode)
{
    return mode ? EFI_UNSUPPORTED : clear(p);
}
static void decimal_console(unsigned n)
{
    if (n >= 10)
        decimal_console(n / 10);
    putchar_serial('0' + n % 10);
}
static EFI_STATUS attribute(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *p, UINTN a)
{
    (void)p;
    if (!boot_active)
        return EFI_UNSUPPORTED;
    if (a > 127)
        return EFI_UNSUPPORTED;
    static const unsigned ansi[8] = {0, 4, 2, 6, 1, 5, 3, 7};
    puts_serial("\033[");
    decimal_console((a & 8 ? 90 : 30) + ansi[a & 7]);
    putchar_serial(';');
    decimal_console(40 + ansi[(a >> 4) & 7]);
    putchar_serial('m');
#ifdef KOTL_NATIVE
    display_attribute(a);
#endif
    text_mode.Attribute = a;
    return EFI_SUCCESS;
}
static EFI_STATUS clear(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *p)
{
    (void)p;
    if (!boot_active)
        return EFI_UNSUPPORTED;
    puts_serial("\033[2J\033[H");
    text_mode.CursorColumn = text_mode.CursorRow = 0;
    return EFI_SUCCESS;
}
static EFI_STATUS cursor(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *p, UINTN x, UINTN y)
{
    (void)p;
    if (!boot_active)
        return EFI_UNSUPPORTED;
    if (x >= 80 || y >= 25)
        return EFI_UNSUPPORTED;
    puts_serial("\033[");
    decimal_console(y + 1);
    putchar_serial(';');
    decimal_console(x + 1);
    putchar_serial('H');
#ifdef KOTL_NATIVE
    display_cursor(x, y);
#endif
    text_mode.CursorColumn = x;
    text_mode.CursorRow = y;
    return EFI_SUCCESS;
}
static EFI_STATUS enable_cursor(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *p, uint8_t v)
{
    (void)p;
    if (!boot_active)
        return EFI_UNSUPPORTED;
    text_mode.CursorVisible = !!v;
    puts_serial(v ? "\033[?25h" : "\033[?25l");
    return EFI_SUCCESS;
}
static EFI_INPUT_KEY pending_key;
static int key_ready;
static int console_poll_key(void)
{
    if (key_ready)
        return 1;
#ifdef KOTL_NATIVE
    uint32_t value = platform_key();
    if (!value)
        return 0;
    pending_key = (EFI_INPUT_KEY){value >> 16, value & 0xffff};
#else
    int value = getchar_serial();
    if (value < 0)
        return 0;
    pending_key = (EFI_INPUT_KEY){0, value};
#endif
    key_ready = 1;
    return 1;
}
static void console_wait(EFI_EVENT event, void *context)
{
    (void)context;
    if (console_poll_key())
        boot_services.SignalEvent(event);
}
static EFI_STATUS input_reset(EFI_SIMPLE_TEXT_INPUT_PROTOCOL *p, uint8_t ext)
{
    (void)p;
    (void)ext;
    if (!boot_active)
        return EFI_UNSUPPORTED;
    key_ready = 0;
    while (console_poll_key())
        key_ready = 0;
    events_clear(console_in.WaitForKey);
    return EFI_SUCCESS;
}
static EFI_STATUS read_key(EFI_SIMPLE_TEXT_INPUT_PROTOCOL *p, EFI_INPUT_KEY *key)
{
    (void)p;
    if (!boot_active || !key)
        return EFI_INVALID_PARAMETER;
    events_poll();
    if (!console_poll_key())
        return EFI_NOT_READY;
    *key = pending_key;
    key_ready = 0;
    events_clear(console_in.WaitForKey);
    return EFI_SUCCESS;
}
static EFI_STATUS exit_boot(EFI_HANDLE image, UINTN key)
{
    if (!boot_active || !image || image != running_image || key != map_key)
        return EFI_INVALID_PARAMETER;
    if (events_tpl() != TPL_APPLICATION)
        return EFI_UNSUPPORTED;
    events_before_exit();
    if (key != map_key)
        return EFI_INVALID_PARAMETER;
    EFI_STATUS status = platform_exit_boot();
    if (status)
        return status;
    events_exit_boot();
    boot_active = 0;
    system_table.BootServices = 0;
    system_table.ConIn = 0;
    system_table.ConOut = 0;
    system_table.StdErr = 0;
    system_table.ConsoleInHandle = 0;
    system_table.ConsoleOutHandle = 0;
    system_table.StandardErrorHandle = 0;
    table_crc(&system_table.Hdr);
    return EFI_SUCCESS;
}
static void reset_system(uint32_t type, EFI_STATUS status, UINTN size, void *data)
{
    (void)size;
    (void)data;
    if (!boot_active && status == EFI_SUCCESS) {
        if (type == 2)
            puts_serial(
                "PASS: ExitBootServices and runtime shutdown\nALPHA EFI SELFTEST COMPLETE\n");
        else
            puts_serial("EFI runtime reset requested\n");
    }
    platform_reset(type);
}
static EFI_STATUS stall(UINTN us)
{
    if (!boot_active || us > UINT64_MAX / 1000)
        return EFI_INVALID_PARAMETER;
    uint64_t start = time_ns();
    do {
        events_poll();
    } while (time_ns() - start < us * 1000);
    return EFI_SUCCESS;
}
void print_map(void)
{
    EFI_MEMORY_DESCRIPTOR map[128];
    UINTN sz = sizeof(map), key, ds;
    uint32_t ver;
    if (get_map(&sz, map, &key, &ds, &ver))
        return;
    for (UINTN i = 0; i < sz / sizeof(*map); i++) {
        hex_serial(map[i].PhysicalStart);
        puts_serial(" pages=");
        hex_serial(map[i].NumberOfPages);
        puts_serial(" type=");
        hex_serial(map[i].Type);
        puts_serial("\n");
    }
}
void services_init(void)
{
#ifdef KOTL_NATIVE
    if (!native_boot_info || native_boot_info->magic != KOTL_BOOT_MAGIC ||
        native_boot_info->ram_base != memory_base ||
        native_boot_info->ram_size < 64 * 1024 * 1024ULL ||
        native_boot_info->ram_size > 1024 * 1024 * 1024ULL) {
        puts_serial("Unsupported native firmware RAM (64 MiB through 1 GiB)\n");
        halt();
    }
    UINTN ram = native_boot_info->ram_size;
    total_pages = ram / 4096;
    memset(page_types, EfiReservedMemoryType, sizeof(page_types));
    for (UINTN p = 0; p < total_pages; p++) {
        page_types[p] = p < 0x200000 / 4096          ? EfiReservedMemoryType
                        : p < 0x400000 / 4096        ? EfiRuntimeServicesCode
                        : p < EFI_ALLOC_START / 4096 ? EfiBootServicesData
                                                     : EfiConventionalMemory;
    }
#else
    /* QEMU PALcode maps the architected HWRPB at 0x10000000. */
    const uint64_t *h = (const uint64_t *)0x10000000;
    if (h[1] != 0x4857525042000000ULL || h[5] != 8192 || h[25] > 0x10000) {
        puts_serial("Invalid QEMU HWRPB\n");
        halt();
    }
    const uint64_t *md = (const uint64_t *)((const uint8_t *)h + h[25]);
    if (md[2] > 16) {
        puts_serial("Invalid memory clusters\n");
        halt();
    }
    UINTN ram = 0;
    for (UINTN i = 0; i < md[2]; i++) {
        const uint64_t *c = md + 3 + i * 7;
        UINTN end = (c[0] + c[1]) * 8192;
        if (end > ram)
            ram = end;
    }
    if (ram < 64 * 1024 * 1024ULL || ram > 1024 * 1024 * 1024ULL) {
        puts_serial("Supported RAM: 64 MiB to 1 GiB\n");
        halt();
    }
    total_pages = ram / 4096;
    memset(page_types, EfiReservedMemoryType, sizeof(page_types));
    for (UINTN i = 0; i < md[2]; i++) {
        const uint64_t *c = md + 3 + i * 7;
        if (c[6])
            continue;
        for (UINTN p = c[0] * 2; p < (c[0] + c[1]) * 2; p++)
            page_types[p] = EfiConventionalMemory;
    }
    for (UINTN p = 0; p < 2 * 1024 * 1024 / 4096; p++)
        page_types[p] = EfiReservedMemoryType;
    for (UINTN p = 2 * 1024 * 1024 / 4096; p < 4 * 1024 * 1024 / 4096; p++)
        page_types[p] = EfiRuntimeServicesCode;
    const uint64_t *params = (const uint64_t *)(ALPHA_KSEG + 0x200000 - 0x6000);
    if (params[33]) {
        UINTN va = params[32], n = params[33];
        if (va < ALPHA_KSEG + 4 * 1024 * 1024 || n > 4 * 1024 * 1024 || va - ALPHA_KSEG >= ram ||
            n > ram - (va - ALPHA_KSEG)) {
            puts_serial("Invalid external EFI image buffer\n");
            halt();
        }
        external_image = (const uint8_t *)va;
        external_image_size = n;
        UINTN lo = (va - ALPHA_KSEG) / 4096, hi = (va - ALPHA_KSEG + n + 4095) / 4096;
        for (UINTN p = lo; p < hi; p++)
            page_types[p] = EfiLoaderData;
    }
#endif
    puts_serial("RAM discovered: ");
    hex_serial(ram);
    puts_serial(" bytes\n");
    /* All absent service slots are callable EFI_UNSUPPORTED stubs. */
    EFI_GENERIC *slots = (EFI_GENERIC *)((uint8_t *)&boot_services + sizeof(EFI_TABLE_HEADER));
    for (UINTN i = 0; i < (sizeof(boot_services) - sizeof(EFI_TABLE_HEADER)) / 8; i++)
        slots[i] = unsupported;
    boot_services.Reserved = 0;
    boot_services.Hdr =
        (EFI_TABLE_HEADER){EFI_BOOT_SERVICES_SIGNATURE, EFI_REVISION, sizeof(boot_services), 0, 0};
    boot_services.AllocatePages = alloc_pages;
    boot_services.FreePages = free_pages;
    boot_services.GetMemoryMap = get_map;
    boot_services.AllocatePool = alloc_pool;
    boot_services.FreePool = free_pool;
    boot_services.CalculateCrc32 = calc_crc;
    boot_services.CopyMem = copy_mem;
    boot_services.SetMem = set_mem;
    protocols_init();
    events_init();
    boot_services.InstallConfigurationTable = install_configuration_table;
    boot_services.LoadImage = load_image;
    boot_services.StartImage = start_image;
    boot_services.Exit = exit_image;
    boot_services.UnloadImage = unload_image;
    boot_services.ExitBootServices = exit_boot;
    boot_services.Stall = stall;
    table_crc(&boot_services.Hdr);
    slots = (EFI_GENERIC *)((uint8_t *)&runtime_services + sizeof(EFI_TABLE_HEADER));
    for (UINTN i = 0; i < (sizeof(runtime_services) - sizeof(EFI_TABLE_HEADER)) / 8; i++)
        slots[i] = unsupported;
    runtime_services.Hdr = (EFI_TABLE_HEADER){EFI_RUNTIME_SERVICES_SIGNATURE, EFI_REVISION,
                                              sizeof(runtime_services), 0, 0};
    runtime_services.ResetSystem = reset_system;
    variables_init(&runtime_services);
    table_crc(&runtime_services.Hdr);
    console_out =
        (EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL){text_reset, output, text_test, query_mode,    set_mode,
                                          attribute,  clear,  cursor,    enable_cursor, &text_mode};
    console_in = (EFI_SIMPLE_TEXT_INPUT_PROTOCOL){input_reset, read_key, 0};
    if (boot_services.CreateEvent(EVT_NOTIFY_WAIT, TPL_CALLBACK, console_wait, 0,
                                  &console_in.WaitForKey))
        halt();
    static const EFI_GUID input_guid = EFI_SIMPLE_TEXT_INPUT_PROTOCOL_GUID;
    static const EFI_GUID output_guid = EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL_GUID;
    if (protocol_publish(&console_in, &input_guid, &console_in) ||
        protocol_publish(&console_out, &output_guid, &console_out))
        halt();
    static CHAR16 vendor[] = {'A', 'l', 'p', 'h', 'a', ' ', 'E', 'F', 'I', 0};
    system_table = (EFI_SYSTEM_TABLE){
        .Hdr = {EFI_SYSTEM_TABLE_SIGNATURE, EFI_REVISION, sizeof(system_table), 0, 0},
        .FirmwareVendor = vendor,
        .FirmwareRevision = 1,
        .ConsoleInHandle = &console_in,
        .ConIn = &console_in,
        .ConsoleOutHandle = &console_out,
        .ConOut = &console_out,
        .StandardErrorHandle = &console_out,
        .StdErr = &console_out,
        .ConfigurationTable = configuration_tables,
        .RuntimeServices = &runtime_services,
        .BootServices = &boot_services};
    table_crc(&system_table.Hdr);
    static EFI_GUID runtime_guid = EFI_RT_PROPERTIES_TABLE_GUID;
    runtime_properties = (EFI_RT_PROPERTIES_TABLE){
        1, sizeof(runtime_properties), 0x2430U | (platform_variables_persistent() ? 0x40U : 0)};
    if (install_configuration_table(&runtime_guid, &runtime_properties))
        halt();
#ifdef KOTL_NATIVE
    static EFI_GUID fdt_guid = {
        0xb1b621d5, 0xf19c, 0x41a5, {0x83, 0x0b, 0xd9, 0x15, 0x2c, 0x69, 0xaa, 0xe0}};
    if (install_configuration_table(&fdt_guid, platform_dtb()))
        halt();
#endif
}
