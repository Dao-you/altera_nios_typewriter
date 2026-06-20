#include "editor.h"

#define STORAGE_MAGIC0 0x54
#define STORAGE_MAGIC1 0x45
#define STORAGE_VERSION 0x02
#define STORAGE_LINE_LEN_OFFSET 8
#define STORAGE_DOCUMENT_OFFSET (STORAGE_LINE_LEN_OFFSET + MAX_LINES)
#define STORAGE_CHECKSUM_OFFSET (EDITOR_STORAGE_SIZE - 2)

/**
 * Copy one fixed-width document row into another row.
 */
static void editor_copy_line(EditorDocument *editor, int dst, int src)
{
    int i;

    for (i = 0; i < LINE_LEN; ++i) {
        editor->document[dst][i] = editor->document[src][i];
    }
    editor->document[dst][LINE_LEN] = '\0';
    editor->line_len[dst] = editor->line_len[src];
}

/**
 * Clear one fixed-width document row.
 */
static void editor_clear_line(EditorDocument *editor, int line)
{
    int i;

    for (i = 0; i < LINE_LEN; ++i) {
        editor->document[line][i] = ' ';
    }
    editor->document[line][LINE_LEN] = '\0';
    editor->line_len[line] = 0;
}

/**
 * Keep cursor and line counts inside the fixed document bounds.
 */
static void editor_normalize(EditorDocument *editor)
{
    int i;

    if (editor->total_lines < 1 || editor->total_lines > MAX_LINES) {
        editor->total_lines = 1;
    }
    if (editor->current_line >= editor->total_lines) {
        editor->current_line = editor->total_lines - 1;
    }

    for (i = 0; i < MAX_LINES; ++i) {
        if (editor->line_len[i] > LINE_LEN) {
            editor->line_len[i] = LINE_LEN;
        }
        editor->document[i][LINE_LEN] = '\0';
    }

    if (editor->cursor_col > editor->line_len[editor->current_line]) {
        editor->cursor_col = editor->line_len[editor->current_line];
    }
}

/**
 * Calculate a small additive checksum over serialized bytes.
 */
static unsigned int editor_checksum(const unsigned char *buffer, unsigned int size)
{
    unsigned int i;
    unsigned int sum;

    sum = 0;
    for (i = 0; i < size; ++i) {
        sum = (sum + buffer[i]) & 0xFFFFu;
    }

    return sum;
}

/**
 * Reset the editor state to one empty line.
 */
void editor_init(EditorDocument *editor)
{
    int i;

    for (i = 0; i < MAX_LINES; ++i) {
        editor_clear_line(editor, i);
    }

    editor->current_line = 0;
    editor->cursor_col = 0;
    editor->total_lines = 1;
    editor->insert_mode = 0;
    editor->dirty = 0;
}

/**
 * Set the edit mode from SW16 without marking document contents dirty.
 */
void editor_set_insert_mode(EditorDocument *editor, int insert_mode)
{
    editor->insert_mode = insert_mode ? 1 : 0;
}

/**
 * Split the current line at the cursor and move the tail to a new line.
 */
static int editor_newline(EditorDocument *editor)
{
    int i;
    int current;
    int new_line;
    int tail_len;

    if (editor->total_lines >= MAX_LINES) {
        return 0;
    }

    current = editor->current_line;
    new_line = current + 1;
    tail_len = editor->line_len[current] - editor->cursor_col;
    if (tail_len < 0) {
        tail_len = 0;
    }

    for (i = editor->total_lines; i > new_line; --i) {
        editor_copy_line(editor, i, i - 1);
    }

    editor_clear_line(editor, new_line);
    for (i = 0; i < tail_len; ++i) {
        editor->document[new_line][i] =
            editor->document[current][editor->cursor_col + i];
        editor->document[current][editor->cursor_col + i] = ' ';
    }
    editor->line_len[new_line] = (unsigned char)tail_len;
    editor->line_len[current] = editor->cursor_col;
    editor->document[current][LINE_LEN] = '\0';
    editor->document[new_line][LINE_LEN] = '\0';

    editor->total_lines++;
    editor->current_line = (unsigned char)new_line;
    editor->cursor_col = 0;
    editor->dirty = 1;
    return 1;
}

/**
 * Insert a printable character at the cursor.
 */
static int editor_insert_char(EditorDocument *editor, unsigned char ascii)
{
    int i;
    int line;
    int len;

    line = editor->current_line;
    len = editor->line_len[line];
    if (len >= LINE_LEN) {
        return 0;
    }

    for (i = len; i > editor->cursor_col; --i) {
        editor->document[line][i] = editor->document[line][i - 1];
    }

    editor->document[line][editor->cursor_col] = (char)ascii;
    editor->line_len[line]++;
    editor->cursor_col++;
    editor->dirty = 1;
    return 1;
}

