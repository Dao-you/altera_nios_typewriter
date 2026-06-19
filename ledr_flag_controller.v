module ledr_flag_controller #(
    parameter CLOCK_HZ = 50000000,
    parameter MARQUEE_STEP_HZ = 8,
    parameter CONFIRM_BLINK_HZ = 2,
    parameter ERROR_BLINK_HZ = 5
) (
    input         clk,
    input         reset_n,
    input  [17:0] nios_ledr,
    input  [7:0]  flag,
    output [17:0] ledr
);

    localparam [7:0] LEDR_FLAG_NIOS_CONTROL       = 8'h01;
    localparam [7:0] LEDR_FLAG_MARQUEE_LEFT_RIGHT = 8'h02;
    localparam [7:0] LEDR_FLAG_MARQUEE_RIGHT_LEFT = 8'h04;
    localparam [7:0] LEDR_FLAG_CONFIRM_BLINK      = 8'h08;
    localparam [7:0] LEDR_FLAG_ERROR_BLINK        = 8'h10;

    localparam integer MARQUEE_DIVISOR = CLOCK_HZ / MARQUEE_STEP_HZ;
    localparam integer CONFIRM_HALF_DIVISOR = CLOCK_HZ / (CONFIRM_BLINK_HZ * 2);
    localparam integer ERROR_HALF_DIVISOR = CLOCK_HZ / (ERROR_BLINK_HZ * 2);

    reg [31:0] marquee_counter;
    reg [31:0] confirm_counter;
    reg [31:0] error_counter;
    reg [4:0]  marquee_index;
    reg        confirm_blink_on;
    reg        error_blink_on;
    reg [17:0] verilog_ledr;

    always @(posedge clk or negedge reset_n) begin
        if (!reset_n) begin
            marquee_counter <= 32'd0;
            confirm_counter <= 32'd0;
            error_counter <= 32'd0;
            marquee_index <= 5'd0;
            confirm_blink_on <= 1'b0;
            error_blink_on <= 1'b0;
        end else begin
            if (marquee_counter >= MARQUEE_DIVISOR - 1) begin
                marquee_counter <= 32'd0;
                if (marquee_index >= 5'd17) begin
                    marquee_index <= 5'd0;
                end else begin
                    marquee_index <= marquee_index + 5'd1;
                end
            end else begin
                marquee_counter <= marquee_counter + 32'd1;
            end

            if (confirm_counter >= CONFIRM_HALF_DIVISOR - 1) begin
                confirm_counter <= 32'd0;
                confirm_blink_on <= ~confirm_blink_on;
            end else begin
                confirm_counter <= confirm_counter + 32'd1;
            end

            if (error_counter >= ERROR_HALF_DIVISOR - 1) begin
                error_counter <= 32'd0;
                error_blink_on <= ~error_blink_on;
            end else begin
                error_counter <= error_counter + 32'd1;
            end
        end
    end

    always @* begin
        if ((flag & LEDR_FLAG_ERROR_BLINK) != 8'd0) begin
            verilog_ledr = {18{error_blink_on}};
        end else if ((flag & LEDR_FLAG_CONFIRM_BLINK) != 8'd0) begin
            verilog_ledr = {18{confirm_blink_on}};
        end else if ((flag & LEDR_FLAG_MARQUEE_RIGHT_LEFT) != 8'd0) begin
            verilog_ledr = 18'b1 << marquee_index;
        end else if ((flag & LEDR_FLAG_MARQUEE_LEFT_RIGHT) != 8'd0) begin
            verilog_ledr = 18'b1 << (5'd17 - marquee_index);
        end else begin
            verilog_ledr = 18'd0;
        end
    end

    ledr_source_mux #(
        .WIDTH(18)
    ) source_mux (
        .verilog_ledr(verilog_ledr),
        .nios_ledr(nios_ledr),
        .select_nios((flag & LEDR_FLAG_NIOS_CONTROL) != 8'd0),
        .ledr(ledr)
    );

endmodule
