#ifndef SEVEN_SEG_H
#define SEVEN_SEG_H

/**
 * Return the active-low seven-segment code for one hexadecimal nibble.
 */
unsigned char seven_seg_encode_hex(unsigned char value);

/**
 * Return the active-low seven-segment blank code.
 */
unsigned char seven_seg_blank(void);

#endif
