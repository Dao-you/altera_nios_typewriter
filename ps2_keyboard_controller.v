module ps2_keyboard_controller #(
    parameter [22:0] INPUT_ACTIVITY_HOLD_CYCLES = 23'd5000000
) (
    input        clk,
    input        reset_n,
    input        ps2_clk,
    input        ps2_dat,
    input        keyboard_ack,
    output [7:0] keyboard_data,
    output [7:0] keyboard_status,
    output reg   keyboard_detected,
    output reg   keyboard_activity,
    output       keyboard_caps_lock
);

    wire [7:0] scan_code;
    wire       scan_code_valid;
    wire       frame_error;
    wire [7:0] make_code;
    wire       make_valid;
    wire       make_extended;
    wire       shift_active;
    wire       caps_lock;
    wire [7:0] mapped_code;
    wire       mapped_valid;
    wire [7:0] fifo_data;
    wire       fifo_empty;
    wire       fifo_full;
    wire       fifo_overflow;
    wire       fifo_pop;
    reg        receiver_error_latched;
    reg [22:0] input_activity_count;

    assign keyboard_caps_lock = caps_lock;

    always @(posedge clk or negedge reset_n) begin
        if (!reset_n) begin
            receiver_error_latched <= 1'b0;
        end else if (frame_error) begin
            receiver_error_latched <= 1'b1;
        end
    end

    /*
     * A receive-only PS/2 interface cannot actively poll for a keyboard.
     * Treat the first valid frame as detection, and stretch each valid-frame
     * pulse to 100 ms at 50 MHz so board-level activity is visible.
     */
    always @(posedge clk or negedge reset_n) begin
        if (!reset_n) begin
            keyboard_detected <= 1'b0;
            keyboard_activity <= 1'b0;
            input_activity_count <= 23'd0;
        end else if (scan_code_valid) begin
            keyboard_detected <= 1'b1;
            keyboard_activity <= 1'b1;
            input_activity_count <= INPUT_ACTIVITY_HOLD_CYCLES - 1'b1;
        end else if (input_activity_count != 23'd0) begin
            input_activity_count <= input_activity_count - 1'b1;
            if (input_activity_count == 23'd1) begin
                keyboard_activity <= 1'b0;
            end
        end else begin
            keyboard_activity <= 1'b0;
        end
    end

    ps2_receiver receiver (
        .clk(clk),
        .reset_n(reset_n),
        .ps2_clk(ps2_clk),
        .ps2_dat(ps2_dat),
        .scan_code(scan_code),
        .scan_code_valid(scan_code_valid),
        .frame_error(frame_error)
    );

    ps2_scancode_parser parser (
        .clk(clk),
        .reset_n(reset_n),
        .scan_code_valid(scan_code_valid),
        .scan_code(scan_code),
        .make_valid(make_valid),
        .make_code(make_code),
        .make_extended(make_extended),
        .shift_active(shift_active),
        .caps_lock(caps_lock)
    );

    ps2_ascii_mapper mapper (
        .scan_code_valid(make_valid),
        .scan_code(make_code),
        .scan_code_extended(make_extended),
        .shift_active(shift_active),
        .caps_lock(caps_lock),
        .ascii_code(mapped_code),
        .ascii_valid(mapped_valid)
    );

    keyboard_fifo fifo (
        .clk(clk),
        .reset_n(reset_n),
        .push(mapped_valid),
        .push_data(mapped_code),
        .pop(fifo_pop),
        .front_data(fifo_data),
        .empty(fifo_empty),
        .full(fifo_full),
        .overflow(fifo_overflow)
    );

    keyboard_pio_interface pio_interface (
        .clk(clk),
        .reset_n(reset_n),
        .fifo_data(fifo_data),
        .fifo_empty(fifo_empty),
        .fifo_full(fifo_full),
        .fifo_overflow(fifo_overflow | receiver_error_latched),
        .keyboard_ack(keyboard_ack),
        .keyboard_data(keyboard_data),
        .keyboard_status(keyboard_status),
        .fifo_pop(fifo_pop)
    );

endmodule
