#ifndef MENU_H
#define MENU_H

#include "key.h"

#define MENU_MAX_OPTIONS 99u
#define MENU_NO_SELECTION (-1)

typedef struct {
    const char *const *options;
    unsigned char option_count;
    unsigned char selected_index;
} MenuState;

/**
 * Initialize a menu from a null-terminated option list.
 *
 * The list is counted up to MENU_MAX_OPTIONS so the LCD counter can remain
 * two decimal digits.
 */
void menu_init(MenuState *menu, const char *const options[]);

/**
 * Handle KEY3/KEY2/KEY0 for the menu and redraw it.
 *
 * Returns the selected zero-based option index when KEY0 confirms, or
 * MENU_NO_SELECTION when no confirmation occurred.
 */
int menu_update(MenuState *menu, const KeyState *keys);

#endif
