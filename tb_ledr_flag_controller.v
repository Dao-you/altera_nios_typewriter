`timescale 1ns/1ps

module tb_ledr_flag_controller;

    localparam integer CLK_HALF_NS = 5;

    reg clk;
    reg reset_n;
    reg [17:0] nios_ledr;
    reg [7:0] flag;
    reg [7:0] phase;

    wire [17:0] ledr;

    integer errors;
    integer sample_index;

    ledr_flag_controller #(
        .CLOCK_HZ(40),
        .MARQUEE_STEP_HZ(4),
        .CONFIRM_BLINK_HZ(2),
        .ERROR_BLINK_HZ(5)
    ) dut (
        .clk(clk),
        .reset_n(reset_n),
        .nios_ledr(nios_ledr),
        .flag(flag),
        .ledr(ledr)
    );

    always #CLK_HALF_NS clk = ~clk;

    task apply_reset;
        begin
            reset_n = 1'b0;
            repeat (3) @(posedge clk);
            reset_n = 1'b1;
            @(negedge clk);
        end
    endtask

    task check_ledr;
        input [17:0] expected;
        begin
            #1;
            if (ledr !== expected) begin
                $display(
                    "ERROR: phase=%0d ledr=0x%05h expected=0x%05h",
                    phase,
                    ledr,
                    expected
                );
                errors = errors + 1;
            end
        end
    endtask

    initial begin
        clk = 1'b0;
        reset_n = 1'b0;
        nios_ledr = 18'h00000;
        flag = 8'h00;
        phase = 8'd0;
        errors = 0;

        /* bit0 selects the Nios LEDR value directly. */
        apply_reset();
        phase = 8'd1;
        nios_ledr = 18'h2A55A;
        flag = 8'h01;
        repeat (6) begin
            @(negedge clk);
            check_ledr(18'h2A55A);
        end

        /* bit1: LEDR17 toward LEDR0 marquee. */
        apply_reset();
        phase = 8'd2;
        flag = 8'h02;
        for (sample_index = 0; sample_index < 24; sample_index = sample_index + 1) begin
            @(negedge clk);
            check_ledr(18'b1 << (5'd17 - dut.marquee_index));
        end

        /* bit2: LEDR0 toward LEDR17 marquee. */
        apply_reset();
        phase = 8'd3;
        flag = 8'h04;
        for (sample_index = 0; sample_index < 24; sample_index = sample_index + 1) begin
            @(negedge clk);
            check_ledr(18'b1 << dut.marquee_index);
        end

        /* bit3: confirmation blink, half-period is 10 test clocks. */
        apply_reset();
        phase = 8'd4;
        flag = 8'h08;
        for (sample_index = 0; sample_index < 24; sample_index = sample_index + 1) begin
            @(negedge clk);
            check_ledr({18{dut.confirm_blink_on}});
        end

        /* bit4: error blink, half-period is 4 test clocks. */
        apply_reset();
        phase = 8'd5;
        flag = 8'h10;
        for (sample_index = 0; sample_index < 20; sample_index = sample_index + 1) begin
            @(negedge clk);
            check_ledr({18{dut.error_blink_on}});
        end

        /* Without bit0, error blink has priority over confirm blink. */
        apply_reset();
        phase = 8'd6;
        flag = 8'h18;
        for (sample_index = 0; sample_index < 16; sample_index = sample_index + 1) begin
            @(negedge clk);
            check_ledr({18{dut.error_blink_on}});
        end

        /* With bit0 set, Nios source selection overrides effect flags. */
        apply_reset();
        phase = 8'd7;
        nios_ledr = 18'h15555;
        flag = 8'h19;
        repeat (12) begin
            @(negedge clk);
            check_ledr(18'h15555);
        end

        phase = 8'd8;
        if (errors == 0) begin
            $display("PASS: ledr_flag_controller testbench");
        end else begin
            $display("FAIL: ledr_flag_controller testbench, errors=%0d", errors);
        end

        #20;
        $finish;
    end

endmodule
