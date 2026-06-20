#include <stdio.h>
#include "system.h"
#include "altera_avalon_pio_regs.h"
#include "priv/alt_busy_sleep.h"
#include "sys/alt_alarm.h"
#include "display.h"
#include "editor.h"
#include "editor_input.h"
#include "eeprom.h"
#include "key.h"
#include "keyboard.h"
#include "menu.h"
#include "sdcard.h"
#include "typing_game.h"

#define MAIN_LOOP_DELAY_US 10000
#define KEYBOARD_DRAIN_LIMIT 16
#define SW_INSERT_MASK 0x00010000u
#define SW_NAV_MASK 0x00020000u
#define SW_TYPING_INPUT_MASK 0x0000007Fu
#define SW_TYPING_READY_CLEAR_MASK 0x0000007Fu
#define VI_COMMAND_MAX_LEN 15u
#define EDITOR_TOP_MARKER "EEPROM"
#define SD_EDITOR_TOP_MARKER "SD"
#define EDITOR_BOTTOM_MARKER "END"
#define TYPING_READY_ACTION_TEXT "KEY1GO KEY0EXIT"
#define TYPING_ERROR_SIGNAL_TICKS 2000u
#define TYPING_RESULT_MESSAGE_LEN 16u

typedef enum {
    APP_STATE_MENU = 0,
    APP_STATE_EDITOR,
    APP_STATE_EDITOR_COMMAND,
    APP_STATE_EDITOR_MENU,
    APP_STATE_SD_VIEW,
    APP_STATE_TYPING_MODE_MENU,
    APP_STATE_TYPING_COUNT_MENU,
    APP_STATE_TYPING_READY,
    APP_STATE_TYPING_GAME,
    APP_STATE_TYPING_MENU,
    APP_STATE_TYPING_DONE,
    APP_STATE_INFO_MESSAGE,
    APP_STATE_CONFIRM_MESSAGE,
    APP_STATE_ERROR_MESSAGE
} AppState;

typedef enum {
    APP_MENU_EDITOR = 0,
    APP_MENU_SD_QUESTION = 1,
    APP_MENU_SD_EDITOR = 2,
    APP_MENU_TYPING_GAME = 3
} AppMenuChoice;

typedef enum {
    EDITOR_MENU_SAVE_PRIMARY = 0,
    EDITOR_MENU_SAVE_SECONDARY,
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
    APP_SAVE_FAILED,
    APP_SAVE_FILE_EXISTS
} AppSaveResult;

typedef enum {
    APP_CONFIRM_NONE = 0,
    APP_CONFIRM_CLEAR_ALL,
    APP_CONFIRM_QUIT_NO_SAVE,
    APP_CONFIRM_OVERWRITE_SD,
    APP_CONFIRM_SAVE_AS_EEPROM
} AppConfirmAction;

typedef enum {
    APP_EDITOR_SOURCE_EEPROM = 0,
    APP_EDITOR_SOURCE_SD
} AppEditorSource;

typedef enum {
    TYPING_MODE_TITLE = 0,
    TYPING_MODE_DEFAULT,
    TYPING_MODE_RANDOM,
    TYPING_MODE_QUIT
} TypingModeChoice;

typedef enum {
    TYPING_MENU_RESTART = 0,
    TYPING_MENU_CONTINUE,
    TYPING_MENU_QUIT
} TypingMenuChoice;

