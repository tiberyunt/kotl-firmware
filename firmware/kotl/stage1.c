/* SPDX-License-Identifier: MIT */
#include "kotl.h"
static KOTL_BOOT_INFO boot __attribute__((section(".bootinfo")));
static int ownership_released;
extern const uint8_t native_firmware_start[], native_firmware_end[];
extern void stage1_trap(void), stage1_ksi(void);
static void barrier(void)
{
    __asm__ volatile("mb" ::: "memory");
}
static void putc_direct(char c)
{
    uint64_t uart = boot.uart ? boot.uart : 0x10000000;
    while (!(*(volatile uint8_t *)(uart + 5) & 32)) {
    }
    *(volatile uint8_t *)uart = c;
    barrier();
}
static void text(const char *p)
{
    while (*p) {
        if (*p == '\n')
            putc_direct('\r');
        putc_direct(*p++);
    }
}
static void stop(int failed)
{
    *(volatile uint64_t *)(boot.syscon ? boot.syscon + 0x100 : 0x02000100) = failed ? 0x3333 : 1;
    for (;;)
        __asm__ volatile("br $31,.");
}
void *memcpy(void *d, const void *s, size_t n)
{
    uint8_t *a = d;
    const uint8_t *b = s;
    while (n--)
        *a++ = *b++;
    return d;
}
void *memset(void *d, int v, size_t n)
{
    uint8_t *a = d;
    while (n--)
        *a++ = v;
    return d;
}
static int supported(uint64_t function)
{
    return function <= KSI_TIME || function == KSI_BOOT_EXIT || function == KSI_WALLTIME;
}
void stage1_service(uint64_t *registers)
{
    uint64_t function = registers[16], argument = registers[17], value = 0;
    int64_t error = KSI_OK;
    switch (function) {
    case KSI_VERSION:
        value = 0x00010000;
        break;
    case KSI_PROBE:
        value = supported(argument);
        break;
    case KSI_SYSTEM_OFF:
        if (ownership_released)
            text("KSI: firmware ownership released; runtime SYSTEM_OFF\n");
        stop(0);
        break;
    case KSI_SYSTEM_RESET:
        *(volatile uint64_t *)(boot.syscon + 0x100) = 2;
        for (;;)
            __asm__ volatile("br $31,.");
    case KSI_CONSOLE_PUTC:
        putc_direct(argument);
        break;
    case KSI_CONSOLE_GETC:
        if (*(volatile uint8_t *)(boot.uart + 5) & 1)
            value = *(volatile uint8_t *)boot.uart;
        else
            error = -6;
        break;
    case KSI_TIME:
        value = *(volatile uint64_t *)boot.timer;
        break;
    case KSI_WALLTIME:
        value=*(volatile uint64_t *)(boot.syscon+0x5000)*1000000000ULL+
              *(volatile uint64_t *)(boot.syscon+0x5008);
        break;
    case KSI_BOOT_EXIT:
        if (argument)
            error = KSI_UNSUPPORTED;
        else
            ownership_released = 1;
        break;
    default:
        error = KSI_UNSUPPORTED;
        break;
    }
    registers[0] = value;
    registers[1] = (uint64_t)error;
}
void stage1_exception(uint64_t *registers)
{
    (void)registers;
    if (KOTL_READ_CSR(0x102) == 11 && KOTL_READ_CSR(0x103) == 0x86) {
        KOTL_WRITE_CSR(0x101, KOTL_READ_CSR(0x101) + 4);
        __asm__ volatile("mb; .long 0x60005400" ::: "memory");
        return;
    }
    text("FAIL: F-level firmware exception\n");
    stop(1);
}
void stage1_main(const void *dtb)
{
    text("Kotl EV9 stage 1 firmware\n");
    if ((uint64_t)dtb != 0x01100000 || !kotl_parse_fdt(dtb, &boot)) {
        text("Invalid or unsupported firmware devicetree\n");
        stop(1);
    }
    boot.ksi_entry = (uint64_t)stage1_ksi;
    KOTL_WRITE_CSR(0x104, (uint64_t)stage1_trap);
    uint64_t bytes = native_firmware_end - native_firmware_start;
    if (!bytes || bytes > 0x180000) {
        text("EFI image exceeds firmware reservation\n");
        stop(1);
    }
    memcpy((void *)KOTL_FW_BASE, native_firmware_start, bytes);
    barrier();
    __asm__ volatile("call_pal 0x86" ::: "memory");
    text("Entering physical F-level UEFI; PAL selection remains pending\n");
    ((void (*)(KOTL_BOOT_INFO *))KOTL_FW_BASE)(&boot);
    stop(1);
}
