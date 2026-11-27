#include "storage.h"
BLOCK_DEVICE disk_device, esp_device;
EFI_STATUS block_read(BLOCK_DEVICE *device, uint64_t lba, UINTN bytes, void *buffer)
{
    if (!boot_active || !device || (!buffer && bytes))
        return EFI_INVALID_PARAMETER;
    if (!device->media.MediaPresent)
        return EFI_NO_MEDIA;
    if (bytes % 512)
        return EFI_BAD_BUFFER_SIZE;
    UINTN count = bytes / 512;
    if (!count)
        return EFI_SUCCESS;
    if (lba >= device->sectors || count > device->sectors - lba)
        return EFI_INVALID_PARAMETER;
    return storage_read_sectors(device->first_lba + lba, count, buffer);
}
static int valid_io(EFI_BLOCK_IO_PROTOCOL *io)
{
    return io == &disk_device.io || io == &esp_device.io;
}
static EFI_STATUS read_blocks(EFI_BLOCK_IO_PROTOCOL *io, uint32_t id, uint64_t lba, UINTN bytes,
                              void *p)
{
    if (!boot_active || !valid_io(io))
        return EFI_INVALID_PARAMETER;
    if (id != io->Media->MediaId)
        return EFI_MEDIA_CHANGED;
    return block_read((BLOCK_DEVICE *)io, lba, bytes, p);
}
static EFI_STATUS write_blocks(EFI_BLOCK_IO_PROTOCOL *io, uint32_t id, uint64_t lba, UINTN bytes,
                               const void *p)
{
    (void)lba;
    (void)bytes;
    (void)p;
    if (!boot_active || !valid_io(io))
        return EFI_INVALID_PARAMETER;
    if (id != io->Media->MediaId)
        return EFI_MEDIA_CHANGED;
    return EFI_WRITE_PROTECTED;
}
static EFI_STATUS reset(EFI_BLOCK_IO_PROTOCOL *io, uint8_t extended)
{
    (void)extended;
    if (!boot_active || !valid_io(io))
        return EFI_INVALID_PARAMETER;
    return io->Media->MediaPresent ? EFI_SUCCESS : EFI_NO_MEDIA;
}
static EFI_STATUS flush(EFI_BLOCK_IO_PROTOCOL *io)
{
    if (!boot_active || !valid_io(io))
        return EFI_INVALID_PARAMETER;
    return EFI_SUCCESS;
}
void block_init(BLOCK_DEVICE *dev, uint64_t first, uint64_t count, int partition)
{
    dev->first_lba = first;
    dev->sectors = count;
    dev->media = (EFI_BLOCK_IO_MEDIA){.MediaId = 1,
                                      .MediaPresent = 1,
                                      .LogicalPartition = partition,
                                      .ReadOnly = 1,
                                      .BlockSize = 512,
                                      .IoAlign = 1,
                                      .LastBlock = count - 1};
    dev->io =
        (EFI_BLOCK_IO_PROTOCOL){0x10000, &dev->media, reset, read_blocks, write_blocks, flush};
}
