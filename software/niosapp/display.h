#ifndef DISPLAY_H
#define DISPLAY_H

#include "editor.h"

typedef enum {
    DISPLAY_LEDG_INSERT = 0,
    DISPLAY_LEDG_NAV_MODE = 1,
    DISPLAY_LEDG_EEPROM_ERROR = 5,
    DISPLAY_LEDG_OVERFLOW = 6,
    DISPLAY_LEDG_DIRTY = 7
} DisplayLedgIndicator;

/*
 * Bit contract for the Qsys pio_out_ledr_flag PIO.
 *
 * Bit 0 selects the LEDR source: 1 = Nios PIO_OUT_LEDR, 0 = Verilog effect.
 * Bits 1..4 request Verilog effects. If multiple effect bits are set,
 * hardware priority is error blink, confirm blink, right-to-left marquee,
 * then left-to-right marquee. Reserved bits must be written as 0.
 */
typedef enum {
    DISPLAY_LEDR_FLAG_NIOS_CONTROL       = 0x01u,
    DISPLAY_LEDR_FLAG_MARQUEE_LEFT_RIGHT = 0x02u,
    DISPLAY_LEDR_FLAG_MARQUEE_RIGHT_LEFT = 0x04u,
    DISPLAY_LEDR_FLAG_CONFIRM_BLINK      = 0x08u,
    DISPLAY_LEDR_FLAG_ERROR_BLINK        = 0x10u
} DisplayLedrFlag;

/**
 * Initialize LCD and clear all display PIO outputs.
 */
void display_init(void);

/**
 * Update LEDR, LEDG, HEX0-HEX7, and LCD from the current editor state.
 */
void display_update(const EditorDocument *editor,
                    unsigned char ascii,
                    int nav_mode,
                    int eeprom_error);

/**
 * Update the editor view with optional boundary markers.
 *
 * top_marker is shown before document line 0 when the cursor is on line 0.
 * bottom_marker is shown after the last document line when the cursor is on
 * the last line. Pass 0 to omit either marker.
 */
void display_update_with_markers(const EditorDocument *editor,
                                 unsigned char ascii,
                                 int nav_mode,
                                 int eeprom_error,
                                 const char *top_marker,
                                 const char *bottom_marker);

/**
 * Show one option from a horizontal menu.
 *
 * LEDR shows the selected option as a progress bar over the option count.
 */
void display_show_menu_item(const char *option_name,
                            unsigned char selected_index,
                            unsigned char option_count);

/**
 * Show one option from a horizontal menu that has a page before option 0.
 *
 * LEDR shows the selected option as a progress bar over the option count.
 */
void display_show_menu_item_with_left_edge(const char *option_name,
                                           unsigned char selected_index,
                                           unsigned char option_count);

/**
 * Show a fixed two-line status message.
 */
void display_show_message(const char *line0, const char *line1);

/**
 * Show the editor menu command prompt.
 *
 * The first row starts with ':' and places the LCD cursor after the command.
 * The second row shows the VI COMMAND page label with a right menu arrow.
 * LEDR uses the 2 Hz confirmation blink while this page is active.
 */
void display_show_vi_command(const char *command);

/**
 * Show an informational message that returns on KEY0.
 */
void display_show_info_message(const char *message);

/**
 * Show a yes/no confirmation message. KEY1 accepts and KEY0 cancels.
 */
void display_show_confirm_message(const char *message);

/**
 * Show an error message that returns on KEY0.
 */
void display_show_error_message(const char *message);

/**
 * Show a custom two-action message with the 2 Hz message LEDR effect.
 */
void display_show_action_message(const char *message, const char *action_text);

/**
 * Show two newline-delimited text rows starting at first_line.
 */
void display_show_text_page(const char *text,
                            unsigned int length,
                            unsigned int first_line);

/**
 * Show the typing game prompt/input view and game status outputs.
 */
void display_show_typing_game(const char *question,
                              unsigned char question_len,
                              const EditorDocument *input,
                              unsigned char current_round,
                              unsigned char total_rounds,
                              unsigned int elapsed_ms);

/**
 * Show the typing game completion screen while keeping final score outputs.
 */
void display_show_typing_done(unsigned char total_rounds,
                              unsigned int elapsed_ms);

/**
 * Set or clear one LEDG indicator through the display controller.
 */
void display_set_ledg(DisplayLedgIndicator indicator, int enabled);

/**
 * Clear every LEDG indicator through the display controller.
 */
void display_clear_ledg(void);

/**
 * Show a LEDR progress bar from LEDR17 toward LEDR0.
 *
 * Percent 0 clears the bar. The final LED, LEDR0, only lights when percent is
 * 100 or greater.
 */
void display_show_progress_percent(unsigned int percent);

/**
 * Show a blinking LCD marker centered in '-' fill characters.
 */
void display_show_blinking_marker(int row, const char *word);

/**
 * Show a blinking LCD marker on the first LCD row.
 */
void display_show_top_blinking_marker(const char *word);

/**
 * Show a blinking LCD marker on the second LCD row.
 */
void display_show_bottom_blinking_marker(const char *word);

/**
 * Show an activity marquee on LEDR17..LEDR1.
 *
 * The tick value is only used to move one lit LED. It does not represent
 * progress, and LEDR0 is kept off for the normal line-progress "final line"
 * meaning.
 */
void display_show_activity_marquee(unsigned int tick);

/**
 * Compatibility wrapper for existing EEPROM activity callbacks.
 */
void display_save_marquee(unsigned int tick);

#endif
