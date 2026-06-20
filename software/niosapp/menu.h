#ifndef MENU_H
#define MENU_H

#include "key.h"

#define MENU_MAX_OPTIONS 99u
#define MENU_NO_SELECTION (-1)

typedef void (*MenuEdgeCallback)(void *context);

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
 * Handle KEY3/KEY2/KEY1 for the menu and redraw it.
 *
 * Returns the selected zero-based option index when KEY1 confirms, or
 * MENU_NO_SELECTION when no confirmation occurred.
 */
int menu_update(MenuState *menu, const KeyState *keys);

/**
 * Handle a menu and call left_edge_callback when KEY3 is pressed on the first
 * option. This lets callers attach a page before option 0 without duplicating
 * the normal menu navigation logic. The first option keeps a left arrow visible
 * to show that KEY3 returns to the attached page.
 */
int menu_update_with_left_edge(MenuState *menu,
                               const KeyState *keys,
                               MenuEdgeCallback left_edge_callback,
                               void *left_edge_context);

#endif
