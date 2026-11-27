#include "device-path.h"
#include "storage.h"
#define MAX_FILES 16
#define MAX_DIRECTORY_ENTRIES 4096
#define EOC 0x0ffffff8U
EFI_HANDLE filesystem_handle;
static unsigned selected_partition, partition_count;
static struct {
    int mounted;
    uint32_t sectors_per_cluster, cluster_shift, fat_lba, fat_sectors, data_lba, clusters, root;
    uint64_t cached_fat, cached_data;
    uint8_t fat[512], data[512];
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL fs;
} volume;
typedef struct {
    uint32_t cluster, size;
    uint8_t attributes;
    CHAR16 name[256];
} DIRECTORY_ENTRY;
typedef struct {
    EFI_FILE_PROTOCOL api;
    int used;
    DIRECTORY_ENTRY entry;
    uint64_t position;
} FILE_HANDLE;
static FILE_HANDLE files[MAX_FILES];
static uint16_t le16(const uint8_t *p)
{
    return p[0] | ((uint16_t)p[1] << 8);
}
static uint32_t le32(const uint8_t *p)
{
    return le16(p) | ((uint32_t)le16(p + 2) << 16);
}
static uint64_t le64(const uint8_t *p)
{
    return le32(p) | ((uint64_t)le32(p + 4) << 32);
}
static EFI_STATUS cached_read(uint64_t lba, int fat, const uint8_t **out)
{
    uint64_t *tag = fat ? &volume.cached_fat : &volume.cached_data;
    uint8_t *data = fat ? volume.fat : volume.data;
    if (*tag != lba) {
        EFI_STATUS s = block_read(&esp_device, lba, 512, data);
        if (s)
            return s;
        *tag = lba;
    }
    *out = data;
    return EFI_SUCCESS;
}
static int valid_cluster(uint32_t c)
{
    return c >= 2 && c < volume.clusters + 2;
}
static EFI_STATUS next_cluster(uint32_t cluster, uint32_t *next)
{
    if (!valid_cluster(cluster))
        return EFI_VOLUME_CORRUPTED;
    const uint8_t *data;
    EFI_STATUS s = cached_read(volume.fat_lba + (cluster >> 7), 1, &data);
    if (s)
        return s;
    *next = le32(data + (cluster & 127) * 4) & 0x0fffffff;
    if (*next < EOC && !valid_cluster(*next))
        return EFI_VOLUME_CORRUPTED;
    return EFI_SUCCESS;
}
static EFI_STATUS validate_chain(const DIRECTORY_ENTRY *entry)
{
    if (entry->attributes & EFI_FILE_DIRECTORY)
        return valid_cluster(entry->cluster) ? EFI_SUCCESS : EFI_VOLUME_CORRUPTED;
    if (!entry->size)
        return entry->cluster ? EFI_VOLUME_CORRUPTED : EFI_SUCCESS;
    uint64_t count =
        ((uint64_t)entry->size + ((1U << volume.cluster_shift) - 1)) >> volume.cluster_shift;
    if (count > volume.clusters)
        return EFI_VOLUME_CORRUPTED;
    uint32_t cluster = entry->cluster;
    for (uint64_t i = 0; i < count; i++) {
        uint32_t next;
        EFI_STATUS s = next_cluster(cluster, &next);
        if (s)
            return s;
        if (i + 1 == count)
            return next >= EOC ? EFI_SUCCESS : EFI_VOLUME_CORRUPTED;
        if (next >= EOC)
            return EFI_VOLUME_CORRUPTED;
        cluster = next;
    }
    return EFI_VOLUME_CORRUPTED;
}
static EFI_STATUS directory_raw(uint32_t first, UINTN index, uint8_t raw[32])
{
    UINTN skip = index >> (volume.cluster_shift - 5);
    if (skip >= volume.clusters)
        return EFI_VOLUME_CORRUPTED;
    uint32_t cluster = first;
    while (skip--) {
        uint32_t next;
        EFI_STATUS s = next_cluster(cluster, &next);
        if (s)
            return s;
        if (next >= EOC)
            return EFI_NOT_FOUND;
        cluster = next;
    }
    if (!valid_cluster(cluster))
        return EFI_VOLUME_CORRUPTED;
    uint32_t sector = (index >> 4) & (volume.sectors_per_cluster - 1);
    const uint8_t *data;
    EFI_STATUS s = cached_read(volume.data_lba +
                                   ((uint64_t)(cluster - 2) << (volume.cluster_shift - 9)) + sector,
                               0, &data);
    if (s)
        return s;
    memcpy(raw, data + (index & 15) * 32, 32);
    return EFI_SUCCESS;
}
static uint8_t short_checksum(const uint8_t *p)
{
    uint8_t sum = 0;
    for (unsigned i = 0; i < 11; i++)
        sum = ((sum & 1) << 7) + (sum >> 1) + p[i];
    return sum;
}
static unsigned name_length(const CHAR16 *p)
{
    unsigned n = 0;
    while (n < 255 && p[n])
        n++;
    return n;
}
static int name_equal(const CHAR16 *a, const CHAR16 *b)
{
    for (unsigned i = 0; i < 256; i++) {
        CHAR16 x = a[i], y = b[i];
        if (x >= 'a' && x <= 'z')
            x -= 32;
        if (y >= 'a' && y <= 'z')
            y -= 32;
        if (x != y)
            return 0;
        if (!x)
            return 1;
    }
    return 0;
}
/* Return one visible directory entry, retaining the raw-entry offset for enumeration. */
static EFI_STATUS directory_find(uint32_t directory, UINTN start, const CHAR16 *name,
                                 DIRECTORY_ENTRY *entry, UINTN *after)
{
    CHAR16 long_name[260];
    memset(long_name, 0xff, sizeof(long_name));
    unsigned expected = 0, total = 0;
    uint8_t checksum = 0;
    for (UINTN index = start; index < MAX_DIRECTORY_ENTRIES; index++) {
        uint8_t raw[32];
        EFI_STATUS s = directory_raw(directory, index, raw);
        if (s)
            return s;
        if (raw[0] == 0)
            return EFI_NOT_FOUND;
        if (raw[0] == 0xe5) {
            expected = total = 0;
            continue;
        }
        if (raw[11] == 0x0f) {
            unsigned seq = raw[0] & 31;
            if (raw[0] & 0x40) {
                memset(long_name, 0xff, sizeof(long_name));
                expected = total = seq;
                checksum = raw[13];
            }
            if (!seq || seq > 20 || seq != expected || raw[13] != checksum || raw[12] ||
                le16(raw + 26) || (raw[0] & 0xa0)) {
                expected = total = 0;
                continue;
            }
            static const uint8_t offsets[13] = {1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30};
            for (unsigned i = 0; i < 13; i++)
                long_name[(seq - 1) * 13 + i] = le16(raw + offsets[i]);
            expected--;
            continue;
        }
        int have_long = total && !expected && short_checksum(raw) == checksum;
        if (raw[11] & 8) {
            expected = total = 0;
            continue;
        }
        memset(entry, 0, sizeof(*entry));
        entry->attributes = raw[11] & 0x3f;
        entry->cluster = ((uint32_t)le16(raw + 20) << 16) | le16(raw + 26);
        entry->size = le32(raw + 28);
        if (have_long) {
            unsigned n = 0;
            while (n < total * 13 && n < 255 && long_name[n] && long_name[n] != 0xffff) {
                entry->name[n] = long_name[n];
                n++;
            }
            entry->name[n] = 0;
            if (n == 255 && total * 13 > 255)
                have_long = 0;
        }
        if (!have_long) {
            unsigned n = 0;
            for (unsigned i = 0; i < 8 && raw[i] != ' '; i++)
                entry->name[n++] = (i == 0 && raw[i] == 5) ? 0xe5 : raw[i];
            if (raw[8] != ' ') {
                entry->name[n++] = '.';
                for (unsigned i = 8; i < 11 && raw[i] != ' '; i++)
                    entry->name[n++] = raw[i];
            }
            entry->name[n] = 0;
        }
        expected = total = 0;
        if (!name || name_equal(name, entry->name)) {
            *after = index + 1;
            return EFI_SUCCESS;
        }
    }
    return EFI_VOLUME_CORRUPTED;
}
static FILE_HANDLE *as_file(EFI_FILE_PROTOCOL *api)
{
    if (!boot_active)
        return 0;
    for (unsigned i = 0; i < MAX_FILES; i++)
        if (files[i].used && api == &files[i].api)
            return &files[i];
    return 0;
}
static EFI_STATUS fill_info(const DIRECTORY_ENTRY *entry, UINTN *size, void *buffer)
{
    if (!size)
        return EFI_INVALID_PARAMETER;
    UINTN need = offsetof(EFI_FILE_INFO, FileName) + (name_length(entry->name) + 1) * 2;
    if (*size < need) {
        *size = need;
        return EFI_BUFFER_TOO_SMALL;
    }
    if (!buffer)
        return EFI_INVALID_PARAMETER;
    *size = need;
    memset(buffer, 0, need);
    EFI_FILE_INFO *info = buffer;
    info->Size = need;
    info->FileSize = entry->size;
    info->PhysicalSize =
        (((uint64_t)entry->size + ((1U << volume.cluster_shift) - 1)) >> volume.cluster_shift)
        << volume.cluster_shift;
    info->Attribute = entry->attributes;
    memcpy(info->FileName, entry->name, need - offsetof(EFI_FILE_INFO, FileName));
    return EFI_SUCCESS;
}
static EFI_STATUS read_content(const DIRECTORY_ENTRY *entry, uint64_t position, UINTN *size,
                               void *buffer)
{
    if (position >= entry->size) {
        *size = 0;
        return EFI_SUCCESS;
    }
    UINTN remaining = entry->size - position;
    if (*size < remaining)
        remaining = *size;
    if (remaining && !buffer)
        return EFI_INVALID_PARAMETER;
    *size = 0;
    uint8_t *out = buffer;
    uint32_t cluster = entry->cluster;
    UINTN skip = position >> volume.cluster_shift;
    while (skip--) {
        uint32_t next;
        EFI_STATUS s = next_cluster(cluster, &next);
        if (s)
            return s;
        if (next >= EOC)
            return EFI_VOLUME_CORRUPTED;
        cluster = next;
    }
    UINTN offset = position & ((1U << volume.cluster_shift) - 1);
    while (remaining) {
        if (!valid_cluster(cluster))
            return EFI_VOLUME_CORRUPTED;
        uint32_t sector = offset >> 9;
        UINTN in_sector = offset & 511;
        const uint8_t *data;
        EFI_STATUS s = cached_read(
            volume.data_lba + ((uint64_t)(cluster - 2) << (volume.cluster_shift - 9)) + sector, 0,
            &data);
        if (s)
            return s;
        UINTN take = 512 - in_sector;
        if (take > remaining)
            take = remaining;
        memcpy(out, data + in_sector, take);
        out += take;
        remaining -= take;
        *size += take;
        offset += take;
        if (offset == (1U << volume.cluster_shift) && remaining) {
            uint32_t next;
            s = next_cluster(cluster, &next);
            if (s)
                return s;
            if (next >= EOC)
                return EFI_VOLUME_CORRUPTED;
            cluster = next;
            offset = 0;
        }
    }
    return EFI_SUCCESS;
}
static EFI_STATUS close_file(EFI_FILE_PROTOCOL *api)
{
    FILE_HANDLE *f = as_file(api);
    if (!f)
        return EFI_INVALID_PARAMETER;
    memset(f, 0, sizeof(*f));
    return EFI_SUCCESS;
}
static EFI_STATUS delete_file(EFI_FILE_PROTOCOL *api)
{
    EFI_STATUS s = close_file(api);
    return s ? s : 2; /* EFI_WARN_DELETE_FAILURE, handle is closed. */
}
static EFI_STATUS file_read(EFI_FILE_PROTOCOL *api, UINTN *size, void *buffer)
{
    FILE_HANDLE *f = as_file(api);
    if (!f || !size)
        return EFI_INVALID_PARAMETER;
    if (f->entry.attributes & EFI_FILE_DIRECTORY) {
        DIRECTORY_ENTRY entry;
        UINTN after;
        EFI_STATUS s = directory_find(f->entry.cluster, f->position, 0, &entry, &after);
        if (s == EFI_NOT_FOUND) {
            *size = 0;
            return EFI_SUCCESS;
        }
        if (s)
            return s;
        s = fill_info(&entry, size, buffer);
        if (!s)
            f->position = after;
        return s;
    }
    if (*size && !buffer)
        return EFI_INVALID_PARAMETER;
    EFI_STATUS s = read_content(&f->entry, f->position, size, buffer);
    f->position += *size;
    return s;
}
static EFI_STATUS file_write(EFI_FILE_PROTOCOL *api, UINTN *size, const void *buffer)
{
    (void)buffer;
    if (!as_file(api) || !size)
        return EFI_INVALID_PARAMETER;
    return EFI_WRITE_PROTECTED;
}
static EFI_STATUS get_position(EFI_FILE_PROTOCOL *api, uint64_t *position)
{
    FILE_HANDLE *f = as_file(api);
    if (!f || !position)
        return EFI_INVALID_PARAMETER;
    if (f->entry.attributes & EFI_FILE_DIRECTORY)
        return EFI_UNSUPPORTED;
    *position = f->position;
    return EFI_SUCCESS;
}
static EFI_STATUS set_position(EFI_FILE_PROTOCOL *api, uint64_t position)
{
    FILE_HANDLE *f = as_file(api);
    if (!f)
        return EFI_INVALID_PARAMETER;
    if (f->entry.attributes & EFI_FILE_DIRECTORY) {
        if (position)
            return EFI_UNSUPPORTED;
        f->position = 0;
        return EFI_SUCCESS;
    }
    f->position = position == UINT64_MAX ? f->entry.size : position;
    return EFI_SUCCESS;
}
static EFI_STATUS get_info(EFI_FILE_PROTOCOL *api, EFI_GUID *guid, UINTN *size, void *buffer)
{
    static const EFI_GUID info_guid = EFI_FILE_INFO_ID;
    FILE_HANDLE *f = as_file(api);
    if (!f || !guid || !size)
        return EFI_INVALID_PARAMETER;
    if (memcmp(guid, &info_guid, sizeof(*guid)))
        return EFI_UNSUPPORTED;
    return fill_info(&f->entry, size, buffer);
}
static EFI_STATUS set_info(EFI_FILE_PROTOCOL *api, EFI_GUID *guid, UINTN size, void *buffer)
{
    (void)guid;
    (void)size;
    (void)buffer;
    return as_file(api) ? EFI_WRITE_PROTECTED : EFI_INVALID_PARAMETER;
}
static EFI_STATUS flush_file(EFI_FILE_PROTOCOL *api)
{
    return as_file(api) ? EFI_SUCCESS : EFI_INVALID_PARAMETER;
}
static EFI_STATUS new_file(const DIRECTORY_ENTRY *, EFI_FILE_PROTOCOL **);
static EFI_STATUS file_open(EFI_FILE_PROTOCOL *api, EFI_FILE_PROTOCOL **out, const CHAR16 *path,
                            uint64_t mode, uint64_t attributes)
{
    (void)attributes;
    FILE_HANDLE *f = as_file(api);
    if (!f || !out || !path)
        return EFI_INVALID_PARAMETER;
    if (mode != EFI_FILE_MODE_READ)
        return EFI_WRITE_PROTECTED;
    DIRECTORY_ENTRY entry = f->entry;
    if (*path == '\\' || *path == '/') {
        entry.cluster = volume.root;
        entry.size = 0;
        entry.attributes = EFI_FILE_DIRECTORY;
        entry.name[0] = '\\';
        entry.name[1] = 0;
        while (*path == '\\' || *path == '/')
            path++;
    }
    unsigned components = 0, total = 0;
    while (*path) {
        if (++components > 32)
            return EFI_UNSUPPORTED;
        if (!(entry.attributes & EFI_FILE_DIRECTORY))
            return EFI_NOT_FOUND;
        CHAR16 name[256];
        unsigned n = 0;
        while (*path && *path != '\\' && *path != '/') {
            if (n == 255 || ++total > 1024)
                return EFI_UNSUPPORTED;
            name[n++] = *path++;
        }
        name[n] = 0;
        while (*path == '\\' || *path == '/')
            path++;
        if (n == 1 && name[0] == '.')
            continue;
        if (n == 2 && name[0] == '.' && name[1] == '.' && entry.cluster == volume.root)
            continue;
        UINTN after;
        EFI_STATUS s = directory_find(entry.cluster, 0, name, &entry, &after);
        if (s)
            return s;
        if ((entry.attributes & EFI_FILE_DIRECTORY) && entry.cluster == 0)
            entry.cluster = volume.root;
    }
    EFI_STATUS s = validate_chain(&entry);
    if (s)
        return s;
    return new_file(&entry, out);
}
static EFI_STATUS new_file(const DIRECTORY_ENTRY *entry, EFI_FILE_PROTOCOL **out)
{
    for (unsigned i = 0; i < MAX_FILES; i++)
        if (!files[i].used) {
            files[i].used = 1;
            files[i].position = 0;
            files[i].entry = *entry;
            files[i].api = (EFI_FILE_PROTOCOL){0x10000,   file_open,  close_file,   delete_file,
                                               file_read, file_write, get_position, set_position,
                                               get_info,  set_info,   flush_file};
            *out = &files[i].api;
            return EFI_SUCCESS;
        }
    return EFI_OUT_OF_RESOURCES;
}
static EFI_STATUS open_volume(EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs, EFI_FILE_PROTOCOL **out)
{
    if (!boot_active || fs != &volume.fs || !out)
        return EFI_INVALID_PARAMETER;
    DIRECTORY_ENTRY root = {
        .cluster = volume.root, .attributes = EFI_FILE_DIRECTORY, .name = {'\\', 0}};
    return new_file(&root, out);
}
static EFI_STATUS gpt_partition(void)
{
    uint8_t header[512];
    EFI_STATUS s = block_read(&disk_device, 0, 512, header);
    if (s)
        return s;
    if (le16(header + 510) != 0xaa55)
        return EFI_VOLUME_CORRUPTED;
    int protective = 0;
    for (unsigned i = 0; i < 4; i++)
        if (header[446 + i * 16 + 4] == 0xee && le32(header + 446 + i * 16 + 8) == 1)
            protective = 1;
    if (!protective)
        return EFI_UNSUPPORTED;
    s = block_read(&disk_device, 1, 512, header);
    if (s)
        return s;
    if (memcmp(header, "EFI PART", 8) || le32(header + 8) != 0x10000)
        return EFI_VOLUME_CORRUPTED;
    uint32_t size = le32(header + 12), checksum = le32(header + 16);
    if (size < 92 || size > 512 || le32(header + 20) || le64(header + 24) != 1 ||
        le64(header + 32) != disk_device.media.LastBlock)
        return EFI_VOLUME_CORRUPTED;
    memset(header + 16, 0, 4);
    if (crc32(header, size) != checksum)
        return EFI_VOLUME_CORRUPTED;
    uint64_t first = le64(header + 40), last = le64(header + 48), table = le64(header + 72);
    uint32_t count = le32(header + 80), stride = le32(header + 84), table_crc = le32(header + 88);
    if (!count || count > 128 || stride != 128 || table < 2 || first > last ||
        last >= disk_device.media.LastBlock)
        return EFI_VOLUME_CORRUPTED;
    UINTN bytes = (UINTN)count * 128, rounded = (bytes + 511) & ~511ULL;
    if (first < 2 + (rounded >> 9) || table >= first || (rounded >> 9) > first - table ||
        (rounded >> 9) >= disk_device.media.LastBlock ||
        last >= disk_device.media.LastBlock - (rounded >> 9))
        return EFI_VOLUME_CORRUPTED;
    uint8_t *entries;
    s = boot_services.AllocatePool(EfiBootServicesData, rounded, (void **)&entries);
    if (s)
        return s;
    s = block_read(&disk_device, table, rounded, entries);
    if (!s && crc32(entries, bytes) != table_crc)
        s = EFI_VOLUME_CORRUPTED;
    static const uint8_t esp_type[16] = {0x28, 0x73, 0x2a, 0xc1, 0x1f, 0xf8, 0xd2, 0x11,
                                         0xba, 0x4b, 0,    0xa0, 0xc9, 0x3e, 0xc9, 0x3b};
    uint64_t begin = 0, end = 0;
    partition_count = 0;
    if (!s) {
        s = EFI_NOT_FOUND;
        for (unsigned i = 0; i < count; i++) {
            const uint8_t *e = entries + i * 128;
            if (memcmp(e, esp_type, 16))
                continue;
            unsigned index = partition_count++;
            if (index == selected_partition) {
                memset(&partition_path,0,sizeof(partition_path));
                uint8_t *dp=partition_path.bytes;
                dp[0]=4;dp[1]=1;dp[2]=42;
                uint32_t number=i+1;
                memcpy(dp+4,&number,4);memcpy(dp+8,e+32,8);
                uint64_t sectors=le64(e+40)-le64(e+32)+1;
                memcpy(dp+16,&sectors,8);memcpy(dp+24,e+16,16);
                dp[40]=dp[41]=2;memcpy(dp+42,(uint8_t[]){0x7f,0xff,4,0},4);
                begin = le64(e + 32);
                end = le64(e + 40);
                s = (begin < first || end > last || begin > end) ? EFI_VOLUME_CORRUPTED
                                                                 : EFI_SUCCESS;
            }
        }
    }
    boot_services.FreePool(entries);
    if (s)
        return s;
    block_init(&esp_device, begin, end - begin + 1, 1);
    return EFI_SUCCESS;
}
EFI_STATUS storage_mount(void)
{
    if (volume.mounted)
        return EFI_SUCCESS;
    EFI_STATUS s = storage_device_init();
    if (s)
        return s;
    s = gpt_partition();
    if (s)
        return s;
    uint8_t b[512];
    s = block_read(&esp_device, 0, 512, b);
    if (s)
        return s;
    uint32_t spc = b[13], reserved = le16(b + 14), fats = b[16], fat_size = le32(b + 36),
             total = le32(b + 32), root = le32(b + 44);
    if (le16(b + 510) != 0xaa55 || le16(b + 11) != 512 || !spc || (spc & (spc - 1)) || spc > 128 ||
        !reserved || (fats != 1 && fats != 2) || le16(b + 17) || le16(b + 19) || le16(b + 22) ||
        !fat_size || le16(b + 42))
        return EFI_VOLUME_CORRUPTED;
    if ((le16(b + 40) & 0x80) && (le16(b + 40) & 15) >= fats)
        return EFI_VOLUME_CORRUPTED;
    uint64_t data = (uint64_t)reserved + (uint64_t)fats * fat_size;
    if (total > esp_device.sectors || data >= total)
        return EFI_VOLUME_CORRUPTED;
    unsigned shift = 0;
    while ((1U << shift) < spc)
        shift++;
    uint64_t clusters = (total - data) >> shift;
    if (clusters < 65525 || clusters > 0x0fffffee || fat_size * 128ULL < clusters + 2 || root < 2 ||
        root >= clusters + 2)
        return EFI_VOLUME_CORRUPTED;
    volume.sectors_per_cluster = spc;
    volume.cluster_shift = shift + 9;
    volume.fat_lba = reserved + ((le16(b + 40) & 0x80) ? (le16(b + 40) & 15) * fat_size : 0);
    volume.fat_sectors = fat_size;
    volume.data_lba = data;
    volume.clusters = clusters;
    volume.root = root;
    volume.cached_fat = volume.cached_data = UINT64_MAX;
    static const EFI_GUID block_guid = EFI_BLOCK_IO_PROTOCOL_GUID,
                          fs_guid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
    volume.fs = (EFI_SIMPLE_FILE_SYSTEM_PROTOCOL){0x10000, open_volume};
    s = protocol_publish(&esp_device, &block_guid, &esp_device.io);
    if (s)
        return s;
    s = protocol_publish(&esp_device, &fs_guid, &volume.fs);
    if (s) {
        protocol_remove_handle(&esp_device);
        return s;
    }
    volume.mounted = 1;
    filesystem_handle = &esp_device;
    s=device_path_publish();
    if(s){protocol_remove_handle(&esp_device);filesystem_handle=0;volume.mounted=0;return s;}
    puts_serial("Mounted GPT EFI System Partition (FAT32, read-only)\n");
    return EFI_SUCCESS;
}
EFI_STATUS storage_boot_image(void **buffer, UINTN *size, EFI_HANDLE *device)
{
    if (!buffer || !size || !device)
        return EFI_INVALID_PARAMETER;
    EFI_STATUS s = storage_mount();
    if (s)
        return s;
    EFI_FILE_PROTOCOL *root = 0, *file = 0;
    s = volume.fs.OpenVolume(&volume.fs, &root);
    if (s)
        return s;
    static const CHAR16 path[] = {'\\', 'E', 'F', 'I', '\\', 'B', 'O', 'O', 'T', '\\', 'B', 'O',
                                  'O',  'T', 'A', 'X', 'P',  '6', '4', '.', 'E', 'F',  'I', 0};
    s = root->Open(root, &file, path, EFI_FILE_MODE_READ, 0);
    root->Close(root);
    if (s)
        return s;
    FILE_HANDLE *f = as_file(file);
    if ((f->entry.attributes & EFI_FILE_DIRECTORY) || !f->entry.size ||
        f->entry.size > 4 * 1024 * 1024) {
        file->Close(file);
        return EFI_LOAD_ERROR;
    }
    *size = f->entry.size;
    s = boot_services.AllocatePool(EfiLoaderData, *size, buffer);
    if (!s) {
        UINTN n = *size;
        s = file->Read(file, &n, *buffer);
        if (!s && n != *size)
            s = EFI_DEVICE_ERROR;
        if (s) {
            boot_services.FreePool(*buffer);
            *buffer = 0;
        }
    }
    file->Close(file);
    if (!s)
        *device = filesystem_handle;
    return s;
}

/* The boot manager switches volumes only before starting an application.
 * Existing File protocol pointers must never survive such a switch. */
EFI_STATUS storage_select_partition(unsigned index)
{
    if (running_image)
        return EFI_NOT_READY;
    for (unsigned i = 0; i < MAX_FILES; i++)
        if (files[i].used)
            return EFI_NOT_READY;
    protocol_remove_handle(&esp_device);
    memset(&volume, 0, sizeof(volume));
    filesystem_handle = 0;
    selected_partition = index;
    partition_count = 0;
    return EFI_SUCCESS;
}
unsigned storage_partition_count(void)
{
    return partition_count;
}
