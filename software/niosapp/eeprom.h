#ifndef EEPROM_H
#define EEPROM_H

#include "editor.h"

#define EEPROM_LOAD_EMPTY 0
#define EEPROM_LOAD_OK 1
#define EEPROM_LOAD_ERROR -1

/**
 * Activity callback used while EEPROM load/save blocks the main loop.
 *
 * The tick value is only an animation counter; it is not a storage progress
 * percentage.
 */
typedef void (*EepromActivityCallback)(unsigned int tick, void *context);

/**
 * Initialize the EEPROM I2C PIO lines to idle.
 */
void eeprom_init(void);

/**
 * Read and validate a saved editor document using caller-provided scratch.
 */
int eeprom_load_document_with_activity_buffer(EditorDocument *editor,
                                              unsigned char *buffer,
                                              unsigned int buffer_size,
                                              EepromActivityCallback activity,
                                              void *activity_context);

/**
 * Save the complete editor document using caller-provided scratch.
 */
int eeprom_save_document_with_activity_buffer(const EditorDocument *editor,
                                              unsigned char *buffer,
                                              unsigned int buffer_size,
                                              EepromActivityCallback activity,
                                              void *activity_context);

#endif
