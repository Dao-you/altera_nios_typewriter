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
#define VI_COMMAND_MAX_LEN 15u
#define EDITOR_TOP_MARKER "EEPROM"
#define EDITOR_BOTTOM_MARKER "END"

typedef enum {
    APP_STATE_MENU = 0,
    APP_STATE_EDITOR,
    APP_STATE_EDITOR_COMMAND,
    APP_STATE_EDITOR_MENU,
    APP_STATE_SD_VIEW,
    APP_STATE_INFO_MESSAGE,
    APP_STATE_CONFIRM_MESSAGE,
    APP_STATE_ERROR_MESSAGE
} AppState;

typedef enum {
    APP_MENU_EDITOR = 0,
    APP_MENU_SD_QUESTION = 1
} AppMenuChoice;

typedef enum {
    EDITOR_MENU_SAVE_TO_ROM = 0,
    EDITOR_MENU_QUIT,
    EDITOR_MENU_RESTORE_WHOLE,
    EDITOR_MENU_CLEAR_THIS_LINE,
    EDITOR_MENU_CLEAR_ALL,
    EDITOR_MENU_MOVE_TO_HEAD,
    EDITOR_MENU_MOVE_TO_END,
    EDITOR_MENU_CANCEL
} EditorMenuChoice;

typedef enum {
    APP_SAVE_SKIPPED = 0,
    APP_SAVE_OK,
    APP_SAVE_FAILED
} AppSaveResult;

typedef enum {
    APP_CONFIRM_NONE = 0,
    APP_CONFIRM_CLEAR_ALL,
    APP_CONFIRM_QUIT_NO_SAVE
} AppConfirmAction;

