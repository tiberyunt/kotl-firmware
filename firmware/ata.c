#include "storage.h"
static unsigned command_port, control_port;
static EFI_STATUS ata_wait(uint8_t mask, uint8_t wanted)
{
    uint64_t start = time_ns();
    do {
        uint8_t s = io_read8(command_port + 7);
        if (s == 0 || s == 0xff)
            return EFI_NO_MEDIA;
        if (!(s & 0x80)) {
            if (s & 0x21)
                return EFI_DEVICE_ERROR;
            if ((s & mask) == wanted)
                return EFI_SUCCESS;
        }
    } while (time_ns() - start < 2000000000ULL);
    return EFI_DEVICE_ERROR;
}
static void select_drive(uint8_t head)
{
    io_write8(command_port + 6, head);
    for (unsigned i = 0; i < 4; i++)
        io_read8(control_port);
}
static EFI_STATUS read_sector(uint64_t lba, void *buffer)
{
    EFI_STATUS s = ata_wait(0x80, 0);
    if (s)
        return s;
    select_drive(0xe0 | ((lba >> 24) & 15));
    io_write8(command_port + 2, 1);
    io_write8(command_port + 3, lba);
    io_write8(command_port + 4, lba >> 8);
    io_write8(command_port + 5, lba >> 16);
    io_write8(command_port + 7, 0x20);
    s = ata_wait(0x88, 8);
    if (s)
        return s;
    uint8_t *p = buffer;
    for (unsigned i = 0; i < 256; i++) {
        uint16_t word = io_read16(command_port);
        p[i * 2] = word;
        p[i * 2 + 1] = word >> 8;
    }
    return ata_wait(0x88, 0);
}
EFI_STATUS storage_read_sectors(uint64_t lba, UINTN count, void *buffer)
{
    for (UINTN i = 0; i < count; i++) {
        EFI_STATUS s = read_sector(lba + i, (uint8_t *)buffer + i * 512);
        if (s)
            return s;
    }
    return EFI_SUCCESS;
}
EFI_STATUS storage_quiesce(void)
{
    return EFI_SUCCESS;
}
EFI_STATUS storage_device_init(void)
{
    if (disk_device.media.MediaPresent)
        return EFI_SUCCESS;
    unsigned bdf;
    /* The PALcode has already assigned BARs. Find CMD646 on PCI bus zero. */
    for (bdf = 8; bdf < 256; bdf += 8)
        if (pci_read32(bdf, 0) == 0x06461095)
            break;
    if (bdf == 256)
        return EFI_NOT_FOUND;
    uint32_t bar0 = pci_read32(bdf, 0x10), bar1 = pci_read32(bdf, 0x14);
    if (!(bar0 & 1) || !(bar1 & 1) || (bar0 & ~3U) > 0xfff8 || (bar1 & ~3U) > 0xfffc)
        return EFI_UNSUPPORTED;
    command_port = bar0 & ~3U;
    control_port = (bar1 & ~3U) + 2;
    io_write8(control_port, 2);
    select_drive(0xa0); /* polled primary master */
    uint8_t status = io_read8(command_port + 7);
    if (!status || status == 0xff)
        return EFI_NO_MEDIA;
    io_write8(command_port + 2, 0);
    io_write8(command_port + 3, 0);
    io_write8(command_port + 4, 0);
    io_write8(command_port + 5, 0);
    io_write8(command_port + 7, 0xec);
    EFI_STATUS s = ata_wait(0x88, 8);
    if (s)
        return s;
    uint16_t identify[256];
    for (unsigned i = 0; i < 256; i++)
        identify[i] = io_read16(command_port);
    if (!(identify[49] & (1 << 9)))
        return EFI_UNSUPPORTED;
    if ((identify[106] & 0xd000) == 0x5000 && (identify[117] != 256 || identify[118]))
        return EFI_UNSUPPORTED;
    uint64_t sectors = identify[60] | ((uint64_t)identify[61] << 16);
    if (!sectors || sectors > 0x10000000ULL)
        return EFI_UNSUPPORTED;
    s = ata_wait(0x88, 0);
    if (s)
        return s;
    block_init(&disk_device, 0, sectors, 0);
    static const EFI_GUID guid = EFI_BLOCK_IO_PROTOCOL_GUID;
    s = protocol_publish(&disk_device, &guid, &disk_device.io);
    if (s)
        return s;
    puts_serial("ATA primary master: ");
    hex_serial(sectors);
    puts_serial(" sectors\n");
    return EFI_SUCCESS;
}
