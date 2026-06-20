#include "sdcard.h"
#include "system.h"

#if defined(SPI_SDCARD_BASE)
#include "io.h"
#include "priv/alt_busy_sleep.h"
#endif

static const unsigned char fat_question_name[11] = {
    'Q', 'U', 'E', 'S', 'T', 'I', 'O', 'N', 'T', 'X', 'T'
};
static const unsigned char fat_editor_name[11] = {
    'E', 'D', 'I', 'T', 'O', 'R', ' ', ' ', 'T', 'X', 'T'
};

#if defined(SPI_SDCARD_BASE)
#define SD_BLOCK_SIZE 512u
#define SD_SPI_SLAVE 0u
#define SD_SPI_TIMEOUT 1000000u
#define SD_CMD_TIMEOUT 10u
#define SD_INIT_RETRIES 1000u
#define SD_TOKEN_TIMEOUT 100000u
#define SD_MAX_CLUSTER_CHAIN 4096u

#define SD_SPI_RXDATA_REG 0u
#define SD_SPI_TXDATA_REG 1u
#define SD_SPI_STATUS_REG 2u
#define SD_SPI_CONTROL_REG 3u
#define SD_SPI_SLAVE_SEL_REG 5u

#define SD_SPI_STATUS_TMT 0x20u
#define SD_SPI_STATUS_TRDY 0x40u
#define SD_SPI_STATUS_RRDY 0x80u
#define SD_SPI_CONTROL_SSO 0x400u

#define SD_CMD_GO_IDLE_STATE 0u
#define SD_CMD_SEND_IF_COND 8u
#define SD_CMD_SEND_CSD 9u
#define SD_CMD_SET_BLOCKLEN 16u
#define SD_CMD_READ_SINGLE_BLOCK 17u
#define SD_CMD_WRITE_SINGLE_BLOCK 24u
#define SD_CMD_APP_CMD 55u
#define SD_CMD_READ_OCR 58u
#define SD_ACMD_SD_SEND_OP_COND 41u
#define SD_DATA_START_TOKEN 0xFEu
#define SD_DATA_RESPONSE_ACCEPTED 0x05u
#define SD_WRITE_BUSY_TIMEOUT 1000000u
#define SD_MAX_WRITE_CLUSTERS 16u

#define FAT_TYPE_FAT16 16u
#define FAT_TYPE_FAT32 32u

typedef struct {
    unsigned int boot_lba;
    unsigned int fat_lba;
    unsigned int root_dir_lba;
    unsigned int data_lba;
    unsigned int sectors_per_fat;
    unsigned int root_dir_sectors;
    unsigned int root_cluster;
    unsigned int fat_count;
    unsigned int cluster_count;
    unsigned char sectors_per_cluster;
    unsigned char fat_type;
} FatInfo;

typedef struct {
    unsigned int first_cluster;
    unsigned int size;
    unsigned int dir_lba;
    unsigned int dir_offset;
} FatFile;

typedef struct {
    unsigned int lba;
    unsigned int offset;
    int found;
} FatDirSlot;

static unsigned char sd_sector[SD_BLOCK_SIZE];
static int sd_spi_error = 0;
static int sd_block_addressed = 0;
static SdCardActivityCallback sd_activity = 0;
static void *sd_activity_context = 0;
static unsigned int sd_activity_tick = 0;

static void sd_report_activity(void)
{
    if (sd_activity != 0) {
        sd_activity(sd_activity_tick, sd_activity_context);
        ++sd_activity_tick;
    }
}

static void sd_set_activity(SdCardActivityCallback activity, void *context)
{
    sd_activity = activity;
    sd_activity_context = context;
    sd_activity_tick = 0u;
}

static void sd_clear_activity(void)
{
    sd_activity = 0;
    sd_activity_context = 0;
    sd_activity_tick = 0u;
}

static unsigned int le16(const unsigned char *p)
{
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8);
}

static unsigned int le32(const unsigned char *p)
{
    return (unsigned int)p[0] |
        ((unsigned int)p[1] << 8) |
        ((unsigned int)p[2] << 16) |
        ((unsigned int)p[3] << 24);
}

static void put_le16(unsigned char *p, unsigned int value)
{
    p[0] = (unsigned char)(value & 0xFFu);
    p[1] = (unsigned char)((value >> 8) & 0xFFu);
}

static void put_le32(unsigned char *p, unsigned int value)
{
    p[0] = (unsigned char)(value & 0xFFu);
    p[1] = (unsigned char)((value >> 8) & 0xFFu);
    p[2] = (unsigned char)((value >> 16) & 0xFFu);
    p[3] = (unsigned char)((value >> 24) & 0xFFu);
}

static unsigned int min_u32(unsigned int a, unsigned int b)
{
    return (a < b) ? a : b;
}

static unsigned int sd_spi_read_reg(unsigned int reg)
{
    return IORD(SPI_SDCARD_BASE, reg);
}

static void sd_spi_write_reg(unsigned int reg, unsigned int value)
{
    IOWR(SPI_SDCARD_BASE, reg, value);
}

