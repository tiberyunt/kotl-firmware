#ifndef STORAGE_H
#define STORAGE_H
#include "efi-storage.h"
#include "services.h"
typedef struct {
    EFI_BLOCK_IO_PROTOCOL io;
    EFI_BLOCK_IO_MEDIA media;
    uint64_t first_lba, sectors;
} BLOCK_DEVICE;
extern BLOCK_DEVICE disk_device, esp_device;
EFI_STATUS storage_device_init(void);
EFI_STATUS storage_read_sectors(uint64_t, UINTN, void *);
EFI_STATUS storage_quiesce(void);
void block_init(BLOCK_DEVICE *, uint64_t, uint64_t, int);
EFI_STATUS block_read(BLOCK_DEVICE *, uint64_t, UINTN, void *);
EFI_STATUS storage_mount(void);
EFI_STATUS storage_boot_image(void **, UINTN *, EFI_HANDLE *);
EFI_STATUS storage_select_partition(unsigned);
unsigned storage_partition_count(void);
extern EFI_HANDLE filesystem_handle;
#endif
