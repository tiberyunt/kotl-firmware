/* Read-only, polled NVMe boot transport for the Kotl draft platform.
 * SPDX-License-Identifier: MIT
 */
#include "kotl.h"
#include "storage.h"
#define DEPTH 16
#define PAGE 0x4000ULL
#define ASQ (KOTL_DMA_BASE + 0 * PAGE)
#define ACQ (KOTL_DMA_BASE + 1 * PAGE)
#define IOSQ (KOTL_DMA_BASE + 2 * PAGE)
#define IOCQ (KOTL_DMA_BASE + 3 * PAGE)
#define IDENTIFY (KOTL_DMA_BASE + 4 * PAGE)
#define BOUNCE (KOTL_DMA_BASE + 5 * PAGE)
typedef struct {
    uint64_t sq, cq;
    uint16_t tail, head, id, cid;
    uint8_t phase;
} QUEUE;
static unsigned requested_bdf;
static uint32_t requested_nsid;
static uint32_t active_namespaces[1024];
static unsigned active_count;
static struct {
    uint64_t bar, bar_size, stride;
    uint32_t bdf, namespace_id;
    QUEUE admin, io;
    int ready, configured, failed;
} controller;
static void barrier(void)
{
    __asm__ volatile("mb" ::: "memory");
}
static uint16_t le16(const uint8_t *p)
{
    return p[0] | (uint16_t)p[1] << 8;
}
static uint32_t le32(const uint8_t *p)
{
    return le16(p) | (uint32_t)le16(p + 2) << 16;
}
static uint64_t le64(const uint8_t *p)
{
    return le32(p) | (uint64_t)le32(p + 4) << 32;
}
static void store32(uint8_t *p, uint32_t v)
{
    p[0] = v;
    p[1] = v >> 8;
    p[2] = v >> 16;
    p[3] = v >> 24;
}
static void store64(uint8_t *p, uint64_t v)
{
    store32(p, v);
    store32(p + 4, v >> 32);
}
static void pci16(unsigned reg, uint16_t v)
{
    *(volatile uint16_t *)(native_boot_info->ecam + ((uint64_t)controller.bdf << 12) + reg) = v;
    barrier();
}
static void pci32(unsigned reg, uint32_t v)
{
    *(volatile uint32_t *)(native_boot_info->ecam + ((uint64_t)controller.bdf << 12) + reg) = v;
    barrier();
}
static uint32_t reg32(unsigned offset)
{
    uint32_t v = *(volatile uint32_t *)(controller.bar + offset);
    barrier();
    return v;
}
static uint64_t reg64(unsigned offset)
{
    uint64_t v = *(volatile uint64_t *)(controller.bar + offset);
    barrier();
    return v;
}
static void write32(unsigned offset, uint32_t value)
{
    *(volatile uint32_t *)(controller.bar + offset) = value;
    barrier();
}
static void write64(unsigned offset, uint64_t value)
{
    *(volatile uint64_t *)(controller.bar + offset) = value;
    barrier();
}
static void iommu64(unsigned offset, uint64_t value)
{
    *(volatile uint64_t *)(native_boot_info->iommu + offset) = value;
    barrier();
}
static EFI_STATUS wait_ready(int expected)
{
    uint64_t start = time_ns();
    do {
        uint32_t status = reg32(0x1c);
        if ((status & 1) == (uint32_t)expected)
            return EFI_SUCCESS;
        if (status & 2)
            return EFI_DEVICE_ERROR;
    } while (time_ns() - start < 2000000000ULL);
    return EFI_DEVICE_ERROR;
}
EFI_STATUS storage_quiesce(void)
{
    if (!controller.configured)
        return EFI_SUCCESS;
    write32(0x14, 0);
    EFI_STATUS status = wait_ready(0);
    pci16(4, 2 | 0x400); /* bus mastering disabled, INTx disabled */
    iommu64(0, 0);
    iommu64(0x10, 1);
    controller.ready = 0;
    controller.configured = 0;
    if (!status)
        puts_serial("NVMe quiesced; IOMMU returned to default deny\n");
    return status;
}
static EFI_STATUS fail(EFI_STATUS status)
{
    controller.failed = 1;
    storage_quiesce();
    return status;
}
static void setup_dma(void)
{
    /* Table metadata is outside the device's mapped DMA pages. The only DMA
       mappings are the six queue/transfer pages; firmware, stacks, the ESP
       read destination and application code are not DMA targets. */
    uint64_t *contexts = (void *)KOTL_RAM, *root = (void *)(KOTL_RAM + PAGE);
    uint64_t *l1 = (void *)(KOTL_RAM + 3 * PAGE),
             *l0 = (void *)(KOTL_RAM + 4 * PAGE);
    iommu64(0, 0);
    memset(contexts, 0, 5 * PAGE);
    memset((void *)KOTL_DMA_BASE, 0, KOTL_DMA_SIZE);
    /* Native 16 KiB now has three levels, with split half roots. */
    root[0] = (uint64_t)l1 | 1;
    l1[KOTL_RAM >> 25] = (uint64_t)l0 | 1;
    for (unsigned i = 0; i < 6; i++) {
        uint64_t address = KOTL_DMA_BASE + i * PAGE;
        l0[(address >> 14) & 2047] = address | 7;
    }
    contexts[controller.bdf * 4] = (uint64_t)root | 1;
    barrier();
    iommu64(8, (uint64_t)contexts);
    iommu64(0x18, 0);
    iommu64(0x10, 1);
    iommu64(0, 1);
}
static EFI_STATUS submit(QUEUE *q, uint8_t command[64], uint32_t *result)
{
    if (controller.failed)
        return EFI_DEVICE_ERROR;
    uint16_t cid = ++q->cid;
    command[2] = cid;
    command[3] = cid >> 8;
    memcpy((void *)(q->sq + q->tail * 64), command, 64);
    barrier();
    q->tail = (q->tail + 1) & (DEPTH - 1);
    write32(0x1000 + q->id * 2 * controller.stride, q->tail);
    volatile uint16_t *status = (void *)(q->cq + q->head * 16 + 14);
    uint64_t start = time_ns();
    while ((*status & 1) != q->phase) {
        barrier();
        if ((reg32(0x1c) & 2) || time_ns() - start >= 2000000000ULL)
            return fail(EFI_DEVICE_ERROR);
    }
    barrier();
    uint8_t completion[16];
    memcpy(completion, (const void *)(q->cq + q->head * 16), 16);
    if (le16(completion + 12) != cid || le16(completion + 10) != q->id ||
        le16(completion + 8) >= DEPTH)
        return fail(EFI_DEVICE_ERROR);
    if (result)
        *result = le32(completion);
    q->head = (q->head + 1) & (DEPTH - 1);
    if (!q->head)
        q->phase ^= 1;
    write32(0x1000 + (q->id * 2 + 1) * controller.stride, q->head);
    if (le16(completion + 14) >> 1)
        return fail(EFI_DEVICE_ERROR);
    return EFI_SUCCESS;
}
static EFI_STATUS identify(uint32_t namespace_id, uint32_t cns)
{
    uint8_t command[64] = {0};
    command[0] = 6;
    store32(command + 4, namespace_id);
    store64(command + 24, IDENTIFY);
    store32(command + 40, cns);
    memset((void *)IDENTIFY, 0, 4096);
    return submit(&controller.admin, command, 0);
}
EFI_STATUS storage_device_init(void)
{
    if (controller.ready)
        return EFI_SUCCESS;
    if (controller.failed)
        return EFI_DEVICE_ERROR;
    unsigned bdf = requested_bdf;
    for (unsigned slot = 1; !bdf && slot < 32; slot++) {
        unsigned functions = (pci_read32(slot * 8, 12) & 0x00800000) ? 8 : 1;
        for (unsigned function = 0; function < functions; function++) {
            unsigned candidate = slot * 8 + function;
            if ((pci_read32(candidate, 8) >> 8) == 0x010802) {
                bdf = candidate;
                break;
            }
        }
        if (bdf)
            break;
    }
    if (!bdf)
        return EFI_NOT_FOUND;
    controller.bdf = bdf;
    uint32_t bar = pci_read32(bdf, 0x10);
    if ((bar & 7) != 4)
        return EFI_UNSUPPORTED; /* memory BAR, 64-bit */
    pci16(4, 0x400);            /* no decode or DMA while sizing the BAR */
    pci32(0x10, 0xffffffff);
    pci32(0x14, 0xffffffff);
    uint64_t mask = (uint64_t)pci_read32(bdf, 0x14) << 32 | (pci_read32(bdf, 0x10) & ~15U);
    uint64_t size = ~mask + 1, base = native_boot_info->pci_mmio32;
    if (!size || (size & (size - 1)) || size < 0x2000 || size > 0x1000000 ||
        size > native_boot_info->pci_mmio32_size)
        return EFI_UNSUPPORTED;
    base = (base + size - 1) & ~(size - 1);
    if (base > UINT32_MAX || base < native_boot_info->pci_mmio32 ||
        size > native_boot_info->pci_mmio32_size - (base - native_boot_info->pci_mmio32))
        return EFI_UNSUPPORTED;
    controller.bar = base;
    controller.bar_size = size;
    pci32(0x10, (uint32_t)base);
    pci32(0x14, 0);
    pci16(4, 2 | 0x400);
    controller.configured = 1;
    write32(0x14, 0);
    EFI_STATUS status = wait_ready(0);
    if (status)
        return fail(status);
    uint64_t cap = reg64(0);
    unsigned dstrd = (cap >> 32) & 15;
    if ((cap & 0xffff) < DEPTH - 1 || ((cap >> 48) & 15) || !(cap & (1ULL << 37)) || dstrd > 7)
        return fail(EFI_UNSUPPORTED);
    controller.stride = 4ULL << dstrd;
    if (0x1000 + 4 * controller.stride > size)
        return fail(EFI_UNSUPPORTED);
    setup_dma();
    pci16(4, 6 | 0x400);
    controller.admin = (QUEUE){ASQ, ACQ, 0, 0, 0, 0, 1};
    controller.io = (QUEUE){IOSQ, IOCQ, 0, 0, 1, 0, 1};
    write32(0x24, (DEPTH - 1) | ((DEPTH - 1) << 16));
    write64(0x28, ASQ);
    write64(0x30, ACQ);
    write32(0x14, 1 | (6 << 16) | (4 << 20));
    status = wait_ready(1);
    if (status)
        return fail(status);
    status = identify(0, 1);
    if (status)
        return status;
    status = identify(0, 2);
    if (status)
        return status;
    active_count = 0;
    for (unsigned i = 0; i < 1024; i++) {
        uint32_t id = le32((const uint8_t *)IDENTIFY + i * 4);
        if (!id)
            break;
        active_namespaces[active_count++] = id;
    }
    uint32_t nsid = requested_nsid ? requested_nsid : (active_count ? active_namespaces[0] : 0);
    if (!nsid)
        return fail(EFI_NO_MEDIA);
    status = identify(nsid, 0);
    if (status)
        return status;
    const uint8_t *ns = (const void *)IDENTIFY;
    uint64_t sectors = le64(ns);
    unsigned format = ns[26] & 15;
    if (!sectors || ns[26] & 0xf0 || format > ns[25] || le16(ns + 128 + format * 4) ||
        ns[130 + format * 4] != 9)
        return fail(EFI_UNSUPPORTED);
    controller.namespace_id = nsid;
    uint8_t command[64] = {0};
    command[0] = 9;
    store32(command + 40, 7);
    status = submit(&controller.admin, command, 0);
    if (status)
        return status;
    memset(command, 0, sizeof(command));
    command[0] = 5;
    store64(command + 24, IOCQ);
    store32(command + 40, 1 | ((DEPTH - 1) << 16));
    store32(command + 44, 1);
    status = submit(&controller.admin, command, 0);
    if (status)
        return status;
    memset(command, 0, sizeof(command));
    command[0] = 1;
    store64(command + 24, IOSQ);
    store32(command + 40, 1 | ((DEPTH - 1) << 16));
    store32(command + 44, 1 | (1 << 16));
    status = submit(&controller.admin, command, 0);
    if (status)
        return status;
    block_init(&disk_device, 0, sectors, 0);
    static const EFI_GUID block_guid = EFI_BLOCK_IO_PROTOCOL_GUID;
    status = protocol_publish(&disk_device, &block_guid, &disk_device.io);
    if (status)
        return fail(status);
    controller.ready = 1;
    puts_serial("NVMe namespace ");
    hex_serial(nsid);
    puts_serial(": ");
    hex_serial(sectors);
    puts_serial(" sectors; six IOMMU DMA pages\n");
    return EFI_SUCCESS;
}
EFI_STATUS storage_read_sectors(uint64_t lba, UINTN count, void *buffer)
{
    if (!controller.ready || controller.failed)
        return EFI_DEVICE_ERROR;
    uint8_t *out = buffer;
    while (count) {
        uint32_t chunk = count > 8 ? 8 : count;
        uint8_t command[64] = {0};
        command[0] = 2;
        store32(command + 4, controller.namespace_id);
        store64(command + 24, BOUNCE);
        store64(command + 40, lba);
        store32(command + 48, chunk - 1);
        EFI_STATUS status = submit(&controller.io, command, 0);
        if (status)
            return status;
        barrier();
        memcpy(out, (const void *)BOUNCE, chunk * 512);
        out += chunk * 512;
        lba += chunk;
        count -= chunk;
    }
    return EFI_SUCCESS;
}

EFI_STATUS nvme_select(unsigned bdf, uint32_t nsid)
{
    EFI_STATUS status = storage_select_partition(0);
    if (status)
        return status;
    status = storage_quiesce();
    if (status)
        return status;
    if (controller.bdf)
        pci16(4, 0x400); /* shared BAR aperture: remove old decode */
    protocol_remove_handle(&disk_device);
    memset(&controller, 0, sizeof(controller));
    requested_bdf = bdf;
    requested_nsid = nsid;
    active_count = 0;
    return storage_device_init();
}
unsigned nvme_namespaces(uint32_t *out, unsigned capacity)
{
    unsigned count = active_count < capacity ? active_count : capacity;
    for (unsigned i = 0; i < count; i++)
        out[i] = active_namespaces[i];
    return count;
}
