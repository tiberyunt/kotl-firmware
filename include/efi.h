#ifndef ALPHA_EFI_H
#define ALPHA_EFI_H
#include <stddef.h>
#include <stdint.h>
typedef uint64_t UINTN;
typedef uint64_t EFI_STATUS;
typedef uint16_t CHAR16;
typedef void *EFI_HANDLE;
typedef void *EFI_EVENT;
typedef UINTN EFI_TPL;
typedef void (*EFI_EVENT_NOTIFY)(EFI_EVENT, void *);
#define TPL_APPLICATION 4
#define TPL_CALLBACK 8
#define TPL_NOTIFY 16
#define TPL_HIGH_LEVEL 31
#define EVT_TIMER 0x80000000U
#define EVT_RUNTIME 0x40000000U
#define EVT_NOTIFY_WAIT 0x100U
#define EVT_NOTIFY_SIGNAL 0x200U
#define EVT_SIGNAL_EXIT_BOOT_SERVICES 0x201U
#define EVT_SIGNAL_VIRTUAL_ADDRESS_CHANGE 0x60000202U
typedef enum { TimerCancel, TimerPeriodic, TimerRelative } EFI_TIMER_DELAY;
#define EFI_EVENT_GROUP_EXIT_BOOT_SERVICES                                                         \
    {0x27abf055, 0xb1b8, 0x4c26, {0x80, 0x48, 0x74, 0x8f, 0x37, 0xba, 0xa2, 0xdf}}
#define EFI_EVENT_GROUP_BEFORE_EXIT_BOOT_SERVICES                                                  \
    {0x8be0e274, 0x3970, 0x4b44, {0x80, 0xc5, 0x1a, 0xb9, 0x50, 0x2f, 0x3b, 0xfc}}
#define EFI_EVENT_GROUP_MEMORY_MAP_CHANGE                                                          \
    {0x78bee926, 0x692f, 0x48fd, {0x9e, 0xdb, 0x01, 0x42, 0x2e, 0xf0, 0xd7, 0xab}}
typedef uint64_t EFI_PHYSICAL_ADDRESS;
#define EFIAPI
#define EFI_SUCCESS 0
#define EFIERR(x) (0x8000000000000000ULL | (x))
#define EFI_LOAD_ERROR EFIERR(1)
#define EFI_INVALID_PARAMETER EFIERR(2)
#define EFI_UNSUPPORTED EFIERR(3)
#define EFI_BUFFER_TOO_SMALL EFIERR(5)
#define EFI_NOT_READY EFIERR(6)
#define EFI_OUT_OF_RESOURCES EFIERR(9)
#define EFI_NOT_FOUND EFIERR(14)
#define EFI_ERROR(s) ((s) >> 63)
#define EFI_SYSTEM_TABLE_SIGNATURE 0x5453595320494249ULL
#define EFI_BOOT_SERVICES_SIGNATURE 0x56524553544f4f42ULL
#define EFI_RUNTIME_SERVICES_SIGNATURE 0x56524553544e5552ULL
#define EFI_REVISION 0x00020064U
#define EFI_PAGE_SIZE 4096
#define EFI_VARIABLE_NON_VOLATILE 1U
#define EFI_VARIABLE_BOOTSERVICE_ACCESS 2U
#define EFI_VARIABLE_RUNTIME_ACCESS 4U
#define EFI_VARIABLE_HARDWARE_ERROR_RECORD 8U
#define EFI_VARIABLE_AUTHENTICATED_WRITE_ACCESS 0x10U
#define EFI_VARIABLE_TIME_BASED_AUTHENTICATED_WRITE_ACCESS 0x20U
#define EFI_VARIABLE_APPEND_WRITE 0x40U
#define EFI_VARIABLE_ENHANCED_AUTHENTICATED_ACCESS 0x80U
#ifdef KOTL_NATIVE
#define ALPHA_KSEG 0ULL
#define EFI_RAM_BASE 0x100000000ULL
#define EFI_ALLOC_START 0x800000ULL
#define EFI_TEST_FIXED_ADDRESS (EFI_RAM_BASE + 0x1800000ULL)
#else
#define ALPHA_KSEG 0xfffffc0000000000ULL
#define EFI_RAM_BASE 0ULL
#define EFI_ALLOC_START 0x400000ULL
#define EFI_TEST_FIXED_ADDRESS 0x800000ULL
#endif
#define EFI_ALPHA64_MACHINE 0x0284
#define EfiReservedMemoryType 0
#define EfiLoaderCode 1
#define EfiLoaderData 2
#define EfiBootServicesCode 3
#define EfiBootServicesData 4
#define EfiRuntimeServicesCode 5
#define EfiRuntimeServicesData 6
#define EfiConventionalMemory 7
#define EFI_MEMORY_WB 8
#define AllocateAnyPages 0
#define AllocateMaxAddress 1
#define AllocateAddress 2
#define EFI_SIMPLE_TEXT_INPUT_PROTOCOL_GUID                                                        \
    {0x387477c1, 0x69c7, 0x11d2, {0x8e, 0x39, 0, 0xa0, 0xc9, 0x69, 0x72, 0x3b}}
