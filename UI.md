# UI.md

本檔說明 `software/niosapp/` 目前的 UI 層做法。修改 LEDR、LEDG、HEX 或 LCD 行為前，請先讀本檔與 `software/niosapp/display.h`。

## 設計原則

- UI 輸出集中在 `display.c/.h`。`main.c`、`eeprom.c`、`sdcard.c` 等功能流程應呼叫 display API，不要自行組 LED bit mask、寫 LCD 字串 viewport，或直接操作 `PIO_OUT_LEDR_BASE` / `PIO_OUT_LEDG_BASE` / HEX PIO。
- LEDR 的長時間活動效果可由 Verilog `ledr_flag_controller.v` 產生。C 端仍只能透過 `display.c/.h` 寫 `PIO_OUT_LEDR_BASE` 或 `PIO_OUT_LEDR_FLAG_BASE`，不要在功能流程直接寫 flag。
- `lcd.c/.h` 是 LCD1602 低階 driver，負責 command/data timing、cursor、兩行文字寫入。一般畫面邏輯不要直接使用它，除非是在擴充 display 層。
- `seven_seg.c/.h` 只處理 active-low 七段顯示編碼。數字欄位如何切位、blank 或顯示十進位/十六進位，應由 `display.c` 決定。
- UI 命名要反映真實語意。跑馬燈是 activity hint，不是 progress；progress bar 才代表百分比。

## 主要模組

- `display.c/.h`：UI facade。負責 LEDR、LEDG、HEX、LCD 的組合顯示與共用 UI helper。
- `ledr_flag_controller.v`：硬體 LEDR effect controller，根據 `pio_out_ledr_flag` 選擇 Nios LEDR 或 Verilog 跑馬燈/閃爍。
- `ledr_source_mux.v` / `hw03_Mux41.v`：LEDR source mux；`hw03_Mux41.v` 複製自 `D:\quartus\BDF_HDL`。
- `menu.c/.h`：共用水平選單狀態機。呼叫端只提供 null-terminated 選項列表，`menu_update()` 會處理 `KEY3` / `KEY2` / `KEY0` 並呼叫 display 層刷新 LCD；`menu_update_with_left_edge()` 支援第一個選項再按 `KEY3` 時回到呼叫端自訂的第 0 頁。
- `lcd.c/.h`：LCD1602 8-bit PIO bit-bang driver。
- `seven_seg.c/.h`：七段顯示 active-low 編碼 helper。

## 可使用的 display API

### 畫面初始化與 editor 主畫面

- `display_init()`：初始化 LCD，清空 HEX、LEDR、LEDG，重設 display 內部狀態。
- `display_update(editor, ascii, nav_mode, eeprom_error)`：editor 主畫面預設入口。會更新 LEDR 文件位置、LEDG 狀態、HEX 數字、LCD 目前行/下一行與 cursor mode，並以 `END` 作為文件最後一行後方的 bottom marker。
- `display_update_with_markers(editor, ascii, nav_mode, eeprom_error, top_marker, bottom_marker)`：editor viewport 的通用 marker 入口。`top_marker` 是文件第 0 行前方的閃爍 boundary marker；游標在第 0 行時第一列顯示 marker、第二列顯示第 0 行並放置 LCD cursor，游標離開第 0 行後 marker 會離開可視範圍。`bottom_marker` 是文件最後一行後方的閃爍 boundary marker，`END` 也由此機制顯示。

### LCD 狀態畫面

- `display_show_menu_item(option_name, selected_index, option_count)`：顯示共用水平選單目前選項。LCD 第一列顯示 `option_name`；第二列第二格在左側仍有選項時顯示 `<`，倒數第二格在右側仍有選項時顯示 `>`，中央顯示十進位 `目前/總數`，支援到 `99/99`。
- `display_show_menu_item_with_left_edge(option_name, selected_index, option_count)`：顯示前方還有第 0 頁的水平選單；即使目前在第 1 頁，也會顯示左箭頭，提示 `KEY3` 可回到第 0 頁。
- `display_show_message(line0, line1)`：顯示兩行固定狀態文字，並隱藏 cursor。
- `display_show_vi_command(command)`：顯示 editor menu 的第 0 頁。第一列以 `:` 加上目前 command buffer，LCD cursor 放在命令尾端；第二列顯示 `VI COMMAND` 與右箭頭，提示 `KEY2` 可進入水平選單。
- `display_show_info_message(message)`：第一列顯示訊息，第二列置中 `KEY0 OK`，等待呼叫端用 `KEY0` 返回，LEDR 使用 `pio_out_ledr_flag` 的 2 Hz 全燈閃爍。
- `display_show_confirm_message(message)`：第一列顯示訊息，第二列顯示 `KEY1YES KEY0NO`，等待呼叫端用 `KEY1` 接受或 `KEY0` 取消，LEDR 使用 2 Hz 全燈閃爍。
- `display_show_error_message(message)`：第一列顯示錯誤訊息，第二列置中 `KEY0 OK`，等待呼叫端用 `KEY0` 返回，LEDR 使用 5 Hz 全燈閃爍。
- `display_show_text_page(text, length, first_line)`：將 newline-delimited 文字以兩列 LCD 顯示，現在用於 SD `QUESTION.TXT` 測試畫面。
- `display_show_blinking_marker(row, word)`：在指定 LCD row 顯示閃爍 marker。函數會將 `word` 自動置中並用 `-` 補滿 16 欄，例如 `END` 會顯示為 `------END-------`。這是 UI hint，不是文件內容。
- `display_show_top_blinking_marker(word)` / `display_show_bottom_blinking_marker(word)`：對第一列 / 第二列 marker 的語意化 wrapper。

