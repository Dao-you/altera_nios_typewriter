#include "editor_input.h"
#include "keyboard.h"

static int editor_input_write_ascii(EditorDocument *editor,
                                    unsigned char ascii,
                                    int allow_newline)
{
    if (ascii == 0x0Du) {
        ascii = 0x0Au;
    }
    if (!allow_newline && ascii == 0x0Au) {
        return 0;
    }

    return editor_write_ascii(editor, ascii);
}

/**
 * Apply one PS/2 decoded byte or ASCII/control byte to an editor document.
 */
int editor_input_apply_code(EditorDocument *editor,
                            unsigned char code,
                            int allow_newline)
{
    switch (code) {
    case KEYBOARD_CODE_LEFT:
        return editor_move_left(editor);
    case KEYBOARD_CODE_RIGHT:
        return editor_move_right(editor);
    case KEYBOARD_CODE_UP:
        return editor_move_up(editor);
    case KEYBOARD_CODE_DOWN:
        return editor_move_down(editor);
    default:
        if (code < 0x80u) {
            return editor_input_write_ascii(editor, code, allow_newline);
        }
        break;
    }

    return 0;
}

/**
 * Dispatch one debounced SW/KEY event set to editor actions.
 */
int editor_input_handle_keys(EditorDocument *editor,
                             const KeyState *keys,
                             unsigned char ascii,
                             int nav_mode,
                             int allow_newline)
{
    int changed;

    changed = 0;
    if (key_pressed_edge(keys, KEY_MASK_1)) {
        changed |= editor_input_write_ascii(editor, ascii, allow_newline);
    }
    if (key_pressed_edge(keys, KEY_MASK_3)) {
        if (nav_mode) {
            changed |= editor_move_up(editor);
        } else {
            changed |= editor_move_left(editor);
        }
    }
    if (key_pressed_edge(keys, KEY_MASK_2)) {
        if (nav_mode) {
            changed |= editor_move_down(editor);
        } else {
            changed |= editor_move_right(editor);
        }
    }

    return changed;
}

/**
 * Drain the small PS/2 hardware FIFO into editor actions.
 */
int editor_input_drain_keyboard(EditorDocument *editor, int allow_newline)
{
    unsigned char code;
    unsigned int count;
    int events;

    events = 0;
    count = 0;
    while (count < EDITOR_INPUT_DRAIN_LIMIT && keyboard_read(&code)) {
        if (code == KEYBOARD_CODE_ESCAPE) {
            events |= EDITOR_INPUT_EVENT_ESCAPE;
            break;
        }
        if (editor_input_apply_code(editor, code, allow_newline)) {
            events |= EDITOR_INPUT_EVENT_CHANGED;
        }
        ++count;
    }

    return events;
}