/**
 * Overwrite the character at the cursor or append at the current line end.
 */
static int editor_overwrite_char(EditorDocument *editor, unsigned char ascii)
{
    int line;

    line = editor->current_line;
    if (editor->cursor_col >= LINE_LEN) {
        return 0;
    }

    editor->document[line][editor->cursor_col] = (char)ascii;
    if (editor->cursor_col >= editor->line_len[line]) {
        editor->line_len[line] = editor->cursor_col + 1;
    }
    editor->cursor_col++;
    editor->dirty = 1;
    return 1;
}

/**
 * Delete the character at a specific column on the current line.
 */
static int editor_delete_at(EditorDocument *editor, int col)
{
    int i;
    int line;
    int len;

    line = editor->current_line;
    len = editor->line_len[line];
    if (col < 0 || col >= len) {
        return 0;
    }

    for (i = col; i < len - 1; ++i) {
        editor->document[line][i] = editor->document[line][i + 1];
    }
    editor->document[line][len - 1] = ' ';
    editor->line_len[line]--;
    if (editor->cursor_col > editor->line_len[line]) {
        editor->cursor_col = editor->line_len[line];
    }
    editor->dirty = 1;
    return 1;
}

/**
 * Join the current line into the previous line, deleting the LF between them.
 */
static int editor_join_with_previous_line(EditorDocument *editor)
{
    int i;
    int prev_line;
    int current_line;
    int prev_len;
    int current_len;

    if (editor->current_line == 0) {
        return 0;
    }

    current_line = editor->current_line;
    prev_line = current_line - 1;
    prev_len = editor->line_len[prev_line];
    current_len = editor->line_len[current_line];

    if (prev_len + current_len > LINE_LEN) {
        return 0;
    }

    for (i = 0; i < current_len; ++i) {
        editor->document[prev_line][prev_len + i] =
            editor->document[current_line][i];
    }
    editor->line_len[prev_line] = (unsigned char)(prev_len + current_len);

    for (i = current_line; i < editor->total_lines - 1; ++i) {
        editor_copy_line(editor, i, i + 1);
    }
    editor_clear_line(editor, editor->total_lines - 1);

    editor->total_lines--;
    editor->current_line = (unsigned char)prev_line;
    editor->cursor_col = (unsigned char)prev_len;
    editor->dirty = 1;
    return 1;
}

/**
 * Delete the character before the cursor and move the cursor left.
 * At column 0, delete the LF before this line by joining with the previous
 * line when the fixed line length allows it.
 */
static int editor_backspace(EditorDocument *editor)
{
    if (editor->cursor_col == 0) {
        return editor_join_with_previous_line(editor);
    }
    editor->cursor_col--;
    return editor_delete_at(editor, editor->cursor_col);
}

/**
 * Delete the character under the cursor.
 */
static int editor_delete(EditorDocument *editor)
{
    return editor_delete_at(editor, editor->cursor_col);
}

/**
 * Write an ASCII byte or supported control byte at the cursor.
 * BS deletes the previous character or previous LF, LF creates a new line,
 * and DEL deletes the character under the cursor. Returns 1 when the document
 * changes.
 */
int editor_write_ascii(EditorDocument *editor, unsigned char ascii)
{
    ascii = ascii & 0x7Fu;

    if (ascii == 0x08u) {
        return editor_backspace(editor);
    }
    if (ascii == 0x0Au) {
        return editor_newline(editor);
    }
    if (ascii == 0x7Fu) {
        return editor_delete(editor);
    }
    if (ascii < 0x20u || ascii > 0x7Eu) {
        return 0;
    }
    if (editor->insert_mode) {
        return editor_insert_char(editor, ascii);
    }

    return editor_overwrite_char(editor, ascii);
}

/**
 * Move the cursor one column to the left.
 * Returns 1 when the cursor moves, or 0 at the boundary.
 */
int editor_move_left(EditorDocument *editor)
{
    if (editor->cursor_col == 0) {
        return 0;
    }
    editor->cursor_col--;
    return 1;
}

/**
 * Move the cursor one column to the right.
 * Returns 1 when the cursor moves, or 0 at the boundary.
 */
int editor_move_right(EditorDocument *editor)
{
    if (editor->cursor_col >= editor->line_len[editor->current_line]) {
        return 0;
    }
    editor->cursor_col++;
    return 1;
}

