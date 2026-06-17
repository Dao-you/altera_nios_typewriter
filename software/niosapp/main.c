#include <stdio.h>
#include "system.h"
#include "altera_avalon_pio_regs.h"
#include "priv/alt_busy_sleep.h"
#include "display.h"
#include "editor.h"
#include "eeprom.h"
#include "key.h"

#define MAIN_LOOP_DELAY_US 10000
#define SW_INSERT_MASK 0x00010000u
#define SW_NAV_MASK 0x00020000u

/**
 * Read SW[17:0] from the Avalon PIO.
 */
static unsigned int app_read_switches(void)
{
    return IORD_ALTERA_AVALON_PIO_DATA(PIO_IN_SW_BASE) & 0x3FFFFu;
}

/**
 * Advance the LEDR activity marquee while EEPROM blocks the main loop.
 *
 * The tick value is an activity animation counter only. It is intentionally
 * not shown as a storage-progress value.
 */
static void app_show_eeprom_activity(unsigned int tick, void *context)
{
    (void)context;
    display_save_marquee(tick);
}

/**
 * Save the document only when dirty; return the current EEPROM error state.
 */
static int app_handle_save(EditorDocument *editor)
{
    if (!editor->dirty) {
        return 0;
    }
    if (eeprom_save_document_with_activity(editor, app_show_eeprom_activity, 0)) {
        editor_mark_saved(editor);
        printf("EEPROM save OK\n");
        return 0;
    }

    printf("EEPROM save failed\n");
    return 1;
}

/**
 * Dispatch one debounced key event set to editor and EEPROM actions.
 */
static void app_handle_keys(EditorDocument *editor,
                            const KeyState *keys,
                            unsigned char ascii,
                            int nav_mode,
                            int *eeprom_error)
{
    if (key_pressed_edge(keys, KEY_MASK_0)) {
        *eeprom_error = app_handle_save(editor);
    }
    if (key_pressed_edge(keys, KEY_MASK_1)) {
        editor_write_ascii(editor, ascii);
    }
    if (key_pressed_edge(keys, KEY_MASK_3)) {
        if (nav_mode) {
            editor_move_up(editor);
        } else {
            editor_move_left(editor);
        }
    }
    if (key_pressed_edge(keys, KEY_MASK_2)) {
        if (nav_mode) {
            editor_move_down(editor);
        } else {
            editor_move_right(editor);
        }
    }
}

/**
 * Start the text editor application and keep polling SW/KEY forever.
 */
int main(void)
{
    EditorDocument editor;
    KeyState keys;
    unsigned int switches;
    unsigned char ascii;
    int nav_mode;
    int eeprom_error;
    int load_status;

    printf("Nios II text editor starting\n");

    editor_init(&editor);
    display_init();
    app_show_eeprom_activity(0, 0);

    eeprom_init();
    load_status = eeprom_load_document_with_activity(&editor,
                                                     app_show_eeprom_activity,
                                                     0);
    eeprom_error = (load_status == EEPROM_LOAD_ERROR);
    if (load_status == EEPROM_LOAD_OK) {
        printf("EEPROM document loaded\n");
    } else if (load_status == EEPROM_LOAD_EMPTY) {
        printf("EEPROM has no valid document; starting blank\n");
    } else {
        printf("EEPROM load failed; starting blank\n");
    }

    key_init(&keys);

    while (1) {
        switches = app_read_switches();
        ascii = (unsigned char)(switches & 0x7Fu);
        nav_mode = (switches & SW_NAV_MASK) != 0;
        editor_set_insert_mode(&editor, (switches & SW_INSERT_MASK) != 0);

        key_update(&keys);
        app_handle_keys(&editor, &keys, ascii, nav_mode, &eeprom_error);
        display_update(&editor, ascii, nav_mode, eeprom_error);

        alt_busy_sleep(MAIN_LOOP_DELAY_US);
    }

    return 0;
}
