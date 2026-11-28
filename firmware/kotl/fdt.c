/* SPDX-License-Identifier: MIT */
#include "kotl.h"
static uint32_t be32(const uint8_t *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}
static uint64_t be64(const uint8_t *p)
{
    return (uint64_t)be32(p) << 32 | be32(p + 4);
}
static int eq(const char *p, uint32_t n, const char *wanted)
{
    uint32_t i = 0;
    while (i < n && wanted[i] && p[i] == wanted[i])
        i++;
    return i < n && !p[i] && !wanted[i];
}
static int compatible(const char *p, uint32_t n, const char *wanted)
{
    while (n) {
        if (eq(p, n, wanted))
            return 1;
        uint32_t i = 0;
        while (i < n && p[i])
            i++;
        if (i == n)
            return 0;
        p += i + 1;
        n -= i + 1;
    }
    return 0;
}
static void zero(void *p, unsigned n)
{
    uint8_t *b = p;
    while (n--)
        *b++ = 0;
}
static int same(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}
int kotl_parse_fdt(const void *blob, KOTL_BOOT_INFO *info)
{
    const uint8_t *b = blob;
    if (be32(b) != 0xd00dfeed)
        return 0;
    uint32_t size = be32(b + 4), off = be32(b + 8), str = be32(b + 12), strings_size = be32(b + 32),
             structure_size = be32(b + 36);
    if (size < 40 || size > 0x100000 || be32(b + 20) < 17 || off > size ||
        structure_size > size - off || str > size || strings_size > size - str)
        return 0;
    struct NODE {
        uint64_t base, size, pci_base, pci_size;
        uint32_t kind, path_length;
        int memory, cpu;
    } stack[64];
    uint64_t uarts[4];
    char uart_paths[4][192], path[192], chosen[192];
    uint32_t depth = 0, path_length = 0, uart_count = 0, found = 0;
    int memory_count = 0, done = 0;
    path[0] = chosen[0] = 0;
    zero(info, sizeof(*info));
    const uint8_t *p = b + off, *end = p + structure_size;
    while (p + 4 <= end) {
        uint32_t token = be32(p);
        p += 4;
        if (token == 1) {
            if (depth == 64)
                return 0;
            uint32_t n = 0;
            while (p + n < end && p[n])
                n++;
            if (p + n == end)
                return 0;
            if (n>=7 && p[0]=='t' && p[1]=='h' && p[2]=='r' && p[3]=='e' &&
                p[4]=='a' && p[5]=='d' && p[6]=='0')info->threads=2;
            unsigned previous = path_length;
            if (depth) {
                if (path_length + 1 + n >= sizeof(path))
                    return 0;
                path[path_length++] = '/';
                for (unsigned i = 0; i < n; i++)
                    path[path_length++] = p[i];
                path[path_length] = 0;
            } else if (n)
                return 0;
            zero(&stack[depth], sizeof(stack[depth]));
            stack[depth++].path_length = previous;
            uint32_t advance = (n + 4) & ~3U;
            if ((uint64_t)(end - p) < advance)
                return 0;
            p += advance;
        } else if (token == 2) {
            if (!depth)
                return 0;
            struct NODE *node = &stack[depth - 1];
            if (node->cpu)info->hart_count++;
            if (node->memory) {
                if (++memory_count != 1 || !node->size)
                    return 0;
                info->ram_base = node->base;
                info->ram_size = node->size;
                found |= 1;
            }
            if (node->kind == 1) {
                if (uart_count == 4 || !node->size)
                    return 0;
                uarts[uart_count] = node->base;
                for (unsigned i = 0; i <= path_length; i++)
                    uart_paths[uart_count][i] = path[i];
                uart_count++;
            } else if (node->kind) {
                if (!node->size)
                    return 0;
                uint32_t bit = 1U << node->kind;
                if (found & bit)
                    return 0;
                found |= bit;
                switch (node->kind) {
                case 2:
                    info->syscon = node->base;
                    break;
                case 3:
                    info->timer = node->base;
                    break;
                case 4:
                    info->kaia = node->base;
                    break;
                case 5:
                    info->iommu = node->base;
                    break;
                case 6:
                    info->ecam = node->base;
                    info->ecam_size = node->size;
                    info->pci_mmio32 = node->pci_base;
                    info->pci_mmio32_size = node->pci_size;
                    break;
                }
            }
            path_length = node->path_length;
            path[path_length] = 0;
            depth--;
        } else if (token == 3) {
            if (!depth || end - p < 8)
                return 0;
            uint32_t n = be32(p), nameoff = be32(p + 4);
            p += 8;
            uint64_t advance = ((uint64_t)n + 3) & ~3ULL;
            if (advance > (uint64_t)(end - p) || nameoff >= strings_size)
                return 0;
            const char *name = (const char *)(b + str + nameoff);
            uint32_t name_space = strings_size - nameoff;
            unsigned z = 0;
            while (z < name_space && name[z])
                z++;
            if (z == name_space)
                return 0;
            struct NODE *node = &stack[depth - 1];
            if (eq(name, name_space, "reg") && n == 16) {
                node->base = be64(p);
                node->size = be64(p + 8);
                if (node->size > UINT64_MAX - node->base)
                    return 0;
            } else if (eq(name, name_space, "device_type") && eq((const char *)p, n, "memory"))
                node->memory = 1;
            else if (eq(name, name_space, "device_type") && eq((const char *)p,n,"cpu"))
                node->cpu=1;
            else if (eq(name, name_space, "compatible")) {
                if (compatible((const char *)p, n, "ns16550a"))
                    node->kind = 1;
                else if (compatible((const char *)p, n, "kotl,syscon-draft01"))
                    node->kind = 2;
                else if (compatible((const char *)p, n, "kotl,ktimer-draft01"))
                    node->kind = 3;
                else if (compatible((const char *)p, n, "kotl,kaia-draft01"))
                    node->kind = 4;
                else if (compatible((const char *)p, n, "kotl,iommu-draft01"))
                    node->kind = 5;
                else if (compatible((const char *)p, n, "pci-host-ecam-generic"))
                    node->kind = 6;
            } else if (eq(name, name_space, "ranges") && n && n % 28 == 0) {
                for (uint32_t i = 0; i < n; i += 28)
                    if ((be32(p + i) & 0x03000000) == 0x02000000) {
                        uint64_t child = be64(p + i + 4), parent = be64(p + i + 12),
                                 length = be64(p + i + 20);
                        if (child != parent || !length || parent > UINT32_MAX ||
                            length > 0x100000000ULL - parent)
                            return 0;
                        node->pci_base = parent;
                        node->pci_size = length;
                    }
            } else if (same(path, "/chosen") && eq(name, name_space, "bootargs")) {
                if (eq((const char *)p, n, "selftest"))
                    info->boot_mode = 1;
                else if (eq((const char *)p, n, "shell"))
                    info->boot_mode = 2;
            } else if (same(path, "/chosen") && eq(name, name_space, "stdout-path")) {
                unsigned i = 0;
                while (i < n && p[i] && p[i] != ':') {
                    if (i + 1 == sizeof(chosen))
                        return 0;
                    chosen[i] = p[i];
                    i++;
                }
                if (i == n)
                    return 0;
                chosen[i] = 0;
            }
            p += advance;
        } else if (token == 9) {
            if (depth)
                return 0;
            done = 1;
            break;
        } else if (token != 4)
            return 0;
    }
    if (!done || (found & 0x7d) != 0x7d || !uart_count || !chosen[0] || !info->pci_mmio32_size)
        return 0;
    int console = 0;
    for (unsigned i = 0; i < uart_count; i++)
        if (same(chosen, uart_paths[i])) {
            info->uart = uarts[i];
            console = 1;
        }
    if (!console || info->ram_base != KOTL_RAM || info->ram_size < 64 * 1024 * 1024ULL ||
        info->ram_size > 1024 * 1024 * 1024ULL)
        return 0;
    if (info->syscon >= 0x100000000ULL || info->timer >= 0x100000000ULL ||
        info->kaia >= 0x100000000ULL || info->iommu >= 0x100000000ULL ||
        info->ecam >= 0x100000000ULL || info->uart >= 0x100000000ULL)
        return 0;
    if(!info->hart_count || info->hart_count>16)return 0;
    if(!info->threads)info->threads=1;
    info->magic = KOTL_BOOT_MAGIC;
    info->version = 1;
    info->dtb = (uint64_t)blob;
    info->dtb_size = size;
    return 1;
}