/**
 * Move the cursor to the previous line and clamp the column to that line.
 * Returns 1 when the cursor moves, or 0 at the first line.
 */
int editor_move_up(EditorDocument *editor)
{
    if (editor->current_line == 0) {
        return 0;
    }
    editor->current_line--;
    if (editor->cursor_col > editor->line_len[editor->current_line]) {
        editor->cursor_col = editor->line_len[editor->current_line];
    }
    return 1;
}

/**
 * Move the cursor to the next line and clamp the column to that line.
 * Returns 1 when the cursor moves, or 0 at the last line.
 */
int editor_move_down(EditorDocument *editor)
{
    if (editor->current_line + 1 >= editor->total_lines) {
        return 0;
    }
    editor->current_line++;
    if (editor->cursor_col > editor->line_len[editor->current_line]) {
        editor->cursor_col = editor->line_len[editor->current_line];
    }
    return 1;
}

/**
 * Clear the current line contents and move the cursor to column 0.
 * Returns 1 when the document changes.
 */
int editor_clear_current_line(EditorDocument *editor)
{
    int changed;
    int line;

    line = editor->current_line;
    changed = editor->line_len[line] > 0u;
    editor_clear_line(editor, line);
    editor->cursor_col = 0;
    if (changed) {
        editor->dirty = 1;
        return 1;
    }

    return 0;
}

/**
 * Clear the whole document back to one empty line.
 * Returns 1 when the document changes.
 */
int editor_clear_all(EditorDocument *editor)
{
    int changed;
    int insert_mode;
    int was_dirty;

    changed = editor->total_lines != 1u || editor->line_len[0] > 0u;
    insert_mode = editor->insert_mode;
    was_dirty = editor->dirty;
    editor_init(editor);
    editor->insert_mode = (unsigned char)insert_mode;
    editor->dirty = (unsigned char)(was_dirty || changed);
    if (changed) {
        return 1;
    }

    return 0;
}

/**
 * Move the cursor to the start of the document.
 * Returns 1 when the cursor moves.
 */
int editor_move_to_head(EditorDocument *editor)
{
    int changed;

    changed = editor->current_line != 0u || editor->cursor_col != 0u;
    editor->current_line = 0;
    editor->cursor_col = 0;
    return changed;
}

/**
 * Move the cursor to the end of the document.
 * Returns 1 when the cursor moves.
 */
int editor_move_to_end(EditorDocument *editor)
{
    unsigned char last_line;
    unsigned char last_col;
    int changed;

    last_line = editor->total_lines - 1u;
    last_col = editor->line_len[last_line];
    changed = editor->current_line != last_line ||
        editor->cursor_col != last_col;
    editor->current_line = last_line;
    editor->cursor_col = last_col;
    return changed;
}

/**
 * Clear the dirty flag after a successful EEPROM save.
 */
void editor_mark_saved(EditorDocument *editor)
{
    editor->dirty = 0;
}

/**
 * Load newline-delimited ASCII text into the fixed editor buffer.
 * Returns 1 when the whole text fits, or 0 when it must be truncated.
 */
int editor_load_text(EditorDocument *editor, const char *buffer, unsigned int length)
{
    unsigned int i;
    unsigned char line;
    unsigned char col;
    unsigned char insert_mode;
    unsigned char ch;
    int truncated;

    insert_mode = editor->insert_mode;
    editor_init(editor);
    editor->insert_mode = insert_mode;

    line = 0;
    col = 0;
    truncated = 0;

    for (i = 0u; i < length; ++i) {
        ch = (unsigned char)buffer[i];
        if (ch == '\r') {
            continue;
        }
        if (ch == '\n') {
            if (line + 1u < MAX_LINES) {
                ++line;
                col = 0;
                if (editor->total_lines < line + 1u) {
                    editor->total_lines = line + 1u;
                }
            } else {
                truncated = 1;
            }
            continue;
        }

        if (ch == '\t') {
            ch = ' ';
        }
        if (ch < 0x20u || ch > 0x7Eu) {
            continue;
        }
        if (col < LINE_LEN) {
            editor->document[line][col] = (char)ch;
            ++col;
            editor->line_len[line] = col;
        } else {
            truncated = 1;
        }
    }

    editor->current_line = 0;
    editor->cursor_col = 0;
    editor->dirty = 0;
    editor_normalize(editor);

    return truncated ? 0 : 1;
}

/**
 * Export the editor as newline-delimited ASCII text.
 * Returns the number of text bytes, excluding the trailing null terminator.
 */
