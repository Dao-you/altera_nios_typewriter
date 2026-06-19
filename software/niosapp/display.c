#include "display.h"
#include "lcd.h"
#include "seven_seg.h"
#include "system.h"
#include "altera_avalon_pio_regs.h"

#define LCD_WIDTH 16
#define LCD_CURSOR_LEFT_CONTEXT 2
#define LCD_CURSOR_RIGHT_CONTEXT 2
#define LCD_CURSOR_MAX_COL (LCD_WIDTH - 1 - LCD_CURSOR_RIGHT_CONTEXT)
#define LCD_END_WORD "END"
/* Approximate the LCD1602 built-in cursor blink cadence in main-loop ticks. */
#define LCD_CURSOR_BLINK_MATCH_TICKS 34u
#define LEDR_PROGRESS_LED_COUNT 18u
#define LEDR_ACTIVITY_LED_COUNT 17u
#define DISPLAY_MENU_LEFT_COL 1
#define DISPLAY_MENU_RIGHT_COL (LCD_WIDTH - 2)
#define DISPLAY_INFO_OK_TEXT "KEY0 OK"
#define DISPLAY_CONFIRM_TEXT "KEY1YES KEY0NO"
#define DISPLAY_VI_COMMAND_TEXT "VI COMMAND"

static int display_lcd_view_start = 0;
static unsigned int display_marker_blink_tick = 0;
static unsigned int display_ledg_state = 0;

static int display_text_length(const char *text, int max_length)
{
    int length;

    length = 0;
    if (text == 0) {
        return 0;
    }
    while (length < max_length && text[length] != '\0') {
        ++length;
    }

    return length;
}

/**
 * Write one encoded digit to a HEX PIO.
 */
static void display_write_hex(unsigned int base, unsigned char value)
{
    IOWR_ALTERA_AVALON_PIO_DATA(base, value);
}

static void display_write_ledr_flag(unsigned char flag)
{
#ifdef PIO_OUT_LEDR_FLAG_BASE
    IOWR_ALTERA_AVALON_PIO_DATA(PIO_OUT_LEDR_FLAG_BASE, flag);
#else
    (void)flag;
#endif
}

static void display_select_nios_ledr(void)
{
    display_write_ledr_flag(DISPLAY_LEDR_FLAG_NIOS_CONTROL);
}

static void display_write_ledr(unsigned int value)
{
    IOWR_ALTERA_AVALON_PIO_DATA(PIO_OUT_LEDR_BASE, value);
    display_select_nios_ledr();
}

