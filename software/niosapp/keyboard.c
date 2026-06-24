#include "keyboard.h"
#include "system.h"
#include "altera_avalon_pio_regs.h"

#if !defined(PIO_IN_KEYBOARD_DATA_BASE) || \
    !defined(PIO_IN_KEYBOARD_STATUS_BASE) || \
    !defined(PIO_OUT_KEYBOARD_ACK_BASE) || \
    !defined(PIO_IN_SW_BASE)
#error "Update software/niosapp_bsp from nios.sopcinfo before building PS/2 keyboard support."
#endif

#define KEYBOARD_ENABLE_SWITCH_MASK (1u << 15)

static void keyboard_acknowledge(void)
{
    IOWR_ALTERA_AVALON_PIO_DATA(PIO_OUT_KEYBOARD_ACK_BASE, 1);
    IOWR_ALTERA_AVALON_PIO_DATA(PIO_OUT_KEYBOARD_ACK_BASE, 0);
}

static int keyboard_input_enabled(void)
{
    return (IORD_ALTERA_AVALON_PIO_DATA(PIO_IN_SW_BASE) &
            KEYBOARD_ENABLE_SWITCH_MASK) != 0u;
}

static void keyboard_discard_pending(void)
{
    unsigned int status;

    status = IORD_ALTERA_AVALON_PIO_DATA(PIO_IN_KEYBOARD_STATUS_BASE);
    while ((status & KEYBOARD_STATUS_HAS_DATA) != 0u) {
        (void)IORD_ALTERA_AVALON_PIO_DATA(PIO_IN_KEYBOARD_DATA_BASE);
        keyboard_acknowledge();
        status = IORD_ALTERA_AVALON_PIO_DATA(PIO_IN_KEYBOARD_STATUS_BASE);
    }
}

void keyboard_init(void)
{
    IOWR_ALTERA_AVALON_PIO_DATA(PIO_OUT_KEYBOARD_ACK_BASE, 0);
}

int keyboard_read(unsigned char *code)
{
    unsigned int status;

    if (!keyboard_input_enabled()) {
        keyboard_discard_pending();
        return 0;
    }

    status = IORD_ALTERA_AVALON_PIO_DATA(PIO_IN_KEYBOARD_STATUS_BASE);
    if ((status & KEYBOARD_STATUS_HAS_DATA) == 0) {
        return 0;
    }

    *code = (unsigned char)IORD_ALTERA_AVALON_PIO_DATA(PIO_IN_KEYBOARD_DATA_BASE);

    keyboard_acknowledge();
    return 1;
}
