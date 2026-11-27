/* GPT hard-drive short paths and FilePath transport. SPDX-License-Identifier: MIT */
#include "device-path.h"
EFI_PARTITION_PATH partition_path;
static const EFI_GUID path_guid = EFI_DEVICE_PATH_PROTOCOL_GUID;
static uint16_t rd16(const uint8_t *p)
{
    return p[0] | ((uint16_t)p[1] << 8);
}
/* Supported grammar: [HD(GPT)] FilePath+ EndEntire, or HD EndEntire for a
   filesystem handle. Explicit lengths bound EFI_LOAD_OPTION parsing. */
EFI_STATUS device_path_validate(const void *path, UINTN limit, UINTN *size, UINTN *file)
{
    if (!path || limit < 4 || limit > EFI_DP_MAX)
        return EFI_INVALID_PARAMETER;
    const uint8_t *p = path;
    UINTN off = 0, first = 0;
    int hd = 0, files = 0;
    while (off + 4 <= limit) {
        UINTN n = rd16(p + off + 2);
        if (n < 4 || n > limit - off)
            return EFI_INVALID_PARAMETER;
        if (p[off] == 0x7f) {
            if (p[off + 1] != 0xff || n != 4 || (!hd && !files))
                return EFI_INVALID_PARAMETER;
            if (size)
                *size = off + 4;
            if (file)
                *file = files ? first : off;
            return EFI_SUCCESS;
        }
        if (p[off] == 4 && p[off + 1] == 1) {
            if (off || hd || n != 42 || p[off + 40] != 2 || p[off + 41] != 2)
                return EFI_UNSUPPORTED;
            hd = 1;
        } else if (p[off] == 4 && p[off + 1] == 4) {
            if (n < 6 || (n & 1) || rd16(p + off + n - 2))
                return EFI_INVALID_PARAMETER;
            for (UINTN i = 4; i + 2 < n; i += 2)
                if (!rd16(p + off + i))
                    return EFI_INVALID_PARAMETER;
            if (!files)
                first = off;
            files = 1;
        } else
            return EFI_UNSUPPORTED;
        off += n;
    }
    return EFI_INVALID_PARAMETER;
}
int device_path_partition_matches(const void *path)
{
    const uint8_t *p = path;
    /* GPT identity is partition GUID + partition number. Start/size can change
       when a disk is resized, so they are deliberately not identity keys. */
    return filesystem_handle && p && p[0] == 4 && p[1] == 1 && rd16(p + 2) == 42 && p[40] == 2 &&
           p[41] == 2 && !memcmp(p + 4, partition_path.bytes + 4, 4) &&
           !memcmp(p + 24, partition_path.bytes + 24, 16);
}
EFI_STATUS device_path_resolve(const void *path, EFI_HANDLE *device, const void **file)
{
    UINTN bytes, offset;
    EFI_STATUS s = device_path_validate(path, EFI_DP_MAX, &bytes, &offset);
    if (s)
        return s;
    const uint8_t *p = path;
    if (p[0] == 4 && p[1] == 1 && !device_path_partition_matches(path)) {
        s = platform_partition_path(path);
        if (s)
            return s;
    }
    if (!filesystem_handle) {
        s = storage_mount();
        if (s)
            return s;
    }
    *device = filesystem_handle;
    *file = p + offset;
    return EFI_SUCCESS;
}
EFI_STATUS device_path_file(const CHAR16 *name, void **out)
{
    if (!name || !out)
        return EFI_INVALID_PARAMETER;
    UINTN n = 0;
    while (n < 480 && name[n])
        n++;
    if (!n || n == 480)
        return EFI_INVALID_PARAMETER;
    UINTN bytes = 4 + (n + 1) * 2 + 4;
    uint8_t *p;
    EFI_STATUS s = boot_services.AllocatePool(EfiBootServicesData, bytes, (void **)&p);
    if (s)
        return s;
    p[0] = 4;
    p[1] = 4;
    p[2] = (bytes - 4) & 255;
    p[3] = (bytes - 4) >> 8;
    memcpy(p + 4, name, (n + 1) * 2);
    memcpy(p + bytes - 4, (uint8_t[]){0x7f, 0xff, 4, 0}, 4);
    *out = p;
    return EFI_SUCCESS;
}
EFI_STATUS device_path_read(const void *path, void **buffer, UINTN *size, EFI_HANDLE *device)
{
    const void *tail;
    EFI_STATUS s = device_path_resolve(path, device, &tail);
    if (s)
        return s;
    const uint8_t *p = tail;
    CHAR16 name[480];
    UINTN units = 0;
    while (p[0] == 4 && p[1] == 4) {
        UINTN n = rd16(p + 2);
        for (UINTN i = 4; i + 2 < n; i += 2) {
            if (units + 1 >= 480)
                return EFI_INVALID_PARAMETER;
            name[units++] = rd16(p + i);
        }
        p += n;
    }
    if (!units) {
        static const CHAR16 fallback[] = u"\\EFI\\BOOT\\BOOTAXP64.EFI";
        memcpy(name, fallback, sizeof fallback);
    } else
        name[units] = 0;
    EFI_GUID guid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs;
    s = boot_services.HandleProtocol(*device, &guid, (void **)&fs);
    if (s)
        return s;
    EFI_FILE_PROTOCOL *root = 0, *file = 0;
    s = fs->OpenVolume(fs, &root);
    if (s)
        return s;
    s = root->Open(root, &file, name, EFI_FILE_MODE_READ, 0);
    root->Close(root);
    if (s)
        return s;
    EFI_GUID info_guid = EFI_FILE_INFO_ID;
    uint8_t data[sizeof(EFI_FILE_INFO) + 512];
    UINTN n = sizeof data;
    s = file->GetInfo(file, &info_guid, &n, data);
    EFI_FILE_INFO *info = (void *)data;
    if (!s && ((info->Attribute & EFI_FILE_DIRECTORY) || !info->FileSize ||
               info->FileSize > 4 * 1024 * 1024))
        s = EFI_LOAD_ERROR;
    if (!s) {
        *size = info->FileSize;
        s = boot_services.AllocatePool(EfiLoaderData, *size, buffer);
        if (!s) {
            n = *size;
            s = file->Read(file, &n, *buffer);
            if (!s && n != *size)
                s = EFI_DEVICE_ERROR;
            if (s) {
                boot_services.FreePool(*buffer);
                *buffer = 0;
            }
        }
    }
    file->Close(file);
    return s;
}
/* Publish the partition identity without exposing a compiler-packed struct. */
EFI_STATUS device_path_publish(void)
{
    return protocol_publish(filesystem_handle, &path_guid, &partition_path);
}
#ifndef KOTL_NATIVE
EFI_STATUS platform_partition_path(const void *path)
{
    EFI_STATUS s = storage_mount();
    if (s)
        return s;
    return device_path_partition_matches(path) ? EFI_SUCCESS : EFI_NOT_FOUND;
}
#endif