static void sd_spi_drain_rx(void)
{
    unsigned int guard;

    guard = SD_SPI_TIMEOUT;
    while ((sd_spi_read_reg(SD_SPI_STATUS_REG) & SD_SPI_STATUS_RRDY) != 0u) {
        (void)sd_spi_read_reg(SD_SPI_RXDATA_REG);
        if (guard == 0u) {
            sd_spi_error = 1;
            return;
        }
        --guard;
    }
}

static unsigned char sd_spi_transfer(unsigned char value)
{
    unsigned int guard;
    unsigned int status;
    unsigned int rx;

    guard = SD_SPI_TIMEOUT;
    do {
        status = sd_spi_read_reg(SD_SPI_STATUS_REG);
        if (guard == 0u) {
            sd_spi_error = 1;
            return 0xFFu;
        }
        --guard;
    } while ((status & SD_SPI_STATUS_TRDY) == 0u);

    sd_spi_write_reg(SD_SPI_TXDATA_REG, value);

    guard = SD_SPI_TIMEOUT;
    do {
        status = sd_spi_read_reg(SD_SPI_STATUS_REG);
        if (guard == 0u) {
            sd_spi_error = 1;
            return 0xFFu;
        }
        --guard;
    } while ((status & SD_SPI_STATUS_RRDY) == 0u);

    rx = sd_spi_read_reg(SD_SPI_RXDATA_REG) & 0xFFu;

    guard = SD_SPI_TIMEOUT;
    do {
        status = sd_spi_read_reg(SD_SPI_STATUS_REG);
        if (guard == 0u) {
            sd_spi_error = 1;
            return 0xFFu;
        }
        --guard;
    } while ((status & SD_SPI_STATUS_TMT) == 0u);

    return (unsigned char)rx;
}

static void sd_spi_select(void)
{
    sd_spi_drain_rx();
    sd_spi_write_reg(SD_SPI_SLAVE_SEL_REG, 1u << SD_SPI_SLAVE);
    sd_spi_write_reg(SD_SPI_CONTROL_REG, SD_SPI_CONTROL_SSO);
    (void)sd_spi_transfer(0xFFu);
}

static void sd_spi_deselect(void)
{
    sd_spi_write_reg(SD_SPI_CONTROL_REG, 0u);
    sd_spi_write_reg(SD_SPI_SLAVE_SEL_REG, 0u);
    (void)sd_spi_transfer(0xFFu);
}

static unsigned char sd_send_command(unsigned char cmd,
                                     unsigned int arg,
                                     unsigned char crc)
{
    unsigned int i;
    unsigned char r1;

    sd_spi_deselect();
    sd_spi_select();
    (void)sd_spi_transfer((unsigned char)(0x40u | cmd));
    (void)sd_spi_transfer((unsigned char)(arg >> 24));
    (void)sd_spi_transfer((unsigned char)(arg >> 16));
    (void)sd_spi_transfer((unsigned char)(arg >> 8));
    (void)sd_spi_transfer((unsigned char)arg);
    (void)sd_spi_transfer(crc);

    r1 = 0xFFu;
    for (i = 0; i < SD_CMD_TIMEOUT; ++i) {
        r1 = sd_spi_transfer(0xFFu);
        if ((r1 & 0x80u) == 0u) {
            break;
        }
    }

    return r1;
}

static int sd_wait_ready(int high_capacity_request)
{
    unsigned int i;
    unsigned int acmd41_arg;
    unsigned char r1;

    acmd41_arg = high_capacity_request ? 0x40000000u : 0u;
    for (i = 0; i < SD_INIT_RETRIES; ++i) {
        sd_report_activity();
        r1 = sd_send_command(SD_CMD_APP_CMD, 0u, 0xFFu);
        sd_spi_deselect();
        if ((r1 & 0xFEu) != 0u) {
            return 0;
        }

        r1 = sd_send_command(SD_ACMD_SD_SEND_OP_COND, acmd41_arg, 0xFFu);
        sd_spi_deselect();
        if (r1 == 0u) {
            return 1;
        }
        alt_busy_sleep(1000);
    }

    return 0;
}

static int sd_init(void)
{
    unsigned int i;
    unsigned char r1;
    unsigned char ocr[4];
    int v2_card;

    sd_spi_error = 0;
    sd_block_addressed = 0;

    sd_spi_write_reg(SD_SPI_CONTROL_REG, 0u);
    sd_spi_write_reg(SD_SPI_SLAVE_SEL_REG, 0u);
    for (i = 0; i < 10u; ++i) {
        (void)sd_spi_transfer(0xFFu);
    }

    r1 = sd_send_command(SD_CMD_GO_IDLE_STATE, 0u, 0x95u);
    sd_spi_deselect();
    sd_report_activity();
    if (r1 != 0x01u || sd_spi_error) {
        return 0;
    }

    v2_card = 0;
    r1 = sd_send_command(SD_CMD_SEND_IF_COND, 0x000001AAu, 0x87u);
    if (r1 == 0x01u) {
        for (i = 0; i < 4u; ++i) {
            ocr[i] = sd_spi_transfer(0xFFu);
        }
        v2_card = (ocr[2] == 0x01u && ocr[3] == 0xAAu);
    }
    sd_spi_deselect();

    if (!v2_card && r1 != 0x05u && r1 != 0x01u) {
        return 0;
    }
    if (!sd_wait_ready(v2_card)) {
        return 0;
    }

    r1 = sd_send_command(SD_CMD_READ_OCR, 0u, 0xFFu);
    if (r1 == 0u) {
        for (i = 0; i < 4u; ++i) {
            ocr[i] = sd_spi_transfer(0xFFu);
        }
        sd_block_addressed = (ocr[0] & 0x40u) != 0u;
    }
    sd_spi_deselect();

    if (!sd_block_addressed) {
        r1 = sd_send_command(SD_CMD_SET_BLOCKLEN, SD_BLOCK_SIZE, 0xFFu);
        sd_spi_deselect();
        if (r1 != 0u) {
            return 0;
        }
    }

    return sd_spi_error == 0;
}

