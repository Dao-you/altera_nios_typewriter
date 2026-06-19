# UI.md

本檔說明 `software/niosapp/` 目前的 UI 層做法。修改 LEDR、LEDG、HEX 或 LCD 行為前，請先讀本檔與 `software/niosapp/display.h`。

## 設計原則

- UI 輸出集中在 `display.c/.h`。`main.c`、`eeprom.c`、`sdcard.c` 等功能流程應呼叫 display API，不要自行組 LED bit mask、寫 LCD 字串 viewport，或直接操作 `PIO_OUT_LEDR_BASE` / `PIO_OUT_LEDG_BASE` / HEX PIO。
- `lcd.c/.h` 是 LCD1602 低階 driver，負責 command/data timing、cursor、兩行文字寫入。一般畫面邏輯不要直接使用它，除非是在擴充 display 層。
- `seven_seg.c/.h` 只處理 active-low 七段顯示編碼。數字欄位如何切位、blank 或顯示十進位/十六進位，應由 `display.c` 決定。
- UI 命名要反映真實語意。跑馬燈是 activity hint，不是 progress；progress bar 才代表百分比。

## 主要模組

- `display.c/.h`：UI facade。負責 LEDR、LEDG、HEX、LCD 的組合顯示與共用 UI helper。
- `menu.c/.h`：共用水平選單狀態機。呼叫端只提供 null-terminated 選項列表，`menu_update()` 會處理 `KEY3` / `KEY2` / `KEY0` 並呼叫 display 層刷新 LCD。
- `lcd.c/.h`：LCD1602 8-bit PIO bit-bang driver。
- `seven_seg.c/.h`：七段顯示 active-low 編碼 helper。

## 可使用的 display API

### 畫面初始化與 editor 主畫面

- `display_init()`：初始化 LCD，清空 HEX、LEDR、LEDG，重設 display 內部狀態。
- `display_update(editor, ascii, nav_mode, eeprom_error)`：editor 主畫面唯一入口。會更新 LEDR 文件位置、LEDG 狀態、HEX 數字、LCD 目前行/下一行與 cursor mode。

### LCD 狀態畫面

- `display_show_menu_item(option_name, selected_index, option_count)`：顯示共用水平選單目前選項。LCD 第一列顯示 `option_name`；第二列第二格在左側仍有選項時顯示 `<`，倒數第二格在右側仍有選項時顯示 `>`，中央顯示十進位 `目前/總數`，支援到 `99/99`。
- `display_show_message(line0, line1)`：顯示兩行固定狀態文字，並隱藏 cursor。
- `display_show_text_page(text, length, first_line)`：將 newline-delimited 文字以兩列 LCD 顯示，現在用於 SD `QUESTION.TXT` 測試畫面。
- `display_show_blinking_marker(row, word)`：在指定 LCD row 顯示閃爍 marker。函數會將 `word` 自動置中並用 `-` 補滿 16 欄，例如 `END` 會顯示為 `------END-------`。這是 UI hint，不是文件內容。

### 共用選單狀態機

- `menu_init(menu, options)`：從 null-terminated `const char *const options[]` 初始化選單，最多計入 99 個選項。
- `menu_update(menu, keys)`：處理 `KEY3` 向左、`KEY2` 向右、`KEY0` 確認，並刷新 LCD；未確認時回傳 `MENU_NO_SELECTION`，確認時回傳 zero-based option index。

### LEDR

- `display_show_progress_percent(percent)`：顯示 LEDR progress bar，方向是 `LEDR17` 往 `LEDR0`。
  - `percent == 0`：全暗。
  - `1..99`：只使用 `LEDR17..LEDR1`，不亮 `LEDR0`。
  - `percent >= 100`：亮到 `LEDR0`，也就是完整 100%。
- `display_show_activity_marquee(tick)`：顯示 activity 跑馬燈，只在 `LEDR17..LEDR1` 之間移動單顆 LED，不代表進度，也永遠不使用 `LEDR0`。
- `display_save_marquee(tick)`：舊名稱相容 wrapper；新程式優先呼叫 `display_show_activity_marquee()`。

