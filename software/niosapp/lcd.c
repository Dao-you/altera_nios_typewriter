#include "lcd.h"
#include "system.h"
#include "altera_avalon_pio_regs.h"
#include "priv/alt_busy_sleep.h"

#define LCD_RS_BIT 0x01u
#define LCD_RW_BIT 0x02u
#define LCD_EN_BIT 0x04u
#define LCD_ON_BIT 0x08u
#define LCD_BLON_BIT 0x10u
#define LCD_IDLE_CTRL (LCD_ON_BIT | LCD_BLON_BIT)
#define LCD_SHORT_DELAY_US 50
#define LCD_LONG_DELAY_US 2000
/* Called once per UI refresh; approximates the LCD cursor blink cadence. */
#define LCD_SOFT_CURSOR_BLINK_TICKS 34u

static unsigned int lcd_soft_cursor_blink_tick = 0;

/**
 * Write the LCD control PIO.
 */
static void lcd_write_ctrl(unsigned int value)
{
    IOWR_ALTERA_AVALON_PIO_DATA(PIO_OUT_LCD_CTRL_BASE, value);
}

/**
 * Pulse LCD_EN after data and RS have been placed on the PIO pins.
 */
static void lcd_pulse_enable(unsigned int ctrl)
{
    lcd_write_ctrl(ctrl | LCD_EN_BIT);
    alt_busy_sleep(2);
    lcd_write_ctrl(ctrl & ~LCD_EN_BIT);
    alt_busy_sleep(LCD_SHORT_DELAY_US);
}

/**
 * Write one LCD command or data byte with fixed conservative timing.
 */
static void lcd_write_raw(unsigned char value, int rs)
{
    unsigned int ctrl;

    ctrl = LCD_IDLE_CTRL;
    if (rs) {
        ctrl |= LCD_RS_BIT;
    }

    IOWR_ALTERA_AVALON_PIO_DATA(PIO_OUT_LCD_DATA_BASE, value);
    lcd_write_ctrl(ctrl);
    alt_busy_sleep(2);
    lcd_pulse_enable(ctrl);
}

/**
 * Write one LCD command byte and wait long enough for clear/home commands.
 */
static void lcd_write_command(unsigned char command)
{
    lcd_write_raw(command, 0);
    if (command == 0x01u || command == 0x02u) {
        alt_busy_sleep(LCD_LONG_DELAY_US);
    }
}

/**
 * Write one LCD data byte.
 */
static void lcd_write_data(unsigned char data)
{
    lcd_write_raw(data, 1);
}

/**
 * Set the LCD cursor to row 0 or 1 and column 0..15.
 */
void lcd_set_cursor(int row, int col)
{
    unsigned char address;

    if (col < 0) {
        col = 0;
    }
    if (col > 15) {
        col = 15;
    }

    address = (unsigned char)((row ? 0x40 : 0x00) + col);
    lcd_write_command((unsigned char)(0x80u | address));
}

/**
 * Initialize the 16x2 LCD in 8-bit write-only mode.
 */
void lcd_init(void)
{
    lcd_write_ctrl(LCD_IDLE_CTRL);
    IOWR_ALTERA_AVALON_PIO_DATA(PIO_OUT_LCD_DATA_BASE, 0);
    alt_busy_sleep(50000);

    lcd_write_command(0x38);
    lcd_write_command(0x38);
    lcd_write_command(0x38);
    lcd_write_command(0x0C);
    lcd_write_command(0x01);
    lcd_write_command(0x06);
    lcd_write_command(0x80);
}

/**
 * Write exactly 16 characters to one LCD row, padding short text with spaces.
 */
void lcd_write_line(int row, const char *text, int length)
{
    int i;
    unsigned char ch;

    lcd_set_cursor(row ? 1 : 0, 0);
    for (i = 0; i < 16; ++i) {
        if (i < length) {
            ch = (unsigned char)text[i];
        } else {
            ch = ' ';
        }
        if (ch < 0x20u || ch > 0x7Eu) {
            ch = ' ';
        }
        lcd_write_data(ch);
    }
}

/**
 * Hide the LCD cursor and disable cursor blinking.
 */
void lcd_hide_cursor(void)
{
    lcd_soft_cursor_blink_tick = 0;
    lcd_write_command(0x0C);
}

/**
 * Select the LCD cursor style used after display refresh.
 * insert_mode=1 soft-blinks the underline cursor; insert_mode=0 shows the
 * LCD built-in blinking block cursor for overwrite mode.
 */
void lcd_set_cursor_mode(int insert_mode)
{
    int underline_visible;

    if (insert_mode) {
        underline_visible =
            ((lcd_soft_cursor_blink_tick / LCD_SOFT_CURSOR_BLINK_TICKS) &
             0x01u) == 0u;
        ++lcd_soft_cursor_blink_tick;
        lcd_write_command(underline_visible ? 0x0E : 0x0C);
    } else {
        lcd_soft_cursor_blink_tick = 0;
        lcd_write_command(0x0F);
    }
}
