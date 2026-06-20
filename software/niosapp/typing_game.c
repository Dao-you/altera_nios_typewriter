#include "typing_game.h"

#define TYPING_GAME_MAX_CANDIDATES 64u
#define TYPING_GAME_DEFAULT_SEED 0x13579BDFu

typedef struct {
    unsigned int start;
    unsigned char length;
} TypingQuestionRef;

static unsigned int typing_game_next_random(unsigned int *state)
{
    unsigned int x;

    x = *state;
    if (x == 0u) {
        x = TYPING_GAME_DEFAULT_SEED;
    }
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;

    return x;
}

static int typing_game_is_alpha(char ch)
{
    return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z');
}

static char typing_game_to_lower(char ch)
{
    if (ch >= 'A' && ch <= 'Z') {
        return (char)(ch + ('a' - 'A'));
    }

    return ch;
}

static char typing_game_to_upper(char ch)
{
    if (ch >= 'a' && ch <= 'z') {
        return (char)(ch - ('a' - 'A'));
    }

    return ch;
}

static unsigned char typing_game_normalize_round_count(unsigned char count)
{
    if (count < TYPING_GAME_MIN_ROUNDS) {
        return TYPING_GAME_MIN_ROUNDS;
    }
    if (count > TYPING_GAME_MAX_ROUNDS) {
        return TYPING_GAME_MAX_ROUNDS;
    }

    return count;
}

static unsigned int typing_game_line_length(const char *text,
                                            unsigned int start,
                                            unsigned int end)
{
    while (end > start &&
           (text[end - 1u] == '\r' || text[end - 1u] == '\n')) {
        --end;
    }
    if (end - start > TYPING_GAME_MAX_QUESTION_LEN) {
        return TYPING_GAME_MAX_QUESTION_LEN;
    }

    return end - start;
}

static unsigned int typing_game_collect_question_refs(
    const char *text,
    unsigned int length,
    TypingQuestionRef *refs,
    unsigned int max_refs)
{
    unsigned int count;
    unsigned int pos;
    unsigned int line_start;
    unsigned int line_end;
    unsigned int line_len;

    count = 0;
    pos = 0;
    while (pos < length && count < max_refs) {
        line_start = pos;
        while (pos < length && text[pos] != '\n') {
            ++pos;
        }
        line_end = pos;
        if (pos < length && text[pos] == '\n') {
            ++pos;
        }

        line_len = typing_game_line_length(text, line_start, line_end);
        if (line_len > 0u) {
            refs[count].start = line_start;
            refs[count].length = (unsigned char)line_len;
            ++count;
        }
    }

    return count;
}

static void typing_game_copy_question(TypingGame *game,
                                      unsigned int round,
                                      const char *text,
                                      const TypingQuestionRef *ref)
{
    unsigned int i;

    for (i = 0; i < ref->length; ++i) {
        game->questions[round][i] = text[ref->start + i];
    }
    game->questions[round][ref->length] = '\0';
    game->question_len[round] = ref->length;
}

static void typing_game_apply_case_mode(TypingGame *game,
                                        unsigned int round,
                                        TypingGameCaseMode case_mode,
                                        unsigned int *seed)
{
    unsigned int i;
    int first_alpha_done;
    char ch;

    if (case_mode == TYPING_GAME_CASE_DEFAULT) {
        return;
    }

    first_alpha_done = 0;
    for (i = 0; i < game->question_len[round]; ++i) {
        ch = game->questions[round][i];
        if (!typing_game_is_alpha(ch)) {
            continue;
        }

        ch = typing_game_to_lower(ch);
        if (case_mode == TYPING_GAME_CASE_TITLE) {
            if (!first_alpha_done) {
                ch = typing_game_to_upper(ch);
                first_alpha_done = 1;
            }
        } else if ((typing_game_next_random(seed) & 0x01u) != 0u) {
            ch = typing_game_to_upper(ch);
        }
        game->questions[round][i] = ch;
    }
}

static void typing_game_pick_questions(TypingGame *game,
                                       const char *text,
                                       TypingQuestionRef *refs,
                                       unsigned int ref_count,
                                       TypingGameCaseMode case_mode,
                                       unsigned int seed)
{
    unsigned int i;
    unsigned int j;
    TypingQuestionRef tmp;

    if (seed == 0u) {
        seed = TYPING_GAME_DEFAULT_SEED;
    }

    for (i = 0; i < game->total_rounds; ++i) {
        j = i + (typing_game_next_random(&seed) % (ref_count - i));
        tmp = refs[i];
        refs[i] = refs[j];
        refs[j] = tmp;
        typing_game_copy_question(game, i, text, &refs[i]);
        typing_game_apply_case_mode(game, i, case_mode, &seed);
    }
}

/**
 * Reset all game state.
 */
void typing_game_init(TypingGame *game)
{
    unsigned int i;

    editor_init(&game->input);
    for (i = 0; i < TYPING_GAME_MAX_ROUNDS; ++i) {
        game->questions[i][0] = '\0';
        game->question_len[i] = 0;
    }
    game->current_round = 0;
    game->total_rounds = TYPING_GAME_DEFAULT_ROUNDS;
    game->elapsed_ms = 0;
    game->last_tick = 0;
    game->timer_started = 0;
    game->timer_running = 0;
}

/**
 * Load random non-empty lines from SD QUESTION.TXT contents.
 */
