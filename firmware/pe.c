/* Static Alpha64 PE32+ loader. SPDX-License-Identifier: MIT */
#include "device-path.h"
#include "services.h"
#define MAX_IMAGES 16
#define MAX_IMAGE_BYTES (4 * 1024 * 1024)
typedef struct {
    EFI_HANDLE handle;
    int loaded, started, active;
    EFI_PHYSICAL_ADDRESS pa;
    UINTN pages, entry;
    EFI_LOADED_IMAGE_PROTOCOL protocol;
    void *path;
    UINTN context[10];
    EFI_STATUS result;
    UINTN exit_bytes;
    CHAR16 *exit_data;
} IMAGE;
static IMAGE images[MAX_IMAGES];
static uint64_t generation;
extern int image_checkpoint(UINTN *) __attribute__((returns_twice));
extern void image_resume(UINTN *) __attribute__((noreturn));
static const EFI_GUID loaded_guid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
static uint16_t u16(const uint8_t *p)
{
    return p[0] | ((uint16_t)p[1] << 8);
}
static uint32_t u32(const uint8_t *p)
{
    return u16(p) | ((uint32_t)u16(p + 2) << 16);
}
static uint64_t u64(const uint8_t *p)
{
    return u32(p) | ((uint64_t)u32(p + 4) << 32);
}
static void put32(uint8_t *p, uint32_t value)
{
    for (unsigned i = 0; i < 4; i++)
        p[i] = value >> (i * 8);
}
static void put64(uint8_t *p, uint64_t value)
{
    for (unsigned i = 0; i < 8; i++)
        p[i] = value >> (i * 8);
}
static int range(UINTN offset, UINTN bytes, UINTN limit)
{
    return offset <= limit && bytes <= limit - offset;
}
static IMAGE *find(EFI_HANDLE handle)
{
    if (!handle)
        return 0;
    for (unsigned i = 0; i < MAX_IMAGES; i++)
        if (images[i].loaded && images[i].handle == handle)
            return &images[i];
    return 0;
}
static int mapped(const uint8_t *table, UINTN sections, UINTN rva, UINTN bytes)
{
    for (UINTN i = 0; i < sections; i++) {
        const uint8_t *section = table + i * 40;
        UINTN va = u32(section + 12), vs = u32(section + 8), raw = u32(section + 16),
              span = vs > raw ? vs : raw;
        if (rva >= va && range(rva - va, bytes, span))
            return 1;
    }
    return 0;
}
static const uint8_t *file_rva(const uint8_t *source, const uint8_t *table, UINTN sections,
                               UINTN rva, UINTN bytes)
{
    for (UINTN i = 0; i < sections; i++) {
        const uint8_t *section = table + i * 40;
        UINTN va = u32(section + 12), raw = u32(section + 16);
        if (rva >= va && range(rva - va, bytes, raw))
            return source + u32(section + 20) + rva - va;
    }
    return 0;
}
static EFI_STATUS fixups(const uint8_t *source, const uint8_t *table, UINTN sections, UINTN headers,
                         UINTN size, UINTN rva, UINTN bytes, uint8_t *seen, uint8_t *destination,
                         uint64_t delta)
{
    if (!bytes)
        return EFI_SUCCESS;
    if (!rva || rva < headers || !range(rva, bytes, size))
        return EFI_LOAD_ERROR;
    const uint8_t *directory = file_rva(source, table, sections, rva, bytes);
    if (!directory)
        return EFI_LOAD_ERROR;
    for (UINTN offset = 0; offset < bytes;) {
        if (!range(offset, 8, bytes))
            return EFI_LOAD_ERROR;
        UINTN page = u32(directory + offset), block = u32(directory + offset + 4);
        if (page % 4096 || page >= size || block < 8 || block % 4 || !range(offset, block, bytes))
            return EFI_LOAD_ERROR;
        for (UINTN i = 8; i < block; i += 2) {
            uint16_t entry = u16(directory + offset + i);
            unsigned type = entry >> 12;
            if (!type)
                continue; /* ABSOLUTE padding has no target */
            UINTN width = type == 10 ? 8 : type == 3 ? 4 : 0;
            if (!width)
                return EFI_UNSUPPORTED;
            UINTN target = page + (entry & 4095);
            if (target < headers || !range(target, width, size) ||
                !mapped(table, sections, target, width) ||
                (target < rva + bytes && rva < target + width))
                return EFI_LOAD_ERROR;
            if (seen)
                for (UINTN byte = target; byte < target + width; byte++) {
                    uint8_t mask = 1U << (byte & 7);
                    if (seen[byte >> 3] & mask)
                        return EFI_LOAD_ERROR;
                    seen[byte >> 3] |= mask;
                }
            if (destination) {
                if (width == 8)
                    put64(destination + target, u64(destination + target) + delta);
                else
                    put32(destination + target, u32(destination + target) + (uint32_t)delta);
            }
        }
        offset += block;
    }
    return EFI_SUCCESS;
}
static EFI_STATUS load_buffer(uint8_t policy, EFI_HANDLE parent, void *path, void *src, UINTN len,
                              EFI_HANDLE *handle)
{
    (void)policy;
    if (!boot_active || !handle || (!src && !path))
        return EFI_INVALID_PARAMETER;
    if (!src)
        return EFI_INVALID_PARAMETER;
    if (events_exiting())
        return EFI_UNSUPPORTED;
    if (parent && !find(parent))
        return EFI_INVALID_PARAMETER;
    const uint8_t *b = src;
    if (len < 64 || u16(b) != 0x5a4d)
        return EFI_LOAD_ERROR;
    UINTN pe = u32(b + 60);
    if (!range(pe, 24, len) || u32(b + pe) != 0x4550)
        return EFI_LOAD_ERROR;
    const uint8_t *coff = b + pe + 4;
    UINTN sections = u16(coff + 2), optsz = u16(coff + 16), opt = pe + 24;
    if (u16(coff) != EFI_ALPHA64_MACHINE)
        return EFI_UNSUPPORTED;
    if (!sections || sections > 16 || optsz < 240 || !range(opt, optsz, len) ||
        !(u16(coff + 18) & 2))
        return EFI_LOAD_ERROR;
    const uint8_t *o = b + opt;
    if (u16(o) != 0x20b || u16(o + 68) != 10)
        return EFI_UNSUPPORTED;
    UINTN entry = u32(o + 16), preferred = u64(o + 24), alignment = u32(o + 32),
          filealign = u32(o + 36), size = u32(o + 56), headers = u32(o + 60);
    int stripped = !!(u16(coff + 18) & 1);
    if (alignment != 4096 || filealign != 512 || !size || size % 4096 || size > MAX_IMAGE_BYTES ||
        preferred % 4096 || size > UINT64_MAX - preferred ||
        (stripped && preferred < ALPHA_KSEG + EFI_RAM_BASE))
        return EFI_LOAD_ERROR;
    if (!range(headers, 0, len) || headers > size || !range(entry, 1, size) || u32(o + 108) > 16)
        return EFI_LOAD_ERROR;
    UINTN reloc_rva = 0, reloc_size = 0;
    for (UINTN i = 0; i < u32(o + 108); i++)
        if (u64(o + 112 + i * 8)) {
            if (i != 5)
                return EFI_UNSUPPORTED;
            reloc_rva = u32(o + 112 + i * 8);
            reloc_size = u32(o + 116 + i * 8);
            if (!reloc_rva || !reloc_size || stripped)
                return EFI_LOAD_ERROR;
        }
    UINTN section_table = opt + optsz;
    if (!range(section_table, sections * 40, len) || headers < section_table + sections * 40)
        return EFI_LOAD_ERROR;
    const uint8_t *table = b + section_table;
    int executable = 0;
    for (UINTN i = 0; i < sections; i++) {
        const uint8_t *s = table + i * 40;
        UINTN vs = u32(s + 8), va = u32(s + 12), raw = u32(s + 16), off = u32(s + 20),
              span = vs > raw ? vs : raw;
        if (!span || va < headers || va % 4096 || off % 512 || !range(va, span, size) ||
            !range(off, raw, len))
            return EFI_LOAD_ERROR;
        for (UINTN j = 0; j < i; j++) {
            const uint8_t *t = table + j * 40;
            UINTN tv = u32(t + 8), tr = u32(t + 16), ta = u32(t + 12), ts = tv > tr ? tv : tr;
            if (va < ta + ts && ta < va + span)
                return EFI_LOAD_ERROR;
        }
        if (entry >= va && entry - va < vs && (u32(s + 36) & 0x20000000))
            executable = 1;
    }
    if (!executable)
        return EFI_LOAD_ERROR;
    IMAGE *image = 0;
    for (unsigned i = 0; i < MAX_IMAGES; i++)
        if (!images[i].handle) {
            image = &images[i];
            image->handle = (void *)((++generation << 8) | 0x80 | i);
            break;
        }
    if (!image)
        return EFI_OUT_OF_RESOURCES;
    EFI_PHYSICAL_ADDRESS pa = preferred >= ALPHA_KSEG + EFI_RAM_BASE ? preferred - ALPHA_KSEG : 0;
    UINTN pages = size / 4096;
    EFI_STATUS status = EFI_NOT_FOUND;
    if (preferred >= ALPHA_KSEG + EFI_RAM_BASE)
        status = boot_services.AllocatePages(AllocateAddress, EfiLoaderCode, pages, &pa);
    if (!stripped && (status == EFI_NOT_FOUND || status == EFI_OUT_OF_RESOURCES))
        status = boot_services.AllocatePages(AllocateAnyPages, EfiLoaderCode, pages, &pa);
    if (status) {
        memset(image, 0, sizeof(*image));
        return status;
    }
    uint8_t *destination = (void *)(ALPHA_KSEG + pa), *seen = 0;
    if (reloc_size) {
        status = boot_services.AllocatePool(EfiBootServicesData, (size + 7) / 8, (void **)&seen);
        if (!status) {
            memset(seen, 0, (size + 7) / 8);
            status = fixups(b, table, sections, headers, size, reloc_rva, reloc_size, seen, 0, 0);
        }
    }
    if (!status) {
        memset(destination, 0, size);
        memcpy(destination, b, headers);
        for (UINTN i = 0; i < sections; i++) {
            const uint8_t *s = table + i * 40;
            memcpy(destination + u32(s + 12), b + u32(s + 20), u32(s + 16));
        }
        status = fixups(b, table, sections, headers, size, reloc_rva, reloc_size, 0, destination,
                        (UINTN)destination - preferred);
    }
    if (seen)
        boot_services.FreePool(seen);
    if (status) {
        boot_services.FreePages(pa, pages);
        memset(image, 0, sizeof(*image));
        return status;
    }
    __asm__ volatile("mb; call_pal 0x86" ::: "memory"); /* IMB */
    image->pa = pa;
    image->pages = pages;
    image->entry = (UINTN)destination + entry;
    image->protocol = (EFI_LOADED_IMAGE_PROTOCOL){.Revision = 0x1000,
                                                  .ParentHandle = parent,
                                                  .SystemTable = &system_table,
                                                  .ImageBase = destination,
                                                  .ImageSize = size,
                                                  .ImageCodeType = EfiLoaderCode,
                                                  .ImageDataType = EfiLoaderData};
    status = protocol_publish(image->handle, &loaded_guid, &image->protocol);
    if (status) {
        boot_services.FreePages(pa, pages);
        memset(image, 0, sizeof(*image));
        return status;
    }
    image->loaded = 1;
    *handle = image->handle;
    return EFI_SUCCESS;
}
EFI_STATUS load_image(uint8_t policy, EFI_HANDLE parent, void *path, void *src, UINTN len,
                      EFI_HANDLE *handle)
{
    if (!boot_active || !handle || (!src && !path) || (parent && !find(parent)))
        return EFI_INVALID_PARAMETER;
    if (events_exiting())
        return EFI_UNSUPPORTED;
    void *buffer = 0, *copy = 0;
    EFI_HANDLE device = 0;
    UINTN bytes = 0, offset = 0;
    EFI_STATUS s = EFI_SUCCESS;
    if (path) {
        s = device_path_validate(path, EFI_DP_MAX, &bytes, &offset);
        if (s)
            return s;
        s = boot_services.AllocatePool(EfiBootServicesData, bytes, &copy);
        if (s)
            return s;
        memcpy(copy, path, bytes);
        if (!src) {
            s = device_path_read(copy, &buffer, &len, &device);
            src = buffer;
        } else {
            const void *tail;
            s = device_path_resolve(copy, &device, &tail);
        }
    }
    if (!s)
        s = load_buffer(policy, parent, path, src, len, handle);
    if (buffer)
        boot_services.FreePool(buffer);
    if (s) {
        if (copy)
            boot_services.FreePool(copy);
        return s;
    }
    IMAGE *image = find(*handle);
    if (copy && ((uint8_t *)copy)[0] == 4 && ((uint8_t *)copy)[1] == 4) {
        void *full = 0;
        if (bytes > EFI_DP_MAX - 42)
            s = EFI_INVALID_PARAMETER;
        else
            s = boot_services.AllocatePool(EfiBootServicesData, bytes + 42, &full);
        if (s) {
            boot_services.FreePool(copy);
            unload_image(*handle);
            *handle = 0;
            return s;
        }
        memcpy(full, partition_path.bytes, 42);
        memcpy((uint8_t *)full + 42, copy, bytes);
        boot_services.FreePool(copy);
        copy = full;
        offset = 42;
    }
    image->path = copy;
    image->protocol.DeviceHandle = device;
    image->protocol.FilePath = copy ? (uint8_t *)copy + offset : 0;
    if (copy) {
        static const EFI_GUID guid = EFI_LOADED_IMAGE_DEVICE_PATH_PROTOCOL_GUID;
        s = protocol_publish(*handle, &guid, copy);
        if (s) {
            unload_image(*handle);
            *handle = 0;
            return s;
        }
    }
    return EFI_SUCCESS;
}
EFI_STATUS start_image(EFI_HANDLE handle, UINTN *size, CHAR16 **data)
{
    IMAGE *volatile image = find(handle);
    if (!boot_active || !image || image->started || events_tpl() != TPL_APPLICATION)
        return EFI_INVALID_PARAMETER;
    if (size)
        *size = 0;
    if (data)
        *data = 0;
    image->started = image->active = 1;
    EFI_HANDLE previous = running_image;
    running_image = handle;
    if (!image_checkpoint(image->context)) {
        EFI_STATUS (*entry)(EFI_HANDLE, EFI_SYSTEM_TABLE *) = (void *)image->entry;
        image->result = entry(handle, &system_table);
    }
    running_image = previous;
    image->active = 0;
    if (image->exit_data) {
        if (size && data) {
            *size = image->exit_bytes;
            *data = image->exit_data;
        } else
            boot_services.FreePool(image->exit_data);
        image->exit_data = 0;
        image->exit_bytes = 0;
    }
    return image->result;
}
EFI_STATUS exit_image(EFI_HANDLE handle, EFI_STATUS status, UINTN size, CHAR16 *data)
{
    IMAGE *image = find(handle);
    if (!boot_active || !image || !image->active || running_image != handle)
        return EFI_INVALID_PARAMETER;
    if (events_exiting() || events_tpl() != TPL_APPLICATION)
        return EFI_UNSUPPORTED;
    if (size && (!data || size > 4096 || (size & 1)))
        return EFI_INVALID_PARAMETER;
    if (size) {
        int terminated = 0;
        for (UINTN i = 0; i < size / 2; i++)
            if (!data[i]) {
                terminated = 1;
                break;
            }
        if (!terminated)
            return EFI_INVALID_PARAMETER;
        EFI_STATUS s =
            boot_services.AllocatePool(EfiBootServicesData, size, (void **)&image->exit_data);
        if (s)
            return s;
        memcpy(image->exit_data, data, size);
        image->exit_bytes = size;
    }
    image->result = status;
    image_resume(image->context);
}
EFI_STATUS unload_image(EFI_HANDLE handle)
{
    IMAGE *image = find(handle);
    if (!boot_active || !image || image->active)
        return EFI_INVALID_PARAMETER;
    if (events_exiting())
        return EFI_UNSUPPORTED;
    image->loaded = 0;
    protocol_remove_handle(handle);
    events_unload_image(handle);
    EFI_STATUS result = boot_services.FreePages(image->pa, image->pages);
    if (result) {
        image->loaded = 1;
        protocol_publish(handle, &loaded_guid, &image->protocol);
        return result;
    }
    if (image->path)
        boot_services.FreePool(image->path);
    memset(image, 0, sizeof(*image));
    return EFI_SUCCESS;
}
void image_set_device(EFI_HANDLE handle, EFI_HANDLE device)
{
    IMAGE *image = find(handle);
    if (image)
        image->protocol.DeviceHandle = device;
}
