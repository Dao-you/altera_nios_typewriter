# LEDR flag PIO contract

`pio_out_ledr_flag` is an 8-bit Qsys output PIO used by Nios II C code to
select whether LEDR is driven directly by `PIO_OUT_LEDR_BASE` or by the
Verilog LEDR effect controller in `ledr_flag_controller.v`.

The C-side constants live in `software/niosapp/display.h` as
`DisplayLedrFlag`. The hardware-side constants are mirrored as localparams in
`ledr_flag_controller.v`.

## Bit map

| Bit | Mask | Name | Meaning |
| --- | --- | --- | --- |
| 0 | `0x01` | `DISPLAY_LEDR_FLAG_NIOS_CONTROL` | `1` selects normal Nios `PIO_OUT_LEDR_BASE` output; `0` selects Verilog effect output. |
| 1 | `0x02` | `DISPLAY_LEDR_FLAG_MARQUEE_LEFT_RIGHT` | Verilog marquee from `LEDR17` toward `LEDR0`. |
| 2 | `0x04` | `DISPLAY_LEDR_FLAG_MARQUEE_RIGHT_LEFT` | Verilog marquee from `LEDR0` toward `LEDR17`. |
| 3 | `0x08` | `DISPLAY_LEDR_FLAG_CONFIRM_BLINK` | Verilog all-LEDR blink at 2 Hz for VI command, informational, and yes/no confirmation pages. |
| 4 | `0x10` | `DISPLAY_LEDR_FLAG_ERROR_BLINK` | Verilog all-LEDR blink at 5 Hz for error messages. |
| 5..7 | `0xE0` | reserved | Write as `0`. |

## Priority

When bit 0 is `1`, Nios owns LEDR and bits 1..4 are ignored.

When bit 0 is `0`, Verilog owns LEDR. If more than one effect bit is set, the
priority is:

1. `DISPLAY_LEDR_FLAG_ERROR_BLINK`
2. `DISPLAY_LEDR_FLAG_CONFIRM_BLINK`
3. `DISPLAY_LEDR_FLAG_MARQUEE_RIGHT_LEFT`
4. `DISPLAY_LEDR_FLAG_MARQUEE_LEFT_RIGHT`

If bit 0 is `0` and no effect bit is set, Verilog drives all LEDR off.

## Implementation notes

- `top.v` connects Qsys `pio_out_ledr_external_connection_export` to an
  internal `ledr_export` wire, connects
  `pio_out_ledr_flag_external_connection_export` to `ledr_flag_controller`,
  and drives board `LEDR[17:0]` from the controller output.
- `ledr_source_mux.v` wraps the copied course `hw03_Mux41.v` component from
  `D:\quartus\BDF_HDL` to select between normal Nios LEDR and Verilog effects.
- `display.c` is still the only C module that writes LEDR-related PIOs. When
  the regenerated BSP provides `PIO_OUT_LEDR_FLAG_BASE`,
  `display_show_activity_marquee()` writes
  `DISPLAY_LEDR_FLAG_MARQUEE_LEFT_RIGHT` and the C callback no longer has to
  advance animation frames.
- `display_show_vi_command()`, `display_show_info_message()`, and
  `display_show_confirm_message()` write `DISPLAY_LEDR_FLAG_CONFIRM_BLINK`;
  `display_show_error_message()` writes `DISPLAY_LEDR_FLAG_ERROR_BLINK`.
- If the BSP has not been regenerated yet, `display.c` falls back to the old
  software-driven marquee so app-only builds can still succeed.
