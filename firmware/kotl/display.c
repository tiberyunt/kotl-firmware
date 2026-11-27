/* SPDX-License-Identifier: MIT */
#include "efi-graphics.h"
#include "kotl.h"
#include "menu.h"
#include "services.h"
#define BASE_CELL_W 12
#define BASE_CELL_H 28
static unsigned cell_w = BASE_CELL_W, cell_h = BASE_CELL_H;
#define CELL_W cell_w
#define CELL_H cell_h
static volatile uint32_t *pixels;
static uint64_t registers;
static unsigned column, row, escape;
static int active;
static int ui_active, console_enabled = 1;
static const struct resolution {
    uint16_t width, height;
} resolutions[] = {
    {1024, 768}, {1920, 1080}, {2560, 1440}, {3840, 2160},
};
static EFI_GRAPHICS_OUTPUT_MODE_INFORMATION info;
static EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE mode = {0, 0, &info, sizeof(info), 0, 0};

#define WIDTH (info.HorizontalResolution)
#define HEIGHT (info.VerticalResolution)
/* 20px Noto Sans Mono, supersampled into grayscale coverage; see font/OFL.txt. */
extern const uint8_t kotl_font[95][BASE_CELL_H][BASE_CELL_W];
extern const uint8_t kotl_sharp_fonts[];
static uint8_t sharp_atlas[95 * 48 * 86];
static const uint8_t *font_atlas = &kotl_font[0][0][0];
static uint32_t text_palette[256];
static uint32_t background;
void display_attribute(unsigned attribute)
{
    static const uint32_t colors[16] = {0x000000, 0x0000aa, 0x00aa00, 0x00aaaa, 0xaa0000, 0xaa00aa,
                                        0xaa5500, 0xaaaaaa, 0x555555, 0x5555ff, 0x55ff55, 0x55ffff,
                                        0xff5555, 0xff55ff, 0xffff55, 0xffffff};
    background = colors[(attribute >> 4) & 7];
    uint32_t foreground = colors[attribute & 15];
    for (unsigned alpha = 0; alpha < 256; alpha++) {
        uint32_t color = 0;
        for (unsigned shift = 0; shift < 24; shift += 8) {
            unsigned bg = (background >> shift) & 255, fg = (foreground >> shift) & 255;
            color |= ((bg * (255 - alpha) + fg * alpha + 127) / 255) << shift;
        }
        text_palette[alpha] = color;
    }
}
void display_cursor(unsigned x, unsigned y)
{
    if (x < 80 && y < 25) {
        column = x;
        row = y;
    }
}
void display_clear(void)
{
    if (!active)
        return;
    for (unsigned i = 0; i < WIDTH * HEIGHT; i++)
        pixels[i] = background;
    column = row = escape = 0;
    __asm__ volatile("mb" ::: "memory");
}
static void newline(void)
{
    column = 0;
    if (++row < 25)
        return;
    row = 24;
    /* Cell boundaries partition the entire framebuffer, including fractional
       rows. Copy forward because source scanlines always follow destinations. */
    for (unsigned y = 0; y < HEIGHT; y++) {
        unsigned r = ((y + 1) * 25 - 1) / HEIGHT;
        unsigned start = r * HEIGHT / 25;
        unsigned next = (r + 1) * HEIGHT / 25;
        unsigned end = (r + 2) * HEIGHT / 25;
        unsigned sy = next + y - start;
        if (sy >= end) sy = end - 1;
        for (unsigned x = 0; x < WIDTH; x++)
            pixels[y * WIDTH + x] = r < 24 ? pixels[sy * WIDTH + x] : background;
    }
}
static void draw_pixel(unsigned x, unsigned y, unsigned scale, uint32_t color)
{
    for (unsigned dy = 0; dy < scale; dy++)
        for (unsigned dx = 0; dx < scale; dx++)
            pixels[(y + dy) * WIDTH + x + dx] = color;
}
static void draw_cell(unsigned x, unsigned y, unsigned code, const uint32_t *palette)
{
    unsigned x0 = x * WIDTH / 80, x1 = (x + 1) * WIDTH / 80;
    unsigned y0 = y * HEIGHT / 25, y1 = (y + 1) * HEIGHT / 25;
    unsigned origin_x = x0 + (x1 - x0 - CELL_W) / 2;
    unsigned origin_y = y0 + (y1 - y0 - CELL_H) / 2;
    for (unsigned py = y0; py < y1; py++)
        for (unsigned px = x0; px < x1; px++)
            pixels[py * WIDTH + px] = palette[0];
    for (unsigned dy = 0; dy < CELL_H; dy++)
        for (unsigned dx = 0; dx < CELL_W; dx++)
            pixels[(origin_y + dy) * WIDTH + origin_x + dx] =
                palette[font_atlas[((code - 32) * CELL_H + dy) * CELL_W + dx]];
}
void display_putc(char c)
{
    if (!active || ui_active || !console_enabled)
        return;
    if (c == 27) {
        escape = 1;
        return;
    }
    if (escape) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) {
            if (c == 'J')
                display_clear();
            escape = 0;
        }
        return;
    }
    if (c == '\r') {
        column = 0;
        return;
    }
    if (c == '\n') {
        newline();
        return;
    }
    if (c == '\b') {
        if (column)
            column--;
        return;
    }
    if (c < 32)
        return;
    if (column == 80)
        newline();
    unsigned code = (unsigned char)c;
    if (code < 32 || code > 126)
        code = '?';
    draw_cell(column, row, code, text_palette);
    if (++column == 80)
        newline();
}
static EFI_STATUS query(EFI_GRAPHICS_OUTPUT_PROTOCOL *p, uint32_t number, UINTN *size,
                        EFI_GRAPHICS_OUTPUT_MODE_INFORMATION **out)
{
    (void)p;
    if (!boot_active || !size || !out)
        return EFI_INVALID_PARAMETER;
    if (number >= mode.MaxMode)
        return EFI_INVALID_PARAMETER;
    EFI_STATUS s = boot_services.AllocatePool(EfiBootServicesData, sizeof(info), (void **)out);
    if (!s) {
        const struct resolution *r = &resolutions[number];
        **out = (EFI_GRAPHICS_OUTPUT_MODE_INFORMATION){0, r->width, r->height, 1,
                                                       {0, 0, 0, 0}, r->width};
        *size = sizeof(info);
    }
    return s;
}
static EFI_STATUS set(EFI_GRAPHICS_OUTPUT_PROTOCOL *p, uint32_t number)
{
    (void)p;
    if (!boot_active || number >= mode.MaxMode)
        return EFI_UNSUPPORTED;

    const struct resolution *r = &resolutions[number];
    info = (EFI_GRAPHICS_OUTPUT_MODE_INFORMATION){0, r->width, r->height, 1,
                                                  {0, 0, 0, 0}, r->width};
    mode.Mode = number;
    mode.FrameBufferSize = (UINTN)r->width * r->height * 4;
    cell_w = BASE_CELL_W;
    cell_h = BASE_CELL_H;
    font_atlas = &kotl_font[0][0][0];
    if (HEIGHT >= 1080) {
        const uint8_t *encoded = kotl_sharp_fonts;
        unsigned atlas_index = HEIGHT >= 2160 ? 2 : HEIGHT >= 1440 ? 1 : 0;
        for (unsigned i = 0; i < atlas_index; i++) {
            unsigned bytes = encoded[0] | (encoded[1] << 8) |
                             (encoded[2] << 16) | (encoded[3] << 24);
            encoded += 4 + bytes;
        }
        cell_w = atlas_index == 2 ? 48 : atlas_index == 1 ? 32 : 24;
        cell_h = atlas_index == 2 ? 86 : atlas_index == 1 ? 57 : 43;
        unsigned size = 95 * cell_w * cell_h;
        encoded += 4;
        for (unsigned offset = 0; offset < size;) {
            unsigned count = *encoded++;
            uint8_t value = *encoded++;
            while (count-- && offset < size)
                sharp_atlas[offset++] = value;
        }
        font_atlas = sharp_atlas;
    }

    volatile uint16_t *vbe = (void *)(registers + 0x500);
    vbe[4] = 0;
    vbe[1] = r->width;
    vbe[2] = r->height;
    vbe[3] = 32;
    vbe[6] = r->width;
    vbe[8] = vbe[9] = 0;
    vbe[4] = 0x41;
    for (UINTN i = 0; i < mode.FrameBufferSize / 4; i++)
        pixels[i] = 0;
    column = row = escape = 0;
    __asm__ volatile("mb" ::: "memory");
    return EFI_SUCCESS;
}
static int rectangle(UINTN x, UINTN y, UINTN width, UINTN height)
{
    return width && height && x < WIDTH && y < HEIGHT && width <= WIDTH - x && height <= HEIGHT - y;
}
static EFI_STATUS blt(EFI_GRAPHICS_OUTPUT_PROTOCOL *p, EFI_GRAPHICS_OUTPUT_BLT_PIXEL *buffer,
                      uint32_t op, UINTN sx, UINTN sy, UINTN dx, UINTN dy, UINTN width,
                      UINTN height, UINTN delta)
{
    (void)p;
    if (!boot_active || op > 3 || !width || !height)
        return EFI_INVALID_PARAMETER;
    if (op != 1 && !rectangle(dx, dy, width, height))
        return EFI_INVALID_PARAMETER;
    if ((op == 1 || op == 3) && !rectangle(sx, sy, width, height))
        return EFI_INVALID_PARAMETER;
    if (op != 3 && !buffer)
        return EFI_INVALID_PARAMETER;
    UINTN bx = op == 1 ? dx : sx, by = op == 1 ? dy : sy;
    if (op == 1 || op == 2) {
        if (!delta) {
            if (width > UINT64_MAX / 4)
                return EFI_INVALID_PARAMETER;
            delta = width * 4;
        }
        if (delta % 4 || bx > UINT64_MAX - width || bx + width > delta / 4 ||
            by > UINT64_MAX - height || by + height > UINT64_MAX / delta)
            return EFI_INVALID_PARAMETER;
    }
    for (UINTN j = 0; j < height; j++) {
        UINTN y = (op == 3 && dy > sy) ? height - 1 - j : j;
        for (UINTN i = 0; i < width; i++) {
            UINTN x = (op == 3 && dy == sy && dx > sx) ? width - 1 - i : i;
            if (op == 0)
                pixels[(dy + y) * WIDTH + dx + x] = *(uint32_t *)buffer;
            else if (op == 1)
                *(uint32_t *)((uint8_t *)buffer + (dy + y) * delta + (dx + x) * 4) =
                    pixels[(sy + y) * WIDTH + sx + x];
            else if (op == 2)
                pixels[(dy + y) * WIDTH + dx + x] =
                    *(uint32_t *)((uint8_t *)buffer + (sy + y) * delta + (sx + x) * 4);
            else
                pixels[(dy + y) * WIDTH + dx + x] = pixels[(sy + y) * WIDTH + sx + x];
        }
    }
    __asm__ volatile("mb" ::: "memory");
    return EFI_SUCCESS;
}
static EFI_GRAPHICS_OUTPUT_PROTOCOL gop = {query, set, blt, &mode};
static void write_pci(unsigned bdf, unsigned reg, uint32_t value)
{
    *(volatile uint32_t *)(native_boot_info->ecam + ((uint64_t)bdf << 12) + reg) = value;
    __asm__ volatile("mb" ::: "memory");
}
void display_init(void)
{
    for (unsigned bdf = 8; bdf < 256; bdf++) {
        if (pci_read32(bdf, 0) != 0x11111234)
            continue;
        *(volatile uint16_t *)(native_boot_info->ecam + ((uint64_t)bdf << 12) + 4) = 0;
        write_pci(bdf, 0x10, 0xffffffff);
        uint32_t fbmask = pci_read32(bdf, 0x10) & ~15U;
        write_pci(bdf, 0x18, 0xffffffff);
        uint32_t regmask = pci_read32(bdf, 0x18) & ~15U;
        uint64_t fbsize = (uint32_t)(~fbmask + 1), regsize = (uint32_t)(~regmask + 1);
        if (fbsize < 1024 * 768 * 4 || regsize != 0x1000)
            return;
        uint64_t aperture = native_boot_info->pci_mmio32;
        uint64_t end = aperture + native_boot_info->pci_mmio32_size;
        if (native_boot_info->pci_mmio32_size < fbsize + 0x1000000)
            return;
        registers = end - 0x1000000;
        uint64_t base = (registers - fbsize) & ~(fbsize - 1);
        if (base < aperture)
            return;
        write_pci(bdf, 0x10, (uint32_t)base);
        write_pci(bdf, 0x18, (uint32_t)registers);
        *(volatile uint16_t *)(native_boot_info->ecam + ((uint64_t)bdf << 12) + 4) = 2;

        for (unsigned i = 0; i < sizeof(resolutions) / sizeof(resolutions[0]); i++) {
            if ((uint64_t)resolutions[i].width * resolutions[i].height * 4 > fbsize)
                break;
            mode.MaxMode++;
        }

        /* Prefer the display's first EDID timing if it fits a GOP mode. */
        const volatile uint8_t *edid = (const void *)registers;
        unsigned preferred = 0;
        if (edid[0] == 0 && edid[1] == 0xff && (edid[54] || edid[55])) {
            unsigned width = edid[56] | ((edid[58] & 0xf0U) << 4);
            unsigned height = edid[59] | ((edid[61] & 0xf0U) << 4);
            for (unsigned i = 0; i < mode.MaxMode; i++)
                if (resolutions[i].width == width && resolutions[i].height == height)
                    preferred = i;
        }
        display_attribute(7);
        pixels = (void *)base;
        mode.FrameBufferBase = base;
        active = 1;
        set(&gop, preferred);
        static const EFI_GUID guid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
        if (protocol_publish(&gop, &guid, &gop)) {
            active = 0;
            return;
        }
        puts_serial("PASS: Bochs virtual display; EFI GOP installed\n");
        return;
    }
}
void display_stop(void)
{
    active = 0;
}

