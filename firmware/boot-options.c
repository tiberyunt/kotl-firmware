/* Standard EFI boot variables. SPDX-License-Identifier: MIT */
#include "device-path.h"
extern EFI_STATUS boot_option_load(void *, EFI_HANDLE *);
static const EFI_GUID global = EFI_GLOBAL_VARIABLE_GUID;
static const CHAR16 next_name[] = u"BootNext", order_name[] = u"BootOrder",
                    current_name[] = u"BootCurrent";
static uint16_t get16(const uint8_t *p)
{
    return p[0] | ((uint16_t)p[1] << 8);
}
static uint32_t get32(const uint8_t *p)
{
    return get16(p) | ((uint32_t)get16(p + 2) << 16);
}
static void option_name(CHAR16 *name, uint16_t number)
{
    memcpy(name, u"Boot0000", 18);
    for (unsigned i = 0; i < 4; i++)
        name[4 + i] = u"0123456789ABCDEF"[(number >> (12 - 4 * i)) & 15];
}
static EFI_STATUS attempt(uint16_t number, int explicit_next)
{
    CHAR16 name[9];
    option_name(name, number);
    uint8_t data[1024];
    UINTN size = sizeof data;
    EFI_RUNTIME_SERVICES *rt = system_table.RuntimeServices;
    EFI_STATUS s = rt->GetVariable(name, &global, 0, &size, data);
    if (s)
        return s;
    if (size < 8)
        return EFI_LOAD_ERROR;
    uint32_t attributes = get32(data);
    UINTN paths = get16(data + 4), off = 6;
    /* Boot categories other than BOOT and inactive options aren't autostarted. */
    if ((!explicit_next && !(attributes & 1)) ||
        ((attributes & 0x1f00) && !(explicit_next && (attributes & 0x1f00) == 0x100)))
        return EFI_NOT_FOUND;
    while (off + 2 <= size && get16(data + off))
        off += 2;
    if (off + 2 > size)
        return EFI_LOAD_ERROR;
    off += 2;
    if (!paths || paths > size - off || paths > EFI_DP_MAX)
        return EFI_LOAD_ERROR;
    UINTN actual;
    s = device_path_validate(data + off, paths, &actual, 0);
    if (s || actual != paths)
        return s ? s : EFI_LOAD_ERROR;
    puts_serial("Trying ");
    for (unsigned i = 0; i < 8; i++)
        putchar_serial((char)name[i]);
    puts_serial("\n");
    EFI_HANDLE image = 0;
    s = boot_option_load(data + off, &image);
    if (s)
        return s;
    EFI_GUID loaded_guid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
    EFI_LOADED_IMAGE_PROTOCOL *loaded = 0;
    s = boot_services.HandleProtocol(image, &loaded_guid, (void **)&loaded);
    if (!s) {
        loaded->LoadOptionsSize = size - off - paths;
        loaded->LoadOptions = loaded->LoadOptionsSize ? data + off + paths : 0;
        s = rt->SetVariable(current_name, &global,
                            EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS, 2,
                            &number);
    }
    if (!s) {
        static const EFI_GUID ready = {
            0x7ce88fb3, 0x4bd7, 0x4679, {0x87, 0xa8, 0xa8, 0xd8, 0xde, 0xe5, 0xd, 0x2b}};
        events_signal_group(&ready);
        s = boot_services.StartImage(image, 0, 0);
    }
    if (boot_active) {
        boot_services.UnloadImage(image);
        rt->SetVariable(current_name, &global, 0, 0, 0);
    }
    return s;
}
EFI_STATUS boot_options_run(void)
{
    EFI_RUNTIME_SERVICES *rt = system_table.RuntimeServices;
    uint8_t next[1024], order[1024];
    UINTN n = sizeof next;
    EFI_STATUS s = rt->GetVariable(next_name, &global, 0, &n, next);
    if (!s || s == EFI_BUFFER_TOO_SMALL) {
        /* A failed durable deletion must not repeatedly execute BootNext. */
        s = rt->SetVariable(next_name, &global, 0, 0, 0);
        if (s) {
            puts_serial("BootNext deletion failed; refusing repeated one-time boot\n");
            return s;
        }
        puts_serial("BootNext consumed\n");
        if (n == 2) {
            attempt(get16(next), 1);
            if (!boot_active)
                return EFI_SUCCESS;
        } else
            puts_serial("Ignoring malformed BootNext\n");
    }
    n = sizeof order;
    s = rt->GetVariable(order_name, &global, 0, &n, order);
    if (!s && !(n & 1)) {
        for (UINTN i = 0; i < n; i += 2) {
            int duplicate = 0;
            for (UINTN j = 0; j < i; j += 2)
                if (get16(order + i) == get16(order + j))
                    duplicate = 1;
            if (duplicate)
                continue;
            attempt(get16(order + i), 0);
            if (!boot_active)
                return EFI_SUCCESS;
        }
    } else if (s != EFI_NOT_FOUND)
        puts_serial("Ignoring malformed BootOrder\n");
    return EFI_NOT_FOUND; /* Fall back to automatic ESP discovery. */
}