static int sd_read_sector(unsigned int lba, unsigned char *buffer)
{
    unsigned int i;
    unsigned int address;
    unsigned char r1;
    unsigned char token;

    sd_report_activity();
    address = sd_block_addressed ? lba : (lba * SD_BLOCK_SIZE);
    r1 = sd_send_command(SD_CMD_READ_SINGLE_BLOCK, address, 0xFFu);
    if (r1 != 0u || sd_spi_error) {
        sd_spi_deselect();
        return 0;
    }

    token = 0xFFu;
    for (i = 0; i < SD_TOKEN_TIMEOUT; ++i) {
        token = sd_spi_transfer(0xFFu);
        if (token != 0xFFu) {
            break;
        }
    }
    if (token != 0xFEu) {
        sd_spi_deselect();
        return 0;
    }

    for (i = 0; i < SD_BLOCK_SIZE; ++i) {
        buffer[i] = sd_spi_transfer(0xFFu);
    }
    (void)sd_spi_transfer(0xFFu);
    (void)sd_spi_transfer(0xFFu);
    sd_spi_deselect();

    return sd_spi_error == 0;
}

static int sd_wait_write_ready(void)
{
    unsigned int i;
    unsigned char value;

    for (i = 0u; i < SD_WRITE_BUSY_TIMEOUT; ++i) {
        if ((i & 0x3FFFu) == 0u) {
            sd_report_activity();
        }
        value = sd_spi_transfer(0xFFu);
        if (value == 0xFFu) {
            return sd_spi_error == 0;
        }
    }

    return 0;
}

static int sd_write_sector(unsigned int lba, const unsigned char *buffer)
{
    unsigned int i;
    unsigned int address;
    unsigned char r1;
    unsigned char response;

    sd_report_activity();
    if (!sd_wait_write_ready()) {
        sd_spi_deselect();
        return 0;
    }
    address = sd_block_addressed ? lba : (lba * SD_BLOCK_SIZE);
    r1 = sd_send_command(SD_CMD_WRITE_SINGLE_BLOCK, address, 0xFFu);
    if (r1 != 0u || sd_spi_error) {
        sd_spi_deselect();
        return 0;
    }
    if (!sd_wait_write_ready()) {
        sd_spi_deselect();
        return 0;
    }

    (void)sd_spi_transfer(SD_DATA_START_TOKEN);
    for (i = 0u; i < SD_BLOCK_SIZE; ++i) {
        (void)sd_spi_transfer(buffer[i]);
    }
    (void)sd_spi_transfer(0xFFu);
    (void)sd_spi_transfer(0xFFu);

    response = sd_spi_transfer(0xFFu);
    if ((response & 0x1Fu) != SD_DATA_RESPONSE_ACCEPTED) {
        sd_spi_deselect();
        return 0;
    }

    if (!sd_wait_write_ready()) {
        sd_spi_deselect();
        return 0;
    }
    sd_spi_deselect();

    return sd_spi_error == 0;
}

static int fat_boot_sector_is_valid(const unsigned char *sector)
{
    if (sector[510] != 0x55u || sector[511] != 0xAAu) {
        return 0;
    }
    if (le16(&sector[11]) != SD_BLOCK_SIZE) {
        return 0;
    }
    if (sector[13] == 0u || le16(&sector[14]) == 0u || sector[16] == 0u) {
        return 0;
    }

    return 1;
}

