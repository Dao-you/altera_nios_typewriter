#ifndef KEYBOARD_H
#define KEYBOARD_H

#define KEYBOARD_STATUS_HAS_DATA 0x01u
#define KEYBOARD_STATUS_FULL     0x02u
#define KEYBOARD_STATUS_ERROR    0x04u

#define KEYBOARD_CODE_LEFT  0x80u
#define KEYBOARD_CODE_RIGHT 0x81u
#define KEYBOARD_CODE_UP    0x82u
#define KEYBOARD_CODE_DOWN  0x83u
#define KEYBOARD_CODE_ESCAPE 0x84u

/**
 * Reset the software-to-hardware acknowledge line.
 */
void keyboard_init(void);

/**
 * Read one decoded keyboard byte when available.
 *
 * Printable keys and editor controls are ASCII-compatible. Arrow keys use the
 * KEYBOARD_CODE_* values above because the editor already treats movement as
 * cursor actions rather than document bytes.
 */
int keyboard_read(unsigned char *code);

#endif
