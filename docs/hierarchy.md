# 專案結構與運作階層整理

本文件整理目前 DE2-115 / Quartus / Qsys / Nios II 專案的硬體與軟體結構，方便後續撰寫報告與繪製方塊圖。本文件不是最終報告，而是工程整理稿，因此會保留較多模組責任、接線、資料流與維護注意事項。

主要依據檔案：`top.v`、`ps2_keyboard_controller.v`、`ps2_receiver.v`、`ps2_scancode_parser.v`、`ps2_ascii_mapper.v`、`keyboard_fifo.v`、`keyboard_pio_interface.v`、`ledr_flag_controller.v`、`ledr_source_mux.v`、`software/niosapp/main.c`、`editor.c/.h`、`editor_input.c/.h`、`display.c/.h`、`menu.c/.h`、`lcd.c/.h`、`key.c/.h`、`keyboard.c/.h`、`eeprom.c/.h`、`sdcard.c/.h`、`typing_game.c/.h`。

---

## 1. 系統總覽

本專案是一個混合式 FPGA 系統：

- **Verilog 硬體層**：負責 top-level 腳位接線、PS/2 keyboard 解碼、LEDR 硬體燈效控制。
- **Qsys / Nios II 層**：提供 CPU、On-Chip Memory、Timer、PIO、SPI SD card core。
- **C 應用層**：負責文字編輯器、SD card 讀寫、FAT16/FAT32 檔案解析、typing game、LCD/HEX/LED UI。

```mermaid
flowchart LR
    Board[DE2-115 Board I/O] --> Top[top.v]
    Top --> Nios[Qsys nios u0]
    Top --> PS2[ps2_keyboard_controller]
    Top --> LEDR[ledr_flag_controller]
    Nios --> C[software/niosapp C 程式]
    C --> UI[display.c reusable UI framework]
    C --> Editor[EditorDocument]
    C --> SD[sdcard.c FAT16/FAT32]
    C --> EEPROM[eeprom.c I2C]
    C --> Typing[typing_game.c]
```

設計理念是：複雜流程與 UI 狀態機交給 C；需要固定時序或持續硬體反應的部分交給 Verilog。

---

## 2. top-level 電路方塊圖與接線

```mermaid
flowchart LR
    subgraph IO[DE2-115 I/O]
        CLK[CLOCK_50]
        SW[SW 17..0]
        KEY[KEY 3..0]
        PS2IO[PS2 CLK/DAT]
        SDIO[SD socket]
        EEPIO[EEPROM]
        LCDIO[LCD1602]
        HEXIO[HEX0~HEX7]
        LEDRIO[LEDR 17..0]
        LEDGIO[LEDG 8..0]
    end

    subgraph TOP[top.v]
        RST[reset_n = SW15]
        NIOS[nios u0]
        KBDCTRL[ps2_keyboard_controller]
        LEDRCTRL[ledr_flag_controller]
        SDGLUE[SD SPI glue]
        EEPGLUE[EEPROM open-drain glue]
    end

    CLK --> NIOS
    CLK --> KBDCTRL
    CLK --> LEDRCTRL
    SW --> RST
    RST --> NIOS
    RST --> KBDCTRL
    RST --> LEDRCTRL
    SW --> NIOS
    KEY --> NIOS
    PS2IO --> KBDCTRL
    KBDCTRL -->|keyboard_data/status| NIOS
    NIOS -->|keyboard_ack| KBDCTRL
    NIOS -->|pio_out_ledr + flag| LEDRCTRL
    LEDRCTRL --> LEDRIO
    NIOS --> LCDIO
    NIOS --> HEXIO
    NIOS --> LEDGIO
    NIOS --> SDGLUE
    SDGLUE <--> SDIO
    NIOS --> EEPGLUE
    EEPGLUE <--> EEPIO
```

`top.v` 是硬體頂層，主要負責把 DE2-115 腳位、Qsys Nios 系統、PS/2 控制器與 LEDR 控制器接起來。

重點接線：