static SdCardResult fat_mount(FatInfo *fs)
{
    unsigned int boot_lba;
    unsigned int root_entry_count;
    unsigned int reserved_sectors;
    unsigned int fat_count;
    unsigned int sectors_per_fat;
    unsigned int total_sectors;
    unsigned int data_sectors;
    unsigned int cluster_count;
    unsigned int part_lba;

    if (!sd_read_sector(0u, sd_sector)) {
        return SDCARD_READ_FAILED;
    }

    boot_lba = 0u;
    if (!fat_boot_sector_is_valid(sd_sector)) {
        if (sd_sector[510] != 0x55u || sd_sector[511] != 0xAAu) {
            return SDCARD_UNSUPPORTED_FS;
        }
        part_lba = le32(&sd_sector[0x1BEu + 8u]);
        if (part_lba == 0u) {
            return SDCARD_UNSUPPORTED_FS;
        }
        if (!sd_read_sector(part_lba, sd_sector)) {
            return SDCARD_READ_FAILED;
        }
        if (!fat_boot_sector_is_valid(sd_sector)) {
            return SDCARD_UNSUPPORTED_FS;
        }
        boot_lba = part_lba;
    }

    fs->boot_lba = boot_lba;
    fs->sectors_per_cluster = sd_sector[13];
    reserved_sectors = le16(&sd_sector[14]);
    fat_count = sd_sector[16];
    root_entry_count = le16(&sd_sector[17]);
    sectors_per_fat = le16(&sd_sector[22]);
    if (sectors_per_fat == 0u) {
        sectors_per_fat = le32(&sd_sector[36]);
    }
    total_sectors = le16(&sd_sector[19]);
    if (total_sectors == 0u) {
        total_sectors = le32(&sd_sector[32]);
    }
    if (fat_count == 0u || sectors_per_fat == 0u || total_sectors == 0u) {
        return SDCARD_UNSUPPORTED_FS;
    }

    fs->fat_count = fat_count;
    fs->fat_lba = boot_lba + reserved_sectors;
    fs->sectors_per_fat = sectors_per_fat;
    fs->root_dir_sectors =
        ((root_entry_count * 32u) + (SD_BLOCK_SIZE - 1u)) / SD_BLOCK_SIZE;
    fs->root_dir_lba = fs->fat_lba + (fat_count * sectors_per_fat);
    fs->data_lba = fs->root_dir_lba + fs->root_dir_sectors;
    fs->root_cluster = le32(&sd_sector[44]);

    data_sectors = total_sectors - reserved_sectors -
        (fat_count * sectors_per_fat) - fs->root_dir_sectors;
    cluster_count = data_sectors / fs->sectors_per_cluster;
    fs->cluster_count = cluster_count;
    if (root_entry_count == 0u || cluster_count >= 65525u) {
        fs->fat_type = FAT_TYPE_FAT32;
        fs->data_lba = fs->fat_lba + (fat_count * sectors_per_fat);
        if (fs->root_cluster < 2u) {
            return SDCARD_UNSUPPORTED_FS;
        }
    } else if (cluster_count >= 4085u) {
        fs->fat_type = FAT_TYPE_FAT16;
        fs->root_cluster = 0u;
    } else {
        return SDCARD_UNSUPPORTED_FS;
    }

    return SDCARD_OK;
}

static unsigned int fat_cluster_to_lba(const FatInfo *fs, unsigned int cluster)
{
    return fs->data_lba +
        ((cluster - 2u) * (unsigned int)fs->sectors_per_cluster);
}

static int fat_is_end_cluster(const FatInfo *fs, unsigned int cluster)
{
    if (fs->fat_type == FAT_TYPE_FAT32) {
        return cluster >= 0x0FFFFFF8u;
    }
    return cluster >= 0xFFF8u;
}

static unsigned int fat_end_cluster_value(const FatInfo *fs)
{
    if (fs->fat_type == FAT_TYPE_FAT32) {
        return 0x0FFFFFFFu;
    }
    return 0xFFFFu;
}

static int fat_read_fat_entry(const FatInfo *fs,
                              unsigned int cluster,
                              unsigned int *value)
{
    unsigned int fat_offset;
    unsigned int sector_lba;
    unsigned int sector_offset;

    if (fs->fat_type == FAT_TYPE_FAT32) {
        fat_offset = cluster * 4u;
    } else {
        fat_offset = cluster * 2u;
    }
    sector_lba = fs->fat_lba + (fat_offset / SD_BLOCK_SIZE);
    sector_offset = fat_offset % SD_BLOCK_SIZE;

    if (!sd_read_sector(sector_lba, sd_sector)) {
        return 0;
    }
    if (fs->fat_type == FAT_TYPE_FAT32) {
        *value = le32(&sd_sector[sector_offset]) & 0x0FFFFFFFu;
    } else {
        *value = le16(&sd_sector[sector_offset]);
    }

    return 1;
}

static int fat_read_next_cluster(const FatInfo *fs,
                                 unsigned int cluster,
                                 unsigned int *next_cluster)
{
    return fat_read_fat_entry(fs, cluster, next_cluster);
}

static int fat_write_fat_entry(const FatInfo *fs,
                               unsigned int cluster,
                               unsigned int value)
{
    unsigned int fat_offset;
    unsigned int sector_offset;
    unsigned int copy_index;
    unsigned int sector_lba;
    unsigned int old_value;

    if (fs->fat_type == FAT_TYPE_FAT32) {
        fat_offset = cluster * 4u;
    } else {
        fat_offset = cluster * 2u;
    }
    sector_offset = fat_offset % SD_BLOCK_SIZE;

    for (copy_index = 0u; copy_index < fs->fat_count; ++copy_index) {
        sector_lba = fs->fat_lba +
            (copy_index * fs->sectors_per_fat) +
            (fat_offset / SD_BLOCK_SIZE);
        if (!sd_read_sector(sector_lba, sd_sector)) {
            return 0;
        }
        if (fs->fat_type == FAT_TYPE_FAT32) {
            old_value = le32(&sd_sector[sector_offset]) & 0xF0000000u;
            put_le32(&sd_sector[sector_offset],
                     old_value | (value & 0x0FFFFFFFu));
        } else {
            put_le16(&sd_sector[sector_offset], value & 0xFFFFu);
        }
        if (!sd_write_sector(sector_lba, sd_sector)) {
            return 0;
        }
    }

    return 1;
}

