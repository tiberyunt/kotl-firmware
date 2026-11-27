/* SPDX-License-Identifier: MIT */
#include "settings.h"
#include "efi-storage.h"
#include "kotl.h"
#include "platform.h"
#define MAGIC 0x315445534c544f4bULL
FIRMWARE_SETTINGS firmware_settings;
void settings_defaults(FIRMWARE_SETTINGS *out)
{
    memset(out, 0, sizeof(*out));
    out->magic = MAGIC;
    out->version = 1;
    out->bytes = sizeof(*out);
    out->timeout = 2;
    out->graphics = 1;
}
static int valid(FIRMWARE_SETTINGS *value)
{
    uint32_t checksum = value->checksum;
    value->checksum = 0;
    int ok = value->magic == MAGIC && value->version == 1 && value->bytes == sizeof(*value) &&
             (value->timeout == 2 || value->timeout == 5 || value->timeout == 10) &&
             value->graphics <= 1 && value->preferred_bdf < 256 &&
             value->preferred_partition < 128 && crc32(value, sizeof(*value)) == checksum;
    value->checksum = checksum;
    return ok;
}
int settings_persistent(void)
{
    return *(volatile uint64_t *)(native_boot_info->syscon + 0x3000) == 2;
}
void settings_load(void)
{
    uint64_t words[8];
    for (unsigned i = 0; i < 8; i++)
        words[i] = *(volatile uint64_t *)(native_boot_info->syscon + 0x3100 + i * 8);
    memcpy(&firmware_settings, words, sizeof(words));
    if (!valid(&firmware_settings)) {
        settings_defaults(&firmware_settings);
        puts_serial("Firmware settings: defaults (empty or invalid store)\n");
    } else
        puts_serial("Firmware settings: loaded saved configuration\n");
}
EFI_STATUS settings_save(const FIRMWARE_SETTINGS *value)
{
    uint64_t capabilities = *(volatile uint64_t *)(native_boot_info->syscon + 0x3000);
    if (capabilities != 1 && capabilities != 2)
        return EFI_UNSUPPORTED;
    FIRMWARE_SETTINGS candidate = *value;
    candidate.checksum = 0;
    candidate.checksum = crc32(&candidate, sizeof(candidate));
    if (!valid(&candidate))
        return EFI_INVALID_PARAMETER;
    uint64_t words[8];
    memcpy(words, &candidate, sizeof(words));
    for (unsigned i = 0; i < 8; i++)
        *(volatile uint64_t *)(native_boot_info->syscon + 0x3100 + i * 8) = words[i];
    __asm__ volatile("mb" ::: "memory");
    *(volatile uint64_t *)(native_boot_info->syscon + 0x3008) = 0x4b4f544c53415645ULL;
    __asm__ volatile("mb" ::: "memory");
    if (*(volatile uint64_t *)(native_boot_info->syscon + 0x3008))
        return EFI_DEVICE_ERROR;
    firmware_settings = candidate;
    return EFI_SUCCESS;
}
