#ifndef PLATFORM_H
#define PLATFORM_H
#include "efi.h"
int platform_variables_persistent(void);
EFI_STATUS platform_variables_load(void *, UINTN);
EFI_STATUS platform_variables_save(const void *, UINTN);
void serial_init(void);
void io_write8(unsigned, uint8_t);
uint8_t io_read8(unsigned);
uint16_t io_read16(unsigned);
uint32_t pci_read32(unsigned, unsigned);
uint64_t time_ns(void);
EFI_STATUS platform_exit_boot(void);
void *platform_dtb(void);
void platform_reset(uint32_t) __attribute__((noreturn));
void putchar_serial(char);
int getchar_serial(void);
void puts_serial(const char *);
void hex_serial(uint64_t);
void halt(void) __attribute__((noreturn));
void *memcpy(void *, const void *, size_t);
void *memset(void *, int, size_t);
int memcmp(const void *, const void *, size_t);
uint32_t crc32(const void *, UINTN);
#endif