static int fat_entry_matches_name(const unsigned char *entry,
                                  const unsigned char *target)
{
    unsigned int i;

    for (i = 0; i < 11u; ++i) {
        if (entry[i] != target[i]) {
            return 0;
        }
    }

    return 1;
}

static int fat_parse_dir_entry(const unsigned char *entry, FatFile *file)
{
    unsigned int high_cluster;
    unsigned int low_cluster;

    high_cluster = le16(&entry[20]);
    low_cluster = le16(&entry[26]);
    file->first_cluster = (high_cluster << 16) | low_cluster;
    file->size = le32(&entry[28]);

    return 1;
}

static int fat_scan_dir_sector(const unsigned char *sector,
                               unsigned int sector_lba,
                               const unsigned char *target,
                               FatFile *file,
                               FatDirSlot *free_slot,
                               int *end_seen)
{
    unsigned int entry_index;
    unsigned int entry_offset;
    const unsigned char *entry;
    unsigned char attr;

    for (entry_index = 0u; entry_index < 16u; ++entry_index) {
        entry_offset = entry_index * 32u;
        entry = &sector[entry_offset];
        if (entry[0] == 0x00u) {
            if (free_slot != 0 && !free_slot->found) {
                free_slot->lba = sector_lba;
                free_slot->offset = entry_offset;
                free_slot->found = 1;
            }
            *end_seen = 1;
            return 0;
        }
        if (entry[0] == 0xE5u) {
            if (free_slot != 0 && !free_slot->found) {
                free_slot->lba = sector_lba;
                free_slot->offset = entry_offset;
                free_slot->found = 1;
            }
            continue;
        }

        attr = entry[11];
        if ((attr & 0x0Fu) == 0x0Fu || (attr & 0x18u) != 0u) {
            continue;
        }
        if (fat_entry_matches_name(entry, target)) {
            if (fat_parse_dir_entry(entry, file)) {
                file->dir_lba = sector_lba;
                file->dir_offset = entry_offset;
                return 1;
            }
        }
    }

    return 0;
}

static SdCardResult fat_find_file_fat16(const FatInfo *fs,
                                        const unsigned char *target,
                                        FatFile *file,
                                        FatDirSlot *free_slot)
{
    unsigned int sector;
    int end_seen;

    end_seen = 0;
    for (sector = 0u; sector < fs->root_dir_sectors; ++sector) {
        if (!sd_read_sector(fs->root_dir_lba + sector, sd_sector)) {
            return SDCARD_READ_FAILED;
        }
        if (fat_scan_dir_sector(sd_sector,
                                fs->root_dir_lba + sector,
                                target,
                                file,
                                free_slot,
                                &end_seen)) {
            return SDCARD_OK;
        }
        if (end_seen) {
            break;
        }
    }

    return SDCARD_FILE_NOT_FOUND;
}

static SdCardResult fat_find_file_fat32(const FatInfo *fs,
                                        const unsigned char *target,
                                        FatFile *file,
                                        FatDirSlot *free_slot)
{
    unsigned int cluster;
    unsigned int next_cluster;
    unsigned int sector;
    unsigned int chain_guard;
    int end_seen;

    cluster = fs->root_cluster;
    chain_guard = 0u;
    while (cluster >= 2u && !fat_is_end_cluster(fs, cluster) &&
           chain_guard < SD_MAX_CLUSTER_CHAIN) {
        for (sector = 0u; sector < fs->sectors_per_cluster; ++sector) {
            if (!sd_read_sector(fat_cluster_to_lba(fs, cluster) + sector,
                                sd_sector)) {
                return SDCARD_READ_FAILED;
            }
            end_seen = 0;
            if (fat_scan_dir_sector(sd_sector,
                                    fat_cluster_to_lba(fs, cluster) + sector,
                                    target,
                                    file,
                                    free_slot,
                                    &end_seen)) {
                return SDCARD_OK;
            }
            if (end_seen) {
                return SDCARD_FILE_NOT_FOUND;
            }
        }

        if (!fat_read_next_cluster(fs, cluster, &next_cluster)) {
            return SDCARD_READ_FAILED;
        }
        cluster = next_cluster;
        ++chain_guard;
    }

    return SDCARD_FILE_NOT_FOUND;
}

static SdCardResult fat_find_file(const FatInfo *fs,
                                  const unsigned char *target,
                                  FatFile *file,
                                  FatDirSlot *free_slot)
{
    if (fs->fat_type == FAT_TYPE_FAT32) {
        return fat_find_file_fat32(fs, target, file, free_slot);
    }
    return fat_find_file_fat16(fs, target, file, free_slot);
}

