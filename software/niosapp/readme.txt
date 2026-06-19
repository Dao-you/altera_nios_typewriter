Readme - DE2-115 Nios II Text Editor

DESCRIPTION:
This application implements a small text editor on the DE2-115 board through
Nios II C code and existing Avalon PIO peripherals.

INPUTS:
- Startup menu:
  - KEY3: move to the previous option.
  - KEY2: move to the next option.
  - KEY0: confirm the selected option.
  - Options: EEPROM EDITOR, SD QUESTION.
- SW[6:0]: 7-bit ASCII input.
- SW15: active-low Nios II reset, 0 = reset and 1 = run.
- SW16: edit mode, 0 = overwrite and 1 = insert.
- SW17: navigation mode, 0 = left/right and 1 = up/down.
- Editor mode:
  - KEY0: save the current document to EEPROM.
  - KEY1: write the current ASCII byte; 0x08 backspaces, including joining
    with the previous line at column 0, 0x0A creates a new line, and 0x7F
    deletes the character under the cursor.
  - KEY3: move left or up.
  - KEY2: move right or down.
- SD view mode:
  - KEY1: retry reading QUESTION.TXT.
  - KEY2/KEY3: scroll down/up by one text line.
  - KEY0: enter the EEPROM-backed text editor.

OUTPUTS:
- Startup LCD: first row shows the selected option name. The second row uses
  column 1 for "<" when an option exists to the left, column 14 for ">" when
  an option exists to the right, and a centered decimal counter such as "1/2".
- LCD: current editor line and next line through a 16-column viewport. The
  viewport scrolls on long lines after the cursor crosses the third column or
  the third column from the right. The second LCD row blinks "------END-------"
  near the built-in cursor blink rate on the final document line so the marker
  is not mistaken for document text.
  Insert mode uses the underline cursor; overwrite mode uses the blinking
  block cursor.
- HEX7..HEX6: current line number, decimal.
- HEX5..HEX4: cursor column, decimal.
- HEX3..HEX2: total line count, decimal.
- HEX1..HEX0: current ASCII input, hexadecimal.
- LEDR: current line progress from LEDR17 toward LEDR0; LEDR0 only lights on
  the final document line. During blocking EEPROM reads or writes,
  LEDR17..LEDR1 temporarily show a single-LED marquee as an activity effect,
  not as a progress value.
- LEDG0: insert mode.
- LEDG1: navigation mode.
- LEDG5: EEPROM error.
- LEDG6: current-line text remains hidden to the right of the LCD viewport.
- LEDG7: unsaved changes.

SOURCE FILES:
- main.c: application loop and event dispatch.
- menu.c/.h: shared LCD menu state machine. Callers provide an option list;
  KEY3/KEY2 move left/right and KEY0 returns the selected option index.
- editor.c/.h: document buffer and editing operations.
- key.c/.h: active-low key debounce and edge detection.
- display.c/.h, lcd.c/.h, seven_seg.c/.h: board display output.
  display.c centralizes LEDR progress, activity marquee, blinking marker, and
  LEDG indicator ownership.
- eeprom.c/.h: 24LC32-compatible I2C EEPROM load/save.
- sdcard.c/.h: SD card SPI-mode initialization and read-only FAT16/FAT32 root
  directory lookup for QUESTION.TXT.

DOCUMENT LIMITS:
- 32 lines, up to 99 characters per line.
- EEPROM storage format v2 is a fixed 3210-byte image:
  magic/version, cursor metadata, line_len[32], document[32][99], checksum.

BUILD NOTES:
Use the Nios II Command Shell from Quartus II 13.1. In this local environment,
the generated BSP Makefile has an old QSYS simulation target that does not
parse under the installed make, so the verified app-only build command is:

cd /cygdrive/d/quartus/quartusFinalProject/software/niosapp
make QSYS=0 MAKEABLE_LIBRARY_ROOT_DIRS= app

When Qsys changes, regenerate software/niosapp_bsp before building the app.
The SD test requires SPI_SDCARD_BASE in software/niosapp_bsp/system.h.
