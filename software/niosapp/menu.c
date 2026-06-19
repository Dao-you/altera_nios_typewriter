#include "menu.h"
#include "display.h"

static unsigned char menu_count_options(const char *const options[])
{
    unsigned char count;

    count = 0;
    if (options == 0) {
        return 0;
    }

    while (count < MENU_MAX_OPTIONS && options[count] != 0) {
        ++count;
    }

    return count;
}

void menu_init(MenuState *menu, const char *const options[])
{
    if (menu == 0) {
        return;
    }

    menu->options = options;
    menu->option_count = menu_count_options(options);
    menu->selected_index = 0;
}

static const char *menu_selected_name(const MenuState *menu)
{
    if (menu == 0 || menu->option_count == 0u || menu->options == 0) {
        return "(no options)";
    }

    return menu->options[menu->selected_index];
}

static void menu_clamp_selection(MenuState *menu)
{
    if (menu->option_count == 0u) {
        menu->selected_index = 0;
    } else if (menu->selected_index >= menu->option_count) {
        menu->selected_index = (unsigned char)(menu->option_count - 1u);
    }
}

int menu_update(MenuState *menu, const KeyState *keys)
{
    int confirmed;

    if (menu == 0) {
        return MENU_NO_SELECTION;
    }

    menu_clamp_selection(menu);

    if (keys != 0 && key_pressed_edge(keys, KEY_MASK_3) &&
        menu->selected_index > 0u) {
        --menu->selected_index;
    }
    if (keys != 0 && key_pressed_edge(keys, KEY_MASK_2) &&
        menu->selected_index + 1u < menu->option_count) {
        ++menu->selected_index;
    }

    confirmed = MENU_NO_SELECTION;
    if (keys != 0 && key_pressed_edge(keys, KEY_MASK_0) &&
        menu->option_count > 0u) {
        confirmed = menu->selected_index;
    }

    display_show_menu_item(menu_selected_name(menu),
                           menu->selected_index,
                           menu->option_count);

    return confirmed;
}