static int fat_find_free_cluster(const FatInfo *fs,
                                 unsigned int start_cluster,
                                 unsigned int *free_cluster)
{
    unsigned int cluster;
    unsigned int value;
    unsigned int last_cluster;
    unsigned int entry_size;
    unsigned int fat_offset;
    unsigned int sector_lba;
    unsigned int sector_offset;

    if (start_cluster < 2u) {
        start_cluster = 2u;
    }
    last_cluster = fs->cluster_count + 1u;
    entry_size = (fs->fat_type == FAT_TYPE_FAT32) ? 4u : 2u;
    fat_offset = start_cluster * entry_size;
    sector_lba = fs->fat_lba + (fat_offset / SD_BLOCK_SIZE);
    sector_offset = fat_offset % SD_BLOCK_SIZE;

    cluster = start_cluster;
    while (cluster <= last_cluster) {
        if (!sd_read_sector(sector_lba, sd_sector)) {
            return 0;
        }
        while (sector_offset + entry_size <= SD_BLOCK_SIZE &&
               cluster <= last_cluster) {
            if (fs->fat_type == FAT_TYPE_FAT32) {
                value = le32(&sd_sector[sector_offset]) & 0x0FFFFFFFu;
            } else {
                value = le16(&sd_sector[sector_offset]);
            }
            if (value == 0u) {
                *free_cluster = cluster;
                return 1;
            }
            ++cluster;
            sector_offset += entry_size;
        }
        ++sector_lba;
        sector_offset = 0u;
    }

    return 0;
}

static int fat_allocate_clusters(const FatInfo *fs,
                                 unsigned int cluster_count,
                                 unsigned int *clusters)
{
    unsigned int i;
    unsigned int search_cluster;
    unsigned int cluster;

    search_cluster = 2u;
    for (i = 0u; i < cluster_count; ++i) {
        if (!fat_find_free_cluster(fs, search_cluster, &cluster)) {
            return 0;
        }
        clusters[i] = cluster;
        search_cluster = cluster + 1u;
    }

    return 1;
}

static int fat_link_clusters(const FatInfo *fs,
                             const unsigned int *clusters,
                             unsigned int cluster_count)
{
    unsigned int i;
    unsigned int value;

    for (i = 0u; i < cluster_count; ++i) {
        if (i + 1u < cluster_count) {
            value = clusters[i + 1u];
        } else {
            value = fat_end_cluster_value(fs);
        }
        if (!fat_write_fat_entry(fs, clusters[i], value)) {
            return 0;
        }
    }

    return 1;
}

static int fat_free_chain(const FatInfo *fs, unsigned int first_cluster)
{
    unsigned int cluster;
    unsigned int next_cluster;
    unsigned int guard;

    cluster = first_cluster;
    guard = 0u;
    while (cluster >= 2u &&
           !fat_is_end_cluster(fs, cluster) &&
           guard < SD_MAX_CLUSTER_CHAIN) {
        if (!fat_read_fat_entry(fs, cluster, &next_cluster)) {
            return 0;
        }
        if (!fat_write_fat_entry(fs, cluster, 0u)) {
            return 0;
        }
        if (next_cluster == 0u || fat_is_end_cluster(fs, next_cluster)) {
            break;
        }
        cluster = next_cluster;
        ++guard;
    }

    return 1;
}

static void fat_clear_sector(unsigned char *sector)
{
    unsigned int i;

    for (i = 0u; i < SD_BLOCK_SIZE; ++i) {
        sector[i] = 0u;
    }
}

static int fat_write_file_data(const FatInfo *fs,
                               const unsigned int *clusters,
                               unsigned int cluster_count,
                               const char *buffer,
                               unsigned int length)
{
    unsigned int cluster_index;
    unsigned int sector;
    unsigned int offset;
    unsigned int remaining;
    unsigned int copy_bytes;
    unsigned int i;

    offset = 0u;
    for (cluster_index = 0u;
         cluster_index < cluster_count;
         ++cluster_index) {
        for (sector = 0u; sector < fs->sectors_per_cluster; ++sector) {
            fat_clear_sector(sd_sector);
            if (offset < length) {
                remaining = length - offset;
                copy_bytes = min_u32(remaining, SD_BLOCK_SIZE);
                for (i = 0u; i < copy_bytes; ++i) {
                    sd_sector[i] = (unsigned char)buffer[offset + i];
                }
                offset += copy_bytes;
            }
            if (!sd_write_sector(
                    fat_cluster_to_lba(fs, clusters[cluster_index]) + sector,
                    sd_sector)) {
                return 0;
            }
        }
    }

    return offset == length;
}

static int fat_update_directory_entry(const unsigned char *target,
                                      unsigned int dir_lba,
                                      unsigned int dir_offset,
                                      unsigned int first_cluster,
                                      unsigned int size)
{
    unsigned int i;
    unsigned char *entry;

    if (!sd_read_sector(dir_lba, sd_sector)) {
        return 0;
    }
    entry = &sd_sector[dir_offset];
    for (i = 0u; i < 32u; ++i) {
        entry[i] = 0u;
    }
    for (i = 0u; i < 11u; ++i) {
        entry[i] = target[i];
    }
    entry[11] = 0x20u;
    put_le16(&entry[20], (first_cluster >> 16) & 0xFFFFu);
    put_le16(&entry[26], first_cluster & 0xFFFFu);
    put_le32(&entry[28], size);

    return sd_write_sector(dir_lba, sd_sector);
}

