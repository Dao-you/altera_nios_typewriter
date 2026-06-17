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

    assign UART_TXD = 1'b1;

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
        .pio_out_ledr_external_connection_export        (LEDR),
        .pio_in_key_external_connection_export          (KEY),
        .pio_in_sw_external_connection_export           (SW)
    );

endmodule
