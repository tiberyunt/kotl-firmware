/* SPDX-License-Identifier: MIT */
#ifndef KOTL_SETTINGS_H
#define KOTL_SETTINGS_H
#include "efi.h"
typedef struct {
    uint64_t magic;
    uint32_t version, bytes, timeout, graphics, preferred_bdf, preferred_nsid, preferred_partition;
    uint32_t checksum, reserved[6];
} FIRMWARE_SETTINGS;
_Static_assert(sizeof(FIRMWARE_SETTINGS) == 64, "settings wire format");
extern FIRMWARE_SETTINGS firmware_settings;
void settings_defaults(FIRMWARE_SETTINGS *);
void settings_load(void);
EFI_STATUS settings_save(const FIRMWARE_SETTINGS *);
int settings_persistent(void);
#endif