### 共用選單狀態機

- `menu_init(menu, options)`：從 null-terminated `const char *const options[]` 初始化選單，最多計入 99 個選項。
- `menu_update(menu, keys)`：處理 `KEY3` 向左、`KEY2` 向右、`KEY0` 確認，並刷新 LCD；未確認時回傳 `MENU_NO_SELECTION`，確認時回傳 zero-based option index。
- `menu_update_with_left_edge(menu, keys, callback, context)`：和 `menu_update()` 相同，但在第一個選項收到 `KEY3` 時呼叫 `callback(context)` 並交回呼叫端處理第 0 頁；顯示第 1 頁時會保留左箭頭，提示可回到第 0 頁。

### LEDR

- `display_show_progress_percent(percent)`：顯示 LEDR progress bar，方向是 `LEDR17` 往 `LEDR0`。
  - `percent == 0`：全暗。
  - `1..99`：只使用 `LEDR17..LEDR1`，不亮 `LEDR0`。
  - `percent >= 100`：亮到 `LEDR0`，也就是完整 100%。
- `display_show_activity_marquee(tick)`：顯示 activity 跑馬燈，不代表進度。若 BSP 已提供 `PIO_OUT_LEDR_FLAG_BASE`，此函數只寫 `pio_out_ledr_flag` 交給 Verilog 自行跑燈；若 BSP 尚未更新，會退回舊的 C 端 `LEDR17..LEDR1` 單燈 fallback。
- `display_save_marquee(tick)`：舊名稱相容 wrapper；新程式優先呼叫 `display_show_activity_marquee()`。

`pio_out_ledr_flag` bit 定義：

- bit0 `0x01`：`1` = Nios `PIO_OUT_LEDR` 控制；`0` = Verilog effect 控制。
- bit1 `0x02`：`LEDR17` 往 `LEDR0` 跑馬燈。
- bit2 `0x04`：`LEDR0` 往 `LEDR17` 跑馬燈。
- bit3 `0x08`：全 LEDR 2 Hz 閃爍，給一般確認/等待訊息使用。
- bit4 `0x10`：全 LEDR 5 Hz 閃爍，給錯誤確認/等待訊息使用。
- bit5..7：保留，寫 `0`。

完整硬體 priority 與接線說明在 `docs/ledr_flag.md`，`AGENTS.md` 也有連結。

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
6. 新增多選項 LCD 頁面時優先使用 `menu.c/.h`，讓功能流程只提供選項列表與處理確認結果，不自行重複 KEY navigation 或 LCD counter 版面。若前面需要 command page 或其他第 0 頁，使用 `menu_update_with_left_edge()`。
7. 更新 `UI.md` 與必要的 `AGENTS.md` / `software/niosapp/readme.txt` 說明，避免文件和實作漂移。

## 現有畫面分工

- 開機選單：`main.c` 提供 `EEPROM EDITOR` / `SD QUESTION` 選項列表給 `menu.c`；`KEY3` / `KEY2` 移動，`KEY0` 確認。
- Editor command / 選單：在 editor 主畫面按 `KEY0` 後，`main.c` 先進 `APP_STATE_EDITOR_COMMAND`，用 `display_show_vi_command()` 顯示 `:VI COMMAND`。空指令返回、`w` 存檔、`q` 離開、`wq` / `x` 存檔後離開、`e!` restore whole；`KEY2` 進入水平選單。水平選單提供 `Save to ROM` / `Quit` / `Restore whole` / `Clear this line` / `Clear All` / `Move to head` / `Move to end` / `Cancel`，並用 `menu_update_with_left_edge()` 讓 `Save to ROM` 再按 `KEY3` 回 command page。
- Modal 訊息：`main.c` 以 `APP_STATE_INFO_MESSAGE`、`APP_STATE_CONFIRM_MESSAGE`、`APP_STATE_ERROR_MESSAGE` 包裝 display 層的互動訊息。`Save to ROM` 會用一般/錯誤訊息回報結果，`Clear All` 與 dirty `Quit` 會先進確認訊息，EEPROM 載入失敗會進錯誤訊息。
- EEPROM 載入/儲存等待：`eeprom_*_with_activity()` callback 透過 `display_show_activity_marquee()` 顯示 activity。
- SD 讀取與檢視：讀取中用 `display_show_message()`，並透過 `sdcard_read_question_text_with_activity()` callback 呼叫 activity marquee；成功後用 `display_show_text_page()`，同時回到 Nios LEDR 控制。
- EEPROM editor 主畫面：每輪由 `display_update_with_markers(..., "EEPROM", "END")` 統一刷新，不在 `main.c` 個別操作 LED 或 LCD。

## 維護檢查

- `rg -n "PIO_OUT_LEDG_BASE|PIO_OUT_LEDR_BASE|PIO_OUT_HEX|PIO_OUT_LCD" software/niosapp` 應能看出 display PIO 操作集中在 display/lcd/seven-seg 相關實作；一般功能模組不應新增直接 display PIO 寫入。
- `rg -n "display_show_menu_item|menu_update" software/niosapp` 應能看出選單 navigation 集中在 `menu.c`，LCD 版面集中在 `display.c`。
- 修改 UI 後至少執行 app-only build。若 Quartus 13.1 的 `nios2-stackreport` 在目前 Windows/Cygwin 環境失敗，可用 `DISABLE_STACKREPORT=1` 重新跑 app target。
- 若新增或重命名 Qsys PIO，先 Generate HDL 再更新 BSP；`PIO_OUT_LEDR_FLAG_BASE` 不在 `system.h` 時，C 端只會使用 fallback，不會真的寫硬體 flag。
