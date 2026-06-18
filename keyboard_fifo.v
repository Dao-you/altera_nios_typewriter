module keyboard_fifo (
    input        clk,
    input        reset_n,
    input        push,
    input  [7:0] push_data,
    input        pop,
    output [7:0] front_data,
    output       empty,
    output       full,
    output reg   overflow
);

    parameter DEPTH = 16;
    parameter ADDR_WIDTH = 4;

    reg [7:0] memory [0:DEPTH - 1];
    reg [ADDR_WIDTH - 1:0] read_ptr;
    reg [ADDR_WIDTH - 1:0] write_ptr;
    reg [ADDR_WIDTH:0] count;

    wire pop_active;
    wire push_active;

    assign empty = (count == 0);
    assign full = (count == DEPTH);
    assign front_data = empty ? 8'h00 : memory[read_ptr];
    assign pop_active = pop && !empty;
    assign push_active = push && (!full || pop_active);

    always @(posedge clk or negedge reset_n) begin
        if (!reset_n) begin
            read_ptr <= {ADDR_WIDTH{1'b0}};
            write_ptr <= {ADDR_WIDTH{1'b0}};
            count <= {(ADDR_WIDTH + 1){1'b0}};
            overflow <= 1'b0;
        end else begin
            if (push_active) begin
                memory[write_ptr] <= push_data;
                write_ptr <= write_ptr + {{(ADDR_WIDTH - 1){1'b0}}, 1'b1};
            end else if (push && full) begin
                overflow <= 1'b1;
            end

            if (pop_active) begin
                read_ptr <= read_ptr + {{(ADDR_WIDTH - 1){1'b0}}, 1'b1};
            end

            if (push_active && !pop_active) begin
                count <= count + {{ADDR_WIDTH{1'b0}}, 1'b1};
            end else if (!push_active && pop_active) begin
                count <= count - {{ADDR_WIDTH{1'b0}}, 1'b1};
            end
        end
    end

endmodule
