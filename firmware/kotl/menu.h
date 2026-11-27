/* SPDX-License-Identifier: MIT */
#ifndef KOTL_MENU_H
#define KOTL_MENU_H
#define UI_BACKGROUND 0x000000U
#define UI_SELECTED 0x262626U
#define UI_MUTED 0xa8a8a8U
#define UI_WHITE 0xffffffU
void menu_text(unsigned, unsigned, unsigned, unsigned, const char *, unsigned);
void menu_frame(const char *, unsigned, const char *);
void menu_field(unsigned, const char *, const char *, int);
void menu_help(const char *, const char *, const char *, const char *);
void menu_leave(void);
#endif