TypingGameLoadResult typing_game_load_questions(TypingGame *game,
                                                const char *text,
                                                unsigned int length,
                                                unsigned char round_count,
                                                TypingGameCaseMode case_mode,
                                                unsigned int seed)
{
    TypingQuestionRef refs[TYPING_GAME_MAX_CANDIDATES];
    unsigned int ref_count;
    unsigned char normalized_rounds;

    typing_game_init(game);
    normalized_rounds = typing_game_normalize_round_count(round_count);
    game->total_rounds = normalized_rounds;
    ref_count = typing_game_collect_question_refs(text,
                                                  length,
                                                  refs,
                                                  TYPING_GAME_MAX_CANDIDATES);
    if (ref_count < normalized_rounds) {
        return TYPING_GAME_LOAD_NOT_ENOUGH_LINES;
    }

    typing_game_pick_questions(game, text, refs, ref_count, case_mode, seed);
    typing_game_restart(game);
    return TYPING_GAME_LOAD_OK;
}

/**
 * Restart the currently loaded game without reshuffling.
 */
void typing_game_restart(TypingGame *game)
{
    unsigned char insert_mode;

    insert_mode = game->input.insert_mode;
    editor_init(&game->input);
    game->input.insert_mode = insert_mode;
    game->current_round = 0;
    game->elapsed_ms = 0;
    game->last_tick = 0;
    game->timer_started = 0;
    game->timer_running = 0;
}

static void typing_game_add_elapsed_ms(TypingGame *game,
                                       unsigned int delta_ms)
{
    if (game->elapsed_ms <= 5999000u - delta_ms) {
        game->elapsed_ms += delta_ms;
    } else {
        game->elapsed_ms = 5999000u;
    }
}

/**
 * Start the stopwatch if it has not started yet.
 */
void typing_game_start_stopwatch(TypingGame *game, unsigned int now_ticks)
{
    if (game->timer_running) {
        return;
    }

    game->timer_started = 1;
    game->timer_running = 1;
    game->last_tick = now_ticks;
}

/**
 * Resume the stopwatch after a pause, but only if it had already started.
 */
void typing_game_resume_stopwatch(TypingGame *game, unsigned int now_ticks)
{
    if (!game->timer_started || game->timer_running) {
        return;
    }

    game->timer_running = 1;
    game->last_tick = now_ticks;
}

/**
 * Refresh elapsed time from the Qsys system timer tick.
 */
void typing_game_update_stopwatch(TypingGame *game, unsigned int now_ticks)
{
    unsigned int delta_ticks;

    if (!game->timer_running) {
        return;
    }

    delta_ticks = now_ticks - game->last_tick;
    game->last_tick = now_ticks;
    typing_game_add_elapsed_ms(game, delta_ticks);
}

/**
 * Pause the stopwatch and preserve elapsed time.
 */
void typing_game_pause_stopwatch(TypingGame *game, unsigned int now_ticks)
{
    typing_game_update_stopwatch(game, now_ticks);
    game->timer_running = 0;
}

static int typing_game_current_answer_matches(const TypingGame *game)
{
    unsigned int i;
    unsigned int round;

    if (typing_game_is_complete(game)) {
        return 0;
    }

    round = game->current_round;
    if (game->input.line_len[0] != game->question_len[round]) {
        return 0;
    }
    for (i = 0; i < game->question_len[round]; ++i) {
        if (game->input.document[0][i] != game->questions[round][i]) {
            return 0;
        }
    }

    return 1;
}

/**
 * Advance when the current input exactly matches the current question.
 */
TypingGameAnswerResult typing_game_accept_if_answer_correct(TypingGame *game)
{
    unsigned char insert_mode;

    if (!typing_game_current_answer_matches(game)) {
        return TYPING_GAME_ANSWER_PENDING;
    }

    ++game->current_round;
    insert_mode = game->input.insert_mode;
    editor_init(&game->input);
    game->input.insert_mode = insert_mode;

    if (typing_game_is_complete(game)) {
        return TYPING_GAME_ANSWER_COMPLETE;
    }

    return TYPING_GAME_ANSWER_ADVANCED;
}

/**
 * Return nonzero when the input is long enough to judge but is not a match.
 */
int typing_game_current_answer_is_complete_mismatch(const TypingGame *game)
{
    unsigned int round;

    if (typing_game_is_complete(game)) {
        return 0;
    }

    round = game->current_round;
    if (game->input.line_len[0] < game->question_len[round]) {
        return 0;
    }

    return !typing_game_current_answer_matches(game);
}

/**
 * Return nonzero after all loaded rounds are complete.
 */
int typing_game_is_complete(const TypingGame *game)
{
    return game->current_round >= game->total_rounds;
}

const char *typing_game_current_question(const TypingGame *game)
{
    if (typing_game_is_complete(game)) {
        return "";
    }

    return game->questions[game->current_round];
}

unsigned char typing_game_current_question_len(const TypingGame *game)
{
    if (typing_game_is_complete(game)) {
        return 0;
    }

    return game->question_len[game->current_round];
}

unsigned char typing_game_current_round_number(const TypingGame *game)
{
    if (typing_game_is_complete(game)) {
        return game->total_rounds;
    }

    return (unsigned char)(game->current_round + 1u);
}

unsigned char typing_game_total_rounds(const TypingGame *game)
{
    return game->total_rounds;
}

unsigned int typing_game_elapsed_ms(const TypingGame *game)
{
    return game->elapsed_ms;
}

unsigned int typing_game_cpm(const TypingGame *game)
{
    unsigned int chars;
    unsigned int i;

    if (game->elapsed_ms == 0u) {
        return 0;
    }

    chars = 0;
    for (i = 0; i < game->total_rounds; ++i) {
        chars += game->question_len[i];
    }

    return ((chars * 60000u) + (game->elapsed_ms / 2u)) / game->elapsed_ms;
}

int typing_game_stopwatch_started(const TypingGame *game)
{
    return game->timer_started != 0u;
}
