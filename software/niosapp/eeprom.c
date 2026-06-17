#include "eeprom.h"
#include "system.h"
#include "altera_avalon_pio_regs.h"
#include "priv/alt_busy_sleep.h"

#define EEPROM_I2C_WRITE 0xA0u
#define EEPROM_I2C_READ 0xA1u
#define EEPROM_PAGE_SIZE 32u
#define EEPROM_DELAY_US 5
#define EEPROM_ACK_POLL_LIMIT 120

/**
 * Drive SCL to a logic level through its output PIO.
 */
static void eeprom_set_scl(int high)
{
    IOWR_ALTERA_AVALON_PIO_DATA(PIO_OUT_EEP_SCL_BASE, high ? 1 : 0);
}

/**
 * Release SDA to high-Z so the pull-up or EEPROM can drive it.
 */
static void eeprom_release_sda(void)
{
    IOWR_ALTERA_AVALON_PIO_DATA(PIO_OUT_EEP_SDA_OUT_BASE, 0);
    IOWR_ALTERA_AVALON_PIO_DATA(PIO_OUT_EEP_SDA_OE_BASE, 0);
}

/**
 * Drive SDA low; this project never actively drives SDA high.
 */
static void eeprom_drive_sda_low(void)
{
    IOWR_ALTERA_AVALON_PIO_DATA(PIO_OUT_EEP_SDA_OUT_BASE, 0);
    IOWR_ALTERA_AVALON_PIO_DATA(PIO_OUT_EEP_SDA_OE_BASE, 1);
}

/**
 * Read SDA after releasing the line.
 */
static int eeprom_read_sda(void)
{
    return (IORD_ALTERA_AVALON_PIO_DATA(PIO_IN_EEP_SDA_IN_BASE) & 0x01u) != 0;
}

/**
 * Wait one conservative I2C half-period.
 */
static void eeprom_delay(void)
{
    alt_busy_sleep(EEPROM_DELAY_US);
}

/**
 * Generate an I2C start condition.
 */
static void eeprom_start(void)
{
    eeprom_release_sda();
    eeprom_set_scl(1);
    eeprom_delay();
    eeprom_drive_sda_low();
    eeprom_delay();
    eeprom_set_scl(0);
    eeprom_delay();
}

/**
 * Generate an I2C stop condition.
 */
static void eeprom_stop(void)
{
    eeprom_drive_sda_low();
    eeprom_delay();
    eeprom_set_scl(1);
    eeprom_delay();
    eeprom_release_sda();
    eeprom_delay();
}

/**
 * Write one I2C data bit.
 */
static void eeprom_write_bit(int bit_value)
{
    if (bit_value) {
        eeprom_release_sda();
    } else {
        eeprom_drive_sda_low();
    }
    eeprom_delay();
    eeprom_set_scl(1);
    eeprom_delay();
    eeprom_set_scl(0);
    eeprom_delay();
}

/**
 * Read one I2C data bit.
 */
static int eeprom_read_bit(void)
{
    int bit_value;

    eeprom_release_sda();
    eeprom_delay();
    eeprom_set_scl(1);
    eeprom_delay();
    bit_value = eeprom_read_sda();
    eeprom_set_scl(0);
    eeprom_delay();
    return bit_value;
}

/**
 * Write one byte and return 1 when the EEPROM acknowledges it.
 */
static int eeprom_write_byte(unsigned char value)
{
    int bit;
    int ack;

    for (bit = 7; bit >= 0; --bit) {
        eeprom_write_bit((value >> bit) & 0x01u);
    }
    ack = eeprom_read_bit();
    return ack == 0;
}

/**
 * Read one byte and send ACK for more bytes or NACK for the last byte.
 */
static unsigned char eeprom_read_byte(int ack_more)
{
    int bit;
    unsigned char value;

    value = 0;
    for (bit = 7; bit >= 0; --bit) {
        if (eeprom_read_bit()) {
            value |= (unsigned char)(1u << bit);
        }
    }
    eeprom_write_bit(ack_more ? 0 : 1);
    return value;
}

/**
 * Poll the EEPROM until its internal write cycle completes.
 */
static int eeprom_wait_ready(void)
{
    int tries;
    int ready;

    for (tries = 0; tries < EEPROM_ACK_POLL_LIMIT; ++tries) {
        eeprom_start();
        ready = eeprom_write_byte(EEPROM_I2C_WRITE);
        eeprom_stop();
        if (ready) {
            return 1;
        }
        alt_busy_sleep(100);
    }

    return 0;
}

/**
 * Write one page-limited chunk without crossing a 32-byte page boundary.
 */
