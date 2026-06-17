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
#define LCD_WIDTH 16
#define LCD_CURSOR_LEFT_CONTEXT 2
#define LCD_CURSOR_RIGHT_CONTEXT 2
#define LCD_CURSOR_MAX_COL (LCD_WIDTH - 1 - LCD_CURSOR_RIGHT_CONTEXT)
#define LCD_END_MARKER "------END-------"
/* Approximate the LCD1602 built-in cursor blink cadence in main-loop ticks. */
#define LCD_CURSOR_BLINK_MATCH_TICKS 34u
#define SAVE_MARQUEE_LED_COUNT 17u

static int display_lcd_view_start = 0;
static unsigned int display_end_blink_tick = 0;

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
    display_lcd_view_start = 0;
    display_end_blink_tick = 0;
    lcd_init();
}

/**
 * Clamp the first visible document column for the current line.
 */
static int display_max_view_start(const EditorDocument *editor)
{
    int line_len;

    line_len = editor->line_len[editor->current_line];
    if (line_len < LCD_WIDTH) {
        return 0;
    }

    return line_len - (LCD_WIDTH - 1);
}

static void display_clamp_view_start(const EditorDocument *editor)
{
    int max_start;

    max_start = display_max_view_start(editor);
    if (display_lcd_view_start < 0) {
        display_lcd_view_start = 0;
    }
    if (display_lcd_view_start > max_start) {
        display_lcd_view_start = max_start;
    }
}

/**
 * Move the 16-character LCD viewport only when the cursor crosses the scroll
 * guard columns. At document ends, the cursor can reach the LCD edge.
 */
static int display_view_start(const EditorDocument *editor)
{
    int cursor;

    cursor = editor->cursor_col;
    display_clamp_view_start(editor);

    if (cursor < display_lcd_view_start + LCD_CURSOR_LEFT_CONTEXT) {
        display_lcd_view_start = cursor - LCD_CURSOR_LEFT_CONTEXT;
    } else if (cursor > display_lcd_view_start + LCD_CURSOR_MAX_COL) {
        display_lcd_view_start = cursor - LCD_CURSOR_MAX_COL;
    }

    display_clamp_view_start(editor);
    return display_lcd_view_start;
}

/**
 * Build one LCD row from a document line and a horizontal viewport.
 */
static void display_build_line_view(const EditorDocument *editor,
                                    unsigned char line,
                                    int view_start,
                                    char *view)
{
    int i;
    int col;

    for (i = 0; i < LCD_WIDTH; ++i) {
        col = view_start + i;
        if (col >= 0 && col < editor->line_len[line]) {
            view[i] = editor->document[line][col];
        } else {
            view[i] = ' ';
        }
    }
}

/**
 * Return nonzero when the LCD END marker should be visible on this refresh.
 *
 * The marker is a UI hint rather than document text, so it blinks to avoid
 * being mistaken for actual saved content.
 */
static int display_should_show_end_marker(void)
{
    int visible;

    visible =
        ((display_end_blink_tick / LCD_CURSOR_BLINK_MATCH_TICKS) & 0x01u) == 0u;
    ++display_end_blink_tick;
    return visible;
}

/**
 * Refresh both LCD rows from the current editor viewport.
 */
static void display_write_editor_lines(const EditorDocument *editor)
{
    char view[LCD_WIDTH];
    int view_start;
    int cursor_col;

    view_start = display_view_start(editor);

    display_build_line_view(editor, editor->current_line, view_start, view);
    lcd_write_line(0, view, LCD_WIDTH);

    if (editor->current_line + 1u < editor->total_lines) {
        display_build_line_view(editor,
                                (unsigned char)(editor->current_line + 1u),
                                view_start,
                                view);
        lcd_write_line(1, view, LCD_WIDTH);
        display_end_blink_tick = 0;
    } else {
        if (display_should_show_end_marker()) {
            lcd_write_line(1, LCD_END_MARKER, LCD_WIDTH);
        } else {
            lcd_write_line(1, "", 0);
        }
    }

    cursor_col = (int)editor->cursor_col - view_start;
    lcd_set_cursor(0, cursor_col);
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

    display_write_editor_lines(editor);
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
