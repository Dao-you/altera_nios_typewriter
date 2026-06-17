Readme - DE2-115 Nios II Text Editor

DESCRIPTION:
This application implements a small text editor on the DE2-115 board through
Nios II C code and existing Avalon PIO peripherals.

INPUTS:
- SW[6:0]: 7-bit ASCII input.
- SW15: active-low Nios II reset, 0 = reset and 1 = run.
- SW16: edit mode, 0 = overwrite and 1 = insert.
- SW17: navigation mode, 0 = left/right and 1 = up/down.
- KEY0: save the current document to EEPROM.
- KEY1: write the current ASCII byte; 0x08 backspaces, 0x0A creates a
  new line, and 0x7F deletes the character under the cursor.
- KEY3: move left or up.
- KEY2: move right or down.

OUTPUTS:
- LCD: current editor line, status, and cursor. Insert mode uses the underline
  cursor; overwrite mode uses the blinking block cursor.
- HEX7..HEX6: current line number, decimal.
- HEX5..HEX4: cursor column, decimal.
- HEX3..HEX2: total line count, decimal.
- HEX1..HEX0: current ASCII input, hexadecimal.
- LEDR: current line progress from LEDR17 toward LEDR0; LEDR0 only lights on
  the final document line. During EEPROM writes, LEDR17..LEDR1 temporarily show
  a single-LED marquee as a save-activity effect, not as a progress value.
- LEDG0: insert mode.
- LEDG1: navigation mode.
- LEDG5: EEPROM error.
- LEDG6: overflow.
- LEDG7: unsaved changes.

SOURCE FILES:
- main.c: application loop and event dispatch.
- editor.c/.h: document buffer and editing operations.
- key.c/.h: active-low key debounce and edge detection.
- display.c/.h, lcd.c/.h, seven_seg.c/.h: board display output.
- eeprom.c/.h: 24LC32-compatible I2C EEPROM load/save.

BUILD NOTES:
Use the Nios II Command Shell from Quartus II 13.1. In this local environment,
the generated BSP Makefile has an old QSYS simulation target that does not
parse under the installed make, so the verified app-only build command is:

cd /cygdrive/d/quartus/quartusFinalProject/software/niosapp
make QSYS=0 MAKEABLE_LIBRARY_ROOT_DIRS= app