static SdCardResult fat_write_file(const FatInfo *fs,
                                   const unsigned char *target,
                                   const char *buffer,
                                   unsigned int length,
                                   int overwrite)
{
    FatFile existing;
    FatDirSlot free_slot;
    SdCardResult find_result;
    unsigned int cluster_bytes;
    unsigned int needed_clusters;
    unsigned int clusters[SD_MAX_WRITE_CLUSTERS];
    unsigned int first_cluster;
    unsigned int dir_lba;
    unsigned int dir_offset;

    free_slot.lba = 0u;
    free_slot.offset = 0u;
    free_slot.found = 0;
    existing.first_cluster = 0u;
    existing.size = 0u;
    existing.dir_lba = 0u;
    existing.dir_offset = 0u;

    find_result = fat_find_file(fs, target, &existing, &free_slot);
    if (find_result != SDCARD_OK &&
        find_result != SDCARD_FILE_NOT_FOUND) {
        return find_result;
    }
    if (find_result == SDCARD_OK && !overwrite) {
        return SDCARD_FILE_EXISTS;
    }
    if (find_result == SDCARD_FILE_NOT_FOUND && !free_slot.found) {
        return SDCARD_NO_SPACE;
    }

    cluster_bytes = (unsigned int)fs->sectors_per_cluster * SD_BLOCK_SIZE;
    needed_clusters = 0u;
    first_cluster = 0u;
    if (length > 0u) {
        needed_clusters = (length + cluster_bytes - 1u) / cluster_bytes;
        if (needed_clusters == 0u ||
            needed_clusters > SD_MAX_WRITE_CLUSTERS) {
            return SDCARD_NO_SPACE;
        }
        if (!fat_allocate_clusters(fs, needed_clusters, clusters)) {
            return SDCARD_NO_SPACE;
        }
        if (!fat_link_clusters(fs, clusters, needed_clusters)) {
            (void)fat_free_chain(fs, clusters[0]);
            return SDCARD_WRITE_FAILED;
        }
        if (!fat_write_file_data(fs,
                                 clusters,
                                 needed_clusters,
                                 buffer,
                                 length)) {
            (void)fat_free_chain(fs, clusters[0]);
            return SDCARD_WRITE_FAILED;
        }
        first_cluster = clusters[0];
    }

    if (find_result == SDCARD_OK) {
        dir_lba = existing.dir_lba;
        dir_offset = existing.dir_offset;
    } else {
        dir_lba = free_slot.lba;
        dir_offset = free_slot.offset;
    }

    if (!fat_update_directory_entry(target,
                                    dir_lba,
                                    dir_offset,
                                    first_cluster,
                                    length)) {
        if (first_cluster >= 2u) {
            (void)fat_free_chain(fs, first_cluster);
        }
        return SDCARD_WRITE_FAILED;
    }

    if (find_result == SDCARD_OK && existing.first_cluster >= 2u) {
        (void)fat_free_chain(fs, existing.first_cluster);
    }

    return SDCARD_OK;
}

static SdCardResult fat_read_file(const FatInfo *fs,
                                  const FatFile *file,
                                  char *buffer,
                                  unsigned int buffer_size,
                                  unsigned int *length)
{
    unsigned int cluster;
    unsigned int next_cluster;
    unsigned int sector;
    unsigned int remaining;
    unsigned int sector_bytes;
    unsigned int copy_bytes;
    unsigned int copied;
    unsigned int i;
    unsigned int chain_guard;
    SdCardResult result;

    buffer[0] = '\0';
    *length = 0u;
    if (buffer_size <= 1u) {
        return SDCARD_OK_TRUNCATED;
    }
    if (file->size == 0u) {
        return SDCARD_OK;
    }
    if (file->first_cluster < 2u) {
        return SDCARD_READ_FAILED;
    }

    cluster = file->first_cluster;
    remaining = file->size;
    copied = 0u;
    chain_guard = 0u;
    result = SDCARD_OK;

    while (remaining > 0u && cluster >= 2u &&
           !fat_is_end_cluster(fs, cluster) &&
           chain_guard < SD_MAX_CLUSTER_CHAIN) {
        for (sector = 0u;
             sector < fs->sectors_per_cluster && remaining > 0u;
             ++sector) {
            if (!sd_read_sector(fat_cluster_to_lba(fs, cluster) + sector,
                                sd_sector)) {
                return SDCARD_READ_FAILED;
            }

            sector_bytes = min_u32(remaining, SD_BLOCK_SIZE);
            copy_bytes = min_u32(sector_bytes, (buffer_size - 1u) - copied);
            for (i = 0u; i < copy_bytes; ++i) {
                buffer[copied + i] = (char)sd_sector[i];
            }
            copied += copy_bytes;
            if (copy_bytes < sector_bytes) {
                buffer[copied] = '\0';
                *length = copied;
                return SDCARD_OK_TRUNCATED;
            }
            remaining -= sector_bytes;

            if (copied >= buffer_size - 1u && remaining > 0u) {
                buffer[copied] = '\0';
                *length = copied;
                return SDCARD_OK_TRUNCATED;
            }
        }

        if (remaining > 0u) {
            if (!fat_read_next_cluster(fs, cluster, &next_cluster)) {
                return SDCARD_READ_FAILED;
            }
            cluster = next_cluster;
            ++chain_guard;
        }
    }

    if (remaining > 0u) {
        result = SDCARD_READ_FAILED;
    }

    buffer[copied] = '\0';
    *length = copied;
    return result;
}
#endif