/* F10 selection promotes a standard option; it never rewrites an existing
   loader's opaque optional data or overwrites an occupied Boot#### variable. */
EFI_STATUS boot_options_set_default(void *path, const CHAR16 *description)
{
    UINTN path_bytes;
    EFI_STATUS s = device_path_validate(path, EFI_DP_MAX, &path_bytes, 0);
    if (s)
        return s;
    uint8_t order[1024], option[1024];
    UINTN old_bytes = sizeof order;
    uint32_t order_attributes = 0;
    EFI_RUNTIME_SERVICES *rt = system_table.RuntimeServices;
    s = rt->GetVariable(order_name, &global, &order_attributes, &old_bytes, order);
    if (s == EFI_NOT_FOUND)
        old_bytes = 0;
    else if (s || (old_bytes & 1))
        return EFI_INVALID_PARAMETER;
    if (old_bytes > 1022)
        return EFI_OUT_OF_RESOURCES;
    unsigned id = 32, free_id = 32;
    for (unsigned i = 0; i < 32; i++) {
        CHAR16 name[9];
        option_name(name, i);
        UINTN n = sizeof option;
        s = rt->GetVariable(name, &global, 0, &n, option);
        if (s == EFI_NOT_FOUND) {
            if (free_id == 32)
                free_id = i;
            continue;
        }
        if (s || n < 8 || !(get32(option) & 1) || (get32(option) & 0x1f00))
            continue;
        UINTN off = 6;
        while (off + 2 <= n && get16(option + off))
            off += 2;
        off += 2;
        if (off <= n && get16(option + 4) == path_bytes && path_bytes <= n - off &&
            !memcmp(option + off, path, path_bytes)) {
            id = i;
            break;
        }
    }
    int created = 0;
    uint32_t attributes = EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS;
    if (platform_variables_persistent())
        attributes |= EFI_VARIABLE_NON_VOLATILE;
    if (id == 32) {
        if (free_id == 32)
            return EFI_OUT_OF_RESOURCES;
        id = free_id;
        memset(option, 0, sizeof option);
        option[0] = 1;
        option[4] = path_bytes & 255;
        option[5] = path_bytes >> 8;
        UINTN off = 6, units = 0;
        while (units < 80 && description[units])
            units++;
        if (units == 80 || 6 + (units + 1) * 2 + path_bytes > sizeof option)
            return EFI_INVALID_PARAMETER;
        memcpy(option + off, description, (units + 1) * 2);
        off += (units + 1) * 2;
        memcpy(option + off, path, path_bytes);
        off += path_bytes;
        CHAR16 name[9];
        option_name(name, id);
        s = rt->SetVariable(name, &global, attributes, off, option);
        if (s)
            return s;
        created = 1;
    }
    uint8_t updated[1024];
    UINTN n = 2;
    updated[0] = id & 255;
    updated[1] = id >> 8;
    for (UINTN i = 0; i < old_bytes; i += 2)
        if (get16(order + i) != id) {
            updated[n++] = order[i];
            updated[n++] = order[i + 1];
        }
    s = rt->SetVariable(order_name, &global, order_attributes ? order_attributes : attributes, n,
                        updated);
    if (s && created) {
        CHAR16 name[9];
        option_name(name, id);
        rt->SetVariable(name, &global, 0, 0, 0);
    }
    return s;
}
