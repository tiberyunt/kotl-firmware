#include "platform.h"
#define PORT_BASE 0xfffffd01fc000000ULL
void io_write8(unsigned p, uint8_t v)
{
    *(volatile uint8_t *)(PORT_BASE + p) = v;
    __asm__ volatile("mb" ::: "memory");
}
uint8_t io_read8(unsigned p)
{
    uint8_t v = *(volatile uint8_t *)(PORT_BASE + p);
    __asm__ volatile("mb" ::: "memory");
    return v;
}
void serial_init(void)
{
    io_write8(0x3f9, 0);
    io_write8(0x3fb, 0x80);
    io_write8(0x3f8, 1);
    io_write8(0x3f9, 0);
    io_write8(0x3fb, 3);
    io_write8(0x3fa, 7);
    io_write8(0x3fc, 3);
}
void putchar_serial(char c)
{
    while (!(io_read8(0x3fd) & 0x20)) {
    }
    io_write8(0x3f8, (uint8_t)c);
}
int getchar_serial(void)
{
    return (io_read8(0x3fd) & 1) ? io_read8(0x3f8) : -1;
}
void halt(void)
{
    __asm__ volatile("call_pal 0" ::: "memory");
    for (;;) {
    }
}
uint16_t io_read16(unsigned port)
{
    uint16_t v = *(volatile uint16_t *)(PORT_BASE + port);
    __asm__ volatile("mb" ::: "memory");
    return v;
}
uint32_t pci_read32(unsigned bdf, unsigned reg)
{
    uint32_t v = *(volatile uint32_t *)(0xfffffd01fe000000ULL + bdf * 256 + reg);
    __asm__ volatile("mb" ::: "memory");
    return v;
}
uint64_t time_ns(void)
{
    register UINTN a0 __asm__("$16") = 7;
    register UINTN v0 __asm__("$0");
    __asm__ volatile("call_pal 9"
                     : "=r"(v0)
                     : "r"(a0)
                     : "$1", "$22", "$23", "$24", "$25", "memory");
    return v0;
}

EFI_STATUS platform_exit_boot(void)
{
    return EFI_SUCCESS;
}
void *platform_dtb(void)
{
    return 0;
}

void platform_reset(uint32_t type)
{
    (void)type;
    halt();
}

int platform_variables_persistent(void)
{
    return 0;
}
EFI_STATUS platform_variables_load(void *buffer, UINTN size)
{
    (void)buffer;
    (void)size;
    return EFI_UNSUPPORTED;
}
EFI_STATUS platform_variables_save(const void *buffer, UINTN size)
{
    (void)buffer;
    (void)size;
    return EFI_UNSUPPORTED;
}
