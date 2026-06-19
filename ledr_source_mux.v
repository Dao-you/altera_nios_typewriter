module ledr_source_mux #(
    parameter WIDTH = 18
) (
    input  [WIDTH-1:0] verilog_ledr,
    input  [WIDTH-1:0] nios_ledr,
    input              select_nios,
    output [WIDTH-1:0] ledr
);

    genvar i;

    generate
        for (i = 0; i < WIDTH; i = i + 1) begin : mux_bits
            hw03_Mux41 bit_mux (
                .I3(1'b0),
                .I2(1'b0),
                .S1(1'b0),
                .S0(select_nios),
                .I1(nios_ledr[i]),
                .I0(verilog_ledr[i]),
                .Y(ledr[i])
            );
        end
    endgenerate

endmodule
