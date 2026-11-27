/* SPDX-License-Identifier: MIT */
#include "kotl.h"
static KOTL_BOOT_INFO boot;
static uint64_t root[2048] __attribute__((aligned(16384)));
static uint64_t level2[2048] __attribute__((aligned(16384)));
static uint64_t level1[2048] __attribute__((aligned(16384)));
static int model_selected;
extern const uint8_t native_firmware_start[], native_firmware_end[];
extern void stage1_trap(void);
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
        __asm__ volatile(".long 0x04000001");
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
    return function <= KSI_TIME || function == KSI_MODEL_SELECT;
}
void stage1_service(uint64_t *registers)
{
    if (KOTL_READ_CSR(3) != 10 || KOTL_READ_CSR(4) != 0x100 ||
        ((KOTL_READ_CSR(12) >> 8) & 3) != 1) {
        text("Native firmware trap outside KSI\n");
        stop(1);
    }
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
        if (model_selected)
            text("KSI: native model selected; runtime SYSTEM_OFF\n");
        stop(0);
        break;
    case KSI_SYSTEM_RESET:
        *(volatile uint64_t *)(boot.syscon + 0x100) = 2;
        for (;;)
            __asm__ volatile(".long 0x04000001");
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
    case KSI_MODEL_SELECT:
        if (argument)
            error = KSI_UNSUPPORTED;
        else
            model_selected = 1;
        break;
    default:
        error = KSI_UNSUPPORTED;
        break;
    }
    registers[0] = (uint64_t)error;
    registers[1] = value;
}
void stage1_main(const void *dtb)
{
    text("Kotl EV9 stage 1 firmware\n");
    if ((uint64_t)dtb != 0x01100000 || !kotl_parse_fdt(dtb, &boot)) {
        text("Invalid or unsupported firmware devicetree\n");
        stop(1);
    }
    KOTL_WRITE_CSR(1, (uint64_t)stage1_trap);
    KOTL_WRITE_CSR(13, (uint64_t)stage1_trap);
    uint64_t bytes = native_firmware_end - native_firmware_start;
    if (!bytes || bytes > 0x180000) {
        text("Native EFI image exceeds firmware reservation\n");
        stop(1);
    }
    memcpy((void *)KOTL_FW_BASE, native_firmware_start, bytes);
    /* Native identity map: low I/O windows and DRAM. 16 KiB table granule,
       conservative 32 MiB leaves; finer code/data permissions are future work. */
    root[0] = (uint64_t)level2 | 1;
    level2[0] = (uint64_t)level1 | 1;
    for (unsigned i = 0; i < 128; i++)
        level1[i] = ((uint64_t)i << 25) | 0x107;
    level1[0] = 15; /* XIP flash and SRAM */
    for (uint64_t address = boot.ram_base; address < boot.ram_base + boot.ram_size;
         address += 0x2000000)
        level1[address >> 25] = address | 15;
    barrier();
    __asm__ volatile("call_pal 0x86" ::: "memory");
    KOTL_WRITE_CSR(5, (uint64_t)root);
    KOTL_WRITE_CSR(2, KOTL_FW_BASE);
    KOTL_WRITE_CSR(12, 0x102);
    text("Entering native K-level EFI firmware with identity mappings\n");
    register KOTL_BOOT_INFO *argument __asm__("$16") = &boot;
    __asm__ volatile(".long 0x04000000" ::"r"(argument) : "memory");
    __builtin_unreachable();
}