#define EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL_GUID                                                       \
    {0x387477c2, 0x69c7, 0x11d2, {0x8e, 0x39, 0, 0xa0, 0xc9, 0x69, 0x72, 0x3b}}
#define EFI_LOADED_IMAGE_PROTOCOL_GUID                                                             \
    {0x5b1b31a1, 0x9562, 0x11d2, {0x8e, 0x3f, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b}}
typedef struct {
    uint32_t Data1;
    uint16_t Data2, Data3;
    uint8_t Data4[8];
} EFI_GUID;
typedef struct {
    uint64_t Signature;
    uint32_t Revision, HeaderSize, CRC32, Reserved;
} EFI_TABLE_HEADER;
typedef struct {
    uint32_t Type, Pad;
    uint64_t PhysicalStart, VirtualStart, NumberOfPages, Attribute;
} EFI_MEMORY_DESCRIPTOR;
typedef struct {
    uint16_t ScanCode;
    CHAR16 UnicodeChar;
} EFI_INPUT_KEY;
typedef EFI_STATUS (*EFI_GENERIC)(void);
struct EFI_SYSTEM_TABLE;
struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;
typedef struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL {
    EFI_STATUS (*Reset)(struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *, uint8_t);
    EFI_STATUS (*OutputString)(struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *, const CHAR16 *);
    EFI_STATUS (*TestString)(struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *, const CHAR16 *);
    EFI_STATUS (*QueryMode)(struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *, UINTN, UINTN *, UINTN *);
    EFI_STATUS (*SetMode)(struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *, UINTN);
    EFI_STATUS (*SetAttribute)(struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *, UINTN);
    EFI_STATUS (*ClearScreen)(struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *);
    EFI_STATUS (*SetCursorPosition)(struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *, UINTN, UINTN);
    EFI_STATUS (*EnableCursor)(struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *, uint8_t);
    struct {
        int32_t MaxMode, Mode, Attribute, CursorColumn, CursorRow;
        uint8_t CursorVisible;
    } *Mode;
} EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;
typedef struct EFI_SIMPLE_TEXT_INPUT_PROTOCOL {
    EFI_STATUS (*Reset)(struct EFI_SIMPLE_TEXT_INPUT_PROTOCOL *, uint8_t);
    EFI_STATUS (*ReadKeyStroke)(struct EFI_SIMPLE_TEXT_INPUT_PROTOCOL *, EFI_INPUT_KEY *);
    EFI_EVENT WaitForKey;
} EFI_SIMPLE_TEXT_INPUT_PROTOCOL;
typedef struct {
    EFI_TABLE_HEADER Hdr;
    EFI_TPL (*RaiseTPL)(EFI_TPL);
    void (*RestoreTPL)(EFI_TPL);
    EFI_STATUS (*AllocatePages)(uint32_t, uint32_t, UINTN, EFI_PHYSICAL_ADDRESS *);
    EFI_STATUS (*FreePages)(EFI_PHYSICAL_ADDRESS, UINTN);
    EFI_STATUS (*GetMemoryMap)(UINTN *, EFI_MEMORY_DESCRIPTOR *, UINTN *, UINTN *, uint32_t *);
    EFI_STATUS (*AllocatePool)(uint32_t, UINTN, void **);
    EFI_STATUS (*FreePool)(void *);
    EFI_STATUS (*CreateEvent)(uint32_t, EFI_TPL, EFI_EVENT_NOTIFY, void *, EFI_EVENT *);
    EFI_STATUS (*SetTimer)(EFI_EVENT, EFI_TIMER_DELAY, uint64_t);
    EFI_STATUS (*WaitForEvent)(UINTN, EFI_EVENT *, UINTN *);
    EFI_STATUS (*SignalEvent)(EFI_EVENT);
    EFI_STATUS (*CloseEvent)(EFI_EVENT);
    EFI_STATUS (*CheckEvent)(EFI_EVENT);
    EFI_STATUS (*InstallProtocolInterface)(EFI_HANDLE *, EFI_GUID *, uint32_t, void *);
    EFI_STATUS (*ReinstallProtocolInterface)(EFI_HANDLE, EFI_GUID *, void *, void *);
    EFI_STATUS (*UninstallProtocolInterface)(EFI_HANDLE, EFI_GUID *, void *);
    EFI_STATUS (*HandleProtocol)(EFI_HANDLE, EFI_GUID *, void **);
    void *Reserved;
    EFI_GENERIC RegisterProtocolNotify;
    EFI_STATUS (*LocateHandle)(uint32_t,EFI_GUID *,void *,UINTN *,EFI_HANDLE *);
    EFI_STATUS (*LocateDevicePath)(EFI_GUID *,void **,EFI_HANDLE *);
    EFI_STATUS (*InstallConfigurationTable)(EFI_GUID *, void *);
    EFI_STATUS (*LoadImage)(uint8_t, EFI_HANDLE, void *, void *, UINTN, EFI_HANDLE *);
    EFI_STATUS (*StartImage)(EFI_HANDLE, UINTN *, CHAR16 **);
    EFI_STATUS (*Exit)(EFI_HANDLE,EFI_STATUS,UINTN,CHAR16 *);
    EFI_STATUS (*UnloadImage)(EFI_HANDLE);
    EFI_STATUS (*ExitBootServices)(EFI_HANDLE, UINTN);
    EFI_GENERIC GetNextMonotonicCount;
    EFI_STATUS (*Stall)(UINTN);
    EFI_GENERIC SetWatchdogTimer, ConnectController, DisconnectController;
    EFI_STATUS (*OpenProtocol)(EFI_HANDLE, EFI_GUID *, void **, EFI_HANDLE, EFI_HANDLE, uint32_t);
    EFI_GENERIC CloseProtocol;
    EFI_GENERIC OpenProtocolInformation, ProtocolsPerHandle;
    EFI_STATUS (*LocateHandleBuffer)(uint32_t, EFI_GUID *, void *, UINTN *, EFI_HANDLE **);
    EFI_STATUS (*LocateProtocol)(EFI_GUID *, void *, void **);
    EFI_GENERIC InstallMultipleProtocolInterfaces, UninstallMultipleProtocolInterfaces;
    EFI_STATUS (*CalculateCrc32)(void *, UINTN, uint32_t *);
    void (*CopyMem)(void *, const void *, UINTN);
    void (*SetMem)(void *, UINTN, uint8_t);
    EFI_STATUS (*CreateEventEx)(uint32_t, EFI_TPL, EFI_EVENT_NOTIFY, void *, const EFI_GUID *,
                                EFI_EVENT *);
} EFI_BOOT_SERVICES;
typedef struct {
    EFI_TABLE_HEADER Hdr;
    EFI_GENERIC GetTime, SetTime, GetWakeupTime, SetWakeupTime;
    EFI_GENERIC SetVirtualAddressMap, ConvertPointer;
    EFI_STATUS (*GetVariable)(const CHAR16 *, const EFI_GUID *, uint32_t *, UINTN *, void *);
    EFI_STATUS (*GetNextVariableName)(UINTN *, CHAR16 *, EFI_GUID *);
    EFI_STATUS (*SetVariable)(const CHAR16 *, const EFI_GUID *, uint32_t, UINTN, const void *);
    EFI_GENERIC GetNextHighMonotonicCount;
    void (*ResetSystem)(uint32_t, EFI_STATUS, UINTN, void *);
    EFI_GENERIC UpdateCapsule, QueryCapsuleCapabilities;
    EFI_STATUS (*QueryVariableInfo)(uint32_t, uint64_t *, uint64_t *, uint64_t *);
} EFI_RUNTIME_SERVICES;
#define EFI_RT_PROPERTIES_TABLE_GUID                                                               \
    {0xeb66918a, 0x7eef, 0x402a, {0x84, 0x2e, 0x93, 0x1d, 0x21, 0xc3, 0x8a, 0xe9}}