static char app_vi_command[VI_COMMAND_MAX_LEN + 1u];
static unsigned char app_vi_command_len = 0;
static char app_sd_text[SDCARD_TEXT_BUFFER_SIZE];
static unsigned int app_sd_text_length = 0;
static unsigned int app_sd_first_line = 0;
static unsigned int app_sd_line_count = 1;
static SdCardResult app_sd_status = SDCARD_NO_SPI;
static TypingGame app_typing_game;
static unsigned int app_entropy = 0;
static unsigned char app_typing_selected_rounds = TYPING_GAME_DEFAULT_ROUNDS;
static TypingGameCaseMode app_typing_case_mode = TYPING_GAME_CASE_DEFAULT;
static unsigned int app_typing_last_switch_input = 0;
static unsigned int app_typing_error_until_tick = 0;
static unsigned int app_typing_last_mismatch_signature = 0;
static char app_typing_result_message[TYPING_RESULT_MESSAGE_LEN + 1u];
static char app_typing_error_message[TYPING_RESULT_MESSAGE_LEN + 1u];
static const char *app_modal_message = "";
static AppState app_modal_return_state = APP_STATE_EDITOR;
static AppConfirmAction app_confirm_action = APP_CONFIRM_NONE;
static const char *const app_start_menu_options[] = {
    "EEPROM EDITOR",
    "SD QUESTION",
    "SD EDITOR",
    "TYPING GAME",
    0
};
static const char *const app_eeprom_editor_menu_options[] = {
    "Save to ROM",
    "Save as SD",
    "Quit",
    "Restore whole",
    "Clear this line",
    "Clear All",
    "Move to head",
    "Move to end",
    "Cancel",
    0
};
static const char *const app_sd_editor_menu_options[] = {
    "Save",
    "Save as EEPROM",
    "Quit",
    "Restore whole",
    "Clear this line",
    "Clear All",
    "Move to head",
    "Move to end",
    "Cancel",
    0
};
static const char *const app_typing_mode_options[] = {
    "Capitalized",
    "Default",
    "Random Caps",
    "Quit",
    0
};
static const char *const app_typing_count_options[] = {
    "5 Questions",
    "10 Questions",
    "15 Questions",
    "20 Questions",
    "25 Questions",
    "30 Questions",
    "35 Questions",
    "40 Questions",
    "45 Questions",
    "50 Questions",
    "Quit",
    0
};
static const char *const app_typing_menu_options[] = {
    "Restart",
    "Continue",
    "Quit",
    0
};

/**
 * Read SW[17:0] from the Avalon PIO.
 */
static unsigned int app_read_switches(void)
{
    return IORD_ALTERA_AVALON_PIO_DATA(PIO_IN_SW_BASE) & 0x3FFFFu;
}

static unsigned int app_timer_ticks(void)
{
    return alt_nticks();
}

static void app_reset_typing_error_signal(void)
{
    app_typing_error_until_tick = 0;
    app_typing_last_mismatch_signature = 0;
}

static unsigned int app_typing_input_signature(void)
{
    const EditorDocument *input;
    unsigned int signature;
    unsigned int i;

    input = &app_typing_game.input;
    signature = 2166136261u ^ (unsigned int)input->line_len[0];
    for (i = 0; i < input->line_len[0]; ++i) {
        signature ^= (unsigned char)input->document[0][i];
        signature *= 16777619u;
    }

    if (signature == 0u) {
        signature = 1u;
    }
    return signature;
}

static void app_update_typing_mismatch_signal(unsigned int now_ticks)
{
    unsigned int signature;

    if (!typing_game_current_answer_is_complete_mismatch(&app_typing_game)) {
        app_typing_last_mismatch_signature = 0;
        return;
    }

    signature = app_typing_input_signature();
    if (signature != app_typing_last_mismatch_signature) {
        app_typing_last_mismatch_signature = signature;
        app_typing_error_until_tick = now_ticks + TYPING_ERROR_SIGNAL_TICKS;
    }
}

static int app_typing_error_signal_active(unsigned int now_ticks)
{
    if (app_typing_error_until_tick == 0u) {
        return 0;
    }

    return ((int)(now_ticks - app_typing_error_until_tick)) < 0;
}

static void app_prepare_typing_result_message(void)
{
    unsigned int cpm_hundredths;

    cpm_hundredths = typing_game_cpm_hundredths(&app_typing_game);
    snprintf(app_typing_result_message,
             sizeof(app_typing_result_message),
             "CPM %u.%02u",
             cpm_hundredths / 100u,
             cpm_hundredths % 100u);
}

static unsigned char app_typing_rounds_from_selection(int selection)
{
    return (unsigned char)((selection + 1) * TYPING_GAME_ROUND_STEP);
}

static int app_typing_count_selection_is_quit(int selection)
{
    return selection >=
        (int)(TYPING_GAME_MAX_ROUNDS / TYPING_GAME_ROUND_STEP);
}