- `reset_n = SW[15]`，`SW15=0` reset，`SW15=1` run。
- `SW[17:0]` 全部輸入 Nios PIO；其中 `SW[6:0]` 是 ASCII，`SW16` 是 Insert / Overwrite，`SW17` 是左右 / 上下移動模式。
- `KEY[3:0]` 輸入 Nios PIO，C 端做 active-low 反相、debounce、pressed-edge 偵測。
- `LEDR[17:0]` 由 `ledr_flag_controller` 輸出，不直接由 Nios PIO 接到板子。
- `LEDG[7:0]` 由一般 LEDG PIO 輸出；`LEDG[8]` 由獨立 1-bit PIO 輸出，給 typing game 作為秒閃冒號。
- `HEX0~HEX7` 的 PIO 是 8-bit，但 top-level 只接 `[6:0]` 到七段顯示器。
- LCD 使用 8-bit data PIO 與 5-bit control PIO。control bit0~4 分別是 `RS`、`RW`、`EN`、`ON`、`BLON`。
- EEPROM SDA 使用 open-drain 類接法，只 drive low 或 release high-Z。
- SD card 使用 SPI mode：`SD_CLK=SCLK`、`SD_CMD=MOSI`、`SD_DAT[0]=MISO`、`SD_DAT[3]=SS_n`，`SD_DAT[1]` 與 `SD_DAT[2]` high-Z。

---

## 3. Qsys / Nios II 子系統

Qsys 系統提供 Nios II CPU、On-Chip Memory、JTAG UART、System ID、Timer、PIO 與 SPI SD card core。C 程式透過 `system.h` 的 base macro 存取外設。

```mermaid
flowchart TB
    CPU[Nios II CPU] --> MEM[On-Chip Memory]
    CPU --> UART[JTAG UART]
    CPU --> TIMER[Timer 1 ms tick]
    CPU --> SYSID[System ID]
    CPU --> PIO[PIO group]
    CPU --> SPI[spi_sdcard]
    PIO --> SWPIO[SW input]
    PIO --> KEYPIO[KEY input]
    PIO --> LEDPIO[LED / HEX / LCD output]
    PIO --> EEP[EEPROM PIO]
    PIO --> KBD[Keyboard data/status/ack]
```

常用外設 macro 包含：`PIO_IN_SW_BASE`、`PIO_IN_KEY_BASE`、`PIO_OUT_LEDR_BASE`、`PIO_OUT_LEDR_FLAG_BASE`、`PIO_OUT_LEDG_BASE`、`PIO_OUT_LEDG8_BASE`、`PIO_OUT_HEX0_BASE` 到 `PIO_OUT_HEX7_BASE`、`PIO_OUT_LCD_DATA_BASE`、`PIO_OUT_LCD_CTRL_BASE`、`PIO_OUT_EEP_SCL_BASE`、`PIO_OUT_EEP_SDA_OUT_BASE`、`PIO_OUT_EEP_SDA_OE_BASE`、`PIO_IN_EEP_SDA_IN_BASE`、`PIO_IN_KEYBOARD_DATA_BASE`、`PIO_IN_KEYBOARD_STATUS_BASE`、`PIO_OUT_KEYBOARD_ACK_BASE`、`SPI_SDCARD_BASE`。

若 Qsys 重新 Generate HDL，必須更新 BSP，否則 `system.h` 可能停在舊版。

---

## 4. PS/2 keyboard controller 內部方塊圖

PS/2 鍵盤由 Verilog 解碼，再透過 PIO 給 Nios C 使用。Nios 不直接讀 PS/2 clock/data，而是讀已解碼的 ASCII 或 control byte。

```mermaid
flowchart LR
    PS2[PS2_CLK / PS2_DAT] --> RX[ps2_receiver]
    RX -->|scan_code| Parser[ps2_scancode_parser]
    Parser -->|make_code + shift/caps| Mapper[ps2_ascii_mapper]
    Mapper -->|ASCII/control byte| FIFO[keyboard_fifo]
    FIFO --> PIO[keyboard_pio_interface]
    PIO -->|data/status| Nios[Nios C keyboard.c]
    Nios -->|ack| PIO
```

模組職責：

