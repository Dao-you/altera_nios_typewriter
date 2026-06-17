#include "seven_seg.h"

/**
 * Return the active-low seven-segment code for one hexadecimal nibble.
 */
unsigned char seven_seg_encode_hex(unsigned char value)
{
    static const unsigned char table[16] = {
        0x40, 0x79, 0x24, 0x30,
        0x19, 0x12, 0x02, 0x78,
        0x00, 0x10, 0x08, 0x03,
        0x46, 0x21, 0x06, 0x0E
    };

    return table[value & 0x0Fu];
}

/**
 * Return the active-low seven-segment blank code.
 */
unsigned char seven_seg_blank(void)
{
    return 0x7Fu;
}
