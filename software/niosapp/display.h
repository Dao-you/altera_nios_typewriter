#ifndef DISPLAY_H
#define DISPLAY_H

#include "editor.h"

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
 * Show a save-activity marquee on LEDR17..LEDR1.
 *
 * The tick value is only used to move one lit LED. It does not represent
 * EEPROM save progress, and LEDR0 is kept off for the normal line-progress
 * "final line" meaning.
 */
void display_save_marquee(unsigned int tick);

#endif