static char app_vi_command[VI_COMMAND_MAX_LEN + 1u];
static unsigned char app_vi_command_len = 0;
static char app_sd_text[SDCARD_TEXT_BUFFER_SIZE];
static unsigned int app_sd_text_length = 0;
static unsigned int app_sd_first_line = 0;
static unsigned int app_sd_line_count = 1;
static SdCardResult app_sd_status = SDCARD_NO_SPI;
static const char *app_modal_message = "";
static AppState app_modal_return_state = APP_STATE_EDITOR;
static AppConfirmAction app_confirm_action = APP_CONFIRM_NONE;
static const char *const app_start_menu_options[] = {
    "EEPROM EDITOR",
    "SD QUESTION",
    0
};
static const char *const app_editor_menu_options[] = {
    "Save to ROM",
    "Quit",
    "Restore whole",
    "Clear this line",
    "Clear All",
    "Move to head",
    "Move to end",
    "Cancel",
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
 * Advance the LEDR activity marquee while a blocking load/save runs.
 *
 * The tick value is an activity animation counter only. It is intentionally
 * not shown as a storage or file-read progress value.
 */
static void app_show_blocking_activity(unsigned int tick, void *context)
{
    (void)context;
    display_show_activity_marquee(tick);
}

static void app_reset_vi_command(void)
{
    app_vi_command_len = 0;
    app_vi_command[0] = '\0';
}

static void app_start_vi_command(AppState *state)
{
    app_reset_vi_command();
    *state = APP_STATE_EDITOR_COMMAND;
}

static void app_return_to_vi_command(void *context)
{
    AppState *state;

    state = (AppState *)context;
    if (state != 0) {
        app_start_vi_command(state);
    }
}

static void app_start_info_message(const char *message,
                                   AppState return_state,
                                   AppState *state)
{
    app_modal_message = message;
    app_modal_return_state = return_state;
    app_confirm_action = APP_CONFIRM_NONE;
    *state = APP_STATE_INFO_MESSAGE;
}

static void app_start_error_message(const char *message,
                                    AppState return_state,
                                    AppState *state)
{
    app_modal_message = message;
    app_modal_return_state = return_state;
    app_confirm_action = APP_CONFIRM_NONE;
    *state = APP_STATE_ERROR_MESSAGE;
}

static void app_start_confirm_message(const char *message,
                                      AppConfirmAction action,
                                      AppState return_state,
                                      AppState *state)
{
    app_modal_message = message;
    app_modal_return_state = return_state;
    app_confirm_action = action;
    *state = APP_STATE_CONFIRM_MESSAGE;
}

/**
 * Save the document only when dirty.
 */
static AppSaveResult app_handle_save(EditorDocument *editor)
{
    if (!editor->dirty) {
        return APP_SAVE_SKIPPED;
    }
    if (eeprom_save_document_with_activity(editor,
                                           app_show_blocking_activity,
                                           0)) {
        editor_mark_saved(editor);
        printf("EEPROM save OK\n");
        return APP_SAVE_OK;
    }

    printf("EEPROM save failed\n");
    return APP_SAVE_FAILED;
}

static int app_load_eeprom_document(EditorDocument *editor)
{
    int load_status;

    display_show_message("Loading EEPROM", "Please wait");
    app_show_blocking_activity(0, 0);
    eeprom_init();
    load_status = eeprom_load_document_with_activity(editor,
                                                     app_show_blocking_activity,
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

static void app_quit_editor(EditorDocument *editor,
                            int *editor_loaded,
                            int *eeprom_error,
                            AppState *state)
{
    editor_init(editor);
    if (editor_loaded != 0) {
        *editor_loaded = 0;
    }
    if (eeprom_error != 0) {
        *eeprom_error = 0;
    }
    app_reset_vi_command();
    *state = APP_STATE_MENU;
}

static void app_start_quit_no_save_confirm(AppState *state)
{
    app_start_confirm_message("Quit no save?",
                              APP_CONFIRM_QUIT_NO_SAVE,
                              APP_STATE_EDITOR,
                              state);
}

static void app_handle_save_feedback(EditorDocument *editor,
                                     int *eeprom_error,
                                     AppState *state)
{
    AppSaveResult save_result;

    save_result = app_handle_save(editor);
    if (save_result == APP_SAVE_SKIPPED) {
        app_start_info_message("No changes", APP_STATE_EDITOR, state);
    } else if (save_result == APP_SAVE_OK) {
        *eeprom_error = 0;
        app_start_info_message("Saved to ROM", APP_STATE_EDITOR, state);
    } else {
        *eeprom_error = 1;
        app_start_error_message("Save failed", APP_STATE_EDITOR, state);
    }
}

static void app_save_and_quit(EditorDocument *editor,
                              int *eeprom_error,
                              int *editor_loaded,
                              AppState *state)
{
    AppSaveResult save_result;

    save_result = app_handle_save(editor);
    if (save_result == APP_SAVE_FAILED) {
        *eeprom_error = 1;
        app_start_error_message("Save failed", APP_STATE_EDITOR, state);
        return;
    }

    *eeprom_error = 0;
    app_quit_editor(editor, editor_loaded, eeprom_error, state);
}

static void app_restore_whole(EditorDocument *editor,
                              int *eeprom_error,
                              AppState *state)
{
    EditorDocument restored;
    int load_status;

    display_show_message("Restoring", "Please wait");
    app_show_blocking_activity(0, 0);
    eeprom_init();
    editor_init(&restored);
    load_status = eeprom_load_document_with_activity(&restored,
                                                     app_show_blocking_activity,
                                                     0);
    if (load_status == EEPROM_LOAD_OK) {
        *editor = restored;
        *eeprom_error = 0;
        app_start_info_message("Restored", APP_STATE_EDITOR, state);
    } else if (load_status == EEPROM_LOAD_EMPTY) {
        app_start_info_message("No ROM data", APP_STATE_EDITOR, state);
    } else {
        *eeprom_error = 1;
        app_start_error_message("Restore failed", APP_STATE_EDITOR, state);
    }
}

static void app_handle_editor_menu_choice(EditorDocument *editor,
                                          int selection,
                                          int *eeprom_error,
                                          AppState *state,
                                          int *editor_loaded)
{
    switch (selection) {
    case EDITOR_MENU_SAVE_TO_ROM:
        app_handle_save_feedback(editor, eeprom_error, state);
        break;
    case EDITOR_MENU_QUIT:
        if (editor->dirty) {
            app_start_quit_no_save_confirm(state);
        } else {
            app_quit_editor(editor, editor_loaded, eeprom_error, state);
        }
        break;
    case EDITOR_MENU_RESTORE_WHOLE:
        app_restore_whole(editor, eeprom_error, state);
        break;
    case EDITOR_MENU_CLEAR_THIS_LINE:
        editor_clear_current_line(editor);
        *state = APP_STATE_EDITOR;
        break;
    case EDITOR_MENU_CLEAR_ALL:
        app_start_confirm_message("Clear All?",
                                  APP_CONFIRM_CLEAR_ALL,
                                  APP_STATE_EDITOR,
                                  state);
        break;
    case EDITOR_MENU_MOVE_TO_HEAD:
        editor_move_to_head(editor);
        *state = APP_STATE_EDITOR;
        break;
    case EDITOR_MENU_MOVE_TO_END:
        editor_move_to_end(editor);
        *state = APP_STATE_EDITOR;
        break;
    case EDITOR_MENU_CANCEL:
        *state = APP_STATE_EDITOR;
        break;
    default:
        *state = APP_STATE_EDITOR;
        break;
    }
}

static void app_handle_confirm_yes(EditorDocument *editor,
                                   AppState *state,
                                   int *editor_loaded,
                                   int *eeprom_error)
{
    switch (app_confirm_action) {
    case APP_CONFIRM_CLEAR_ALL:
        editor_clear_all(editor);
        break;
    case APP_CONFIRM_QUIT_NO_SAVE:
        app_confirm_action = APP_CONFIRM_NONE;
        app_quit_editor(editor, editor_loaded, eeprom_error, state);
        return;
    default:
        break;
    }

    app_confirm_action = APP_CONFIRM_NONE;
    *state = app_modal_return_state;
}

static char app_vi_lower_char(char ch)
{
    if (ch >= 'A' && ch <= 'Z') {
        return (char)(ch + ('a' - 'A'));
    }
    return ch;
}

static int app_vi_command_is(const char *expected)
{
    unsigned int i;

    i = 0;
    while (expected[i] != '\0') {
        if (i >= app_vi_command_len ||
            app_vi_lower_char(app_vi_command[i]) != expected[i]) {
            return 0;
        }
        ++i;
    }

    return i == app_vi_command_len;
}

static void app_vi_command_backspace(void)
{
    if (app_vi_command_len == 0u) {
        return;
    }

    --app_vi_command_len;
    app_vi_command[app_vi_command_len] = '\0';
}

static void app_vi_command_append(unsigned char code)
{
    if (app_vi_command_len >= VI_COMMAND_MAX_LEN) {
        return;
    }
    if (code < 0x20u || code > 0x7Eu) {
        return;
    }

    app_vi_command[app_vi_command_len] = (char)code;
    ++app_vi_command_len;
    app_vi_command[app_vi_command_len] = '\0';
}

static void app_execute_vi_command(EditorDocument *editor,
                                   int *eeprom_error,
                                   int *editor_loaded,
                                   AppState *state)
{
    if (app_vi_command_len == 0u) {
        *state = APP_STATE_EDITOR;
    } else if (app_vi_command_is("w")) {
        app_reset_vi_command();
        app_handle_save_feedback(editor, eeprom_error, state);
    } else if (app_vi_command_is("q")) {
        app_reset_vi_command();
        if (editor->dirty) {
            app_start_quit_no_save_confirm(state);
        } else {
            app_quit_editor(editor, editor_loaded, eeprom_error, state);
        }
    } else if (app_vi_command_is("wq") || app_vi_command_is("x")) {
        app_reset_vi_command();
        app_save_and_quit(editor, eeprom_error, editor_loaded, state);
    } else if (app_vi_command_is("e!")) {
        app_reset_vi_command();
        app_restore_whole(editor, eeprom_error, state);
    } else {
        app_start_error_message("Bad command",
                                APP_STATE_EDITOR_COMMAND,
                                state);
    }
}

static void app_handle_vi_command_code(EditorDocument *editor,
                                       unsigned char code,
                                       int *eeprom_error,
                                       int *editor_loaded,
                                       AppState *state)
{
    switch (code) {
    case 0x08u:
    case 0x7Fu:
        app_vi_command_backspace();
        break;
    case 0x0Au:
    case 0x0Du:
        app_execute_vi_command(editor, eeprom_error, editor_loaded, state);
        break;
    default:
        app_vi_command_append(code);
        break;
    }
}

static void app_handle_vi_keyboard(EditorDocument *editor,
                                   int *eeprom_error,
                                   int *editor_loaded,
                                   AppState *state)
{
    unsigned char code;
    unsigned int count;

    count = 0;
    while (count < KEYBOARD_DRAIN_LIMIT &&
           *state == APP_STATE_EDITOR_COMMAND &&
           keyboard_read(&code)) {
        if (code < 0x80u) {
            app_handle_vi_command_code(editor,
                                       code,
                                       eeprom_error,
                                       editor_loaded,
                                       state);
        }
        ++count;
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
    app_show_blocking_activity(0, 0);
    app_sd_status = sdcard_read_question_text_with_activity(
        app_sd_text,
        SDCARD_TEXT_BUFFER_SIZE,
        &app_sd_text_length,
        app_show_blocking_activity,
        0);
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
                if (eeprom_error) {
                    app_start_error_message("EEPROM load fail",
                                            APP_STATE_EDITOR,
                                            &state);
                } else {
                    state = APP_STATE_EDITOR;
                }
            } else if (menu_selection == APP_MENU_SD_QUESTION) {
                app_load_sd_question();
                state = APP_STATE_SD_VIEW;
            }
        } else if (state == APP_STATE_INFO_MESSAGE) {
            display_show_info_message(app_modal_message);
            if (key_pressed_edge(&keys, KEY_MASK_0)) {
                state = app_modal_return_state;
            }
        } else if (state == APP_STATE_ERROR_MESSAGE) {
            display_show_error_message(app_modal_message);
            if (key_pressed_edge(&keys, KEY_MASK_0)) {
                state = app_modal_return_state;
            }
        } else if (state == APP_STATE_CONFIRM_MESSAGE) {
            display_show_confirm_message(app_modal_message);
            if (key_pressed_edge(&keys, KEY_MASK_1)) {
                app_handle_confirm_yes(&editor,
                                       &state,
                                       &editor_loaded,
                                       &eeprom_error);
            } else if (key_pressed_edge(&keys, KEY_MASK_0)) {
                app_confirm_action = APP_CONFIRM_NONE;
                state = app_modal_return_state;
            }
        } else if (state == APP_STATE_SD_VIEW) {
            if (key_pressed_edge(&keys, KEY_MASK_0)) {
                if (!editor_loaded) {
                    eeprom_error = app_load_eeprom_document(&editor);
                    editor_loaded = 1;
                }
                if (eeprom_error) {
                    app_start_error_message("EEPROM load fail",
                                            APP_STATE_EDITOR,
                                            &state);
                } else {
                    state = APP_STATE_EDITOR;
                }
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
        } else if (state == APP_STATE_EDITOR_COMMAND) {
            if (key_pressed_edge(&keys, KEY_MASK_2)) {
                menu_init(&editor_menu, app_editor_menu_options);
                state = APP_STATE_EDITOR_MENU;
            } else {
                if (key_pressed_edge(&keys, KEY_MASK_1)) {
                    app_handle_vi_command_code(&editor,
                                               ascii,
                                               &eeprom_error,
                                               &editor_loaded,
                                               &state);
                }
                if (state == APP_STATE_EDITOR_COMMAND &&
                    key_pressed_edge(&keys, KEY_MASK_0)) {
                    app_execute_vi_command(&editor,
                                           &eeprom_error,
                                           &editor_loaded,
                                           &state);
                }
                if (state == APP_STATE_EDITOR_COMMAND) {
                    app_handle_vi_keyboard(&editor,
                                           &eeprom_error,
                                           &editor_loaded,
                                           &state);
                }
                if (state == APP_STATE_EDITOR_COMMAND) {
                    display_show_vi_command(app_vi_command);
                }
            }
        } else if (state == APP_STATE_EDITOR_MENU) {
            menu_selection = menu_update_with_left_edge(&editor_menu,
                                                       &keys,
                                                       app_return_to_vi_command,
                                                       &state);
            if (state == APP_STATE_EDITOR_MENU &&
                menu_selection != MENU_NO_SELECTION) {
                app_handle_editor_menu_choice(&editor,
                                              menu_selection,
                                              &eeprom_error,
                                              &state,
                                              &editor_loaded);
            }
        } else {
            if (key_pressed_edge(&keys, KEY_MASK_0)) {
                app_start_vi_command(&state);
            } else {
                app_handle_keys(&editor, &keys, ascii, nav_mode);
                app_handle_keyboard(&editor);
                display_update_with_markers(&editor,
                                            ascii,
                                            nav_mode,
                                            eeprom_error,
                                            EDITOR_TOP_MARKER,
                                            EDITOR_BOTTOM_MARKER);
            }
        }

        alt_busy_sleep(MAIN_LOOP_DELAY_US);
    }

    return 0;
}