unsigned int editor_export_text(const EditorDocument *editor,
                                char *buffer,
                                unsigned int buffer_size)
{
    unsigned int line;
    unsigned int col;
    unsigned int length;

    length = 0u;
    for (line = 0u; line < editor->total_lines; ++line) {
        for (col = 0u; col < editor->line_len[line]; ++col) {
            if (buffer != 0 && length + 1u < buffer_size) {
                buffer[length] = editor->document[line][col];
            }
            ++length;
        }
        if (line + 1u < editor->total_lines) {
            if (buffer != 0 && length + 1u < buffer_size) {
                buffer[length] = '\n';
            }
            ++length;
        }
    }

    if (buffer != 0 && buffer_size > 0u) {
        if (length < buffer_size) {
            buffer[length] = '\0';
        } else {
            buffer[buffer_size - 1u] = '\0';
        }
    }

    return length;
}

/**
 * Serialize the editor into the fixed EEPROM byte layout.
 */
void editor_serialize(const EditorDocument *editor, unsigned char *buffer, unsigned int size)
{
    unsigned int i;
    unsigned int line;
    unsigned int col;
    unsigned int offset;
    unsigned int checksum;

    if (size < EDITOR_STORAGE_SIZE) {
        return;
    }

    for (i = 0; i < EDITOR_STORAGE_SIZE; ++i) {
        buffer[i] = 0;
    }

    buffer[0] = STORAGE_MAGIC0;
    buffer[1] = STORAGE_MAGIC1;
    buffer[2] = STORAGE_VERSION;
    buffer[3] = editor->total_lines;
    buffer[4] = editor->current_line;
    buffer[5] = editor->cursor_col;
    buffer[6] = editor->insert_mode;
    buffer[7] = 0;

    for (line = 0; line < MAX_LINES; ++line) {
        buffer[STORAGE_LINE_LEN_OFFSET + line] = editor->line_len[line];
    }

    offset = STORAGE_DOCUMENT_OFFSET;
    for (line = 0; line < MAX_LINES; ++line) {
        for (col = 0; col < LINE_LEN; ++col) {
            buffer[offset++] = (unsigned char)editor->document[line][col];
        }
    }

    checksum = editor_checksum(buffer, STORAGE_CHECKSUM_OFFSET);
    buffer[STORAGE_CHECKSUM_OFFSET] = (unsigned char)(checksum & 0xFFu);
    buffer[STORAGE_CHECKSUM_OFFSET + 1] = (unsigned char)((checksum >> 8) & 0xFFu);
}

/**
 * Load an editor from the fixed EEPROM byte layout.
 * Returns 1 for a valid saved document, or 0 for bad magic/checksum/data.
 */
int editor_deserialize(EditorDocument *editor, const unsigned char *buffer, unsigned int size)
{
    unsigned int line;
    unsigned int col;
    unsigned int offset;
    unsigned int stored_checksum;
    unsigned int calculated_checksum;

    if (size < EDITOR_STORAGE_SIZE) {
        return 0;
    }
    if (buffer[0] != STORAGE_MAGIC0 ||
        buffer[1] != STORAGE_MAGIC1 ||
        buffer[2] != STORAGE_VERSION) {
        return 0;
    }

    stored_checksum = buffer[STORAGE_CHECKSUM_OFFSET] |
        ((unsigned int)buffer[STORAGE_CHECKSUM_OFFSET + 1] << 8);
    calculated_checksum = editor_checksum(buffer, STORAGE_CHECKSUM_OFFSET);
    if (stored_checksum != calculated_checksum) {
        return 0;
    }
    if (buffer[3] < 1 || buffer[3] > MAX_LINES) {
        return 0;
    }

    editor_init(editor);
    editor->total_lines = buffer[3];
    editor->current_line = buffer[4];
    editor->cursor_col = buffer[5];
    editor->insert_mode = buffer[6] ? 1 : 0;

    for (line = 0; line < MAX_LINES; ++line) {
        if (buffer[STORAGE_LINE_LEN_OFFSET + line] > LINE_LEN) {
            return 0;
        }
        editor->line_len[line] = buffer[STORAGE_LINE_LEN_OFFSET + line];
    }

    offset = STORAGE_DOCUMENT_OFFSET;
    for (line = 0; line < MAX_LINES; ++line) {
        for (col = 0; col < LINE_LEN; ++col) {
            editor->document[line][col] = (char)buffer[offset++];
        }
        editor->document[line][LINE_LEN] = '\0';
    }

    editor->dirty = 0;
    editor_normalize(editor);
    return 1;
}
