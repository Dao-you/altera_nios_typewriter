#ifndef EDITOR_H
#define EDITOR_H

#define MAX_LINES 32
#define LINE_LEN 99
#define EDITOR_STORAGE_SIZE (40 + (MAX_LINES * LINE_LEN) + 2)
#define EDITOR_TEXT_BUFFER_SIZE ((MAX_LINES * LINE_LEN) + MAX_LINES)

typedef struct {
    char document[MAX_LINES][LINE_LEN + 1];
    unsigned char line_len[MAX_LINES];
    unsigned char current_line;
    unsigned char cursor_col;
    unsigned char total_lines;
    unsigned char insert_mode;
    unsigned char dirty;
} EditorDocument;

/**
 * Reset the editor state to one empty line.
 */
void editor_init(EditorDocument *editor);

/**
 * Set the edit mode from SW16 without marking document contents dirty.
 */
void editor_set_insert_mode(EditorDocument *editor, int insert_mode);

/**
 * Write an ASCII byte or supported control byte at the cursor.
 * BS deletes the previous character or previous LF, LF creates a new line,
 * and DEL deletes the character under the cursor. Returns 1 when the document
 * changes.
 */
int editor_write_ascii(EditorDocument *editor, unsigned char ascii);

/**
 * Move the cursor one column to the left.
 * Returns 1 when the cursor moves, or 0 at the boundary.
 */
int editor_move_left(EditorDocument *editor);

/**
 * Move the cursor one column to the right.
 * Returns 1 when the cursor moves, or 0 at the boundary.
 */
int editor_move_right(EditorDocument *editor);

/**
 * Move the cursor to the previous line and clamp the column to that line.
 * Returns 1 when the cursor moves, or 0 at the first line.
 */
int editor_move_up(EditorDocument *editor);

/**
 * Move the cursor to the next line and clamp the column to that line.
 * Returns 1 when the cursor moves, or 0 at the last line.
 */
int editor_move_down(EditorDocument *editor);

/**
 * Clear the current line contents and move the cursor to column 0.
 * Returns 1 when the document changes.
 */
int editor_clear_current_line(EditorDocument *editor);

/**
 * Clear the whole document back to one empty line.
 * Returns 1 when the document changes.
 */
int editor_clear_all(EditorDocument *editor);

/**
 * Move the cursor to the start of the document.
 * Returns 1 when the cursor moves.
 */
int editor_move_to_head(EditorDocument *editor);

/**
 * Move the cursor to the end of the document.
 * Returns 1 when the cursor moves.
 */
int editor_move_to_end(EditorDocument *editor);

/**
 * Clear the dirty flag after a successful primary save.
 */
void editor_mark_saved(EditorDocument *editor);

/**
 * Load newline-delimited ASCII text into the fixed editor buffer.
 * Returns 1 when the whole text fits, or 0 when it must be truncated.
 */
int editor_load_text(EditorDocument *editor, const char *buffer, unsigned int length);

/**
 * Export the editor as newline-delimited ASCII text.
 * Returns the number of text bytes, excluding the trailing null terminator.
 */
unsigned int editor_export_text(const EditorDocument *editor,
                                char *buffer,
                                unsigned int buffer_size);

/**
 * Serialize the editor into the fixed EEPROM byte layout.
 */
void editor_serialize(const EditorDocument *editor, unsigned char *buffer, unsigned int size);

/**
 * Load an editor from the fixed EEPROM byte layout.
 * Returns 1 for a valid saved document, or 0 for bad magic/checksum/data.
 */
int editor_deserialize(EditorDocument *editor, const unsigned char *buffer, unsigned int size);

#endif
