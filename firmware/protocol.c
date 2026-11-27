#include "device-path.h"
#include "efi-storage.h"
#include "services.h"
#define MAX_BINDINGS 48
#define MAX_HANDLES 24
static struct {
    EFI_HANDLE handle;
    EFI_GUID guid;
    void *interface;
} bindings[MAX_BINDINGS];
static uint64_t handles[MAX_HANDLES];
static int same(const EFI_GUID *a, const EFI_GUID *b)
{
    return memcmp(a, b, sizeof(*a)) == 0;
}
EFI_STATUS protocol_publish(EFI_HANDLE handle, const EFI_GUID *guid, void *interface)
{
    if (!boot_active || !handle || !guid)
        return EFI_INVALID_PARAMETER;
    int free = -1;
    for (unsigned i = 0; i < MAX_BINDINGS; i++) {
        if (bindings[i].handle == handle && same(&bindings[i].guid, guid))
            return EFI_INVALID_PARAMETER;
        if (!bindings[i].handle && free < 0)
            free = i;
    }
    if (free < 0)
        return EFI_OUT_OF_RESOURCES;
    bindings[free].handle = handle;
    bindings[free].guid = *guid;
    bindings[free].interface = interface;
    return EFI_SUCCESS;
}
void protocol_remove_handle(EFI_HANDLE handle)
{
    for (unsigned i = 0; i < MAX_BINDINGS; i++)
        if (bindings[i].handle == handle)
            memset(&bindings[i], 0, sizeof(bindings[i]));
}
static EFI_STATUS handle_protocol(EFI_HANDLE handle, EFI_GUID *guid, void **out)
{
    if (!boot_active || !handle || !guid || !out)
        return EFI_INVALID_PARAMETER;
    int known = 0;
    for (unsigned i = 0; i < MAX_BINDINGS; i++)
        if (bindings[i].handle == handle) {
            known = 1;
            if (same(&bindings[i].guid, guid)) {
                *out = bindings[i].interface;
                return EFI_SUCCESS;
            }
        }
    return known ? EFI_UNSUPPORTED : EFI_INVALID_PARAMETER;
}
static EFI_STATUS install(EFI_HANDLE *handle, EFI_GUID *guid, uint32_t type, void *interface)
{
    if (!boot_active || !handle || !guid || type)
        return EFI_INVALID_PARAMETER;
    if (!*handle) {
        for (unsigned slot = 0; slot < MAX_HANDLES; slot++) {
            EFI_HANDLE h = &handles[slot];
            int used = 0;
            for (unsigned i = 0; i < MAX_BINDINGS; i++)
                if (bindings[i].handle == h)
                    used = 1;
            if (used)
                continue;
            EFI_STATUS s = protocol_publish(h, guid, interface);
            if (!s)
                *handle = h;
            return s;
        }
        return EFI_OUT_OF_RESOURCES;
    }
    int known = 0;
    for (unsigned i = 0; i < MAX_BINDINGS; i++)
        if (bindings[i].handle == *handle)
            known = 1;
    return known ? protocol_publish(*handle, guid, interface) : EFI_INVALID_PARAMETER;
}
static EFI_STATUS reinstall(EFI_HANDLE handle, EFI_GUID *guid, void *old, void *interface)
{
    if (!boot_active || !handle || !guid)
        return EFI_INVALID_PARAMETER;
    for (unsigned i = 0; i < MAX_BINDINGS; i++)
        if (bindings[i].handle == handle && same(&bindings[i].guid, guid)) {
            if (bindings[i].interface != old)
                return EFI_NOT_FOUND;
            bindings[i].interface = interface;
            return EFI_SUCCESS;
        }
    return EFI_NOT_FOUND;
}
static EFI_STATUS uninstall(EFI_HANDLE handle, EFI_GUID *guid, void *interface)
{
    if (!boot_active || !handle || !guid)
        return EFI_INVALID_PARAMETER;
    for (unsigned i = 0; i < MAX_BINDINGS; i++)
        if (bindings[i].handle == handle && same(&bindings[i].guid, guid) &&
            bindings[i].interface == interface) {
            memset(&bindings[i], 0, sizeof(bindings[i]));
            return EFI_SUCCESS;
        }
    return EFI_NOT_FOUND;
}
static EFI_STATUS locate_protocol(EFI_GUID *guid, void *registration, void **out)
{
    if (!boot_active || !guid || !out)
        return EFI_INVALID_PARAMETER;
    if (registration)
        return EFI_UNSUPPORTED;
    for (unsigned i = 0; i < MAX_BINDINGS; i++)
        if (bindings[i].handle && same(&bindings[i].guid, guid)) {
            *out = bindings[i].interface;
            return EFI_SUCCESS;
        }
    return EFI_NOT_FOUND;
}
static EFI_STATUS locate_handles(uint32_t type, EFI_GUID *guid, void *key, UINTN *count,
                                 EFI_HANDLE **out)
{
    if (!boot_active || !count || !out || (type == ByProtocol && !guid))
        return EFI_INVALID_PARAMETER;
    if (key || (type != ByProtocol && type != AllHandles))
        return EFI_UNSUPPORTED;
    EFI_HANDLE found[MAX_BINDINGS];
    UINTN n = 0;
    for (unsigned i = 0; i < MAX_BINDINGS; i++)
        if (bindings[i].handle && (type == AllHandles || same(&bindings[i].guid, guid))) {
            UINTN j = 0;
            while (j < n && found[j] != bindings[i].handle)
                j++;
            if (j == n)
                found[n++] = bindings[i].handle;
        }
    *count = 0;
    *out = 0;
    if (!n)
        return EFI_NOT_FOUND;
    EFI_STATUS s =
        boot_services.AllocatePool(EfiBootServicesData, n * sizeof(*found), (void **)out);
    if (s)
        return s;
    memcpy(*out, found, n * sizeof(*found));
    *count = n;
    return EFI_SUCCESS;
}
static EFI_STATUS open_protocol(EFI_HANDLE handle, EFI_GUID *guid, void **out, EFI_HANDLE agent,
                                EFI_HANDLE controller, uint32_t attributes)
{
    (void)agent;
    (void)controller;
    if (attributes != EFI_OPEN_PROTOCOL_GET_PROTOCOL &&
        attributes != EFI_OPEN_PROTOCOL_TEST_PROTOCOL)
        return EFI_UNSUPPORTED;
    void *interface;
    EFI_STATUS s = handle_protocol(handle, guid, &interface);
    if (s)
        return s;
    if (attributes == EFI_OPEN_PROTOCOL_TEST_PROTOCOL)
        return EFI_SUCCESS;
    if (!out)
        return EFI_INVALID_PARAMETER;
    *out = interface;
    return EFI_SUCCESS;
}
static EFI_STATUS locate_handle(uint32_t type, EFI_GUID *guid, void *key, UINTN *bytes,
                                EFI_HANDLE *out)
{
    if (!bytes)
        return EFI_INVALID_PARAMETER;
    UINTN count = 0;
    EFI_HANDLE *found = 0;
    EFI_STATUS s = locate_handles(type, guid, key, &count, &found);
    if (s)
        return s;
    UINTN needed = count * sizeof(EFI_HANDLE), capacity = *bytes;
    *bytes = needed;
    if (capacity < needed || !out)
        s = EFI_BUFFER_TOO_SMALL;
    else
        memcpy(out, found, needed);
    boot_services.FreePool(found);
    return s;
}
static EFI_STATUS locate_device_path(EFI_GUID *guid, void **path, EFI_HANDLE *device)
{
    if (!boot_active || !guid || !path || !*path || !device)
        return EFI_INVALID_PARAMETER;
    const void *tail;
    EFI_HANDLE found;
    EFI_STATUS s = device_path_resolve(*path, &found, &tail);
    if (s)
        return s;
    void *interface;
    s = handle_protocol(found, guid, &interface);
    if (s)
        return EFI_NOT_FOUND;
    *path = (void *)tail;
    *device = found;
    return EFI_SUCCESS;
}
void protocols_init(void)
{
    boot_services.InstallProtocolInterface = install;
    boot_services.ReinstallProtocolInterface = reinstall;
    boot_services.UninstallProtocolInterface = uninstall;
    boot_services.HandleProtocol = handle_protocol;
    boot_services.LocateProtocol = locate_protocol;
    boot_services.LocateHandle = locate_handle;
    boot_services.LocateDevicePath = locate_device_path;
    boot_services.LocateHandleBuffer = locate_handles;
    boot_services.OpenProtocol = open_protocol;
}