- `ps2_receiver.v`：同步與濾波 PS/2 clock/data，在 falling edge 取樣，解析 start、data、parity、stop bit，並輸出 `scan_code_valid` 或 `frame_error`。
- `ps2_scancode_parser.v`：處理 `E0` extended prefix、`F0` release prefix、Shift 狀態與 Caps Lock，只輸出 make code。
- `ps2_ascii_mapper.v`：把 make code 轉成 printable ASCII、Backspace、LF、Delete 或方向鍵控制碼。
- `keyboard_fifo.v`：16-byte FIFO，避免 C 主迴圈輪詢速度不足導致漏字。
- `keyboard_pio_interface.v`：提供 `keyboard_data`、`keyboard_status`、`keyboard_ack` handshake。

keyboard status：bit0 是 FIFO has data，bit1 是 FIFO full，bit2 是 FIFO overflow 或 PS/2 frame error。C 端 `keyboard_read()` 會先讀 status，若有資料再讀 data，最後 pulse ack 讓 Verilog pop FIFO。

---

## 5. LEDR flag controller 內部方塊圖

LEDR 有雙來源：Nios C 顯示 progress，Verilog 顯示 blocking I/O activity marquee、confirm blink、error blink。

```mermaid
flowchart LR
    Flag[pio_out_ledr_flag] --> Ctrl[ledr_flag_controller]
    NiosLedr[pio_out_ledr] --> Mux[ledr_source_mux]
    Ctrl -->|verilog_ledr| Mux
    Flag -->|bit0 select_nios| Mux
    Mux --> LEDR[LEDR 17..0]
```

`pio_out_ledr_flag` bit 定義：

| bit | mask | 意義 |
|---|---:|---|
| 0 | `0x01` | 1 選 Nios LEDR；0 選 Verilog effect |
| 1 | `0x02` | `LEDR17` 到 `LEDR0` 跑馬燈 |
| 2 | `0x04` | `LEDR0` 到 `LEDR17` 跑馬燈 |
| 3 | `0x08` | 2 Hz 全燈閃爍 |
| 4 | `0x10` | 5 Hz 全燈閃爍 |
| 5..7 | `0xE0` | 保留 |

當 Verilog 控制 LEDR 且多個 effect bit 同時為 1，priority 是 error blink、confirm blink、right-to-left marquee、left-to-right marquee。`ledr_source_mux.v` 使用 18 個 `hw03_Mux41`，等效成 18-bit 2-to-1 mux。

---

## 6. C 程式分層

```mermaid
flowchart TB
    MAIN[main.c AppState] --> MENU[menu.c]
    MAIN --> DISPLAY[display.c]
    MAIN --> EDITOR[editor.c]
    MAIN --> INPUT[editor_input.c]
    MAIN --> EEPROM[eeprom.c]
    MAIN --> SDCARD[sdcard.c]
    MAIN --> TYPING[typing_game.c]
    MAIN --> KEY[key.c]
    MAIN --> KBD[keyboard.c]
    DISPLAY --> LCD[lcd.c]
    DISPLAY --> SEG[seven_seg.c]
    INPUT --> EDITOR
    INPUT --> KBD
    EEPROM --> EDITOR
    SDCARD --> EDITOR
    TYPING --> EDITOR
```

主要檔案職責：

- `main.c`：AppState 主狀態機，整合首頁、editor、SD view、typing game、modal message。
- `editor.c/.h`：`EditorDocument`、文字插入/覆蓋/刪除/換行/移動、序列化。
- `editor_input.c/.h`：把 SW/KEY/PS2 byte 轉成 editor action。
- `display.c/.h`：可重用 UI 畫面框架，提供選單、訊息、確認、錯誤、閃爍 marker、loading 跑馬燈、editor viewport、typing game 畫面等共用顯示元件。
- `menu.c/.h`：共用水平選單。
- `lcd.c/.h`：LCD1602 8-bit PIO driver。
- `key.c/.h`：KEY debounce 與 edge detection。
- `keyboard.c/.h`：讀 PS/2 FIFO PIO。
- `eeprom.c/.h`：24LC32 類 I2C bit-bang。
- `sdcard.c/.h`：SPI SD card 與 FAT16/FAT32 root directory 讀寫。
- `typing_game.c/.h`：題目抽樣、答案比對、秒表、CPM。

---

## 7. `main.c` 狀態機流程

