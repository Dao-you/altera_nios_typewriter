module ps2_receiver (
    input        clk,
    input        reset_n,
    input        ps2_clk,
    input        ps2_dat,
    output reg [7:0] scan_code,
    output reg       scan_code_valid,
    output reg       frame_error
);

    parameter TIMEOUT_CYCLES = 16'd50000;

    reg [7:0] ps2_clk_filter;
    reg       ps2_clk_filtered;
    reg       ps2_clk_previous;
    reg [2:0] ps2_dat_sync;
    reg [3:0] bit_count;
    reg [7:0] scan_shift;
    reg       parity_bit;
    reg [15:0] timeout_count;

    wire ps2_clk_falling;
    wire ps2_data_sample;

    assign ps2_clk_falling = ps2_clk_previous & ~ps2_clk_filtered;
    assign ps2_data_sample = ps2_dat_sync[2];

    always @(posedge clk or negedge reset_n) begin
        if (!reset_n) begin
            ps2_clk_filter <= 8'hFF;
            ps2_clk_filtered <= 1'b1;
            ps2_clk_previous <= 1'b1;
            ps2_dat_sync <= 3'b111;
            bit_count <= 4'd0;
            scan_shift <= 8'h00;
            parity_bit <= 1'b0;
            timeout_count <= 16'd0;
            scan_code <= 8'h00;
            scan_code_valid <= 1'b0;
            frame_error <= 1'b0;
        end else begin
            scan_code_valid <= 1'b0;
            frame_error <= 1'b0;

            ps2_clk_filter <= {ps2_clk_filter[6:0], ps2_clk};
            ps2_dat_sync <= {ps2_dat_sync[1:0], ps2_dat};

            if (ps2_clk_filter == 8'hFF) begin
                ps2_clk_filtered <= 1'b1;
            end else if (ps2_clk_filter == 8'h00) begin
                ps2_clk_filtered <= 1'b0;
            end
            ps2_clk_previous <= ps2_clk_filtered;

            if (ps2_clk_falling) begin
                timeout_count <= 16'd0;

                case (bit_count)
                    4'd0: begin
                        if (!ps2_data_sample) begin
                            bit_count <= 4'd1;
                        end else begin
                            frame_error <= 1'b1;
                        end
                    end

                    4'd1,
                    4'd2,
                    4'd3,
                    4'd4,
                    4'd5,
                    4'd6,
                    4'd7,
                    4'd8: begin
                        scan_shift <= {ps2_data_sample, scan_shift[7:1]};
                        bit_count <= bit_count + 4'd1;
                    end

                    4'd9: begin
                        parity_bit <= ps2_data_sample;
                        bit_count <= 4'd10;
                    end

                    4'd10: begin
                        if (ps2_data_sample && ((^scan_shift) ^ parity_bit)) begin
                            scan_code <= scan_shift;
                            scan_code_valid <= 1'b1;
                        end else begin
                            frame_error <= 1'b1;
                        end
                        bit_count <= 4'd0;
                    end

                    default: begin
                        bit_count <= 4'd0;
                        frame_error <= 1'b1;
                    end
                endcase
            end else if (bit_count != 4'd0) begin
                if (timeout_count >= TIMEOUT_CYCLES) begin
                    bit_count <= 4'd0;
                    timeout_count <= 16'd0;
                    frame_error <= 1'b1;
                end else begin
                    timeout_count <= timeout_count + 16'd1;
                end
            end else begin
                timeout_count <= 16'd0;
            end
        end
    end

endmodule
