/* SPDX-License-Identifier: MIT */
#ifndef EFI_DEVICE_PATH_H
#define EFI_DEVICE_PATH_H
#include "efi.h"
#define EFI_DEVICE_PATH_PROTOCOL_GUID {0x9576e91,0x6d3f,0x11d2,{0x8e,0x39,0,0xa0,0xc9,0x69,0x72,0x3b}}
#define EFI_LOADED_IMAGE_DEVICE_PATH_PROTOCOL_GUID {0xbc62157e,0x3e33,0x4fec,{0x99,0x20,0x2d,0x3b,0x36,0xd7,0x50,0xdf}}
#define EFI_GLOBAL_VARIABLE_GUID {0x8be4df61,0x93ca,0x11d2,{0xaa,0x0d,0,0xe0,0x98,3,0x2b,0x8c}}
#define EFI_DP_MAX 1024
#define EFI_DP_HD_SIZE 42
#define EFI_DP_END_SIZE 4
/* Device paths are packed byte streams, including unaligned 64-bit fields. */
typedef struct { uint8_t Type,SubType,Length[2]; } EFI_DEVICE_PATH_PROTOCOL;
typedef struct { uint8_t bytes[EFI_DP_HD_SIZE+EFI_DP_END_SIZE]; } EFI_PARTITION_PATH;
#endif