const char *sdcard_result_text(SdCardResult result)
{
    switch (result) {
    case SDCARD_OK:
        return "SD read OK";
    case SDCARD_OK_TRUNCATED:
        return "Read truncated";
    case SDCARD_NO_SPI:
        return "Update BSP first";
    case SDCARD_INIT_FAILED:
        return "SD init failed";
    case SDCARD_READ_FAILED:
        return "SD read failed";
    case SDCARD_WRITE_FAILED:
        return "SD write failed";
    case SDCARD_UNSUPPORTED_FS:
        return "FAT unsupported";
    case SDCARD_FILE_NOT_FOUND:
        return "File missing";
    case SDCARD_FILE_EXISTS:
        return "File exists";
    case SDCARD_NO_SPACE:
        return "SD full";
    default:
        return "SD error";
    }
}

static SdCardResult sdcard_read_text_with_activity(
    const unsigned char *target,
    char *buffer,
    unsigned int buffer_size,
    unsigned int *length,
    SdCardActivityCallback activity,
    void *activity_context)
{
#if defined(SPI_SDCARD_BASE)
    FatInfo fs;
    FatFile file;
    SdCardResult result;

    if (length != 0) {
        *length = 0u;
    }
    if (buffer == 0 || buffer_size == 0u || length == 0) {
        return SDCARD_READ_FAILED;
    }
    buffer[0] = '\0';

    sd_set_activity(activity, activity_context);
    sd_report_activity();

    if (!sd_init()) {
        sd_clear_activity();
        return SDCARD_INIT_FAILED;
    }

    result = fat_mount(&fs);
    if (result != SDCARD_OK) {
        sd_clear_activity();
        return result;
    }

    result = fat_find_file(&fs, target, &file, 0);
    if (result != SDCARD_OK) {
        sd_clear_activity();
        return result;
    }

    result = fat_read_file(&fs, &file, buffer, buffer_size, length);
    sd_clear_activity();
    return result;
#else
    (void)target;
    if (length != 0) {
        *length = 0u;
    }
    if (buffer != 0 && buffer_size > 0u) {
        buffer[0] = '\0';
    }
    if (activity != 0) {
        activity(0u, activity_context);
    }
    return SDCARD_NO_SPI;
#endif
}

SdCardResult sdcard_read_question_text_with_activity(
    char *buffer,
    unsigned int buffer_size,
    unsigned int *length,
    SdCardActivityCallback activity,
    void *activity_context)
{
    return sdcard_read_text_with_activity(fat_question_name,
                                          buffer,
                                          buffer_size,
                                          length,
                                          activity,
                                          activity_context);
}

SdCardResult sdcard_read_editor_text_with_activity(
    char *buffer,
    unsigned int buffer_size,
    unsigned int *length,
    SdCardActivityCallback activity,
    void *activity_context)
{
    return sdcard_read_text_with_activity(fat_editor_name,
                                          buffer,
                                          buffer_size,
                                          length,
                                          activity,
                                          activity_context);
}

SdCardResult sdcard_write_editor_text_with_activity(
    const char *buffer,
    unsigned int length,
    int overwrite,
    SdCardActivityCallback activity,
    void *activity_context)
{
#if defined(SPI_SDCARD_BASE)
    FatInfo fs;
    SdCardResult result;

    if (buffer == 0 && length > 0u) {
        return SDCARD_WRITE_FAILED;
    }

    sd_set_activity(activity, activity_context);
    sd_report_activity();

    if (!sd_init()) {
        sd_clear_activity();
        return SDCARD_INIT_FAILED;
    }

    result = fat_mount(&fs);
    if (result != SDCARD_OK) {
        sd_clear_activity();
        return result;
    }

    result = fat_write_file(&fs,
                            fat_editor_name,
                            buffer,
                            length,
                            overwrite);
    sd_clear_activity();
    return result;
#else
    (void)buffer;
    (void)length;
    (void)overwrite;
    if (activity != 0) {
        activity(0u, activity_context);
    }
    return SDCARD_NO_SPI;
#endif
}

SdCardResult sdcard_read_question_text(char *buffer,
                                       unsigned int buffer_size,
                                       unsigned int *length)
{
    return sdcard_read_question_text_with_activity(buffer,
                                                  buffer_size,
                                                  length,
                                                  0,
                                                  0);
}
