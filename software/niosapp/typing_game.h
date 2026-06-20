#ifndef TYPING_GAME_H
#define TYPING_GAME_H

#include "editor.h"

#define TYPING_GAME_ROUNDS 10u
#define TYPING_GAME_MAX_QUESTION_LEN LINE_LEN

typedef enum {
    TYPING_GAME_LOAD_OK = 0,
    TYPING_GAME_LOAD_NOT_ENOUGH_LINES
} TypingGameLoadResult;

typedef enum {
    TYPING_GAME_ANSWER_PENDING = 0,
    TYPING_GAME_ANSWER_ADVANCED,
    TYPING_GAME_ANSWER_COMPLETE
} TypingGameAnswerResult;

typedef struct {
    EditorDocument input;
    char questions[TYPING_GAME_ROUNDS][TYPING_GAME_MAX_QUESTION_LEN + 1u];
    unsigned char question_len[TYPING_GAME_ROUNDS];
    unsigned char current_round;
    unsigned char total_rounds;
    unsigned int elapsed_ms;
    unsigned int last_tick;
    unsigned char timer_started;
    unsigned char timer_running;
} TypingGame;

/**
 * Reset all game state.
 */
void typing_game_init(TypingGame *game);

/**
 * Load ten random non-empty lines from SD QUESTION.TXT contents.
 *
 * Lines longer than TYPING_GAME_MAX_QUESTION_LEN are truncated to fit the
 * shared one-line EditorDocument input buffer.
 */
TypingGameLoadResult typing_game_load_questions(TypingGame *game,
                                                const char *text,
                                                unsigned int length,
                                                unsigned int seed);

/**
 * Restart the currently loaded ten-question game without reshuffling.
 */
void typing_game_restart(TypingGame *game);

/**
 * Start the stopwatch if it has not started yet.
 */
void typing_game_start_stopwatch(TypingGame *game, unsigned int now_ticks);

/**
 * Resume the stopwatch after a pause, but only if it had already started.
 */
void typing_game_resume_stopwatch(TypingGame *game, unsigned int now_ticks);

/**
 * Pause the stopwatch and preserve elapsed time.
 */
void typing_game_pause_stopwatch(TypingGame *game, unsigned int now_ticks);

/**
 * Refresh elapsed time from the Qsys system timer tick.
 */
void typing_game_update_stopwatch(TypingGame *game, unsigned int now_ticks);

/**
 * Advance when the current input exactly matches the current question.
 */
TypingGameAnswerResult typing_game_accept_if_answer_correct(TypingGame *game);

/**
 * Return nonzero when the input is long enough to judge but is not a match.
 */
int typing_game_current_answer_is_complete_mismatch(const TypingGame *game);

/**
 * Return nonzero after all loaded rounds are complete.
 */
int typing_game_is_complete(const TypingGame *game);

const char *typing_game_current_question(const TypingGame *game);
unsigned char typing_game_current_question_len(const TypingGame *game);
unsigned char typing_game_current_round_number(const TypingGame *game);
unsigned char typing_game_total_rounds(const TypingGame *game);
unsigned int typing_game_elapsed_ms(const TypingGame *game);
unsigned int typing_game_cpm(const TypingGame *game);
int typing_game_stopwatch_started(const TypingGame *game);

#endif
