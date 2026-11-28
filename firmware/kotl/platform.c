/* SPDX-License-Identifier: MIT */
#include "platform.h"
#include "kotl.h"
#include "storage.h"
KOTL_BOOT_INFO *native_boot_info;
static void barrier(void)
{
    __asm__ volatile("mb" ::: "memory");
}
void serial_init(void)
{
    volatile uint8_t *p = (void *)native_boot_info->uart;
    p[1] = 0;
    p[3] = 0x80;
    p[0] = 1;
    p[1] = 0;
    p[3] = 3;
    p[2] = 7;
    p[4] = 3;
    barrier();
}
void putchar_serial(char c)
{
    volatile uint8_t *p = (void *)native_boot_info->uart;
    while (!(p[5] & 32)) {
    }
    p[0] = (uint8_t)c;
    display_putc(c);
    barrier();
}
static uint32_t read_ui_key(void)
{
    uint64_t value = *(volatile uint64_t *)(native_boot_info->syscon + 0x2008);
    return value >> 63 ? (uint32_t)value : 0;
}
uint32_t platform_key(void)
{
    uint32_t key = read_ui_key();
    if (key)
        return key;
    volatile uint8_t *p = (void *)native_boot_info->uart;
    return (p[5] & 1) ? p[0] : 0;
}
int getchar_serial(void)
{
    uint32_t key = read_ui_key();
    if (key && !(key >> 16))
        return key;

    volatile uint8_t *p = (void *)native_boot_info->uart;
    return (p[5] & 1) ? p[0] : -1;
}
uint32_t pci_read32(unsigned bdf, unsigned reg)
{
    uint32_t value = *(volatile uint32_t *)(native_boot_info->ecam + ((uint64_t)bdf << 12) + reg);
    barrier();
    return value;
}
uint64_t time_ns(void)
{
    KSI_RESULT result = ksi_call(KSI_TIME, 0);
    if (result.error)
        kotl_fatal();
    return (result.value / 3) * 125 + (result.value % 3) * 125 / 3;
}
void *platform_dtb(void)
{
    return (void *)native_boot_info->dtb;
}
EFI_STATUS platform_exit_boot(void)
{
    EFI_STATUS status = storage_quiesce();
    if (status)
        return status;
    if (ksi_call(KSI_BOOT_EXIT, 0).error)
        return EFI_UNSUPPORTED;
    display_stop();
    return EFI_SUCCESS;
}
void halt(void)
{
    ksi_call(KSI_SYSTEM_OFF, 0);
    for (;;)
        __asm__ volatile(".long 0x47ff041f");
}
void kotl_fatal(void)
{
    puts_serial("FAIL: native firmware trap cause=");
    hex_serial(KOTL_READ_CSR(0x102));
    puts_serial(" EPC=");
    hex_serial(KOTL_READ_CSR(0x101));
    puts_serial(" TVAL=");
    hex_serial(KOTL_READ_CSR(0x103));
    puts_serial("\n");
    halt();
}

void platform_reset(uint32_t type)
{
    if (type == 0 || type == 1)
        ksi_call(KSI_SYSTEM_RESET, 0);
    halt();
}

int platform_variables_persistent(void)
{
    return *(volatile uint64_t *)(native_boot_info->syscon + 0x4000) == 2;
}
EFI_STATUS platform_variables_load(void *buffer, UINTN size)
{
    if (!platform_variables_persistent())
        return EFI_UNSUPPORTED;
    if (size != 65536)
        return EFI_INVALID_PARAMETER;
    uint64_t *out = buffer;
    for (unsigned i = 0; i < 8192; i++)
        out[i] = *(volatile uint64_t *)(native_boot_info->syscon + 0x10000 + i * 8);
    return EFI_SUCCESS;
}
EFI_STATUS platform_variables_save(const void *buffer, UINTN size)
{
    if (!platform_variables_persistent())
        return EFI_UNSUPPORTED;
    if (size != 65536)
        return EFI_INVALID_PARAMETER;
    const uint64_t *source = buffer;
    for (unsigned i = 0; i < 8192; i++)
        *(volatile uint64_t *)(native_boot_info->syscon + 0x10000 + i * 8) = source[i];
    barrier();
    *(volatile uint64_t *)(native_boot_info->syscon + 0x4008) = 0x4b4f544c56415253ULL;
    barrier();
    return *(volatile uint64_t *)(native_boot_info->syscon + 0x4008) ? EFI_DEVICE_ERROR
                                                                     : EFI_SUCCESS;
}
