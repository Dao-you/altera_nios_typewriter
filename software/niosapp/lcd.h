#ifndef LCD_H
#define LCD_H

/**
 * Initialize the 16x2 LCD in 8-bit write-only mode.
 */
void lcd_init(void);

/**
 * Write exactly 16 characters to one LCD row, padding short text with spaces.
 */
void lcd_write_line(int row, const char *text, int length);

/**
 * Set the LCD cursor to row 0 or 1 and column 0..15.
 */
void lcd_set_cursor(int row, int col);

/**
 * Hide the LCD cursor and disable cursor blinking.
 */
void lcd_hide_cursor(void);

/**
 * Select the LCD cursor style used after display refresh.
 * insert_mode=1 shows the non-blinking underline cursor; insert_mode=0 shows
 * the blinking block cursor for overwrite mode.
 */
void lcd_set_cursor_mode(int insert_mode);

#endif
