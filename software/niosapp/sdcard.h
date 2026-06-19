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

typedef void (*SdCardActivityCallback)(unsigned int tick, void *context);

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
 * Read QUESTION.TXT and report blocking activity through a UI callback.
 *
 * The activity tick is an animation counter only, not a read-progress value.
 */
SdCardResult sdcard_read_question_text_with_activity(
    char *buffer,
    unsigned int buffer_size,
    unsigned int *length,
    SdCardActivityCallback activity,
    void *activity_context);

/**
 * Return a short LCD-friendly status string for an SD card result.
 */
const char *sdcard_result_text(SdCardResult result);

#endif
