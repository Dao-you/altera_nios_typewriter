#include "display.h"
#include "lcd.h"
#include "seven_seg.h"
#include "system.h"
#include "altera_avalon_pio_regs.h"

#define LEDG_INSERT 0x01u
#define LEDG_NAV_MODE 0x02u
#define LEDG_EEPROM_ERROR 0x20u
#define LEDG_OVERFLOW 0x40u
#define LEDG_DIRTY 0x80u
#define SAVE_MARQUEE_LED_COUNT 17u

/**
 * Write one encoded digit to a HEX PIO.
 */
static void display_write_hex(unsigned int base, unsigned char value)
{
    IOWR_ALTERA_AVALON_PIO_DATA(base, value);
}

/**
 * Write an 8-bit value as two hexadecimal digits on a high/low HEX pair.
 */
static void display_write_hex_byte(unsigned int high_base,
                                   unsigned int low_base,
                                   unsigned char value)
{
    display_write_hex(high_base, seven_seg_encode_hex((unsigned char)(value >> 4)));
    display_write_hex(low_base, seven_seg_encode_hex(value));
}

/**
 * Write a value as two decimal digits on a high/low HEX pair.
 *
 * Values above 99 are clamped because each field owns only two displays.
 */
static void display_write_decimal_pair(unsigned int high_base,
                                       unsigned int low_base,
                                       unsigned char value)
{
    if (value > 99u) {
        value = 99u;
    }

    display_write_hex(high_base, seven_seg_encode_hex((unsigned char)(value / 10u)));
    display_write_hex(low_base, seven_seg_encode_hex((unsigned char)(value % 10u)));
}

/**
 * Build a current-line progress mask from LEDR17 down to LEDR0.
 * LEDR0 lights only when the cursor is on the final document line.
 */
static unsigned int display_line_progress(unsigned char current_line,
                                          unsigned char total_lines)
{
    unsigned int lit;
    unsigned int i;
    unsigned int mask;

    if (total_lines <= 1u || current_line + 1u >= total_lines) {
        lit = 18u;
    } else {
        lit = 1u + ((unsigned int)current_line * 16u) /
            ((unsigned int)total_lines - 1u);
    }
    if (lit > 18u) {
        lit = 18u;
    } else if (lit < 1u) {
        lit = 1u;
    }

    mask = 0;
    for (i = 0; i < lit; ++i) {
        mask |= 1u << (17u - i);
    }

    return mask;
}

/**
 * Put a two-digit decimal value into a character buffer.
 */
static void display_write_two_digits(char *dst, unsigned char value)
{
    if (value > 99u) {
        value = 99u;
    }
    dst[0] = (char)('0' + (value / 10u));
    dst[1] = (char)('0' + (value % 10u));
}

/**
 * Copy a short status word into the 16-character LCD status buffer.
 */
static void display_copy_status_word(char *dst, const char *word)
{
    int i;

    for (i = 0; word[i] != '\0' && (8 + i) < 16; ++i) {
        dst[8 + i] = word[i];
    }
}

/**
 * Write the second LCD line with line, cursor, and save state.
 */
static void display_write_status_line(const EditorDocument *editor, int eeprom_error)
{
    char status[16];
    int i;

    for (i = 0; i < 16; ++i) {
        status[i] = ' ';
    }

    status[0] = 'L';
    display_write_two_digits(&status[1], (unsigned char)(editor->current_line + 1u));
    status[3] = ' ';
    status[4] = 'C';
    display_write_two_digits(&status[5], editor->cursor_col);
    status[7] = ' ';

    if (eeprom_error) {
        display_copy_status_word(status, "EEPERR");
    } else if (editor->dirty) {
        display_copy_status_word(status, "DIRTY");
    } else {
        display_copy_status_word(status, "SAVED");
    }

    lcd_write_line(1, status, 16);
}

/**
 * Initialize LCD and clear all display PIO outputs.
 */
void display_init(void)
{
    display_write_hex(PIO_OUT_HEX0_BASE, seven_seg_blank());
    display_write_hex(PIO_OUT_HEX1_BASE, seven_seg_blank());
    display_write_hex(PIO_OUT_HEX2_BASE, seven_seg_blank());
    display_write_hex(PIO_OUT_HEX3_BASE, seven_seg_blank());
    display_write_hex(PIO_OUT_HEX4_BASE, seven_seg_blank());
    display_write_hex(PIO_OUT_HEX5_BASE, seven_seg_blank());
    display_write_hex(PIO_OUT_HEX6_BASE, seven_seg_blank());
    display_write_hex(PIO_OUT_HEX7_BASE, seven_seg_blank());
    IOWR_ALTERA_AVALON_PIO_DATA(PIO_OUT_LEDR_BASE, 0);
    IOWR_ALTERA_AVALON_PIO_DATA(PIO_OUT_LEDG_BASE, 0);
    lcd_init();
}

/**
 * Update LEDR, LEDG, HEX0-HEX7, and LCD from the current editor state.
 */
void display_update(const EditorDocument *editor,
                    unsigned char ascii,
                    int nav_mode,
                    int eeprom_error)
{
    unsigned int ledg;

    ledg = 0;
    if (editor->insert_mode) {
        ledg |= LEDG_INSERT;
    }
    if (nav_mode) {
        ledg |= LEDG_NAV_MODE;
    }
    if (eeprom_error) {
        ledg |= LEDG_EEPROM_ERROR;
    }
    if (editor->overflow) {
        ledg |= LEDG_OVERFLOW;
    }
    if (editor->dirty) {
        ledg |= LEDG_DIRTY;
    }

    IOWR_ALTERA_AVALON_PIO_DATA(PIO_OUT_LEDR_BASE,
                                display_line_progress(editor->current_line,
                                                      editor->total_lines));
    IOWR_ALTERA_AVALON_PIO_DATA(PIO_OUT_LEDG_BASE, ledg);

    display_write_decimal_pair(PIO_OUT_HEX7_BASE, PIO_OUT_HEX6_BASE,
                               (unsigned char)(editor->current_line + 1u));
    display_write_decimal_pair(PIO_OUT_HEX5_BASE, PIO_OUT_HEX4_BASE,
                               editor->cursor_col);
    display_write_decimal_pair(PIO_OUT_HEX3_BASE, PIO_OUT_HEX2_BASE,
                               editor->total_lines);
    display_write_hex_byte(PIO_OUT_HEX1_BASE, PIO_OUT_HEX0_BASE,
                           (unsigned char)(ascii & 0x7Fu));

    lcd_write_line(0,
                   editor->document[editor->current_line],
                   editor->line_len[editor->current_line]);
    display_write_status_line(editor, eeprom_error);
    lcd_set_cursor(0, editor->cursor_col);
    lcd_set_cursor_mode(editor->insert_mode);
}

/**
 * Show a save-activity marquee on LEDR17..LEDR1.
 */
void display_save_marquee(unsigned int tick)
{
    unsigned int led_index;

    led_index = 17u - (tick % SAVE_MARQUEE_LED_COUNT);
    IOWR_ALTERA_AVALON_PIO_DATA(PIO_OUT_LEDR_BASE, 1u << led_index);
}
