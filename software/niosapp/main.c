#include <stdio.h>
#include "system.h"
#include "altera_avalon_pio_regs.h"
#include "priv/alt_busy_sleep.h"
#include "display.h"
#include "editor.h"
#include "eeprom.h"
#include "key.h"
#include "keyboard.h"
#include "menu.h"
#include "sdcard.h"

#define MAIN_LOOP_DELAY_US 10000
#define KEYBOARD_DRAIN_LIMIT 16
#define SW_INSERT_MASK 0x00010000u
#define SW_NAV_MASK 0x00020000u

typedef enum {
    APP_STATE_MENU = 0,
    APP_STATE_EDITOR,
    APP_STATE_EDITOR_MENU,
    APP_STATE_SD_VIEW
} AppState;

typedef enum {
    APP_MENU_EDITOR = 0,
    APP_MENU_SD_QUESTION = 1
} AppMenuChoice;

typedef enum {
    EDITOR_MENU_SAVE_TO_ROM = 0,
    EDITOR_MENU_CLEAR_THIS_LINE,
    EDITOR_MENU_CLEAR_ALL,
    EDITOR_MENU_MOVE_TO_HEAD,
    EDITOR_MENU_MOVE_TO_END
} EditorMenuChoice;

static char app_sd_text[SDCARD_TEXT_BUFFER_SIZE];
static unsigned int app_sd_text_length = 0;
static unsigned int app_sd_first_line = 0;
static unsigned int app_sd_line_count = 1;
static SdCardResult app_sd_status = SDCARD_NO_SPI;
static const char *const app_start_menu_options[] = {
    "EEPROM EDITOR",
    "SD QUESTION",
    0
};
static const char *const app_editor_menu_options[] = {
    "Save to ROM",
    "Clear this line",
    "Clear All",
    "Move to head",
    "Move to end",
    0
};

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
    display_show_activity_marquee(tick);
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

static int app_load_eeprom_document(EditorDocument *editor)
{
    int load_status;

    display_show_message("Loading EEPROM", "Please wait");
    app_show_eeprom_activity(0, 0);
    eeprom_init();
    load_status = eeprom_load_document_with_activity(editor,
                                                     app_show_eeprom_activity,
                                                     0);
    if (load_status == EEPROM_LOAD_OK) {
        printf("EEPROM document loaded\n");
    } else if (load_status == EEPROM_LOAD_EMPTY) {
        printf("EEPROM has no valid document; starting blank\n");
    } else {
        printf("EEPROM load failed; starting blank\n");
    }

    return load_status == EEPROM_LOAD_ERROR;
}

static void app_handle_editor_menu_choice(EditorDocument *editor,
                                          int selection,
                                          int *eeprom_error)
{
    switch (selection) {
    case EDITOR_MENU_SAVE_TO_ROM:
        *eeprom_error = app_handle_save(editor);
        break;
    case EDITOR_MENU_CLEAR_THIS_LINE:
        editor_clear_current_line(editor);
        break;
    case EDITOR_MENU_CLEAR_ALL:
        editor_clear_all(editor);
        break;
    case EDITOR_MENU_MOVE_TO_HEAD:
        editor_move_to_head(editor);
        break;
    case EDITOR_MENU_MOVE_TO_END:
        editor_move_to_end(editor);
        break;
    default:
        break;
    }
}

static unsigned int app_count_text_lines(const char *text, unsigned int length)
{
    unsigned int i;
    unsigned int count;

    if (length == 0u) {
        return 1u;
    }

    count = 1u;
    for (i = 0u; i < length; ++i) {
        if (text[i] == '\n' && i + 1u < length) {
            ++count;
        }
    }

    return count;
}

static int app_sd_text_available(void)
{
    return app_sd_status == SDCARD_OK ||
        app_sd_status == SDCARD_OK_TRUNCATED;
}

static void app_display_sd_view(void)
{
    if (app_sd_text_available()) {
        display_show_text_page(app_sd_text,
                               app_sd_text_length,
                               app_sd_first_line);
    } else {
        display_show_message("SD QUESTION.TXT",
                             sdcard_result_text(app_sd_status));
    }
}

static void app_load_sd_question(void)
{
    display_show_message("Reading SD", "QUESTION.TXT");
    display_show_activity_marquee(0);
    app_sd_status = sdcard_read_question_text(app_sd_text,
                                              SDCARD_TEXT_BUFFER_SIZE,
                                              &app_sd_text_length);
    app_sd_first_line = 0u;
    app_sd_line_count = app_count_text_lines(app_sd_text, app_sd_text_length);
    printf("SD QUESTION.TXT: %s, %u bytes\n",
           sdcard_result_text(app_sd_status),
           app_sd_text_length);
}

