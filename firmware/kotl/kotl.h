/* SPDX-License-Identifier: MIT */
#ifndef KOTL_FIRMWARE_H
#define KOTL_FIRMWARE_H
#include <stddef.h>
#include <stdint.h>
#define KOTL_RAM 0x100000000ULL
#define KOTL_FW_BASE (KOTL_RAM + 0x200000)
#define KOTL_DMA_BASE (KOTL_RAM + 0x400000)
#define KOTL_DMA_SIZE (6 * 0x4000)
#define KOTL_BOOT_MAGIC 0x4b4f544c424f4f54ULL
#define KSI_VERSION 0
#define KSI_PROBE 1
#define KSI_SYSTEM_OFF 2
#define KSI_SYSTEM_RESET 3
#define KSI_CONSOLE_PUTC 4
#define KSI_CONSOLE_GETC 5
#define KSI_TIME 6
#define KSI_MODEL_SELECT 15
#define KSI_BOOT_EXIT 16
#define KSI_WALLTIME 17
#define KOTL_BOOT_INFO_BASE 0x01000000ULL
#define KSI_OK 0
#define KSI_UNSUPPORTED (-2LL)
#define KSI_INVALID_PARAMETER (-3LL)
#define KOTL_READ_CSR(csr)                                                                         \
    ({                                                                                             \
        register uint64_t value __asm__("$1");                                                     \
        __asm__ volatile(".long 0x64200000 | %1" : "=r"(value) : "i"(csr) : "memory");             \
        value;                                                                                     \
    })
#define KOTL_WRITE_CSR(csr, v)                                                                     \
    do {                                                                                           \
        register uint64_t value __asm__("$1") = (v);                                               \
        __asm__ volatile(".long 0x74010000 | %1" ::"r"(value), "i"(csr) : "memory");               \
    } while (0)
typedef struct {
    uint64_t magic, version, boot_mode;
    uint64_t ram_base, ram_size, dtb, dtb_size;
    uint64_t uart, syscon, timer, kaia, iommu, ecam, ecam_size, pci_mmio32, pci_mmio32_size;
    uint64_t ksi_entry, hart_count, threads;
} KOTL_BOOT_INFO;
_Static_assert(offsetof(KOTL_BOOT_INFO,ksi_entry)==128,"KSI assembly binding");
typedef struct {
    int64_t error;
    uint64_t value;
} KSI_RESULT;
/* C adapter to the retained F-level raw KSI ABI (R0=value, R1=error).
 * The physical entry is published in the fixed firmware boot information. */
KSI_RESULT kotl_ksi_call(uint64_t function, uint64_t argument);
static inline KSI_RESULT ksi_call(uint64_t function, uint64_t argument)
{
    return kotl_ksi_call(function, argument);
}
unsigned nvme_namespaces(uint32_t *, unsigned);
uint64_t nvme_select(unsigned, uint32_t);
void boot_manager(void);
void kotl_pal_init(void);
int kotl_pal_fdt_init(void);
void kotl_pal_fdt_state(unsigned,int);
int boot_use_variables(void);
uint64_t boot_selected(void **, uint64_t *, void **);
void display_ui_begin(void);
void display_boot_logo(void);
void display_ui_text(unsigned, unsigned, uint32_t, uint32_t, const char *, unsigned);
void display_ui_leave(void);
void display_console(int);
void display_init(void);
void display_putc(char);
void display_clear(void);
void display_attribute(unsigned);
void display_cursor(unsigned, unsigned);
void display_stop(void);
uint32_t platform_key(void);
extern KOTL_BOOT_INFO *native_boot_info;
int kotl_parse_fdt(const void *, KOTL_BOOT_INFO *);
void kotl_fatal(void) __attribute__((noreturn));
void kotl_trap_entry(void);
#endif
