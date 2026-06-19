module top (
    input         CLOCK_50,
    input  [17:0] SW,
    input  [3:0]  KEY,
    output [17:0] LEDR,
    output [7:0]  LEDG,
    output [6:0]  HEX0,
    output [6:0]  HEX1,
    output [6:0]  HEX2,
    output [6:0]  HEX3,
    output [6:0]  HEX4,
    output [6:0]  HEX5,
    output [6:0]  HEX6,
    output [6:0]  HEX7,
    output [7:0]  LCD_DATA,
    output        LCD_EN,
    output        LCD_ON,
    output        LCD_RS,
    output        LCD_RW,
    output        LCD_BLON,
    output        EEP_I2C_SCLK,
    inout         EEP_I2C_SDAT,
    input         PS2_CLK,
    input         PS2_DAT,
    output        SD_CLK,
    output        SD_CMD,
    inout  [3:0]  SD_DAT,
    input         SD_WP_N,
    input         UART_RXD,
    output        UART_TXD
);

    wire [7:0] hex0_export;
    wire [7:0] hex1_export;
    wire [7:0] hex2_export;
    wire [7:0] hex3_export;
    wire [7:0] hex4_export;
    wire [7:0] hex5_export;
    wire [7:0] hex6_export;
    wire [7:0] hex7_export;

    wire [7:0] lcd_data_export;
    wire [4:0] lcd_ctrl_export;

    wire eep_sda_in;
    wire eep_sda_oe;
    wire eep_sda_out;
    wire eep_scl;

    wire [7:0] keyboard_data_export;
    wire [7:0] keyboard_status_export;
    wire keyboard_ack_export;

    wire [17:0] ledr_export;
    wire [7:0] ledr_flag_export;

    wire sd_miso;
    wire sd_mosi;
    wire sd_sclk;
    wire sd_ss_n;

    wire reset_n;

    // SW[15] is the board-level reset: SW15=0 resets Nios II, SW15=1 runs.
    // SW[16] remains available to software for Insert / Overwrite mode.
    assign reset_n = SW[15];

    assign HEX0 = hex0_export[6:0];
    assign HEX1 = hex1_export[6:0];
    assign HEX2 = hex2_export[6:0];
    assign HEX3 = hex3_export[6:0];
    assign HEX4 = hex4_export[6:0];
    assign HEX5 = hex5_export[6:0];
    assign HEX6 = hex6_export[6:0];
    assign HEX7 = hex7_export[6:0];

    assign LCD_DATA = lcd_data_export;
    assign LCD_RS   = lcd_ctrl_export[0];
    assign LCD_RW   = lcd_ctrl_export[1];
    assign LCD_EN   = lcd_ctrl_export[2];
    assign LCD_ON   = lcd_ctrl_export[3];
    assign LCD_BLON = lcd_ctrl_export[4];

    assign EEP_I2C_SCLK = eep_scl;
    assign eep_sda_in = EEP_I2C_SDAT;
    assign EEP_I2C_SDAT = eep_sda_oe ? (eep_sda_out ? 1'bz : 1'b0) : 1'bz;

    // SD card socket in SPI mode:
    // CMD is MOSI, DAT0 is MISO, and DAT3 is chip select.
    assign SD_CLK = sd_sclk;
    assign SD_CMD = sd_mosi;
    assign sd_miso = SD_DAT[0];
    assign SD_DAT[0] = 1'bz;
    assign SD_DAT[1] = 1'bz;
    assign SD_DAT[2] = 1'bz;
    assign SD_DAT[3] = sd_ss_n;

    assign UART_TXD = 1'b1;

    ledr_flag_controller ledr0 (
        .clk(CLOCK_50),
        .reset_n(reset_n),
        .nios_ledr(ledr_export),
        .flag(ledr_flag_export),
        .ledr(LEDR)
    );

    ps2_keyboard_controller keyboard0 (
        .clk(CLOCK_50),
        .reset_n(reset_n),
        .ps2_clk(PS2_CLK),
        .ps2_dat(PS2_DAT),
        .keyboard_ack(keyboard_ack_export),
        .keyboard_data(keyboard_data_export),
        .keyboard_status(keyboard_status_export)
    );

    nios u0 (
        .clk_clk                                        (CLOCK_50),
        .reset_reset_n                                  (reset_n),
        .pio_in_eep_sda_in_external_connection_export   (eep_sda_in),
        .pio_out_eep_sda_oe_external_connection_export  (eep_sda_oe),
        .pio_out_eep_sda_out_external_connection_export (eep_sda_out),
        .pio_out_eep_scl_external_connection_export     (eep_scl),
        .pio_out_lcd_ctrl_external_connection_export    (lcd_ctrl_export),
        .pio_out_lcd_data_external_connection_export    (lcd_data_export),
        .pio_out_hex7_external_connection_export        (hex7_export),
        .pio_out_hex6_external_connection_export        (hex6_export),
        .pio_out_hex5_external_connection_export        (hex5_export),
        .pio_out_hex4_external_connection_export        (hex4_export),
        .pio_out_hex3_external_connection_export        (hex3_export),
        .pio_out_hex2_external_connection_export        (hex2_export),
        .pio_out_hex1_external_connection_export        (hex1_export),
        .pio_out_hex0_external_connection_export        (hex0_export),
        .pio_out_ledg_external_connection_export        (LEDG),
        .pio_out_ledr_external_connection_export        (ledr_export),
        .pio_in_key_external_connection_export          (KEY),
        .pio_in_sw_external_connection_export           (SW),
        .pio_in_keyboard_data_external_connection_export (keyboard_data_export),
        .pio_in_keyboard_status_external_connection_export (keyboard_status_export),
        .pio_out_keyboard_ack_external_connection_export (keyboard_ack_export),
        .spi_sdcard_external_MISO                       (sd_miso),
        .spi_sdcard_external_MOSI                       (sd_mosi),
        .spi_sdcard_external_SCLK                       (sd_sclk),
        .spi_sdcard_external_SS_n                       (sd_ss_n),
        .pio_out_ledr_flag_external_connection_export   (ledr_flag_export)
    );

endmodule
