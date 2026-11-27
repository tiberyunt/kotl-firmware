#ifndef EFI_STORAGE_H
#define EFI_STORAGE_H
#include "efi.h"
#define EFI_DEVICE_ERROR EFIERR(7)
#define EFI_WRITE_PROTECTED EFIERR(8)
#define EFI_VOLUME_CORRUPTED EFIERR(10)
#define EFI_NO_MEDIA EFIERR(12)
#define EFI_MEDIA_CHANGED EFIERR(13)
#define EFI_BAD_BUFFER_SIZE EFIERR(4)
#define EFI_ACCESS_DENIED EFIERR(15)
#define EFI_BLOCK_IO_PROTOCOL_GUID                                                                 \
    {0x964e5b21, 0x6459, 0x11d2, {0x8e, 0x39, 0, 0xa0, 0xc9, 0x69, 0x72, 0x3b}}
#define EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID                                                       \
    {0x964e5b22, 0x6459, 0x11d2, {0x8e, 0x39, 0, 0xa0, 0xc9, 0x69, 0x72, 0x3b}}
#define EFI_FILE_INFO_ID {0x9576e92, 0x6d3f, 0x11d2, {0x8e, 0x39, 0, 0xa0, 0xc9, 0x69, 0x72, 0x3b}}
#define EFI_FILE_MODE_READ 1
#define EFI_FILE_DIRECTORY 0x10
#define ByProtocol 2
#define AllHandles 0
#define EFI_OPEN_PROTOCOL_GET_PROTOCOL 2
#define EFI_OPEN_PROTOCOL_TEST_PROTOCOL 4
typedef struct {
    uint32_t MediaId;
    uint8_t RemovableMedia, MediaPresent, LogicalPartition, ReadOnly, WriteCaching;
    uint32_t BlockSize, IoAlign;
    uint64_t LastBlock;
} EFI_BLOCK_IO_MEDIA;
typedef struct EFI_BLOCK_IO_PROTOCOL {
    uint64_t Revision;
    EFI_BLOCK_IO_MEDIA *Media;
    EFI_STATUS (*Reset)(struct EFI_BLOCK_IO_PROTOCOL *, uint8_t);
    EFI_STATUS (*ReadBlocks)(struct EFI_BLOCK_IO_PROTOCOL *, uint32_t, uint64_t, UINTN, void *);
    EFI_STATUS (*WriteBlocks)(struct EFI_BLOCK_IO_PROTOCOL *, uint32_t, uint64_t, UINTN,
                              const void *);
    EFI_STATUS (*FlushBlocks)(struct EFI_BLOCK_IO_PROTOCOL *);
} EFI_BLOCK_IO_PROTOCOL;
typedef struct {
    uint16_t Year;
    uint8_t Month, Day, Hour, Minute, Second, Pad1;
    uint32_t Nanosecond;
    int16_t TimeZone;
    uint8_t Daylight, Pad2;
} EFI_TIME;
typedef struct {
    uint64_t Size, FileSize, PhysicalSize;
    EFI_TIME CreateTime, LastAccessTime, ModificationTime;
    uint64_t Attribute;
    CHAR16 FileName[1];
} EFI_FILE_INFO;
typedef struct EFI_FILE_PROTOCOL EFI_FILE_PROTOCOL;
struct EFI_FILE_PROTOCOL {
    uint64_t Revision;
    EFI_STATUS (*Open)(EFI_FILE_PROTOCOL *, EFI_FILE_PROTOCOL **, const CHAR16 *, uint64_t,
                       uint64_t);
    EFI_STATUS (*Close)(EFI_FILE_PROTOCOL *);
    EFI_STATUS (*Delete)(EFI_FILE_PROTOCOL *);
    EFI_STATUS (*Read)(EFI_FILE_PROTOCOL *, UINTN *, void *);
    EFI_STATUS (*Write)(EFI_FILE_PROTOCOL *, UINTN *, const void *);
    EFI_STATUS (*GetPosition)(EFI_FILE_PROTOCOL *, uint64_t *);
    EFI_STATUS (*SetPosition)(EFI_FILE_PROTOCOL *, uint64_t);
    EFI_STATUS (*GetInfo)(EFI_FILE_PROTOCOL *, EFI_GUID *, UINTN *, void *);
    EFI_STATUS (*SetInfo)(EFI_FILE_PROTOCOL *, EFI_GUID *, UINTN, void *);
    EFI_STATUS (*Flush)(EFI_FILE_PROTOCOL *);
};
typedef struct EFI_SIMPLE_FILE_SYSTEM_PROTOCOL {
    uint64_t Revision;
    EFI_STATUS (*OpenVolume)(struct EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *, EFI_FILE_PROTOCOL **);
} EFI_SIMPLE_FILE_SYSTEM_PROTOCOL;
_Static_assert(sizeof(EFI_TIME) == 16, "EFI_TIME ABI");
_Static_assert(offsetof(EFI_FILE_INFO, FileName) == 80, "EFI_FILE_INFO ABI");
_Static_assert(offsetof(EFI_BLOCK_IO_MEDIA, LastBlock) == 24, "Block IO media ABI");
#endif