static void app_apply_typing_mode_selection(int selection)
{
    switch (selection) {
    case TYPING_MODE_TITLE:
        app_typing_case_mode = TYPING_GAME_CASE_TITLE;
        break;
    case TYPING_MODE_RANDOM:
        app_typing_case_mode = TYPING_GAME_CASE_RANDOM;
        break;
    case TYPING_MODE_DEFAULT:
    default:
        app_typing_case_mode = TYPING_GAME_CASE_DEFAULT;
        break;
    }
}

static const char *const *app_editor_menu_options_for_source(AppEditorSource source)
{
    if (source == APP_EDITOR_SOURCE_SD) {
        return app_sd_editor_menu_options;
    }
    return app_eeprom_editor_menu_options;
}

static const char *app_editor_top_marker(AppEditorSource source)
{
    if (source == APP_EDITOR_SOURCE_SD) {
        return SD_EDITOR_TOP_MARKER;
    }
    return EDITOR_TOP_MARKER;
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

static void app_enter_home_menu(AppState *state)
{
    display_clear_hex();
    if (state != 0) {
        *state = APP_STATE_MENU;
    }
}

static void app_set_state(AppState *state, AppState next_state)
{
    if (next_state == APP_STATE_MENU) {
        app_enter_home_menu(state);
    } else if (state != 0) {
        *state = next_state;
    }
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

static SdCardResult app_load_sd_editor_document(EditorDocument *editor)
{
    char editor_text[EDITOR_TEXT_BUFFER_SIZE];
    SdCardResult result;
    unsigned int length;
    int text_fits;

    display_show_message("Loading SD", SDCARD_EDITOR_FILE_NAME);
    app_show_blocking_activity(0, 0);
    length = 0u;
    result = sdcard_read_editor_text_with_activity(
        editor_text,
        EDITOR_TEXT_BUFFER_SIZE,
        &length,
        app_show_blocking_activity,
        0);
    if (result == SDCARD_OK || result == SDCARD_OK_TRUNCATED) {
        text_fits = editor_load_text(editor, editor_text, length);
        if (!text_fits && result == SDCARD_OK) {
            result = SDCARD_OK_TRUNCATED;
        }
        printf("SD %s loaded: %s, %u bytes\n",
               SDCARD_EDITOR_FILE_NAME,
               sdcard_result_text(result),
               length);
        return result;
    }

    editor_init(editor);
    printf("SD %s load failed: %s\n",
           SDCARD_EDITOR_FILE_NAME,
           sdcard_result_text(result));
    return result;
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
    app_enter_home_menu(state);
}

static void app_start_quit_no_save_confirm(AppState *state)
{
    app_start_confirm_message("Quit no save?",
                              APP_CONFIRM_QUIT_NO_SAVE,
                              APP_STATE_EDITOR,
                              state);
}

static AppSaveResult app_save_to_eeprom(EditorDocument *editor,
                                        int mark_primary_saved)
{
    if (mark_primary_saved && !editor->dirty) {
        return APP_SAVE_SKIPPED;
    }

    display_show_message("Saving EEPROM", "Please wait");
    app_show_blocking_activity(0, 0);
    eeprom_init();
    if (eeprom_save_document_with_activity(editor,
                                           app_show_blocking_activity,
                                           0)) {
        if (mark_primary_saved) {
            editor_mark_saved(editor);
        }
        printf("EEPROM save OK\n");
        return APP_SAVE_OK;
    }

    printf("EEPROM save failed\n");
    return APP_SAVE_FAILED;
}

static AppSaveResult app_save_to_sd(EditorDocument *editor,
                                    int overwrite,
                                    int mark_primary_saved)
{
    char editor_text[EDITOR_TEXT_BUFFER_SIZE];
    unsigned int length;
    SdCardResult result;

    if (mark_primary_saved && !editor->dirty) {
        return APP_SAVE_SKIPPED;
    }

    length = editor_export_text(editor,
                                editor_text,
                                EDITOR_TEXT_BUFFER_SIZE);
    if (length >= EDITOR_TEXT_BUFFER_SIZE) {
        printf("SD export too large\n");
        return APP_SAVE_FAILED;
    }

    display_show_message("Saving SD", SDCARD_EDITOR_FILE_NAME);
    app_show_blocking_activity(0, 0);
    result = sdcard_write_editor_text_with_activity(
        editor_text,
        length,
        overwrite,
        app_show_blocking_activity,
        0);
    if (result == SDCARD_OK) {
        if (mark_primary_saved) {
            editor_mark_saved(editor);
        }
        printf("SD %s save OK, %u bytes\n",
               SDCARD_EDITOR_FILE_NAME,
               length);
        return APP_SAVE_OK;
    }
    if (result == SDCARD_FILE_EXISTS) {
        printf("SD %s already exists\n", SDCARD_EDITOR_FILE_NAME);
        return APP_SAVE_FILE_EXISTS;
    }

    printf("SD %s save failed: %s\n",
           SDCARD_EDITOR_FILE_NAME,
           sdcard_result_text(result));
    return APP_SAVE_FAILED;
}

static void app_handle_primary_save_feedback(EditorDocument *editor,
                                             int *eeprom_error,
                                             AppEditorSource source,
                                             AppState *state)
{
    AppSaveResult save_result;

    if (source == APP_EDITOR_SOURCE_SD) {
        save_result = app_save_to_sd(editor, 1, 1);
    } else {
        save_result = app_save_to_eeprom(editor, 1);
    }
    if (save_result == APP_SAVE_SKIPPED) {
        app_start_info_message("No changes", APP_STATE_EDITOR, state);
    } else if (save_result == APP_SAVE_OK) {
        *eeprom_error = 0;
        if (source == APP_EDITOR_SOURCE_SD) {
            app_start_info_message("Saved to SD", APP_STATE_EDITOR, state);
        } else {
            app_start_info_message("Saved to ROM", APP_STATE_EDITOR, state);
        }
    } else {
        *eeprom_error = 1;
        app_start_error_message("Save failed", APP_STATE_EDITOR, state);
    }
}

static void app_handle_save_as_sd_feedback(EditorDocument *editor,
                                           int *eeprom_error,
                                           AppState *state)
{
    AppSaveResult save_result;

    save_result = app_save_to_sd(editor, 0, 0);
    if (save_result == APP_SAVE_FILE_EXISTS) {
        app_start_confirm_message("Overwrite SD?",
                                  APP_CONFIRM_OVERWRITE_SD,
                                  APP_STATE_EDITOR,
                                  state);
    } else if (save_result == APP_SAVE_OK) {
        *eeprom_error = 0;
        app_start_info_message("Saved to SD", APP_STATE_EDITOR, state);
    } else {
        *eeprom_error = 1;
        app_start_error_message("SD save failed", APP_STATE_EDITOR, state);
    }
}

static void app_handle_overwrite_sd_confirm(EditorDocument *editor,
                                            int *eeprom_error,
                                            AppState *state)
{
    if (app_save_to_sd(editor, 1, 0) == APP_SAVE_OK) {
        *eeprom_error = 0;
        app_start_info_message("Saved to SD", APP_STATE_EDITOR, state);
    } else {
        *eeprom_error = 1;
        app_start_error_message("SD save failed", APP_STATE_EDITOR, state);
    }
}

static void app_start_save_as_eeprom_confirm(AppState *state)
{
    app_start_confirm_message("Overwrite ROM?",
                              APP_CONFIRM_SAVE_AS_EEPROM,
                              APP_STATE_EDITOR,
                              state);
}

static void app_handle_save_as_eeprom_confirm(EditorDocument *editor,
                                              int *eeprom_error,
                                              AppState *state)
{
    if (app_save_to_eeprom(editor, 0) == APP_SAVE_OK) {
        *eeprom_error = 0;
        app_start_info_message("Saved to EEPROM", APP_STATE_EDITOR, state);
    } else {
        *eeprom_error = 1;
        app_start_error_message("Save failed", APP_STATE_EDITOR, state);
    }
}

static void app_save_and_quit(EditorDocument *editor,
                              int *eeprom_error,
                              int *editor_loaded,
                              AppEditorSource source,
                              AppState *state)
{
    AppSaveResult save_result;

    if (source == APP_EDITOR_SOURCE_SD) {
        save_result = app_save_to_sd(editor, 1, 1);
    } else {
        save_result = app_save_to_eeprom(editor, 1);
    }
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
                              AppEditorSource source,
                              AppState *state)
{
    int load_status;
    SdCardResult sd_result;

    display_show_message("Restoring", "Please wait");
    app_show_blocking_activity(0, 0);
    if (source == APP_EDITOR_SOURCE_SD) {
        sd_result = app_load_sd_editor_document(editor);
        if (sd_result == SDCARD_OK) {
            *eeprom_error = 0;
            app_start_info_message("Restored", APP_STATE_EDITOR, state);
        } else if (sd_result == SDCARD_OK_TRUNCATED) {
            *eeprom_error = 1;
            app_start_error_message("Text truncated", APP_STATE_EDITOR, state);
        } else if (sd_result == SDCARD_FILE_NOT_FOUND) {
            *eeprom_error = 0;
            app_start_info_message("No SD file", APP_STATE_EDITOR, state);
        } else {
            *eeprom_error = 1;
            app_start_error_message("Restore failed", APP_STATE_EDITOR, state);
        }
        return;
    }

    eeprom_init();
    load_status = eeprom_load_document_with_activity(editor,
                                                     app_show_blocking_activity,
                                                     0);
    if (load_status == EEPROM_LOAD_OK) {
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
                                          AppEditorSource source,
                                          AppState *state,
                                          int *editor_loaded)
{
    switch (selection) {
    case EDITOR_MENU_SAVE_PRIMARY:
        app_handle_primary_save_feedback(editor, eeprom_error, source, state);
        break;
    case EDITOR_MENU_SAVE_SECONDARY:
        if (source == APP_EDITOR_SOURCE_SD) {
            app_start_save_as_eeprom_confirm(state);
        } else {
            app_handle_save_as_sd_feedback(editor, eeprom_error, state);
        }
        break;
    case EDITOR_MENU_QUIT:
        if (editor->dirty) {
            app_start_quit_no_save_confirm(state);
        } else {
            app_quit_editor(editor, editor_loaded, eeprom_error, state);
        }
        break;
    case EDITOR_MENU_RESTORE_WHOLE:
        app_restore_whole(editor, eeprom_error, source, state);
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
    case APP_CONFIRM_OVERWRITE_SD:
        app_confirm_action = APP_CONFIRM_NONE;
        app_handle_overwrite_sd_confirm(editor, eeprom_error, state);
        return;
    case APP_CONFIRM_SAVE_AS_EEPROM:
        app_confirm_action = APP_CONFIRM_NONE;
        app_handle_save_as_eeprom_confirm(editor, eeprom_error, state);
        return;
    default:
        break;
    }

    app_confirm_action = APP_CONFIRM_NONE;
    app_set_state(state, app_modal_return_state);
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
                                   AppEditorSource source,
                                   AppState *state)
{
    if (app_vi_command_len == 0u) {
        *state = APP_STATE_EDITOR;
    } else if (app_vi_command_is("w")) {
        app_reset_vi_command();
        app_handle_primary_save_feedback(editor, eeprom_error, source, state);
    } else if (app_vi_command_is("q")) {
        app_reset_vi_command();
        if (editor->dirty) {
            app_start_quit_no_save_confirm(state);
        } else {
            app_quit_editor(editor, editor_loaded, eeprom_error, state);
        }
    } else if (app_vi_command_is("wq") || app_vi_command_is("x")) {
        app_reset_vi_command();
        app_save_and_quit(editor, eeprom_error, editor_loaded, source, state);
    } else if (app_vi_command_is("e!")) {
        app_reset_vi_command();
        app_restore_whole(editor, eeprom_error, source, state);
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
                                       AppEditorSource source,
                                       AppState *state)
{
    switch (code) {
    case 0x08u:
    case 0x7Fu:
        app_vi_command_backspace();
        break;
    case 0x0Au:
    case 0x0Du:
        app_execute_vi_command(editor,
                               eeprom_error,
                               editor_loaded,
                               source,
                               state);
        break;
    default:
        app_vi_command_append(code);
        break;
    }
}

static void app_handle_vi_keyboard(EditorDocument *editor,
                                   int *eeprom_error,
                                   int *editor_loaded,
                                   AppEditorSource source,
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
                                       source,
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

static int app_sd_read_ok_for_text(SdCardResult result)
{
    return result == SDCARD_OK || result == SDCARD_OK_TRUNCATED;
}

static int app_load_typing_game(unsigned int seed, AppState *state)
{
    TypingGameLoadResult load_result;

    display_show_message("Typing loading", "QUESTION.TXT");
    app_show_blocking_activity(0, 0);
    app_sd_status = sdcard_read_question_text_with_activity(
        app_sd_text,
        SDCARD_TEXT_BUFFER_SIZE,
        &app_sd_text_length,
        app_show_blocking_activity,
        0);
    if (!app_sd_read_ok_for_text(app_sd_status)) {
        printf("Typing SD read failed: %s\n",
               sdcard_result_text(app_sd_status));
        app_start_error_message("SD read failed",
                                APP_STATE_TYPING_READY,
                                state);
        return 0;
    }

    load_result = typing_game_load_questions(&app_typing_game,
                                             app_sd_text,
                                             app_sd_text_length,
                                             app_typing_selected_rounds,
                                             app_typing_case_mode,
                                             seed);
    if (load_result != TYPING_GAME_LOAD_OK) {
        printf("Typing game needs at least %u questions\n",
               (unsigned int)app_typing_selected_rounds);
        snprintf(app_typing_error_message,
                 sizeof(app_typing_error_message),
                 "Need %u lines",
                 (unsigned int)app_typing_selected_rounds);
        app_start_error_message(app_typing_error_message,
                                APP_STATE_TYPING_READY,
                                state);
        return 0;
    }

    printf("Typing game loaded %u questions from SD\n",
           (unsigned int)app_typing_selected_rounds);
    return 1;
}

static void app_display_typing_game(unsigned char ascii,
                                    int nav_mode,
                                    int error_signal)
{
    display_show_typing_game(
        typing_game_current_question(&app_typing_game),
        typing_game_current_question_len(&app_typing_game),
        &app_typing_game.input,
        typing_game_current_round_number(&app_typing_game),
        typing_game_total_rounds(&app_typing_game),
        typing_game_elapsed_ms(&app_typing_game),
        ascii,
        nav_mode,
        error_signal);
}

static int app_handle_typing_game_input(const KeyState *keys,
                                        unsigned char ascii,
                                        int nav_mode,
                                        unsigned int now_ticks,
                                        AppState *state)
{
    TypingGameAnswerResult answer_result;
    int input_changed;

    input_changed = editor_input_handle_keys(&app_typing_game.input,
                                             keys,
                                             ascii,
                                             nav_mode,
                                             0);
    input_changed |= editor_input_drain_keyboard(&app_typing_game.input, 0);

    answer_result = typing_game_accept_if_answer_correct(&app_typing_game);
    if (answer_result == TYPING_GAME_ANSWER_COMPLETE) {
        app_reset_typing_error_signal();
        *state = APP_STATE_TYPING_DONE;
    } else if (answer_result == TYPING_GAME_ANSWER_ADVANCED) {
        app_reset_typing_error_signal();
    } else {
        app_update_typing_mismatch_signal(now_ticks);
    }

    return input_changed;
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
    MenuState typing_mode_menu;
    MenuState typing_count_menu;
    MenuState typing_menu;
    AppState state;
    unsigned int switches;
    unsigned char ascii;
    int menu_selection;
    int nav_mode;
    int eeprom_error;
    int editor_loaded;
    AppEditorSource app_editor_source;
    SdCardResult load_result;

    printf("Nios II text editor starting\n");

    editor_init(&editor);
    display_init();
    key_init(&keys);
    keyboard_init();
    menu_init(&start_menu, app_start_menu_options);
    app_editor_source = APP_EDITOR_SOURCE_EEPROM;
    menu_init(&editor_menu, app_editor_menu_options_for_source(app_editor_source));
    menu_init(&typing_mode_menu, app_typing_mode_options);
    menu_init(&typing_count_menu, app_typing_count_options);
    menu_init(&typing_menu, app_typing_menu_options);
    typing_game_init(&app_typing_game);
    app_reset_typing_error_signal();
    eeprom_error = 0;
    editor_loaded = 0;
    app_enter_home_menu(&state);

    while (1) {
        ++app_entropy;
        switches = app_read_switches();
        ascii = (unsigned char)(switches & 0x7Fu);
        nav_mode = (switches & SW_NAV_MASK) != 0;
        editor_set_insert_mode(&editor, (switches & SW_INSERT_MASK) != 0);
        editor_set_insert_mode(&app_typing_game.input,
                               (switches & SW_INSERT_MASK) != 0);

        key_update(&keys);
        if (state == APP_STATE_MENU) {
            menu_selection = menu_update(&start_menu, &keys);
            if (menu_selection == APP_MENU_EDITOR) {
                if (!editor_loaded ||
                    app_editor_source != APP_EDITOR_SOURCE_EEPROM) {
                    app_editor_source = APP_EDITOR_SOURCE_EEPROM;
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
            } else if (menu_selection == APP_MENU_SD_EDITOR) {
                app_editor_source = APP_EDITOR_SOURCE_SD;
                load_result = app_load_sd_editor_document(&editor);
                editor_loaded = 1;
                if (load_result == SDCARD_OK) {
                    eeprom_error = 0;
                    state = APP_STATE_EDITOR;
                } else if (load_result == SDCARD_OK_TRUNCATED) {
                    eeprom_error = 1;
                    app_start_error_message("Text truncated",
                                            APP_STATE_EDITOR,
                                            &state);
                } else if (load_result == SDCARD_FILE_NOT_FOUND) {
                    eeprom_error = 0;
                    app_start_info_message("No SD file",
                                           APP_STATE_EDITOR,
                                           &state);
                } else {
                    eeprom_error = 1;
                    app_start_error_message("SD load fail",
                                            APP_STATE_EDITOR,
                                            &state);
                }
            } else if (menu_selection == APP_MENU_TYPING_GAME) {
                typing_game_init(&app_typing_game);
                app_reset_typing_error_signal();
                app_typing_selected_rounds = TYPING_GAME_DEFAULT_ROUNDS;
                app_typing_case_mode = TYPING_GAME_CASE_DEFAULT;
                menu_init(&typing_mode_menu, app_typing_mode_options);
                state = APP_STATE_TYPING_MODE_MENU;
            }
        } else if (state == APP_STATE_INFO_MESSAGE) {
            display_show_info_message(app_modal_message);
            if (key_pressed_edge(&keys, KEY_MASK_0)) {
                app_set_state(&state, app_modal_return_state);
            }
        } else if (state == APP_STATE_ERROR_MESSAGE) {
            display_show_error_message(app_modal_message);
            if (key_pressed_edge(&keys, KEY_MASK_0)) {
                app_set_state(&state, app_modal_return_state);
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
                app_set_state(&state, app_modal_return_state);
            }
        } else if (state == APP_STATE_SD_VIEW) {
            if (key_pressed_edge(&keys, KEY_MASK_0)) {
                app_enter_home_menu(&state);
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
        } else if (state == APP_STATE_TYPING_MODE_MENU) {
            menu_selection = menu_update(&typing_mode_menu, &keys);
            if (menu_selection != MENU_NO_SELECTION) {
                if (menu_selection == TYPING_MODE_QUIT) {
                    app_enter_home_menu(&state);
                } else {
                    app_apply_typing_mode_selection(menu_selection);
                    menu_init(&typing_count_menu, app_typing_count_options);
                    state = APP_STATE_TYPING_COUNT_MENU;
                }
            }
        } else if (state == APP_STATE_TYPING_COUNT_MENU) {
            menu_selection = menu_update(&typing_count_menu, &keys);
            if (menu_selection != MENU_NO_SELECTION) {
                if (app_typing_count_selection_is_quit(menu_selection)) {
                    app_enter_home_menu(&state);
                } else {
                    app_typing_selected_rounds =
                        app_typing_rounds_from_selection(menu_selection);
                    typing_game_init(&app_typing_game);
                    app_reset_typing_error_signal();
                    state = APP_STATE_TYPING_READY;
                }
            }
        } else if (state == APP_STATE_TYPING_READY) {
            display_show_action_message("SW6-0 OFF",
                                        TYPING_READY_ACTION_TEXT);
            if (key_pressed_edge(&keys, KEY_MASK_0)) {
                app_enter_home_menu(&state);
            } else if (key_pressed_edge(&keys, KEY_MASK_1) &&
                       (switches & SW_TYPING_READY_CLEAR_MASK) == 0u) {
                if (app_load_typing_game(app_entropy ^ switches, &state)) {
                    app_typing_last_switch_input =
                        switches & SW_TYPING_INPUT_MASK;
                    app_reset_typing_error_signal();
                    state = APP_STATE_TYPING_GAME;
                }
            }
        } else if (state == APP_STATE_TYPING_GAME) {
            if (key_pressed_edge(&keys, KEY_MASK_0)) {
                typing_game_pause_stopwatch(&app_typing_game,
                                            app_timer_ticks());
                app_reset_typing_error_signal();
                menu_init(&typing_menu, app_typing_menu_options);
                state = APP_STATE_TYPING_MENU;
            } else {
                unsigned int now_ticks;
                unsigned int switch_input;
                int input_changed;

                now_ticks = app_timer_ticks();
                switch_input = switches & SW_TYPING_INPUT_MASK;
                if (switch_input != app_typing_last_switch_input) {
                    typing_game_start_stopwatch(&app_typing_game, now_ticks);
                    app_typing_last_switch_input = switch_input;
                }

                input_changed =
                    app_handle_typing_game_input(&keys,
                                                 ascii,
                                                 nav_mode,
                                                 now_ticks,
                                                 &state);
                if (input_changed) {
                    typing_game_start_stopwatch(&app_typing_game, now_ticks);
                }
                typing_game_update_stopwatch(&app_typing_game, now_ticks);

                if (state == APP_STATE_TYPING_DONE) {
                    typing_game_pause_stopwatch(&app_typing_game, now_ticks);
                    app_prepare_typing_result_message();
                    app_start_info_message(app_typing_result_message,
                                           APP_STATE_MENU,
                                           &state);
                } else {
                    app_display_typing_game(
                        ascii,
                        nav_mode,
                        app_typing_error_signal_active(now_ticks));
                }
            }
        } else if (state == APP_STATE_TYPING_MENU) {
            menu_selection = menu_update(&typing_menu, &keys);
            if (menu_selection == TYPING_MENU_RESTART) {
                typing_game_restart(&app_typing_game);
                app_typing_last_switch_input = switches & SW_TYPING_INPUT_MASK;
                app_reset_typing_error_signal();
                state = APP_STATE_TYPING_GAME;
            } else if (menu_selection == TYPING_MENU_CONTINUE) {
                typing_game_resume_stopwatch(&app_typing_game,
                                             app_timer_ticks());
                app_typing_last_switch_input = switches & SW_TYPING_INPUT_MASK;
                app_reset_typing_error_signal();
                state = APP_STATE_TYPING_GAME;
            } else if (menu_selection == TYPING_MENU_QUIT) {
                app_reset_typing_error_signal();
                app_enter_home_menu(&state);
            }
        } else if (state == APP_STATE_EDITOR_COMMAND) {
            if (key_pressed_edge(&keys, KEY_MASK_2)) {
                menu_init(&editor_menu,
                          app_editor_menu_options_for_source(app_editor_source));
                state = APP_STATE_EDITOR_MENU;
            } else {
                if (key_pressed_edge(&keys, KEY_MASK_1)) {
                    app_handle_vi_command_code(&editor,
                                               ascii,
                                               &eeprom_error,
                                               &editor_loaded,
                                               app_editor_source,
                                               &state);
                }
                if (state == APP_STATE_EDITOR_COMMAND &&
                    key_pressed_edge(&keys, KEY_MASK_0)) {
                    app_execute_vi_command(&editor,
                                           &eeprom_error,
                                           &editor_loaded,
                                           app_editor_source,
                                           &state);
                }
                if (state == APP_STATE_EDITOR_COMMAND) {
                    app_handle_vi_keyboard(&editor,
                                           &eeprom_error,
                                           &editor_loaded,
                                           app_editor_source,
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
                                              app_editor_source,
                                              &state,
                                              &editor_loaded);
            }
        } else {
            if (key_pressed_edge(&keys, KEY_MASK_0)) {
                app_start_vi_command(&state);
            } else {
                editor_input_handle_keys(&editor, &keys, ascii, nav_mode, 1);
                editor_input_drain_keyboard(&editor, 1);
                display_update_with_markers(&editor,
                                            ascii,
                                            nav_mode,
                                            eeprom_error,
                                            app_editor_top_marker(app_editor_source),
                                            EDITOR_BOTTOM_MARKER);
            }
        }

        alt_busy_sleep(MAIN_LOOP_DELAY_US);
    }

    return 0;
}
