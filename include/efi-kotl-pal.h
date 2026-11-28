/* Experimental Kotl PAL protocol; provisional GUID/structures. SPDX-License-Identifier: MIT */
#ifndef EFI_KOTL_PAL_H
#define EFI_KOTL_PAL_H
#include "efi.h"
#define EFI_KOTL_PAL_PROTOCOL_GUID {0x4b50414c,0x4556,0x3901,{0x80,0x00,0x4b,0x4f,0x54,0x4c,0x00,0x01}}
#define KOTL_HANDOFF_MAGIC 0x31464f484c544f4bULL
#define KOTL_RESIDENT_PAL 1
/* Physical descriptor. Roots include the architectural format selectors.
 * Kernel arguments, stack/GP and entry are interpreted in the selected regime. */
typedef struct {
    uint64_t magic, version, size, entry, stack, gp, root_u, root_k;
    uint64_t args[6];
} KOTL_OS_HANDOFF;
typedef EFI_STATUS (*KOTL_PAL_ENTER)(void *context);
typedef struct {
    uint64_t revision, ppi, personality, resident_image, supported_models, policy;
} KOTL_PAL_INFO;
typedef struct EFI_KOTL_PAL_PROTOCOL EFI_KOTL_PAL_PROTOCOL;
struct EFI_KOTL_PAL_PROTOCOL {
    uint64_t revision;
    EFI_STATUS (*GetInfo)(EFI_KOTL_PAL_PROTOCOL *, KOTL_PAL_INFO *);
    EFI_STATUS (*Stage)(EFI_KOTL_PAL_PROTOCOL *, const void *, UINTN, uint64_t *);
    EFI_STATUS (*Activate)(EFI_KOTL_PAL_PROTOCOL *, uint64_t image, uint64_t model,
                           const KOTL_OS_HANDOFF *, UINTN, KOTL_PAL_ENTER *, void **);
};
#endif
