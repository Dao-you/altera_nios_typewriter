#ifndef EDITOR_H
#define EDITOR_H

#define MAX_LINES 32
#define LINE_LEN 16
#define EDITOR_STORAGE_SIZE 554

typedef struct {
    char document[MAX_LINES][LINE_LEN + 1];
    unsigned char line_len[MAX_LINES];
    unsigned char current_line;
    unsigned char cursor_col;
    unsigned char total_lines;
    unsigned char insert_mode;
    unsigned char dirty;
    unsigned char overflow;
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
 * BS deletes the previous character, LF creates a new line, and DEL deletes
 * the character under the cursor. Returns 1 when the document changes.
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
 * Clear the dirty flag after a successful EEPROM save.
 */
void editor_mark_saved(EditorDocument *editor);

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
