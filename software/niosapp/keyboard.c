#include "keyboard.h"
#include "system.h"
#include "altera_avalon_pio_regs.h"

#if !defined(PIO_IN_KEYBOARD_DATA_BASE) || \
    !defined(PIO_IN_KEYBOARD_STATUS_BASE) || \
    !defined(PIO_OUT_KEYBOARD_ACK_BASE)
#error "Update software/niosapp_bsp from nios.sopcinfo before building PS/2 keyboard support."
#endif

void keyboard_init(void)
{
    IOWR_ALTERA_AVALON_PIO_DATA(PIO_OUT_KEYBOARD_ACK_BASE, 0);
}

int keyboard_read(unsigned char *code)
{
    unsigned int status;

    status = IORD_ALTERA_AVALON_PIO_DATA(PIO_IN_KEYBOARD_STATUS_BASE);
    if ((status & KEYBOARD_STATUS_HAS_DATA) == 0) {
        return 0;
    }

    *code = (unsigned char)IORD_ALTERA_AVALON_PIO_DATA(PIO_IN_KEYBOARD_DATA_BASE);

    IOWR_ALTERA_AVALON_PIO_DATA(PIO_OUT_KEYBOARD_ACK_BASE, 1);
    IOWR_ALTERA_AVALON_PIO_DATA(PIO_OUT_KEYBOARD_ACK_BASE, 0);
    return 1;
}