static int eeprom_write_page(unsigned int address,
                             const unsigned char *data,
                             unsigned int length)
{
    unsigned int i;

    if (length == 0 || length > EEPROM_PAGE_SIZE) {
        return 0;
    }
    if (((address & (EEPROM_PAGE_SIZE - 1u)) + length) > EEPROM_PAGE_SIZE) {
        return 0;
    }

    eeprom_start();
    if (!eeprom_write_byte(EEPROM_I2C_WRITE) ||
        !eeprom_write_byte((unsigned char)((address >> 8) & 0xFFu)) ||
        !eeprom_write_byte((unsigned char)(address & 0xFFu))) {
        eeprom_stop();
        return 0;
    }

    for (i = 0; i < length; ++i) {
        if (!eeprom_write_byte(data[i])) {
            eeprom_stop();
            return 0;
        }
    }

    eeprom_stop();
    return eeprom_wait_ready();
}

/**
 * Write a byte buffer using 24LC32 32-byte page writes.
 *
 * The optional activity callback is called once before each page write so the
 * application can show a "saving" animation while this blocking loop runs.
 * The callback tick is an animation counter, not a storage progress value.
 */
static int eeprom_write_bytes(unsigned int address,
                              const unsigned char *data,
                              unsigned int length,
                              EepromActivityCallback activity,
                              void *activity_context)
{
    unsigned int offset;
    unsigned int page_space;
    unsigned int chunk;
    unsigned int step;

    offset = 0;
    step = 0;
    while (offset < length) {
        page_space = EEPROM_PAGE_SIZE - ((address + offset) & (EEPROM_PAGE_SIZE - 1u));
        chunk = length - offset;
        if (chunk > page_space) {
            chunk = page_space;
        }
        if (activity != 0) {
            activity(step, activity_context);
        }
        if (!eeprom_write_page(address + offset, &data[offset], chunk)) {
            return 0;
        }
        offset += chunk;
        ++step;
    }

    return 1;
}

/**
 * Read a byte buffer using a random-read address phase and sequential reads.
 */
static int eeprom_read_bytes(unsigned int address,
                             unsigned char *data,
                             unsigned int length)
{
    unsigned int i;

    if (length == 0) {
        return 1;
    }

    eeprom_start();
    if (!eeprom_write_byte(EEPROM_I2C_WRITE) ||
        !eeprom_write_byte((unsigned char)((address >> 8) & 0xFFu)) ||
        !eeprom_write_byte((unsigned char)(address & 0xFFu))) {
        eeprom_stop();
        return 0;
    }

    eeprom_start();
    if (!eeprom_write_byte(EEPROM_I2C_READ)) {
        eeprom_stop();
        return 0;
    }

    for (i = 0; i < length; ++i) {
        data[i] = eeprom_read_byte(i + 1u < length);
    }
    eeprom_stop();
    return 1;
}

/**
 * Initialize the EEPROM I2C PIO lines to idle.
 */
void eeprom_init(void)
{
    int i;

    eeprom_release_sda();
    eeprom_set_scl(1);
    eeprom_delay();

    for (i = 0; i < 9 && !eeprom_read_sda(); ++i) {
        eeprom_set_scl(0);
        eeprom_delay();
        eeprom_set_scl(1);
        eeprom_delay();
    }
    eeprom_stop();
}

/**
 * Read and validate a saved editor document from EEPROM.
 */
int eeprom_load_document(EditorDocument *editor)
{
    unsigned char buffer[EDITOR_STORAGE_SIZE];

    if (!eeprom_read_bytes(0, buffer, EDITOR_STORAGE_SIZE)) {
        return EEPROM_LOAD_ERROR;
    }
    if (!editor_deserialize(editor, buffer, EDITOR_STORAGE_SIZE)) {
        return EEPROM_LOAD_EMPTY;
    }

    return EEPROM_LOAD_OK;
}

/**
 * Save the complete editor document to EEPROM.
 */
int eeprom_save_document(const EditorDocument *editor)
{
    return eeprom_save_document_with_activity(editor, 0, 0);
}

/**
 * Save the complete editor document to EEPROM and report save activity.
 *
 * The callback is optional. When supplied, it is used only to indicate that
 * the blocking EEPROM write loop is active.
 */
int eeprom_save_document_with_activity(const EditorDocument *editor,
                                       EepromActivityCallback activity,
                                       void *activity_context)
{
    unsigned char buffer[EDITOR_STORAGE_SIZE];

    editor_serialize(editor, buffer, EDITOR_STORAGE_SIZE);
    return eeprom_write_bytes(0,
                              buffer,
                              EDITOR_STORAGE_SIZE,
                              activity,
                              activity_context);
}
