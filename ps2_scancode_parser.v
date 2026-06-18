module ps2_scancode_parser (
    input        clk,
    input        reset_n,
    input        scan_code_valid,
    input  [7:0] scan_code,
    output reg       make_valid,
    output reg [7:0] make_code,
    output reg       make_extended,
    output           shift_active,
    output reg       caps_lock
);

    localparam SCAN_EXTENDED  = 8'hE0;
    localparam SCAN_RELEASE   = 8'hF0;
    localparam SCAN_LEFT_SHIFT  = 8'h12;
    localparam SCAN_RIGHT_SHIFT = 8'h59;
    localparam SCAN_CAPS_LOCK   = 8'h58;

    reg extended_prefix;
    reg release_prefix;
    reg left_shift_down;
    reg right_shift_down;
    reg caps_lock_down;

    assign shift_active = left_shift_down | right_shift_down;

    always @(posedge clk or negedge reset_n) begin
        if (!reset_n) begin
            make_valid <= 1'b0;
            make_code <= 8'h00;
            make_extended <= 1'b0;
            caps_lock <= 1'b0;
            extended_prefix <= 1'b0;
            release_prefix <= 1'b0;
            left_shift_down <= 1'b0;
            right_shift_down <= 1'b0;
            caps_lock_down <= 1'b0;
        end else begin
            make_valid <= 1'b0;

            if (scan_code_valid) begin
                if (scan_code == SCAN_EXTENDED) begin
                    extended_prefix <= 1'b1;
                end else if (scan_code == SCAN_RELEASE) begin
                    release_prefix <= 1'b1;
                end else if (release_prefix) begin
                    if (!extended_prefix && scan_code == SCAN_LEFT_SHIFT) begin
                        left_shift_down <= 1'b0;
                    end else if (!extended_prefix && scan_code == SCAN_RIGHT_SHIFT) begin
                        right_shift_down <= 1'b0;
                    end else if (!extended_prefix && scan_code == SCAN_CAPS_LOCK) begin
                        caps_lock_down <= 1'b0;
                    end

                    release_prefix <= 1'b0;
                    extended_prefix <= 1'b0;
                end else begin
                    if (!extended_prefix && scan_code == SCAN_LEFT_SHIFT) begin
                        left_shift_down <= 1'b1;
                    end else if (!extended_prefix && scan_code == SCAN_RIGHT_SHIFT) begin
                        right_shift_down <= 1'b1;
                    end else if (!extended_prefix && scan_code == SCAN_CAPS_LOCK) begin
                        if (!caps_lock_down) begin
                            caps_lock <= ~caps_lock;
                        end
                        caps_lock_down <= 1'b1;
                    end else begin
                        make_code <= scan_code;
                        make_extended <= extended_prefix;
                        make_valid <= 1'b1;
                    end

                    extended_prefix <= 1'b0;
                end
            end
        end
    end

endmodule
