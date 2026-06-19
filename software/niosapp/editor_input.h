#ifndef EDITOR_INPUT_H
#define EDITOR_INPUT_H

#include "editor.h"
#include "key.h"

#define EDITOR_INPUT_DRAIN_LIMIT 16u

/**
 * Apply one PS/2 decoded byte or ASCII/control byte to an editor document.
 *
 * Set allow_newline to 0 for single-line editor users such as the typing game.
 * Printable ASCII, BS, DEL, and cursor movement still use the same behavior as
 * the normal EEPROM editor.
 */
int editor_input_apply_code(EditorDocument *editor,
                            unsigned char code,
                            int allow_newline);

/**
 * Dispatch one debounced SW/KEY event set to editor actions.
 */
void editor_input_handle_keys(EditorDocument *editor,
                              const KeyState *keys,
                              unsigned char ascii,
                              int nav_mode,
                              int allow_newline);

/**
 * Drain the small PS/2 hardware FIFO into editor actions.
 */
void editor_input_drain_keyboard(EditorDocument *editor, int allow_newline);

#endif