### LEDG

LEDG 採用類似物件導向的控制方式：外部指定 indicator，不直接組 bit mask。

- `display_set_ledg(indicator, enabled)`：設定或清除單一 LEDG indicator。
- `display_clear_ledg()`：清空所有 LEDG indicator。

目前 indicator 定義在 `display.h`：

- `DISPLAY_LEDG_INSERT`：LEDG0，Insert mode。
- `DISPLAY_LEDG_NAV_MODE`：LEDG1，上下移動模式。
- `DISPLAY_LEDG_EEPROM_ERROR`：LEDG5，EEPROM error。
- `DISPLAY_LEDG_OVERFLOW`：LEDG6，目前 LCD viewport 右側還有未顯示文字。
- `DISPLAY_LEDG_DIRTY`：LEDG7，文件有 unsaved changes。

新增 LEDG 狀態時，請先在 `DisplayLedgIndicator` enum 加入明確名稱，再由 `display.c` 統一寫出，不要在功能流程直接寫 `PIO_OUT_LEDG_BASE`。

## 新增 UI 功能流程

1. 先判斷是低階 driver 問題還是 UI 行為問題。LCD timing、PIO bit mapping 才改 `lcd.c`；畫面內容、狀態燈、progress、marquee、marker 都改 `display.c/.h`。
2. 先在 `display.h` 加清楚的共用 API，讓 `main.c` 或其他模組只描述「要顯示什麼狀態」，不要描述「哪顆 LED 要亮」。
3. 若功能是百分比，使用或擴充 `display_show_progress_percent()`，並保留 `100%` 才亮最後一顆 LED 的規則。
4. 若功能只是等待或載入中的視覺活動，使用 `display_show_activity_marquee()`，不要把它描述為進度。
5. 若需要 LCD 分隔提示文字，使用 `display_show_blinking_marker()`，不要硬寫固定 16 字元 marker。
6. 新增多選項 LCD 頁面時優先使用 `menu.c/.h`，讓功能流程只提供選項列表與處理確認結果，不自行重複 KEY navigation 或 LCD counter 版面。
7. 更新 `UI.md` 與必要的 `AGENTS.md` / `software/niosapp/readme.txt` 說明，避免文件和實作漂移。

## 現有畫面分工

- 開機選單：`main.c` 提供 `EEPROM EDITOR` / `SD QUESTION` 選項列表給 `menu.c`；`KEY3` / `KEY2` 移動，`KEY0` 確認。
- Editor 選單：在 editor 主畫面按 `KEY0` 後，`main.c` 提供 `Save to ROM` / `Clear this line` / `Clear All` / `Move to head` / `Move to end` 選項列表給 `menu.c`；確認後回到 editor 主畫面。
- EEPROM 載入/儲存等待：`eeprom_*_with_activity()` callback 透過 `display_show_activity_marquee()` 顯示 activity。
- SD 讀取與檢視：讀取中用 `display_show_message()`，並透過 `sdcard_read_question_text_with_activity()` callback 呼叫 activity marquee；成功後用 `display_show_text_page()`。
- Editor 主畫面：每輪由 `display_update()` 統一刷新，不在 `main.c` 個別操作 LED 或 LCD。

## 維護檢查

- `rg -n "PIO_OUT_LEDG_BASE|PIO_OUT_LEDR_BASE|PIO_OUT_HEX|PIO_OUT_LCD" software/niosapp` 應能看出 display PIO 操作集中在 display/lcd/seven-seg 相關實作；一般功能模組不應新增直接 display PIO 寫入。
- `rg -n "display_show_menu_item|menu_update" software/niosapp` 應能看出選單 navigation 集中在 `menu.c`，LCD 版面集中在 `display.c`。
- 修改 UI 後至少執行 app-only build。若 Quartus 13.1 的 `nios2-stackreport` 在目前 Windows/Cygwin 環境失敗，可用 `DISABLE_STACKREPORT=1` 重新跑 app target。