/* BIOS setup uses the same antialiased atlas with positioned, colored cells. */
void display_ui_begin(void)
{
    ui_active = 1;
    if (!active)
        return;
    for (unsigned i = 0; i < WIDTH * HEIGHT; i++)
        pixels[i] = UI_BACKGROUND;
}
void display_boot_logo(void)
{
    enum { LOGO_WIDTH = 288, LOGO_HEIGHT = 106 };
    extern const uint8_t kotl_boot_logo[LOGO_HEIGHT][LOGO_WIDTH];

    display_ui_begin();
    if (!active)
        return;
    unsigned logo_scale = HEIGHT >= 1440 ? 2 : 1;
    unsigned x0 = (WIDTH - LOGO_WIDTH * logo_scale) / 2;
    unsigned y0 = (HEIGHT - LOGO_HEIGHT * logo_scale) / 2;
    for (unsigned y = 0; y < LOGO_HEIGHT; y++) {
        for (unsigned x = 0; x < LOGO_WIDTH; x++) {
            uint32_t gray = kotl_boot_logo[y][x];
            draw_pixel(x0 + x * logo_scale, y0 + y * logo_scale, logo_scale, gray * 0x010101);
        }
    }
    __asm__ volatile("mb" ::: "memory");
}
void display_ui_text(unsigned x, unsigned y, uint32_t fg, uint32_t bg, const char *text,
                     unsigned width)
{
    if (!active || x >= 80 || y >= 25)
        return;
    if (width > 80 - x)
        width = 80 - x;
    uint32_t palette[256];
    for (unsigned alpha = 0; alpha < 256; alpha++) {
        uint32_t color = 0;
        for (unsigned shift = 0; shift < 24; shift += 8) {
            unsigned a = (bg >> shift) & 255, b = (fg >> shift) & 255;
            color |= ((a * (255 - alpha) + b * alpha + 127) / 255) << shift;
        }
        palette[alpha] = color;
    }
    for (unsigned i = 0; i < width; i++) {
        unsigned code = *text ? (uint8_t)*text++ : ' ';
        if (code < 32 || code > 126)
            code = '?';
        draw_cell(x + i, y, code, palette);
    }
    __asm__ volatile("mb" ::: "memory");
}
void display_ui_leave(void)
{
    ui_active = 0;
    display_clear();
}
void display_console(int enabled)
{
    display_clear();
    console_enabled = enabled;
}