`main.c` 每輪主迴圈會讀 SW、讀 KEY edge、更新 insert mode、取得 ASCII 與 nav mode，再依 `AppState` 處理對應功能。

```mermaid
flowchart TD
    INIT[初始化 editor/display/key/keyboard/menu/typing] --> HOME[APP_STATE_MENU]
    HOME -->|EEPROM EDITOR| LOADROM[load EEPROM]
    LOADROM --> EDITOR[APP_STATE_EDITOR]
    HOME -->|SD EDITOR| LOADSD[load EDITOR.TXT]
    LOADSD --> EDITOR
    HOME -->|SD QUESTIONS| SDVIEW[APP_STATE_SD_VIEW]
    SDVIEW -->|KEY0| HOME
    HOME -->|TYPING GAME| TMODE[APP_STATE_TYPING_MODE_MENU]
    TMODE --> TCOUNT[APP_STATE_TYPING_COUNT_MENU]
    TCOUNT --> TREADY[APP_STATE_TYPING_READY]
    TREADY --> TGAME[APP_STATE_TYPING_GAME]
    TGAME -->|KEY0| TPAUSE[APP_STATE_TYPING_MENU]
    TPAUSE --> TGAME
    TPAUSE --> HOME
    TGAME -->|done| INFO[APP_STATE_INFO_MESSAGE]
    INFO --> HOME
    EDITOR -->|KEY0| VICMD[APP_STATE_EDITOR_COMMAND]
    VICMD -->|KEY2| EMENU[APP_STATE_EDITOR_MENU]
    EMENU --> EDITOR
    VICMD --> EDITOR
    EDITOR --> MODAL[INFO / CONFIRM / ERROR]
    MODAL --> EDITOR
```

首頁選單包含 `EEPROM EDITOR`、`SD EDITOR`、`SD QUESTIONS`、`TYPING GAME`。Editor 主畫面中，`KEY0` 進入 VI command，`KEY1` 寫入目前 `SW[6:0]` ASCII，`KEY3/KEY2` 依 `SW17` 決定左右或上下移動。VI command 支援 `w`、`q`、`wq`、`x`、`e!`，並可用 `KEY2` 進入 editor menu。

---

## 8. EditorDocument 與資料流

`EditorDocument` 是 EEPROM editor、SD editor、typing game input 共用的文字核心。限制是 32 行、每行 99 字元。

```mermaid
flowchart LR
    Input[SW/KEY/PS2 input] --> Dispatch[editor_input]
    Dispatch --> Doc[EditorDocument]
    Doc --> Display[display_update]
    Doc --> Serialize[editor_serialize]
    Serialize --> EEPROM[EEPROM binary layout]
    Doc --> Export[editor_export_text]
    Export --> SD[EDITOR.TXT]
    SD --> LoadText[editor_load_text]
    EEPROM --> Deserialize[editor_deserialize]
    LoadText --> Doc
    Deserialize --> Doc
```

`editor_write_ascii()` 支援 Backspace `0x08`、LF `0x0A`、Delete `0x7F`、printable ASCII `0x20..0x7E`。SD editor 使用 newline-delimited ASCII；EEPROM editor 使用固定 3210-byte binary layout，包含 magic、version、行數、游標、insert mode、line length、文件內容與 checksum。

---

## 9. UI framework 設計理念

本專案的 UI framework 不是單純把 LCD、HEX、LEDR、LEDG 的輸出集中到 `display.c`，而是設計了一組**可重用的顯示畫面與互動樣式**。不同功能不需要各自重寫 LCD 排版、LED 狀態、七段顯示或等待提示，而是呼叫已經定義好的 UI pattern，讓整個系統看起來像同一套介面。

```mermaid
flowchart TB
    Feature[main.c / editor / SD / EEPROM / typing game] --> API[display.c reusable UI APIs]

    API --> Menu[水平選單畫面]
    API --> Modal[Info / Confirm / Error 訊息畫面]
    API --> Marker[閃爍 top / bottom marker]
    API --> Loading[Loading / blocking activity 跑馬燈]
    API --> EditorView[Editor viewport 顯示]
    API --> TextPage[SD text page 兩列檢視]
    API --> TypingView[Typing game 專用畫面]

    API --> LCD[LCD1602]
    API --> HEX[HEX0~HEX7]
    API --> LEDR[LEDR progress / effect]
    API --> LEDG[LEDG 狀態燈]
```

