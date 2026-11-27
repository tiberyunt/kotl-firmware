#include "platform.h"
void puts_serial(const char *s)
{
    while (*s) {
        if (*s == '\n')
            putchar_serial('\r');
        putchar_serial(*s++);
    }
}
void hex_serial(uint64_t v)
{
    puts_serial("0x");
    for (int i = 60; i >= 0; i -= 4)
        putchar_serial("0123456789abcdef"[(v >> i) & 15]);
}
void *memcpy(void *d, const void *s, size_t n)
{
    uint8_t *a = d;
    const uint8_t *b = s;
    while (n--)
        *a++ = *b++;
    return d;
}
void *memset(void *d, int c, size_t n)
{
    uint8_t *a = d;
    while (n--)
        *a++ = (uint8_t)c;
    return d;
}
int memcmp(const void *a, const void *b, size_t n)
{
    const uint8_t *x = a, *y = b;
    while (n--) {
        if (*x != *y)
            return *x - *y;
        x++;
        y++;
    }
    return 0;
}
uint32_t crc32(const void *d, UINTN n)
{
    const uint8_t *p = d;
    uint32_t c = ~0U;
    while (n--) {
        c ^= *p++;
        for (int i = 0; i < 8; i++)
            c = (c >> 1) ^ (0xedb88320U & -(c & 1));
    }
    return ~c;
}
