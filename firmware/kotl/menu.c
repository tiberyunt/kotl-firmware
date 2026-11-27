/* SPDX-License-Identifier: MIT */
#include "menu.h"
#include "kotl.h"
#include "platform.h"
static void number(unsigned n)
{
    char buffer[12];
    unsigned count = 0;
    do {
        buffer[count++] = '0' + n % 10;
        n /= 10;
    } while (n);
    while (count)
        putchar_serial(buffer[--count]);
}
void menu_text(unsigned x, unsigned y, unsigned fg, unsigned bg, const char *text, unsigned width)
{
    if (x >= 80 || y >= 25)
        return;
    if (width > 80 - x)
        width = 80 - x;
    display_ui_text(x, y, fg, bg, text, width);
    puts_serial("\033[");
    number(y + 1);
    putchar_serial(';');
    number(x + 1);
    putchar_serial('H');
    puts_serial(fg == UI_WHITE ? "\033[97m" : "\033[37m");
    puts_serial(bg == UI_SELECTED ? "\033[100m" : "\033[40m");
    for (unsigned i = 0; i < width; i++)
        putchar_serial(*text ? *text++ : ' ');
}
void menu_frame(const char *title, unsigned tab, const char *status)
{
    display_ui_begin();
    puts_serial("\033[40m\033[2J\033[H");
    menu_text(0, 0, UI_WHITE, UI_BACKGROUND, title, 80);
    static const char *tabs[] = {" Main ", " Boot ", " Console ", " Exit "};
    menu_text(0, 1, UI_WHITE, UI_BACKGROUND, "", 80);
    unsigned x = 2;
    for (unsigned i = 0; tab < 4 && i < 4; i++) {
        unsigned width = i == 2 ? 11 : 9;
        menu_text(x, 1, i == tab ? UI_WHITE : UI_MUTED, i == tab ? UI_SELECTED : UI_BACKGROUND, tabs[i],
                  width);
        x += width;
    }
    if (tab >= 4)
        menu_text(2, 1, UI_WHITE, UI_BACKGROUND, "One-time boot selection", 76);
    menu_text(0, 3, UI_MUTED, UI_BACKGROUND,
              "+---------------------------------------------------+--------------------------+",
              80);
    for (unsigned y = 4; y <= 20; y++) {
        menu_text(0, y, UI_MUTED, UI_BACKGROUND, "|", 1);
        menu_text(52, y, UI_MUTED, UI_BACKGROUND, "|", 1);
        menu_text(79, y, UI_MUTED, UI_BACKGROUND, "|", 1);
    }
    menu_text(0, 21, UI_MUTED, UI_BACKGROUND,
              "+---------------------------------------------------+--------------------------+",
              80);
    menu_text(0, 22, UI_MUTED, UI_BACKGROUND, status, 80);
    menu_text(0, 23, UI_WHITE, UI_BACKGROUND, " Left/Right: Tab   Up/Down: Select   Enter: Change/Select",
              80);
    menu_text(0, 24, UI_WHITE, UI_BACKGROUND, " F9: Defaults   F10: Save & Exit   Esc: Discard & Exit",
              80);
}
void menu_field(unsigned row, const char *label, const char *value, int selected)
{
    menu_text(2, row, selected ? UI_WHITE : UI_MUTED, selected ? UI_SELECTED : UI_BACKGROUND, label, 27);
    menu_text(29, row, selected ? UI_WHITE : UI_MUTED, selected ? UI_SELECTED : UI_BACKGROUND, value, 22);
}
void menu_help(const char *a, const char *b, const char *c, const char *d)
{
    menu_text(55, 5, UI_MUTED, UI_BACKGROUND, "Item Specific Help", 22);
    menu_text(55, 7, UI_MUTED, UI_BACKGROUND, a, 22);
    menu_text(55, 8, UI_MUTED, UI_BACKGROUND, b, 22);
    menu_text(55, 9, UI_MUTED, UI_BACKGROUND, c, 22);
    menu_text(55, 10, UI_MUTED, UI_BACKGROUND, d, 22);
}
void menu_leave(void)
{
    display_ui_leave();
    puts_serial("\033[0m\033[2J\033[H");
}
