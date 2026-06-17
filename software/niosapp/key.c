#include "key.h"
#include "system.h"
#include "altera_avalon_pio_regs.h"

#define KEY_DEBOUNCE_TICKS 3

/**
 * Read KEY[3:0] as active-high pressed bits.
 */
static unsigned char key_read_raw_pressed(void)
{
    unsigned int raw;

    raw = IORD_ALTERA_AVALON_PIO_DATA(PIO_IN_KEY_BASE);
    return (unsigned char)((~raw) & 0x0Fu);
}

/**
 * Initialize debounce state from the current active-low KEY input.
 */
void key_init(KeyState *keys)
{
    keys->stable = key_read_raw_pressed();
    keys->previous_stable = keys->stable;
    keys->last_raw = keys->stable;
    keys->debounce_ticks = 0;
    keys->pressed_edges = 0;
}

/**
 * Poll the active-low KEY PIO and update debounced pressed-edge bits.
 */
void key_update(KeyState *keys)
{
    unsigned char raw;

    raw = key_read_raw_pressed();
    keys->pressed_edges = 0;

    if (raw != keys->last_raw) {
        keys->last_raw = raw;
        keys->debounce_ticks = 0;
        return;
    }

    if (keys->debounce_ticks < KEY_DEBOUNCE_TICKS) {
        keys->debounce_ticks++;
        if (keys->debounce_ticks == KEY_DEBOUNCE_TICKS &&
            raw != keys->stable) {
            keys->previous_stable = keys->stable;
            keys->stable = raw;
            keys->pressed_edges =
                (unsigned char)(keys->stable & (unsigned char)(~keys->previous_stable));
        }
    }
}

/**
 * Return nonzero when a debounced press edge exists for any bit in mask.
 */
int key_pressed_edge(const KeyState *keys, unsigned char mask)
{
    return (keys->pressed_edges & mask) != 0;
}
