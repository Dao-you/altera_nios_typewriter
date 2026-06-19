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
 * Update the EEPROM editor main view.
 *
 * The first LCD row shows a blinking EEPROM title marker. The second row shows
 * the current editor line and owns the LCD cursor.
 */
void display_update_eeprom_editor(const EditorDocument *editor,
                                  unsigned char ascii,
                                  int nav_mode,
                                  int eeprom_error);

/**
 * Show one option from a horizontal menu.
 */
void display_show_menu_item(const char *option_name,
                            unsigned char selected_index,
                            unsigned char option_count);

/**
 * Show a fixed two-line status message.
 */
void display_show_message(const char *line0, const char *line1);

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
 * Show two newline-delimited text rows starting at first_line.
 */
void display_show_text_page(const char *text,
                            unsigned int length,
                            unsigned int first_line);

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