這套 UI framework 的重點是「畫面元件化」：

- **選單畫面**：`display_show_menu_item()` 與 `display_show_menu_item_with_left_edge()` 提供共用水平選單顯示。LCD 第一列顯示選項名稱，第二列顯示左右箭頭與 `目前/總數`，LEDR 同步顯示選單位置進度。
- **訊息畫面**：`display_show_info_message()`、`display_show_confirm_message()`、`display_show_error_message()` 提供一致的 modal message。不同功能只要傳入訊息內容，不必各自設計 `KEY0 OK`、`KEY1YES KEY0NO` 或錯誤閃爍。
- **閃爍字樣**：`display_show_blinking_marker()`、`display_show_top_blinking_marker()`、`display_show_bottom_blinking_marker()` 提供可重用的閃爍 marker，例如 `EEPROM`、`SD`、`END`。這些 marker 是 UI hint，不是文件內容。
- **Loading / activity 畫面**：`display_show_activity_marquee()` 提供 blocking I/O 期間的共用等待提示。EEPROM 與 SD card 讀寫都透過 callback 呼叫它；若 LEDR flag hardware 存在，跑馬燈由 Verilog 持續產生。
- **Editor viewport**：`display_update_with_markers()` 統一處理目前行、下一行、水平捲動、cursor mode、top marker、bottom marker、HEX 與 LEDG 狀態。
- **文字檢視畫面**：`display_show_text_page()` 提供 SD `QUESTION.TXT` 的兩列文字瀏覽畫面。
- **Typing game 畫面**：`display_show_typing_game()` 重用 editor viewport 概念，但改成第一列顯示輸入、第二列顯示題目，並同步輸出題號、時間、錯誤提示與秒閃 LEDG8。

因此，`display.c/.h` 在架構上的角色比較接近一個小型 UI component library。`main.c`、`sdcard.c`、`eeprom.c`、`typing_game.c` 等功能模組只決定「現在要顯示哪一種畫面」，實際 LCD 排版、LED 效果、HEX 欄位與 cursor 行為都由 display framework 統一處理。

Editor 模式顯示：

| 輸出 | 內容 |
|---|---|
| LCD row0 | 目前行 viewport，或 top marker |
| LCD row1 | 下一行、目前行或 `END` marker |
| HEX7~6 | 目前行號 |
| HEX5~4 | 游標欄位 |
| HEX3~2 | 總行數 |
| HEX1~0 | `SW[6:0]` ASCII hex |
| LEDR | 文件位置進度 |
| LEDG0 | Insert mode |
| LEDG1 | Nav mode |
| LEDG5 | storage error |
| LEDG6 | 右側 overflow |
| LEDG7 | dirty |

Typing game 顯示：LCD row0 是使用者輸入，row1 是題目；LEDR 是題數進度或錯誤閃爍；LEDG8 是秒閃冒號；HEX7~6 顯示題號或總題數，HEX5~4 分鐘，HEX3~2 秒，HEX1~0 ASCII。

---

## 10. SD card 讀寫與 FAT 流程

`sdcard.c` 透過 Qsys SPI core 操作 SD card，並自行實作最小 FAT16/FAT32 root directory 讀寫。

```mermaid
flowchart TD
    API[sdcard API] --> INIT[sd_init]
    INIT --> CMD0[CMD0 idle]
    CMD0 --> CMD8[CMD8 check v2]
    CMD8 --> ACMD41[ACMD41 wait ready]
    ACMD41 --> CMD58[CMD58 OCR]
    CMD58 --> MOUNT[fat_mount]
    MOUNT --> FIND[fat_find_file]
    FIND --> READ[fat_read_file]
    FIND --> WRITE[fat_write_file]
```

SD 初始化會送 80 clocks、CMD0、CMD8、CMD55+ACMD41、CMD58，並判斷 block addressing。若不是 block-addressed card，會用 CMD16 設定 block length = 512。

FAT mount 會先讀 LBA0；若不是 FAT boot sector，則視為 MBR，讀第一個 partition 的 boot sector。支援 FAT16 / FAT32、root directory、8.3 短檔名。不支援 exFAT、長檔名、子目錄 traversal。