/**
 * Dispatch one debounced key event set to editor and EEPROM actions.
 */
static void app_handle_keys(EditorDocument *editor,
                            const KeyState *keys,
                            unsigned char ascii,
                            int nav_mode)
{
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
 * Dispatch one decoded PS/2 keyboard byte to the existing editor actions.
 */
static void app_handle_keyboard_code(EditorDocument *editor, unsigned char code)
{
    switch (code) {
    case KEYBOARD_CODE_LEFT:
        editor_move_left(editor);
        break;
    case KEYBOARD_CODE_RIGHT:
        editor_move_right(editor);
        break;
    case KEYBOARD_CODE_UP:
        editor_move_up(editor);
        break;
    case KEYBOARD_CODE_DOWN:
        editor_move_down(editor);
        break;
    default:
        if (code < 0x80u) {
            editor_write_ascii(editor, code);
        }
        break;
    }
}

/**
 * Drain the small hardware FIFO so typed characters reach the editor quickly.
 */
static void app_handle_keyboard(EditorDocument *editor)
{
    unsigned char code;
    unsigned int count;

    count = 0;
    while (count < KEYBOARD_DRAIN_LIMIT && keyboard_read(&code)) {
        app_handle_keyboard_code(editor, code);
        ++count;
    }
}

/**
 * Start the text editor application and keep polling SW/KEY forever.
 */
int main(void)
{
    EditorDocument editor;
    KeyState keys;
    MenuState start_menu;
    MenuState editor_menu;
    AppState state;
    unsigned int switches;
    unsigned char ascii;
    int menu_selection;
    int nav_mode;
    int eeprom_error;
    int editor_loaded;

    printf("Nios II text editor starting\n");

    editor_init(&editor);
    display_init();
    key_init(&keys);
    keyboard_init();
    menu_init(&start_menu, app_start_menu_options);
    menu_init(&editor_menu, app_editor_menu_options);
    eeprom_error = 0;
    editor_loaded = 0;
    state = APP_STATE_MENU;

    while (1) {
        switches = app_read_switches();
        ascii = (unsigned char)(switches & 0x7Fu);
        nav_mode = (switches & SW_NAV_MASK) != 0;
        editor_set_insert_mode(&editor, (switches & SW_INSERT_MASK) != 0);

        key_update(&keys);
        if (state == APP_STATE_MENU) {
            menu_selection = menu_update(&start_menu, &keys);
            if (menu_selection == APP_MENU_EDITOR) {
                if (!editor_loaded) {
                    eeprom_error = app_load_eeprom_document(&editor);
                    editor_loaded = 1;
                }
                state = APP_STATE_EDITOR;
            } else if (menu_selection == APP_MENU_SD_QUESTION) {
                app_load_sd_question();
                state = APP_STATE_SD_VIEW;
            }
        } else if (state == APP_STATE_SD_VIEW) {
            if (key_pressed_edge(&keys, KEY_MASK_0)) {
                if (!editor_loaded) {
                    eeprom_error = app_load_eeprom_document(&editor);
                    editor_loaded = 1;
                }
                state = APP_STATE_EDITOR;
            } else {
                if (key_pressed_edge(&keys, KEY_MASK_1)) {
                    app_load_sd_question();
                }
                if (key_pressed_edge(&keys, KEY_MASK_2) &&
                    app_sd_first_line + 2u < app_sd_line_count) {
                    ++app_sd_first_line;
                }
                if (key_pressed_edge(&keys, KEY_MASK_3) &&
                    app_sd_first_line > 0u) {
                    --app_sd_first_line;
                }
                app_display_sd_view();
            }
        } else if (state == APP_STATE_EDITOR_MENU) {
            menu_selection = menu_update(&editor_menu, &keys);
            if (menu_selection != MENU_NO_SELECTION) {
                app_handle_editor_menu_choice(&editor,
                                              menu_selection,
                                              &eeprom_error);
                state = APP_STATE_EDITOR;
            }
        } else {
            if (key_pressed_edge(&keys, KEY_MASK_0)) {
                menu_init(&editor_menu, app_editor_menu_options);
                state = APP_STATE_EDITOR_MENU;
            } else {
                app_handle_keys(&editor, &keys, ascii, nav_mode);
                app_handle_keyboard(&editor);
                display_update(&editor, ascii, nav_mode, eeprom_error);
            }
        }

        alt_busy_sleep(MAIN_LOOP_DELAY_US);
    }

    return 0;
}
