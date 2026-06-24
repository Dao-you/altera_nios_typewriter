`timescale 1ns/1ps

module tb_ps2_keyboard_controller;

    localparam integer CLK_HALF_NS = 10;
    localparam integer PS2_HALF_NS = 400;

    reg clk;
    reg reset_n;
    reg ps2_clk;
    reg ps2_dat;
    reg keyboard_ack;
    reg [7:0] phase;

    wire [7:0] keyboard_data;
    wire [7:0] keyboard_status;
    wire keyboard_detected;
    wire keyboard_activity;
    wire keyboard_caps_lock;

    integer errors;
    integer timeout_cycles;

    ps2_keyboard_controller #(
        .INPUT_ACTIVITY_HOLD_CYCLES(23'd50)
    ) dut (
        .clk(clk),
        .reset_n(reset_n),
        .ps2_clk(ps2_clk),
        .ps2_dat(ps2_dat),
        .keyboard_ack(keyboard_ack),
        .keyboard_data(keyboard_data),
        .keyboard_status(keyboard_status),
        .keyboard_detected(keyboard_detected),
        .keyboard_activity(keyboard_activity),
        .keyboard_caps_lock(keyboard_caps_lock)
    );

    always #CLK_HALF_NS clk = ~clk;

    task apply_reset;
        begin
            reset_n = 1'b0;
            keyboard_ack = 1'b0;
            ps2_clk = 1'b1;
            ps2_dat = 1'b1;
            repeat (8) @(posedge clk);
            reset_n = 1'b1;
            repeat (8) @(posedge clk);
        end
    endtask

    task send_ps2_bit;
        input bit_value;
        begin
            ps2_dat = bit_value;
            #PS2_HALF_NS;
            ps2_clk = 1'b0;
            #PS2_HALF_NS;
            ps2_clk = 1'b1;
            #PS2_HALF_NS;
        end
    endtask

    task send_ps2_frame;
        input [7:0] data;
        input bad_parity;
        reg parity;
        integer bit_index;
        begin
            parity = ~(^data);
            if (bad_parity) begin
                parity = ~parity;
            end

            send_ps2_bit(1'b0);
            for (bit_index = 0; bit_index < 8; bit_index = bit_index + 1) begin
                send_ps2_bit(data[bit_index]);
            end
            send_ps2_bit(parity);
            send_ps2_bit(1'b1);

            ps2_dat = 1'b1;
            ps2_clk = 1'b1;
            #800;
        end
    endtask

    task expect_fifo_byte;
        input [7:0] expected;
        begin
            timeout_cycles = 0;
            while ((keyboard_status[0] !== 1'b1) && (timeout_cycles < 200)) begin
                @(posedge clk);
                timeout_cycles = timeout_cycles + 1;
            end

            if (keyboard_status[0] !== 1'b1) begin
                $display("ERROR: FIFO timeout, expected 0x%02h", expected);
                errors = errors + 1;
            end else if (keyboard_data !== expected) begin
                $display(
                    "ERROR: FIFO data 0x%02h, expected 0x%02h",
                    keyboard_data,
                    expected
                );
                errors = errors + 1;
            end

            keyboard_ack = 1'b1;
            repeat (3) @(posedge clk);
            keyboard_ack = 1'b0;
            repeat (4) @(posedge clk);

            if (keyboard_status[0] !== 1'b0) begin
                $display("ERROR: FIFO did not become empty after ACK");
                errors = errors + 1;
            end
        end
    endtask

    task expect_fifo_empty;
        begin
            repeat (8) @(posedge clk);
            if (keyboard_status[0] !== 1'b0) begin
                $display(
                    "ERROR: modifier/prefix unexpectedly generated FIFO data 0x%02h",
                    keyboard_data
                );
                errors = errors + 1;
                keyboard_ack = 1'b1;
                repeat (3) @(posedge clk);
                keyboard_ack = 1'b0;
            end
        end
    endtask

    initial begin
        clk = 1'b0;
        reset_n = 1'b0;
        ps2_clk = 1'b1;
        ps2_dat = 1'b1;
        keyboard_ack = 1'b0;
        phase = 8'd0;
        errors = 0;

        apply_reset();

        /* Plain A make code -> lowercase ASCII 'a'. */
        phase = 8'd1;
        send_ps2_frame(8'h1C, 1'b0);
        expect_fifo_byte(8'h61);

        if (!keyboard_detected) begin
            $display("ERROR: keyboard_detected was not latched");
            errors = errors + 1;
        end

        /* Shift itself is a modifier and must not enter the FIFO. */
        phase = 8'd2;
        send_ps2_frame(8'h12, 1'b0);
        expect_fifo_empty();

        /* Shift + A -> uppercase ASCII 'A'. */
        phase = 8'd3;
        send_ps2_frame(8'h1C, 1'b0);
        expect_fifo_byte(8'h41);

        /* Release left Shift: F0 12. */
        phase = 8'd4;
        send_ps2_frame(8'hF0, 1'b0);
        send_ps2_frame(8'h12, 1'b0);
        expect_fifo_empty();

        /* Toggle Caps Lock and release it. */
        phase = 8'd5;
        send_ps2_frame(8'h58, 1'b0);
        send_ps2_frame(8'hF0, 1'b0);
        send_ps2_frame(8'h58, 1'b0);
        expect_fifo_empty();

        if (keyboard_caps_lock !== 1'b1) begin
            $display("ERROR: Caps Lock did not toggle on");
            errors = errors + 1;
        end

        /* Caps Lock + A -> uppercase ASCII 'A'. */
        phase = 8'd6;
        send_ps2_frame(8'h1C, 1'b0);
        expect_fifo_byte(8'h41);

        /* Extended left arrow: E0 6B -> internal editor code 0x80. */
        phase = 8'd7;
        send_ps2_frame(8'hE0, 1'b0);
        send_ps2_frame(8'h6B, 1'b0);
        expect_fifo_byte(8'h80);

        /* Bad parity must latch receiver error into keyboard_status[2]. */
        phase = 8'd8;
        send_ps2_frame(8'h1C, 1'b1);
        repeat (20) @(posedge clk);

        if (keyboard_status[2] !== 1'b1) begin
            $display("ERROR: bad parity did not latch keyboard_status[2]");
            errors = errors + 1;
        end

        if (keyboard_status[0] !== 1'b0) begin
            $display("ERROR: bad frame unexpectedly generated FIFO data");
            errors = errors + 1;
        end

        /* Let the shortened activity timer expire for a visible falling edge. */
        phase = 8'd9;
        repeat (80) @(posedge clk);

        if (keyboard_activity !== 1'b0) begin
            $display("ERROR: keyboard_activity did not expire");
            errors = errors + 1;
        end

        phase = 8'd10;
        if (errors == 0) begin
            $display("PASS: ps2_keyboard_controller testbench");
        end else begin
            $display("FAIL: ps2_keyboard_controller testbench, errors=%0d", errors);
        end

        #200;
        $finish;
    end

endmodule
