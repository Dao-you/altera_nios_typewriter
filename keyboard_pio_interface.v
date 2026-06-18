module keyboard_pio_interface (
    input        clk,
    input        reset_n,
    input  [7:0] fifo_data,
    input        fifo_empty,
    input        fifo_full,
    input        fifo_overflow,
    input        keyboard_ack,
    output [7:0] keyboard_data,
    output [7:0] keyboard_status,
    output reg   fifo_pop
);

    reg ack_sync;
    reg ack_previous;

    assign keyboard_data = fifo_empty ? 8'h00 : fifo_data;
    assign keyboard_status = {
        5'b00000,
        fifo_overflow,
        fifo_full,
        ~fifo_empty
    };

    always @(posedge clk or negedge reset_n) begin
        if (!reset_n) begin
            ack_sync <= 1'b0;
            ack_previous <= 1'b0;
            fifo_pop <= 1'b0;
        end else begin
            ack_sync <= keyboard_ack;
            ack_previous <= ack_sync;
            fifo_pop <= ack_sync & ~ack_previous & ~fifo_empty;
        end
    end

endmodule
