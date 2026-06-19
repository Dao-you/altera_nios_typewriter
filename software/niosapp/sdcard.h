#ifndef SDCARD_H
#define SDCARD_H

#define SDCARD_TEXT_BUFFER_SIZE 1024u

typedef enum {
    SDCARD_OK = 0,
    SDCARD_OK_TRUNCATED,
    SDCARD_NO_SPI,
    SDCARD_INIT_FAILED,
    SDCARD_READ_FAILED,
    SDCARD_UNSUPPORTED_FS,
    SDCARD_FILE_NOT_FOUND
} SdCardResult;

/**
 * Read QUESTION.TXT from a FAT16/FAT32 SD card through the Qsys SPI core.
 *
 * This is intentionally read-only and intended as a bring-up test. The text is
 * null-terminated when buffer_size is nonzero.
 */
SdCardResult sdcard_read_question_text(char *buffer,
                                       unsigned int buffer_size,
                                       unsigned int *length);

/**
 * Return a short LCD-friendly status string for an SD card result.
 */
const char *sdcard_result_text(SdCardResult result);

#endif
