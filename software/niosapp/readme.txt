Readme - DE2-115 Nios II Text Editor

DESCRIPTION:
This application implements a small text editor on the DE2-115 board through
Nios II C code and existing Avalon PIO peripherals.

INPUTS:
- Startup menu:
  - KEY3: move to the previous option.
  - KEY2: move to the next option.
  - KEY0: confirm the selected option.
  - Options: EEPROM EDITOR, SD QUESTION, TYPING GAME.
- SW[6:0]: 7-bit ASCII input.
- SW15: active-low Nios II reset, 0 = reset and 1 = run.
- SW16: edit mode, 0 = overwrite and 1 = insert.
- SW17: navigation mode, 0 = left/right and 1 = up/down.
- Editor mode:
  - KEY0: open the editor VI command page.
  - KEY1: write the current ASCII byte; 0x08 backspaces, including joining
    with the previous line at column 0, 0x0A creates a new line, and 0x7F
    deletes the character under the cursor.
  - KEY3: move left or up.
  - KEY2: move right or down.
- Editor VI command page:
  - Row 1 starts with ":" and accepts command input.
  - KEY1 appends the current SW[6:0] ASCII byte. 0x08/0x7F backspaces and
    0x0A executes the command.
  - PS/2 printable keys append directly. PS/2 Backspace/Delete delete a
    command character, and Enter executes the command.
  - KEY0 executes the command. An empty command returns to editor mode.
  - KEY2 opens the horizontal editor menu.
  - Commands: w saves, q quits, wq and x save then quit, e! restores the whole
    document from EEPROM.
- Editor menu:
  - KEY3: move to the previous option.
  - KEY2: move to the next option.
  - KEY0: confirm the selected option.
  - Pressing KEY3 on Save to ROM returns to the VI command page.
  - Options: Save to ROM, Quit, Restore whole, Clear this line, Clear All,
    Move to head, Move to end, Cancel.
- SD view mode:
  - KEY1: retry reading QUESTION.TXT.
  - KEY2/KEY3: scroll down/up by one text line.
  - KEY0: enter the EEPROM-backed text editor.
- Typing game:
  - Starts from the TYPING GAME startup-menu option.
  - The ready screen asks for SW0..SW7 to be off. KEY1 starts only when those
    switches are clear; KEY0 returns to the startup menu.
  - On start, QUESTION.TXT is read from SD and ten non-empty lines are selected
    randomly. Each line is one question; lines longer than 99 characters are
    truncated to match the shared editor line buffer.
  - Input uses the same SW[6:0] + KEY1 and PS/2 printable-key path as the text
    editor. Backspace/Delete and left/right movement work through the shared
    editor input dispatcher. LF/Enter is ignored so each answer stays one line.
  - The game advances automatically when the typed answer exactly matches the
    current question.
  - KEY0 opens the typing-game menu: Quit, Restart, Continue.

OUTPUTS:
- Menu LCD: first row shows the selected option name. The second row uses
  column 1 for "<" when an option exists to the left, column 14 for ">" when
  an option exists to the right, and a centered decimal counter such as "1/2".
  Startup and editor menus share this layout. The editor menu has a VI command
  page before the first option, so its first option also shows "<". LEDR shows
  the selected menu option as a progress bar over the option count.
- VI command LCD: row 1 shows ":" followed by the command buffer and places
  the LCD cursor after it. Row 2 shows "VI COMMAND" with only the right menu
  arrow. LEDR uses the 2 Hz full-blink effect.
- LCD: the EEPROM editor treats the centered blinking "EEPROM" marker as a
  boundary before document line 0, matching the END marker at the document end.
  When the cursor is on line 0, row 1 shows the marker and row 2 shows line 0.
  On later lines the marker scrolls away and the LCD returns to the normal
  current-line / next-line viewport. The viewport scrolls on long lines after
  the cursor crosses the third column or the third column from the right.
  Insert mode soft-blinks the underline cursor; overwrite mode uses the LCD
  built-in blinking block cursor.
- Modal messages: informational messages show "KEY0 OK" centered on row 2 and
  return on KEY0 with 2 Hz LEDR blinking. Confirmation messages show
  "KEY1YES KEY0NO" and use KEY1 for yes / KEY0 for no with 2 Hz LEDR blinking.
  Dirty Quit uses "Quit no save?". Error messages show "KEY0 OK" centered and
  use 5 Hz LEDR blinking.
- HEX7..HEX6: current line number, decimal.
- HEX5..HEX4: cursor column, decimal.
- HEX3..HEX2: total line count, decimal.
- HEX1..HEX0: current ASCII input, hexadecimal.
- LEDR: current line progress from LEDR17 toward LEDR0 in the editor; menu
  screens use the same bar for selected option / option count. LEDR0 only
  lights on the final document line or final menu option. VI command and modal
  confirmation pages use the 2 Hz full-blink effect. During blocking EEPROM
  reads/writes or SD reads, pio_out_ledr_flag can hand LEDR to the Verilog
  effect controller for a single-LED activity marquee. This is not a progress
  value. If the BSP has not been regenerated with PIO_OUT_LEDR_FLAG_BASE,
  display.c falls back to the older software-driven marquee.
- LEDG0: insert mode.
- LEDG1: navigation mode.
- LEDG5: EEPROM error.
- LEDG6: current-line text remains hidden to the right of the LCD viewport.
- LEDG7: unsaved changes.
- Typing game outputs:
  - LCD row 1: current question viewport.
  - LCD row 2: current answer viewport with cursor.
  - LEDG7..LEDG0: current-question progress over the ten-question game.
  - HEX7..HEX6: current question number, decimal.
  - HEX5..HEX4: total question count, decimal.
  - HEX3..HEX2: elapsed minutes, decimal.
  - HEX1..HEX0: elapsed seconds, decimal.

SOURCE FILES:
- main.c: application loop and event dispatch.
  It owns editor VI command parsing, modal message states for informational,
  confirmation, and error prompts, typing-game app states, and the
  EEPROM-backed quit/restore actions.
- menu.c/.h: shared LCD menu state machine. Callers provide an option list;
  KEY3/KEY2 move left/right and KEY0 returns the selected option index.
  menu_update_with_left_edge() lets a caller attach a page before option 0,
  and keeps the left arrow visible on option 0.
- editor.c/.h: document buffer and editing operations.
- editor_input.c/.h: shared dispatch from debounced SW/KEY and PS/2 decoded
  bytes into EditorDocument operations. The EEPROM editor allows LF; the typing
  game reuses the same path with LF disabled for single-line answers.
- typing_game.c/.h: typing-game question sampling, answer comparison, round
  state, restart, and software stopwatch state.
- key.c/.h: active-low key debounce and edge detection.
- display.c/.h, lcd.c/.h, seven_seg.c/.h: board display output.
  display.c centralizes LEDR progress, activity marquee, modal messages,
  blinking markers, and LEDG indicator ownership. display.h also defines the
  pio_out_ledr_flag bit masks used by the Verilog LEDR effect controller.
- eeprom.c/.h: 24LC32-compatible I2C EEPROM load/save.
- sdcard.c/.h: SD card SPI-mode initialization and read-only FAT16/FAT32 root
  directory lookup for QUESTION.TXT, with activity callback support while the
  blocking read is running.

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
The hardware LEDR effect path requires PIO_OUT_LEDR_FLAG_BASE in system.h.
The typing-game stopwatch currently uses the 10 ms main-loop tick because this
BSP has ALT_SYS_CLK set to none. Add a Qsys interval timer and regenerate the
BSP if stricter timing accuracy is needed.