typedef struct {
    uint16_t Version, Length;
    uint32_t RuntimeServicesSupported;
} EFI_RT_PROPERTIES_TABLE;
typedef struct {
    EFI_GUID VendorGuid;
    void *VendorTable;
} EFI_CONFIGURATION_TABLE;
typedef struct EFI_SYSTEM_TABLE {
    EFI_TABLE_HEADER Hdr;
    CHAR16 *FirmwareVendor;
    uint32_t FirmwareRevision;
    EFI_HANDLE ConsoleInHandle;
    EFI_SIMPLE_TEXT_INPUT_PROTOCOL *ConIn;
    EFI_HANDLE ConsoleOutHandle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *ConOut;
    EFI_HANDLE StandardErrorHandle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *StdErr;
    EFI_RUNTIME_SERVICES *RuntimeServices;
    EFI_BOOT_SERVICES *BootServices;
    UINTN NumberOfTableEntries;
    EFI_CONFIGURATION_TABLE *ConfigurationTable;
} EFI_SYSTEM_TABLE;
typedef struct {
    uint32_t Revision;
    EFI_HANDLE ParentHandle;
    EFI_SYSTEM_TABLE *SystemTable;
    EFI_HANDLE DeviceHandle;
    void *FilePath, *Reserved;
    uint32_t LoadOptionsSize;
    void *LoadOptions, *ImageBase;
    uint64_t ImageSize;
    uint32_t ImageCodeType, ImageDataType;
    EFI_STATUS (*Unload)(EFI_HANDLE);
} EFI_LOADED_IMAGE_PROTOCOL;
_Static_assert(sizeof(EFI_TABLE_HEADER) == 24, "table header ABI");
_Static_assert(sizeof(EFI_MEMORY_DESCRIPTOR) == 40, "memory descriptor ABI");
_Static_assert(offsetof(EFI_SYSTEM_TABLE, BootServices) == 96, "system table ABI");
_Static_assert(offsetof(EFI_BOOT_SERVICES, LoadImage) == 200, "boot services ABI");
#endif