static void display_select_ledr_effect(unsigned char flag)
{
    display_write_ledr_flag(flag);
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

static unsigned int display_progress_mask_from_percent(unsigned int percent)
{
    unsigned int lit;
    unsigned int i;
    unsigned int mask;

    if (percent == 0u) {
        return 0;
    }
    if (percent >= 100u) {
        lit = LEDR_PROGRESS_LED_COUNT;
    } else {
        lit = 1u + ((percent - 1u) * (LEDR_PROGRESS_LED_COUNT - 2u)) / 98u;
    }
    if (lit > LEDR_PROGRESS_LED_COUNT) {
        lit = LEDR_PROGRESS_LED_COUNT;
    }
    if (lit < 1u) {
        lit = 1u;
    }

    mask = 0;
    for (i = 0; i < lit; ++i) {
        mask |= 1u << ((LEDR_PROGRESS_LED_COUNT - 1u) - i);
    }

    return mask;
}

/**
 * Convert the current document line to a 0..100 position percentage.
 */
static unsigned int display_line_progress_percent(unsigned char current_line,
                                                  unsigned char total_lines)
{
    if (total_lines <= 1u || current_line + 1u >= total_lines) {
        return 100u;
    }

    return 1u + (((unsigned int)current_line * 98u) /
        ((unsigned int)total_lines - 1u));
}

static unsigned int display_ledg_mask(DisplayLedgIndicator indicator)
{
    switch (indicator) {
    case DISPLAY_LEDG_INSERT:
    case DISPLAY_LEDG_NAV_MODE:
    case DISPLAY_LEDG_EEPROM_ERROR:
    case DISPLAY_LEDG_OVERFLOW:
    case DISPLAY_LEDG_DIRTY:
        return 1u << (unsigned int)indicator;
    default:
        return 0;
    }
}

static void display_write_ledg_state(void)
{
    IOWR_ALTERA_AVALON_PIO_DATA(PIO_OUT_LEDG_BASE, display_ledg_state);
}

/**
 * Set or clear one LEDG indicator through the display controller.
 */
void display_set_ledg(DisplayLedgIndicator indicator, int enabled)
{
    unsigned int mask;

    mask = display_ledg_mask(indicator);
    if (mask == 0u) {
        return;
    }

    if (enabled) {
        display_ledg_state |= mask;
    } else {
        display_ledg_state &= ~mask;
    }
    display_write_ledg_state();
}

/**
 * Clear every LEDG indicator through the display controller.
 */
void display_clear_ledg(void)
{
    display_ledg_state = 0;
    display_write_ledg_state();
}

/**
 * Show a LEDR progress bar from LEDR17 toward LEDR0.
 */
void display_show_progress_percent(unsigned int percent)
{
    display_write_ledr(display_progress_mask_from_percent(percent));
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
    display_write_ledr(0);
    display_clear_ledg();
    display_lcd_view_start = 0;
    display_marker_blink_tick = 0;
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

static int display_should_show_blinking_marker(void)
{
    int visible;

    visible =
        ((display_marker_blink_tick / LCD_CURSOR_BLINK_MATCH_TICKS) &
         0x01u) == 0u;
    ++display_marker_blink_tick;
    return visible;
}

static void display_reset_blinking_marker(void)
{
    display_marker_blink_tick = 0;
}

static unsigned int display_marker_word_length(const char *word)
{
    unsigned int length;

    length = 0;
    if (word == 0) {
        return 0;
    }
    while (length < LCD_WIDTH && word[length] != '\0') {
        ++length;
    }

    return length;
}

static void display_build_center_marker(const char *word, char *marker)
{
    unsigned int word_length;
    unsigned int word_start;
    unsigned int i;

    word_length = display_marker_word_length(word);
    word_start = (LCD_WIDTH - word_length) / 2u;

    for (i = 0; i < LCD_WIDTH; ++i) {
        marker[i] = '-';
    }
    for (i = 0; i < word_length; ++i) {
        marker[word_start + i] = word[i];
    }
}

static void display_build_center_text(const char *text, char *row)
{
    unsigned int text_length;
    unsigned int text_start;
    unsigned int i;

    text_length = display_marker_word_length(text);
    text_start = (LCD_WIDTH - text_length) / 2u;

    for (i = 0; i < LCD_WIDTH; ++i) {
        row[i] = ' ';
    }
    for (i = 0; i < text_length; ++i) {
        row[text_start + i] = text[i];
    }
}

/**
 * Show a blinking LCD marker centered in '-' fill characters.
 */
void display_show_blinking_marker(int row, const char *word)
{
    char marker[LCD_WIDTH];

    if (display_should_show_blinking_marker()) {
        display_build_center_marker(word, marker);
        lcd_write_line(row, marker, LCD_WIDTH);
    } else {
        lcd_write_line(row, "", 0);
    }
}

/**
 * Show a blinking LCD marker on the first LCD row.
 */
void display_show_top_blinking_marker(const char *word)
{
    display_show_blinking_marker(0, word);
}

/**
 * Show a blinking LCD marker on the second LCD row.
 */
void display_show_bottom_blinking_marker(const char *word)
{
    display_show_blinking_marker(1, word);
}

static int display_line_has_hidden_right(const EditorDocument *editor,
                                         int view_start)
{
    return editor->line_len[editor->current_line] > view_start + LCD_WIDTH;
}

static unsigned int display_skip_text_lines(const char *text,
                                            unsigned int length,
                                            unsigned int first_line)
{
    unsigned int pos;
    unsigned int line;

    pos = 0;
    line = 0;
    while (line < first_line && pos < length) {
        while (pos < length && text[pos] != '\n') {
            ++pos;
        }
        if (pos < length && text[pos] == '\n') {
            ++pos;
        }
        ++line;
    }

    return pos;
}

static unsigned int display_build_text_row(const char *text,
                                           unsigned int length,
                                           unsigned int pos,
                                           char *row)
{
    int col;
    unsigned char ch;

    col = 0;
    while (pos < length && text[pos] != '\n') {
        if (col < LCD_WIDTH && text[pos] != '\r') {
            ch = (unsigned char)text[pos];
            if (ch < 0x20u || ch > 0x7Eu) {
                ch = ' ';
            }
            row[col] = (char)ch;
            ++col;
        }
        ++pos;
    }
    while (col < LCD_WIDTH) {
        row[col] = ' ';
        ++col;
    }
    if (pos < length && text[pos] == '\n') {
        ++pos;
    }

    return pos;
}

static int display_has_marker(const char *word)
{
    return word != 0 && word[0] != '\0';
}

/**
 * Refresh both LCD rows from the current editor viewport.
 */
static void display_write_editor_lines(const EditorDocument *editor,
                                       int view_start,
                                       const char *top_marker,
                                       const char *bottom_marker)
{
    char view[LCD_WIDTH];
    int cursor_col;
    int cursor_row;

    cursor_row = 0;
    if (display_has_marker(top_marker) && editor->current_line == 0u) {
        display_show_top_blinking_marker(top_marker);
        display_build_line_view(editor, 0, view_start, view);
        lcd_write_line(1, view, LCD_WIDTH);
        cursor_row = 1;
    } else {
        display_build_line_view(editor, editor->current_line, view_start, view);
        lcd_write_line(0, view, LCD_WIDTH);

        if (editor->current_line + 1u < editor->total_lines) {
            display_build_line_view(editor,
                                    (unsigned char)(editor->current_line + 1u),
                                    view_start,
                                    view);
            lcd_write_line(1, view, LCD_WIDTH);
            display_reset_blinking_marker();
        } else if (display_has_marker(bottom_marker)) {
            display_show_bottom_blinking_marker(bottom_marker);
        } else {
            lcd_write_line(1, "", 0);
            display_reset_blinking_marker();
        }
    }

    cursor_col = (int)editor->cursor_col - view_start;
    lcd_set_cursor(cursor_row, cursor_col);
}

/**
 * Update shared editor LEDR, LEDG, and HEX fields.
 */
static int display_update_editor_status(const EditorDocument *editor,
                                        unsigned char ascii,
                                        int nav_mode,
                                        int eeprom_error)
{
    int view_start;

    view_start = display_view_start(editor);

    display_clear_ledg();
    display_set_ledg(DISPLAY_LEDG_INSERT, editor->insert_mode);
    display_set_ledg(DISPLAY_LEDG_NAV_MODE, nav_mode);
    display_set_ledg(DISPLAY_LEDG_EEPROM_ERROR, eeprom_error);
    display_set_ledg(DISPLAY_LEDG_OVERFLOW,
                     display_line_has_hidden_right(editor, view_start));
    display_set_ledg(DISPLAY_LEDG_DIRTY, editor->dirty);

    display_show_progress_percent(
        display_line_progress_percent(editor->current_line,
                                      editor->total_lines));

    display_write_decimal_pair(PIO_OUT_HEX7_BASE, PIO_OUT_HEX6_BASE,
                               (unsigned char)(editor->current_line + 1u));
    display_write_decimal_pair(PIO_OUT_HEX5_BASE, PIO_OUT_HEX4_BASE,
                               editor->cursor_col);
    display_write_decimal_pair(PIO_OUT_HEX3_BASE, PIO_OUT_HEX2_BASE,
                               editor->total_lines);
    display_write_hex_byte(PIO_OUT_HEX1_BASE, PIO_OUT_HEX0_BASE,
                           (unsigned char)(ascii & 0x7Fu));

    return view_start;
}

/**
 * Update LEDR, LEDG, HEX0-HEX7, and LCD from the current editor state.
 */
void display_update(const EditorDocument *editor,
                    unsigned char ascii,
                    int nav_mode,
                    int eeprom_error)
{
    display_update_with_markers(editor,
                                ascii,
                                nav_mode,
                                eeprom_error,
                                0,
                                LCD_END_WORD);
}

/**
 * Update LEDR, LEDG, HEX0-HEX7, and LCD from the current editor state with
 * optional document boundary markers.
 */
void display_update_with_markers(const EditorDocument *editor,
                                 unsigned char ascii,
                                 int nav_mode,
                                 int eeprom_error,
                                 const char *top_marker,
                                 const char *bottom_marker)
{
    int view_start;

    view_start = display_update_editor_status(editor,
                                              ascii,
                                              nav_mode,
                                              eeprom_error);
    display_write_editor_lines(editor, view_start, top_marker, bottom_marker);
    lcd_set_cursor_mode(editor->insert_mode);
}

static unsigned char display_write_decimal(unsigned char value, char *buffer)
{
    if (value > 99u) {
        value = 99u;
    }
    if (value >= 10u) {
        buffer[0] = (char)('0' + (value / 10u));
        buffer[1] = (char)('0' + (value % 10u));
        return 2u;
    }

    buffer[0] = (char)('0' + value);
    return 1u;
}

static void display_build_menu_counter(unsigned char selected_index,
                                       unsigned char option_count,
                                       char *row)
{
    char counter[5];
    unsigned char display_index;
    unsigned char length;
    unsigned char start;
    unsigned char i;

    display_index = 0;
    if (option_count > 0u) {
        display_index = (unsigned char)(selected_index + 1u);
    }
    if (display_index > 99u) {
        display_index = 99u;
    }
    if (option_count > 99u) {
        option_count = 99u;
    }

    length = display_write_decimal(display_index, counter);
    counter[length] = '/';
    ++length;
    length = (unsigned char)(length +
        display_write_decimal(option_count, &counter[length]));

    start = (unsigned char)((LCD_WIDTH - length) / 2u);
    for (i = 0u; i < length; ++i) {
        row[start + i] = counter[i];
    }
}

/**
 * Show one option from a horizontal menu.
 */
void display_show_menu_item(const char *option_name,
                            unsigned char selected_index,
                            unsigned char option_count)
{
    char row[LCD_WIDTH];
    unsigned char i;

    for (i = 0u; i < LCD_WIDTH; ++i) {
        row[i] = ' ';
    }

    if (option_count > 0u && selected_index > 0u) {
        row[DISPLAY_MENU_LEFT_COL] = '<';
    }
    if (option_count > 0u && selected_index + 1u < option_count) {
        row[DISPLAY_MENU_RIGHT_COL] = '>';
    }
    display_build_menu_counter(selected_index, option_count, row);

    display_write_ledr(0);
    display_clear_ledg();
    lcd_write_line(0, option_name, display_text_length(option_name, LCD_WIDTH));
    lcd_write_line(1, row, LCD_WIDTH);
    lcd_hide_cursor();
}

/**
 * Show a fixed two-line status message.
 */
void display_show_message(const char *line0, const char *line1)
{
    display_write_ledr(0);
    lcd_write_line(0, line0, display_text_length(line0, LCD_WIDTH));
    lcd_write_line(1, line1, display_text_length(line1, LCD_WIDTH));
    lcd_hide_cursor();
}

/**
 * Show the editor menu command prompt.
 */
void display_show_vi_command(const char *command)
{
    char row0[LCD_WIDTH];
    char row1[LCD_WIDTH];
    unsigned int command_length;
    unsigned int label_length;
    unsigned int label_start;
    unsigned int i;

    display_write_ledr(0);
    display_clear_ledg();

    for (i = 0; i < LCD_WIDTH; ++i) {
        row0[i] = ' ';
        row1[i] = ' ';
    }

    row0[0] = ':';
    command_length = display_marker_word_length(command);
    if (command_length > LCD_WIDTH - 1u) {
        command_length = LCD_WIDTH - 1u;
    }
    for (i = 0; i < command_length; ++i) {
        row0[i + 1u] = command[i];
    }

    label_length = display_marker_word_length(DISPLAY_VI_COMMAND_TEXT);
    label_start = (LCD_WIDTH - label_length) / 2u;
    row1[DISPLAY_MENU_LEFT_COL] = '<';
    row1[DISPLAY_MENU_RIGHT_COL] = '>';
    for (i = 0; i < label_length; ++i) {
        row1[label_start + i] = DISPLAY_VI_COMMAND_TEXT[i];
    }

    lcd_write_line(0, row0, LCD_WIDTH);
    lcd_write_line(1, row1, LCD_WIDTH);
    lcd_set_cursor(0, (int)command_length + 1);
    lcd_set_cursor_mode(1);
}

static void display_show_key_message(const char *message,
                                     const char *action_text,
                                     unsigned char ledr_flag)
{
    char row[LCD_WIDTH];

    display_select_ledr_effect(ledr_flag);
    display_clear_ledg();
    display_build_center_text(action_text, row);
    lcd_write_line(0, message, display_text_length(message, LCD_WIDTH));
    lcd_write_line(1, row, LCD_WIDTH);
    lcd_hide_cursor();
}

/**
 * Show an informational message that returns on KEY0.
 */
void display_show_info_message(const char *message)
{
    display_show_key_message(message,
                             DISPLAY_INFO_OK_TEXT,
                             DISPLAY_LEDR_FLAG_CONFIRM_BLINK);
}

/**
 * Show a yes/no confirmation message. KEY1 accepts and KEY0 cancels.
 */
void display_show_confirm_message(const char *message)
{
    display_show_key_message(message,
                             DISPLAY_CONFIRM_TEXT,
                             DISPLAY_LEDR_FLAG_CONFIRM_BLINK);
}

/**
 * Show an error message that returns on KEY0.
 */
void display_show_error_message(const char *message)
{
    display_show_key_message(message,
                             DISPLAY_INFO_OK_TEXT,
                             DISPLAY_LEDR_FLAG_ERROR_BLINK);
}

/**
 * Show two newline-delimited text rows starting at first_line.
 */
void display_show_text_page(const char *text,
                            unsigned int length,
                            unsigned int first_line)
{
    char row[LCD_WIDTH];
    unsigned int pos;

    display_write_ledr(0);
    pos = display_skip_text_lines(text, length, first_line);
    if (length == 0u) {
        lcd_write_line(0, "(empty)", 7);
        lcd_write_line(1, "", 0);
        lcd_hide_cursor();
        return;
    }

    pos = display_build_text_row(text, length, pos, row);
    lcd_write_line(0, row, LCD_WIDTH);
    (void)display_build_text_row(text, length, pos, row);
    lcd_write_line(1, row, LCD_WIDTH);
    lcd_hide_cursor();
}

/**
 * Show an activity marquee on LEDR17..LEDR1.
 */
void display_show_activity_marquee(unsigned int tick)
{
#ifdef PIO_OUT_LEDR_FLAG_BASE
    (void)tick;
    display_write_ledr_flag(DISPLAY_LEDR_FLAG_MARQUEE_LEFT_RIGHT);
#else
    unsigned int led_index;

    led_index = 17u - (tick % LEDR_ACTIVITY_LED_COUNT);
    display_write_ledr(1u << led_index);
#endif
}

/**
 * Compatibility wrapper for existing EEPROM activity callbacks.
 */
void display_save_marquee(unsigned int tick)
{
    display_show_activity_marquee(tick);
}