讀 `QUESTION.TXT` 或 `EDITOR.TXT`：`sd_init()` → `fat_mount()` → `fat_find_file()` → `fat_read_file()` → copy 到 C buffer 並補 `\0`。若 buffer 不夠，回傳 `SDCARD_OK_TRUNCATED`。

寫 `EDITOR.TXT`：`sd_init()` → `fat_mount()` → 搜尋既有檔與 free directory slot → 視 overwrite 決定是否覆寫 → 分配或重用 cluster → 寫 data sector → 更新 directory entry → 必要時釋放舊 chain。

SD 讀寫期間會透過 activity callback 顯示 LEDR activity marquee。這不是進度條，只代表 blocking I/O 正在執行。

---

## 11. EEPROM 讀寫流程

EEPROM 使用 C 端 bit-bang I2C。SDA 只能 drive low 或 release high-Z。

```mermaid
flowchart TD
    APP[main.c] --> INIT[eeprom_init]
    INIT --> SAVELOAD{save or load}
    SAVELOAD -->|save| SER[editor_serialize]
    SER --> PAGE[32-byte page write]
    PAGE --> ACK[ACK polling]
    SAVELOAD -->|load| READ[random read + sequential read]
    READ --> DESER[editor_deserialize]
```

重點：control byte 使用 `0xA0` write、`0xA1` read；page size 為 32 bytes；寫入時不跨 page boundary；每頁寫入後使用 ACK polling 等待內部寫入完成；讀取整份 3210-byte editor layout 後，用 magic/version/checksum 判斷是否有效。

---

## 12. Typing game 流程

```mermaid
flowchart TD
    HOME[Home menu] --> MODE[Select case mode]
    MODE --> COUNT[Select question count]
    COUNT --> READY[SW6-0 OFF then KEY1]
    READY --> LOAD[Read QUESTION.TXT]
    LOAD --> PICK[Random pick non-empty lines]
    PICK --> GAME[Typing game]
    GAME --> CHECK{answer correct?}
    CHECK -->|yes next| GAME
    CHECK -->|yes final| RESULT[Show CPM]
    CHECK -->|length enough but wrong| ERROR[2 sec LEDR error blink]
    GAME -->|KEY0| PAUSE[Restart / Continue / Quit]
```

Typing game 從 SD `QUESTION.TXT` 讀題目，最多支援 50 題。題目來源是非空行，長度最多 `LINE_LEN`。大小寫模式包含 `Capitalized`、`Default`、`Random Caps`。

輸入沿用 `editor_input.c`，但 `allow_newline = 0`，因此 Enter 不會換行。秒表使用 Qsys timer 的 `alt_nticks()`，第一次 SW 變化或實際輸入時啟動。完成後以總字數與時間計算 CPM。

---

## 13. 維護與報告重點

報告可強調以下特色：

1. 硬體與軟體分工清楚：Verilog 處理 PS/2 timing 與 LEDR blocking animation，C 處理 UI、editor、SD/FAT、typing game。
2. 同一份 `EditorDocument` 被 EEPROM editor、SD editor、typing game input 重用。
3. `display.c/.h` 是可重用 UI 畫面框架，將選單、閃爍字樣、loading 跑馬燈、modal message、editor viewport、typing game display 等畫面 pattern 提供給不同功能呼叫。
4. SD card 不是 raw sector demo，而是支援 FAT16/FAT32 root directory 讀寫固定短檔名。
5. blocking I/O 期間仍能由 Verilog LEDR controller 顯示 activity marquee。
6. PS/2 keyboard 透過 FIFO 與 PIO handshake 接入 Nios，保留 SW/KEY 測試輸入。

後續修改提醒：Qsys 改動後一定要更新 BSP；LEDR flag bit 若改動，需同步更新 `display.h`、`ledr_flag_controller.v`、`docs/ledr_flag.md`；新增 UI 畫面時優先擴充 `display.c/.h`；新增選單時優先使用 `menu.c/.h`；SD card 目前不支援長檔名、子目錄與 exFAT；EEPROM layout 若改版，應調整 version 或加入相容讀取。
