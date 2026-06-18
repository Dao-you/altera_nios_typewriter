module ps2_ascii_mapper (
    input        scan_code_valid,
    input  [7:0] scan_code,
    input        scan_code_extended,
    input        shift_active,
    input        caps_lock,
    output reg [7:0] ascii_code,
    output reg       ascii_valid
);

    localparam KEYBOARD_CODE_LEFT  = 8'h80;
    localparam KEYBOARD_CODE_RIGHT = 8'h81;
    localparam KEYBOARD_CODE_UP    = 8'h82;
    localparam KEYBOARD_CODE_DOWN  = 8'h83;

    wire letter_upper;

    assign letter_upper = shift_active ^ caps_lock;

    always @(*) begin
        ascii_code = 8'h00;
        ascii_valid = scan_code_valid;

        if (scan_code_extended) begin
            case (scan_code)
                8'h6B: ascii_code = KEYBOARD_CODE_LEFT;
                8'h74: ascii_code = KEYBOARD_CODE_RIGHT;
                8'h75: ascii_code = KEYBOARD_CODE_UP;
                8'h72: ascii_code = KEYBOARD_CODE_DOWN;
                8'h71: ascii_code = 8'h7F; /* Delete */
                default: ascii_valid = 1'b0;
            endcase
        end else begin
            case (scan_code)
                /* Letters */
                8'h1C: ascii_code = letter_upper ? 8'h41 : 8'h61; /* A */
                8'h32: ascii_code = letter_upper ? 8'h42 : 8'h62; /* B */
                8'h21: ascii_code = letter_upper ? 8'h43 : 8'h63; /* C */
                8'h23: ascii_code = letter_upper ? 8'h44 : 8'h64; /* D */
                8'h24: ascii_code = letter_upper ? 8'h45 : 8'h65; /* E */
                8'h2B: ascii_code = letter_upper ? 8'h46 : 8'h66; /* F */
                8'h34: ascii_code = letter_upper ? 8'h47 : 8'h67; /* G */
                8'h33: ascii_code = letter_upper ? 8'h48 : 8'h68; /* H */
                8'h43: ascii_code = letter_upper ? 8'h49 : 8'h69; /* I */
                8'h3B: ascii_code = letter_upper ? 8'h4A : 8'h6A; /* J */
                8'h42: ascii_code = letter_upper ? 8'h4B : 8'h6B; /* K */
                8'h4B: ascii_code = letter_upper ? 8'h4C : 8'h6C; /* L */
                8'h3A: ascii_code = letter_upper ? 8'h4D : 8'h6D; /* M */
                8'h31: ascii_code = letter_upper ? 8'h4E : 8'h6E; /* N */
                8'h44: ascii_code = letter_upper ? 8'h4F : 8'h6F; /* O */
                8'h4D: ascii_code = letter_upper ? 8'h50 : 8'h70; /* P */
                8'h15: ascii_code = letter_upper ? 8'h51 : 8'h71; /* Q */
                8'h2D: ascii_code = letter_upper ? 8'h52 : 8'h72; /* R */
                8'h1B: ascii_code = letter_upper ? 8'h53 : 8'h73; /* S */
                8'h2C: ascii_code = letter_upper ? 8'h54 : 8'h74; /* T */
                8'h3C: ascii_code = letter_upper ? 8'h55 : 8'h75; /* U */
                8'h2A: ascii_code = letter_upper ? 8'h56 : 8'h76; /* V */
                8'h1D: ascii_code = letter_upper ? 8'h57 : 8'h77; /* W */
                8'h22: ascii_code = letter_upper ? 8'h58 : 8'h78; /* X */
                8'h35: ascii_code = letter_upper ? 8'h59 : 8'h79; /* Y */
                8'h1A: ascii_code = letter_upper ? 8'h5A : 8'h7A; /* Z */

                /* Number row */
                8'h16: ascii_code = shift_active ? 8'h21 : 8'h31; /* 1 ! */
                8'h1E: ascii_code = shift_active ? 8'h40 : 8'h32; /* 2 @ */
                8'h26: ascii_code = shift_active ? 8'h23 : 8'h33; /* 3 # */
                8'h25: ascii_code = shift_active ? 8'h24 : 8'h34; /* 4 $ */
                8'h2E: ascii_code = shift_active ? 8'h25 : 8'h35; /* 5 % */
                8'h36: ascii_code = shift_active ? 8'h5E : 8'h36; /* 6 ^ */
                8'h3D: ascii_code = shift_active ? 8'h26 : 8'h37; /* 7 & */
                8'h3E: ascii_code = shift_active ? 8'h2A : 8'h38; /* 8 * */
                8'h46: ascii_code = shift_active ? 8'h28 : 8'h39; /* 9 ( */
                8'h45: ascii_code = shift_active ? 8'h29 : 8'h30; /* 0 ) */

                /* Common punctuation */
                8'h0E: ascii_code = shift_active ? 8'h7E : 8'h60; /* ` ~ */
                8'h4E: ascii_code = shift_active ? 8'h5F : 8'h2D; /* - _ */
                8'h55: ascii_code = shift_active ? 8'h2B : 8'h3D; /* = + */
                8'h54: ascii_code = shift_active ? 8'h7B : 8'h5B; /* [ { */
                8'h5B: ascii_code = shift_active ? 8'h7D : 8'h5D; /* ] } */
                8'h5D: ascii_code = shift_active ? 8'h7C : 8'h5C; /* \ | */
                8'h4C: ascii_code = shift_active ? 8'h3A : 8'h3B; /* ; : */
                8'h52: ascii_code = shift_active ? 8'h22 : 8'h27; /* ' " */
                8'h41: ascii_code = shift_active ? 8'h3C : 8'h2C; /* , < */
                8'h49: ascii_code = shift_active ? 8'h3E : 8'h2E; /* . > */
                8'h4A: ascii_code = shift_active ? 8'h3F : 8'h2F; /* / ? */

                /* Controls used by the existing editor core */
                8'h29: ascii_code = 8'h20; /* Space */
                8'h5A: ascii_code = 8'h0A; /* Enter -> LF */
                8'h66: ascii_code = 8'h08; /* Backspace */

                default: ascii_valid = 1'b0;
            endcase
        end
    end

endmodule
