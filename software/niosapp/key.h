#ifndef KEY_H
#define KEY_H

#define KEY_MASK_0 0x01u
#define KEY_MASK_1 0x02u
#define KEY_MASK_2 0x04u
#define KEY_MASK_3 0x08u

typedef struct {
    unsigned char stable;
    unsigned char previous_stable;
    unsigned char last_raw;
    unsigned char debounce_ticks;
    unsigned char pressed_edges;
} KeyState;

/**
 * Initialize debounce state from the current active-low KEY input.
 */
void key_init(KeyState *keys);

/**
 * Poll the active-low KEY PIO and update debounced pressed-edge bits.
 */
void key_update(KeyState *keys);

/**
 * Return nonzero when a debounced press edge exists for any bit in mask.
 */
int key_pressed_edge(const KeyState *keys, unsigned char mask);

#endif
