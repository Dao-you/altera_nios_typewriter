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
 * Show the startup mode selection menu.
 */
void display_show_menu(void);

/**
 * Show a fixed two-line status message.
 */
void display_show_message(const char *line0, const char *line1);

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
