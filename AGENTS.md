# AGENTS.md

本檔案是給 Codex/代理在 `D:\quartus\quartusFinalProject` 工作時使用的專案指引。請先讀本檔，再修改 Quartus、Qsys、Verilog 或 Nios II C 程式。

## 專案目標

本專案使用 ALTERA DE2-115 FPGA 開發板，在板上實作一個簡易文字編輯器。主架構採用 Nios II + C 語言：Qsys/Platform Designer 建立 CPU 與 Avalon PIO，C 程式負責讀取 SW/KEY、維護文件 buffer，並控制 LED、HEX、LCD 與 EEPROM。

主要功能規劃：

- 開機後 LCD 先顯示共用選單：`KEY3` / `KEY2` 在選項間左右移動，`KEY1` 確認。目前選項順序為 `EEPROM EDITOR`、`SD EDITOR`、`SD QUESTIONS` 與 `TYPING GAME`。
- `SW[6:0]` 輸入 7-bit ASCII。
- 進入文字編輯器後，`KEY1` 寫入目前 ASCII；若 ASCII 是 `0x08` 執行 BS 刪除左側字元，行頭時刪除上一行 LF 並合併行，`0x0A` 執行 LF 換行，`0x7F` 執行 DEL 刪除游標所在字元。
- 進入文字編輯器後，`KEY0` 先開啟 editor command mode。第一列以 `:` 提供簡易 VI 指令輸入，第二列顯示 `VI COMMAND` 與右箭頭，LEDR 使用 2 Hz 全燈閃爍；`KEY2` 可從 command mode 進入水平 editor 選單。支援指令為空值返回、`w` 存到目前 editor 的 primary storage、`q` 離開、`wq` / `x` 存檔後離開、`e!` 從目前 editor 的 primary storage 重新載入整份文件。EEPROM editor 選單選項為 `Save to ROM`、`Save as SD`、`Quit`、`Restore whole`、`Clear this line`、`Clear All`、`Move to head`、`Move to end`、`Cancel`；SD editor 選單選項為 `Save`、`Save as EEPROM`、`Quit`、`Restore whole`、`Clear this line`、`Clear All`、`Move to head`、`Move to end`、`Cancel`。在第一個選項按 `KEY3` 會回到 command mode，且第 1 頁會顯示左箭頭提示可返回第 0 頁；選單頁面以 LEDR 顯示 `目前選項/選項總數` 進度條。
- `SW16` 切換 Insert / Overwrite：`0` 為 Overwrite，`1` 為 Insert。
- `SW17` 切換移動模式：左右 / 上下。
- `KEY3` 往前 / 往上，`KEY2` 往後 / 往下。
- 一般 editor viewport 可用 LCD 第一列顯示目前編輯行、第二列顯示下一行；若目前已是最後一行，第二列以接近 LCD 內建游標的頻率閃爍顯示 `------END-------` 標記，避免與真實文字混淆。目前 EEPROM editor 主畫面在文件第 0 行之前加入置中的閃爍 `-----EEPROM-----` marker：游標在第 0 行時 marker 顯示在第一列、文件第 0 行顯示在第二列；游標離開第 0 行後 marker 會像 `END` 一樣離開可視範圍。
- HEX 以十進位顯示目前行號、游標位置、總行數；`HEX1~HEX0` 以十六進位顯示目前 ASCII。
- LEDR 平時顯示目前行在文件總行數中的位置，從 `LEDR17` 往 `LEDR0` 亮；只有目前行是最後一行時 `LEDR0` 才亮。
- editor 選單確認 `Save to ROM` 且實際寫入 EEPROM 期間，LEDR 暫時改為 Verilog 控制的單燈跑馬燈；這只是儲存中視覺效果，不代表儲存進度。
- LEDG 顯示模式、PS/2 鍵盤偵測/輸入/Caps Lock、目前 LCD 視窗右側是否還有未顯示內容、unsaved 狀態。
- 文件先存在 C 程式的 Document Buffer，後續寫入 DE2-115 板上 24LC32 類 EEPROM。
- PS/2 鍵盤由 Verilog 接收 scan code 並轉成 ASCII / editor control byte，透過 Qsys PIO FIFO 介面交給 Nios II C 程式，原本 SW/KEY 測試輸入仍保留。
- SD 卡以 Qsys `altera_avalon_spi` SPI mode 讀寫 FAT16/FAT32 根目錄短檔名。開機選單確認 `SD QUESTIONS` 會讀 `QUESTION.TXT` 並在 LCD 顯示內容，`KEY0` 返回首頁選單；開機選單確認 `SD EDITOR` 會載入固定短檔名 `EDITOR.TXT` 到 editor。EEPROM editor 的 `Save as SD` 也寫入 `EDITOR.TXT`，若檔案已存在會用既有 confirmation UI 顯示 `Overwrite SD?`，`KEY1` 確認覆寫、`KEY0` 取消。SD editor 的 `Save` 直接覆寫 `EDITOR.TXT`，`Save as EEPROM` 會先用 confirmation UI 顯示 `Overwrite ROM?`。
- 打字速度遊戲從開機選單 `TYPING GAME` 進入。先用共用選單選題目大小寫模式：`Capitalized` 會將字母轉小寫後把第一個字母轉大寫，`Default` 保留題目原文，`Random Caps` 會將字母轉小寫後在 runtime 隨機把每個字母轉大寫；非字母不轉換，各選單最後都有 `Quit`。接著用共用選單選題目數量，5 題一級距、最多 50 題。選完後 LCD 提示關閉 `SW[6:0]`，`KEY1` 開始、`KEY0` 返回；開始時從 SD `QUESTION.TXT` 非空行隨機抽取所選題數，讀取期間使用 loading 燈號。遊戲中共用 editor 的 SW/KEY/PS/2 輸入派發，但答案限制為單行，完全輸入正確後自動進下一題；字數達到或超過題目長度但內容不符時，LEDR 顯示 2 秒既有 5 Hz error effect；`KEY0` 進入 `Restart` / `Continue` / `Quit` 暫停選單。LCD 第一列顯示輸入、第二列顯示題目；秒表使用 Qsys `timer` 作為 HAL system clock，從第一個 `SW[6:0]` 變化或第一個實際輸入動作開始。LEDR 顯示題數進度，LEDG0/1/6 顯示 Insert / 移動模式 / overflow，LEDG2/3/4 顯示 PS/2 detected/activity/Caps Lock，獨立 `LEDG8` 每秒閃爍作為 `mm:ss` 冒號；HEX7~HEX6 每四秒循環顯示三秒目前題號與一秒總題數，HEX5~HEX4 顯示分鐘，HEX3~HEX2 顯示秒數，HEX1~HEX0 顯示 `SW[6:0]` 十六進位。完成後以一般 UI 訊息顯示 `CPM n.nn` 與 `KEY0 OK`，按 `KEY0` 回首頁。

## 目前狀態

目前已不是空 Qsys 專案。觀察到的狀態如下：

- Quartus 專案：`EP4.qpf` / `EP4.qsf`
- Top-level entity：`top`
- 裝置：Cyclone IV E `EP4CE115F29C7`
- Quartus 版本紀錄：Quartus II 13.1 Web Edition
- Qsys 系統：`nios.qsys`
- 已產生：`nios.sopcinfo`、`nios/synthesis/nios.qip`
- Nios II Eclipse app：`software/niosapp`
- BSP：`software/niosapp_bsp`
- 目前 C 程式：`software/niosapp/` 內的模組化 Nios II C editor app。
- 目前 PS/2 Verilog 模組在專案根目錄：`ps2_keyboard_controller.v`、`ps2_receiver.v`、`ps2_scancode_parser.v`、`ps2_ascii_mapper.v`、`keyboard_fifo.v`、`keyboard_pio_interface.v`。
- 目前 Qsys 已有 `spi_sdcard` Avalon SPI master，並已匯出到 `qsys_verilog_example.v` / `nios/synthesis/nios.v` 的 `spi_sdcard_external_*` ports。
- 目前 Qsys 已有 `pio_out_ledr_flag` 8-bit output PIO，預計匯出為 `pio_out_ledr_flag_external_connection_export`，給 Verilog LEDR effect controller 使用。
- 目前 Qsys 已有 `timer` Avalon interval timer，BSP 設為 HAL system clock：`ALT_SYS_CLK TIMER`、`TIMER_TICKS_PER_SEC 1000.0`。
- 目前 Qsys 已有獨立 `pio_out_ledg8` 1-bit output PIO，匯出給 `LEDG[8]` 作為打字遊戲時間冒號，不混入一般 `PIO_OUT_LEDG_BASE` 8-bit 狀態/progress PIO。
- 最近 Quartus flow 報告顯示成功，輸出 bitstream 在 `output_files/EP4.sof`
- 最近 Nios app-only build 成功，輸出程式為 `software/niosapp/niosapp.elf`。

目前 `software/niosapp/` 已實作：

- `main.c`：開機選單與主迴圈狀態切換；開機時提供選項列表給 `menu.c`，確認後進入 EEPROM editor、SD `QUESTION.TXT` 讀取測試畫面、SD `EDITOR.TXT` editor 或打字遊戲；editor 內 `KEY0` 進入 `:VI COMMAND` command mode，再可用 `KEY2` 進入 editor 選單；`KEY1` 寫入，`KEY2/KEY3` 移動。
- `editor.c/.h`：固定大小 `EditorDocument`、Insert / Overwrite、BS、LF、DEL、左右上下移動、清除目前行、清除全文、移到文件開頭/結尾、dirty flag、EEPROM 固定格式序列化 / 反序列化，以及 SD editor 用 newline-delimited ASCII 匯入 / 匯出。
- `editor_input.c/.h`：共用 SW/KEY 與 PS/2 decoded byte 到 `EditorDocument` 的派發；EEPROM editor 允許 LF，打字遊戲重用同一派發但禁用 LF。
- `typing_game.c/.h`：打字遊戲題庫抽樣、大小寫模式轉換、單行答案比對、回合狀態、restart 與 Qsys timer 秒表狀態。
- `menu.c/.h`：共用 LCD 選單狀態機；呼叫端只提供 null-terminated 選項列表，`KEY3` / `KEY2` 左右移動，`KEY1` 確認並回傳 option index；需要第 0 頁時可用 `menu_update_with_left_edge()` 在第一個選項按 `KEY3` 時回呼呼叫端，並讓第一個選項也顯示左箭頭提示可回第 0 頁。
- `display.c/.h`：LEDR、LEDG、HEX、LCD 更新；HEX7~HEX2 使用十進位，HEX1~HEX0 顯示 ASCII 十六進位；LCD 支援一般 editor viewport、打字遊戲輸入/題目 viewport、通用 top/bottom boundary marker、VI command page、上下列閃爍 marker、互動式一般/確認/錯誤訊息；共用選單畫面第一列顯示選項名稱，第二列用 `<` / `>` 與十進位 `目前/總數` 顯示位置，LEDR 顯示選項位置進度條；VI command page 使用 LEDR 2 Hz 全燈閃爍；EEPROM 讀寫、SD 讀取與打字遊戲載入期間顯示 LEDR 跑馬燈；打字遊戲中 `PIO_OUT_LEDG8_BASE` 獨立控制 `LEDG8` 秒閃冒號。
- `lcd.c/.h`：LCD1602 8-bit PIO bit-bang、兩行文字更新、LCD 內建 cursor 模式切換。
- `key.c/.h`：active-low KEY 讀取、簡單 debounce、pressed-edge 偵測。
- `keyboard.c/.h`：讀取 Verilog PS/2 keyboard FIFO PIO，將 decoded byte 交回現有 editor action。
- `eeprom.c/.h`：24LC32 類 I2C bit-bang、全文件 load/save、32-byte page write、ACK polling、讀寫中 activity callback。
- `sdcard.c/.h`：透過 Qsys SPI core 做 SD card SPI mode 初始化、FAT16/FAT32 root directory 讀取 `QUESTION.TXT` 給 LCD/打字遊戲，並讀寫固定 `EDITOR.TXT` 給 SD editor / EEPROM editor `Save as SD`。blocking SD 讀寫都透過 activity callback 顯示 LEDR activity marquee。
- `seven_seg.c/.h`：active-low HEX 編碼與 blank。

重要差異：

- `top.v` 目前將 `SW[14]` 接成整個 Nios 系統的 active-low `reset_n` 來源：`SW14=0` reset，`SW14=1` run。
- `SW15` 控制是否允許 PS/2 鍵盤輸入：`0` 時 C 端清除並忽略鍵盤 FIFO，`1` 時正常接受輸入。
- `SW16` 已保留給文字編輯器 Insert / Overwrite 模式，不要再拿來當 reset。
- `hello_world.c` 已移除；目前 app 入口在 `main.c`。
- 目前 `system.h` 顯示 BSP system timer 已設定為 Qsys `timer`：`ALT_SYS_CLK TIMER`。打字遊戲秒表可用 `alt_nticks()`；一般短延遲仍可用 `alt_busy_sleep()`。
- EEPROM PIO 已存在，不是未新增狀態。
- PS/2 keyboard PIO 已由 Qsys 新增，但 Qsys 重新 Generate HDL 後仍必須更新 Nios BSP，否則 `system.h` 內 PIO base address 會停在舊版而導致 C 程式寫錯外設。
- SD card SPI core 已由 Qsys 新增；Qsys 重新 Generate HDL 後同樣必須更新 Nios BSP，確認 `system.h` 內有 `SPI_SDCARD_BASE`。

## 重要目錄

- `docs/initial_plan.md`：完整專題計畫，包含硬體對應、C 資料結構、開發階段與 EEPROM 儲存規劃。
- `docs/孫培鈞112360104_初版提案.md`：初版提案摘要，可用來維持報告口徑。
- `context/01PDF/CH07NIOS.pdf`：Nios II / Qsys 流程講義。
- `context/05SOPC.mp4.srt`：老師建立 Nios/Qsys/Eclipse 的影片字幕，內含很多實務操作提醒。
- `context/00BDF_HDL/`：課堂提供的 BDF/Verilog 基礎元件。
- `context/03EEPROM/24LC32.pdf`：EEPROM datasheet。
- `context/04LCD_DEMO/`：LCD datasheet 與 Verilog demo。
- `UI.md`：目前 Nios C UI 層的維護性說明，包含 `display.c/.h` 的共用 API、LEDR/LEDG/LCD/HEX 顯示約定與新增 UI 功能時的做法。
- `docs/ledr_flag.md`：`pio_out_ledr_flag` bit 定義、Verilog priority、C/BSP fallback 與 `top.v` 接線說明。
- `hw20_CpuExample/`：CPU + PIO + Eclipse app 範例，可參考 Nios 建置流程。
- `hw21_LcdMenu/hw21/`：LCD、EEPROM、key pulse、seven-seg 等更接近本專題的 Verilog 範例。
- `software/niosapp/`：目前應優先修改的 Nios II C application。
- `software/niosapp_bsp/`：BSP 產物。不要手改 generated files，Qsys 改動後用 Eclipse/Nios tools 更新。
- `nios/`、`db/`、`incremental_db/`、`output_files/`：Quartus/Qsys 產物。除非是在重新產生或驗證，不要手改 generated output。

## Qsys / BSP 現況

目前 `nios.qsys` 包含：

- clock source：50 MHz
- Nios II CPU：`cpu`，tiny implementation
- On-Chip Memory：256 KiB，base `0x00040000`
- JTAG UART
- System ID
- SW input PIO：18 bit
- KEY input PIO：4 bit
- LEDR output PIO：18 bit
- LEDG output PIO：8 bit
- LEDG8 output PIO：`pio_out_ledg8`，1 bit，獨立控制 `LEDG[8]`
- HEX0~HEX7 output PIO：各 8 bit
- LCD data output PIO：8 bit
- LCD ctrl output PIO：5 bit
- EEPROM PIO：SCL out、SDA out、SDA output-enable、SDA in
- PS/2 keyboard PIO：keyboard data in 8 bit、keyboard status in 8 bit、keyboard ack out 1 bit
- SD card SPI master：`spi_sdcard`，匯出 MISO / MOSI / SCLK / SS_n
- LEDR flag output PIO：`pio_out_ledr_flag`，8 bit，匯出到 Verilog LEDR effect controller
- Timer：`timer` Avalon interval timer，1 ms tick，BSP HAL system clock

在 C 裡請使用 `software/niosapp_bsp/system.h` 實際產生的 macro 名稱，不要憑計畫文件猜 base name。此專案目前常用名稱是：

- `PIO_IN_KEYBOARD_DATA_BASE`
- `PIO_IN_KEYBOARD_STATUS_BASE`
- `PIO_OUT_KEYBOARD_ACK_BASE`
- `PIO_IN_SW_BASE`
- `PIO_IN_KEY_BASE`
- `PIO_OUT_LEDR_BASE`
- `PIO_OUT_LEDR_FLAG_BASE`
- `PIO_OUT_LEDG_BASE`
- `PIO_OUT_LEDG8_BASE`
- `PIO_OUT_HEX0_BASE` ... `PIO_OUT_HEX7_BASE`
- `PIO_OUT_LCD_DATA_BASE`
- `PIO_OUT_LCD_CTRL_BASE`
- `PIO_OUT_EEP_SCL_BASE`
- `PIO_OUT_EEP_SDA_OUT_BASE`
- `PIO_OUT_EEP_SDA_OE_BASE`
- `PIO_IN_EEP_SDA_IN_BASE`
- `SPI_SDCARD_BASE`

若重新命名 Qsys 元件，BSP macro 會改變；C 程式必須一起更新。

## Top-level 接線約定

`top.v` 是 Quartus top-level，已將 Qsys `nios u0` 接到 DE2-115 腳位。修改前先讀目前版本。

目前重要接線：

- `CLOCK_50` 接 Qsys clock。
- `SW[14]` 產生 `reset_n`；`SW15` 由 C 程式讀取作為 PS/2 鍵盤輸入 enable。
- `PS2_CLK` / `PS2_DAT` 接 `ps2_keyboard_controller`，再以 keyboard data/status/ack PIO 對接 Nios。
- `SD_CLK` / `SD_CMD` / `SD_DAT[3:0]` 接 Qsys `spi_sdcard_external_*`，目前用 SD SPI mode：`SCLK -> SD_CLK`、`MOSI -> SD_CMD`、`MISO <- SD_DAT[0]`、`SS_n -> SD_DAT[3]`，`SD_DAT[1]` / `SD_DAT[2]` 先 release high-Z，`SD_WP_N` 只保留為 top-level input。
- `SW[17:0]` 全部輸入 Nios PIO；C 端使用 `SW17` 作為移動模式、`SW16` 作為 Insert / Overwrite、`SW[6:0]` 作為 ASCII。
- `KEY[3:0]` 全部輸入 Nios PIO。
- `LEDR[17:0]` 由 `ledr_flag_controller.v` 輸出；`pio_out_ledr_flag` bit0 為 `1` 時選擇 Nios `PIO_OUT_LEDR`，bit0 為 `0` 時由 Verilog 燈效控制。`LEDG0`、`LEDG1`、`LEDG5`、`LEDG6`、`LEDG7` 由 `pio_out_ledg` 8-bit PIO 對應位元輸出；`LEDG2`、`LEDG3`、`LEDG4` 由 `ps2_keyboard_controller` 分別輸出 keyboard detected、約 100 ms input activity、Caps Lock 狀態；`LEDG8` 由獨立 `pio_out_ledg8` 1-bit PIO 輸出，專供打字遊戲秒閃冒號使用。
- `pio_out_ledr_flag` 定義請同步參考 `docs/ledr_flag.md` 與 `software/niosapp/display.h`；目前 bit0 為 Nios/Verilog source select，bit1 為 `LEDR17` 到 `LEDR0` 跑馬燈，bit2 為反向跑馬燈，bit3 為 VI command / 一般確認 2 Hz 全燈閃爍，bit4 為錯誤 5 Hz 全燈閃爍，bit5..7 保留。
- HEX PIO 是 8 bit，但 `top.v` 只接 `hex*_export[6:0]` 到 `HEX*`，bit 7 可當小數點保留位或忽略。
- LCD ctrl bit mapping：
  - bit 0：`LCD_RS`
  - bit 1：`LCD_RW`
  - bit 2：`LCD_EN`
  - bit 3：`LCD_ON`
  - bit 4：`LCD_BLON`
- EEPROM SDA 是 open-drain 類接法：只能 drive low 或 release high-Z，不能主動 drive high。
- `UART_TXD` 目前固定為 `1'b1`，不要假設板上 UART 已接到 Nios。
- `keyboard_status` bit 0 表示 FIFO 有資料，bit 1 表示 FIFO full，bit 2 表示 FIFO overflow/error；C 端讀完 `keyboard_data` 後 pulse `keyboard_ack` 取下一筆。
- PS/2 controller 是 receive-only，無法在鍵盤完全靜止時主動輪詢插拔狀態；`LEDG2` 因此定義為 reset 後曾收到有效 PS/2 frame，拔除後需 reset 才會清除。`LEDG3` 將每個有效 frame 的活動 pulse 延長約 100 ms，`LEDG4` 直接反映 parser 的 Caps Lock toggle 狀態。
- SD card `spi_sdcard` 目前只用 C 端輪詢讀寫，不使用 interrupt；`sdcard.c` 只支援 FAT16/FAT32 根目錄 8.3 短檔名，`QUESTION.TXT` 為讀取題庫/檢視用，`EDITOR.TXT` 為 SD editor 固定讀寫檔。`EDITOR.TXT` writer 會在覆寫既有檔案且 cluster chain 足夠時原地重用，只寫 file size 實際覆蓋的 sectors；建檔時 FAT32 會優先使用 FSInfo next-free hint，避免 16GB 等大卡從 FAT 開頭慢速掃描。

## C 程式開發約定

優先在 `software/niosapp/` 內發展應用程式。若程式變大，建議拆成：

- `main.c`：初始化與主迴圈
- `menu.c` / `menu.h`：共用選單狀態機
- `editor.c` / `editor.h`：Document Buffer、insert/overwrite、BS、LF 換行、DEL、游標移動
- `editor_input.c` / `editor_input.h`：共用 SW/KEY/PS/2 editor input 派發
- `typing_game.c` / `typing_game.h`：打字遊戲題目抽樣、答案比對、回合與秒表狀態
- `display.c` / `display.h`：LED、HEX、LCD 更新
- `key.c` / `key.h`：active-low KEY 反相、去彈跳、edge detection
- `keyboard.c` / `keyboard.h`：PS/2 decoded byte PIO 讀取與 ack pulse
- `eeprom.c` / `eeprom.h`：24LC32 bit-bang I2C 儲存/讀取
- `sdcard.c` / `sdcard.h`：SD card SPI mode + FAT16/FAT32 `QUESTION.TXT` 讀取與 `EDITOR.TXT` 讀寫
- `seven_seg.c` / `seven_seg.h`：active-low 七段顯示編碼

為了程式碼可維護性，UI 相關邏輯盡量寫成共用模組，不要在 `main.c` 或各功能模組裡散落直接控制 LEDR、LEDG、HEX、LCD 的程式。新增畫面、狀態燈、跑馬燈、進度條、閃爍訊息或 LCD 狀態文字時，優先擴充 `display.c/.h` 的共用函數，並同步更新根目錄 `UI.md`。`lcd.c/.h` 和 `seven_seg.c/.h` 是 display 層使用的底層 helper；除非正在修改驅動本身，一般功能流程不應直接操作它們或直接寫 display PIO。

新增多選項 LCD menu 時，優先使用 `menu.c/.h`：呼叫端只傳入 null-terminated option list，`menu_update()` 會處理 `KEY3` 向左、`KEY2` 向右、`KEY1` 確認，LCD 版面由 `display_show_menu_item()` 統一輸出，LEDR 版面由 display 層以選項位置進度條統一輸出。若選單前方還有第 0 頁，使用 `menu_update_with_left_edge()` 在第一個選項收到 `KEY3` 時回呼呼叫端切回該頁；這種選單的第 1 頁也會顯示左箭頭，提示前方還有第 0 頁。

目前核心資料結構使用 `editor.h` 的固定大小 `EditorDocument`：

```c
#define MAX_LINES 32
#define LINE_LEN  99
#define EDITOR_STORAGE_SIZE (40 + (MAX_LINES * LINE_LEN) + 2)

typedef struct {
    char document[MAX_LINES][LINE_LEN + 1];
    unsigned char line_len[MAX_LINES];
    unsigned char current_line;
    unsigned char cursor_col;
    unsigned char total_lines;
    unsigned char insert_mode;
    unsigned char dirty;
} EditorDocument;
```

Nios II app RAM 很緊，app-only build 曾只剩約 4 KiB stack + heap。不要在 SD/FAT、EEPROM/I2C、typing game 載入等 blocking 呼叫鏈上宣告 `EDITOR_TEXT_BUFFER_SIZE`、`EDITOR_STORAGE_SIZE` 這類 3 KiB 級區域陣列；stack 被蓋掉時，常見症狀不是乾淨的錯誤碼，而是讀取中途突然跳回首頁選單、狀態亂跳或 modal 消失。遇到這類現象時，先查大型 local array 與 build 報告的 `Bytes free for stack + heap`；修法優先用 caller-provided scratch buffer、`static` / `union` 共用工作區或 streaming parser，避免同時增加 BSS 和 stack 壓力。

`nav_mode` 不存入 `EditorDocument`，由 `main.c` 每輪直接讀取 `SW17`。`insert_mode` 由 `main.c` 每輪讀取 `SW16` 後用 `editor_set_insert_mode()` 更新，不因切換模式設定 dirty。

PS/2 鍵盤輸入不直接修改 `EditorDocument` 結構。Verilog mapper 輸出 printable ASCII、`0x08` Backspace、`0x0A` Enter/LF、`0x7F` Delete；方向鍵使用 `keyboard.h` 的 internal control byte `0x80..0x83`，Esc 使用 `0x84`。`editor_input.c` 會 dispatch 方向鍵，並把 Esc 回報給 `main.c`；Esc 只在 editor、VI command 與打字遊戲輸入畫面開啟各自既有選單，其他狀態會丟棄鍵盤輸入。

PIO 存取請使用 Altera HAL：

```c
#include "system.h"
#include "altera_avalon_pio_regs.h"
#include "priv/alt_busy_sleep.h"

unsigned int sw = IORD_ALTERA_AVALON_PIO_DATA(PIO_IN_SW_BASE);
IOWR_ALTERA_AVALON_PIO_DATA(PIO_OUT_LEDR_BASE, value);
```

KEY 在 DE2-115 上通常是 active-low。輪詢時請先反相：

```c
unsigned int keys_pressed = (~IORD_ALTERA_AVALON_PIO_DATA(PIO_IN_KEY_BASE)) & 0x0F;
```

七段顯示器通常是 active-low。`hw21_LcdMenu/hw21/seven_seg_hex.v` 和 `context/00BDF_HDL/seg7.v` 可作為編碼參考；C 版目前以 `seven_seg_encode_hex()` 產生 0~F 編碼，以 `seven_seg_blank()` 回傳 `0x7F` 作為 blank。`display.c` 對 HEX7~HEX2 使用十進位拆位後送入七段編碼，只有 HEX1~HEX0 保留十六進位顯示。

## LCD 注意事項

LCD 是 16x2、8-bit parallel。可參考：

- `context/04LCD_DEMO/LCD1602.pdf`
- `context/04LCD_DEMO/CFAH1602BTMCJP.pdf`
- `context/04LCD_DEMO/LCD_Controller.v`
- `hw21_LcdMenu/hw21/lcd_controller.v`
- `hw21_LcdMenu/hw21/lcd_text_driver.v`

LCD 常用重點：

- 第一列 DDRAM address：`0x00` 到 `0x0F`，set DDRAM command 為 `0x80 | addr`。
- 第二列 DDRAM address：`0x40` 到 `0x4F`，set DDRAM command 為 `0x80 | addr`。
- 初始化可參考 `lcd_text_driver.v`：`0x38`、`0x38`、`0x38`、`0x0C`、`0x01`、`0x06`、`0x80`。
- 目前 C 版用 LCD cursor 顯示編輯位置：Insert 模式由 `lcd.c` 軟體切換底線 cursor 顯示 / 隱藏以近似閃爍，Overwrite 模式使用 LCD 內建 blinking block cursor。
- 一般 editor viewport 第一列顯示目前行，第二列顯示下一行；長行會依游標位置水平捲動 16 欄 viewport。目前 EEPROM editor 會把 `EEPROM` 閃爍 marker 當成文件第 0 行前方的 boundary marker：游標在第 0 行時第一列顯示 marker、第二列顯示第 0 行且游標在第二列；游標在其他行時 marker 消失，畫面回到目前行/下一行 layout。
- Editor command mode 第一列顯示 `:` 與目前 VI command buffer，LCD cursor 放在命令尾端；第二列顯示 `VI COMMAND` 與右箭頭，LEDR 使用 2 Hz 全燈閃爍。`KEY1` 可寫入目前 `SW[6:0]` ASCII，PS/2 可直接輸入 printable 字元，Backspace/Delete 刪除命令字元，Enter 或 `KEY0` 執行。
- 若目前行是文件最後一行，第二列顯示 `------END-------`，並在顯示 / 空白之間以接近 LCD 內建游標的頻率閃爍，讓它不會被誤認為文件內容。此為 C 端依主迴圈 refresh tick 的頻率近似，並未讀回 LCD 內部 blink 相位。
- 一般訊息畫面第一列顯示訊息、第二列置中 `KEY0 OK`，等待 `KEY0` 返回，LEDR 使用 2 Hz 全燈閃爍；確認訊息第二列顯示 `KEY1YES KEY0NO`，`KEY1` 確認、`KEY0` 取消，LEDR 使用 2 Hz 全燈閃爍；錯誤訊息第二列置中 `KEY0 OK`，LEDR 使用 5 Hz 全燈閃爍。
- Clear display / return home 需約 1.5 ms 以上，其餘 command/data 通常至少約 40 us。
- 若用 C bit-bang，務必封裝 `lcd_write_command()`、`lcd_write_data()`、`lcd_pulse_enable()`，不要在主邏輯散落控制 bit。

## EEPROM 注意事項

DE2-115 板上 EEPROM 規劃以 24LC32 類 32 Kbit 裝置為準：

- 容量：4K x 8 = 4096 bytes。
- 介面：I2C / 2-wire，SCL + SDA。
- 常見最高 clock：400 kHz。
- control byte 格式：`1010 A2 A1 A0 R/W`，常見 base write/read 為 `0xA0` / `0xA1`。
- 本專題目前 `MAX_LINES=32`、`LINE_LEN=99`，固定儲存格式為 3210 bytes，仍小於 24LC32 的 4096-byte 容量。

可參考：

- `context/03EEPROM/24LC32.pdf`
- `hw21_LcdMenu/hw21/eeprom_i2c_16bit.v`

目前 `top.v` 的 SDA open-drain 接線要求：

- 要輸出 0：讓 SDA PIO drive low。
- 要輸出 1：release SDA，不要 drive high。
- 讀 ACK 或資料 bit 前必須 release SDA，再讀 `PIO_IN_EEP_SDA_IN_BASE`。

目前 C 版已使用固定 3210-byte EEPROM 儲存格式：

```text
0..1    magic: 0x54 0x45
2       version: 0x02
3       total_lines
4       current_line
5       cursor_col
6       insert_mode
7       reserved
8..39   line_len[32]
40..3207 document[32][99]
3208..3209 additive checksum, little-endian
```

目前 `eeprom.c` 會以 32-byte page write 儲存整份文件，並在每頁寫入後 ACK polling。內容修改後只設定 `dirty`，不自動寫 EEPROM；EEPROM editor 選單確認 `Save to ROM` 且 dirty 時才寫入，成功後呼叫 `editor_mark_saved()` 清除 dirty，失敗則保留 dirty 並設定 storage error 顯示。若 dirty 為 0，primary `Save` 會略過實際儲存。`Save as SD` / `Save as EEPROM` 是 secondary save，不清除目前 editor primary storage 的 dirty 狀態。

`eeprom_load_document_with_activity()` / `eeprom_save_document_with_activity()` 與 `sdcard_read_question_text_with_activity()` 的 callback 僅用於 blocking 期間視覺效果，不代表讀寫進度。

## 可重用 Verilog / 講義資源

優先參考順序：

1. `hw21_LcdMenu/hw21/`：最接近本專題周邊。
2. `context/04LCD_DEMO/`：LCD demo 與 datasheet。
3. `context/03EEPROM/`：24LC32 datasheet 與示意圖。
4. `context/00BDF_HDL/`：基礎元件。
5. `hw20_CpuExample/`：Nios/Qsys/Eclipse 範例。
6. `context/01PDF/CH07NIOS.pdf` 與 `context/05SOPC.mp4.srt`：Qsys 操作流程。

`context/00BDF_HDL/` 中特別值得參考：

- `seg7.v` / `seg7.vhd`：七段顯示編碼。
- `KEY_DEBOUNCE.bdf`、`key_dect.v`、`key_dect_ver2.v`：按鍵偵測。
- `EDG_TRIG.v`、`EDGE_FALL.v`、`EDGE_RISE.v`：edge trigger。
- `FrequencyDivider.v`、`DIV10.v`、`CLKDIV*.bdf`：除頻。
- `CNT*.v`：counter。

若功能可由 C 程式可靠完成，優先在 Nios C 實作，避免不必要新增 Verilog IP。只有在 timing、LCD/EEPROM bit-level 控制、或硬體模組更清楚時，才考慮新增 Verilog。

若未來判斷 LCD、EEPROM、debounce、timer 或其他功能應改成 Verilog/Qsys/IP 並行實作，必須先提出變更計畫並與使用者確認；不要直接改硬體架構。

## 建議開發流程

### Nios 自動化腳本

可提交的自動化腳本放在 `scripts/nios/`。腳本會依照自身所在位置找回專案根目錄，因此可以從任意工作目錄執行。

本機工具路徑不要寫入 tracked 文件。若需要固定 Nios II command shell 位置，複製 `scripts/nios/local.env.example.ps1` 為 `scripts/nios/local.env.ps1`，並在 local env 裡設定：

```powershell
$env:NIOS2_COMMAND_SHELL = "<path-to-Nios-II-Command-Shell.bat>"
```

`scripts/nios/local.env.ps1` 已由 `.gitignore` 忽略，專門放本機安裝路徑。若只在目前 PowerShell session 執行 `$env:NIOS2_COMMAND_SHELL = ...`，關掉 terminal 或重開機後不會保留；需要跨 session 可使用 `local.env.ps1`，或由使用者自行設定永久 User environment variable。

腳本尋找 Nios II command shell 的順序是：命令列 `-NiosShell` 參數、`local.env.ps1` / `NIOS2_COMMAND_SHELL` 環境變數、常見 Altera/Intel FPGA 安裝根目錄。找不到時才要求使用者補路徑。

目前腳本用途：

- `scripts/nios/01-qsys-quartus-program.ps1`：執行 `qsys-generate`、`quartus_sh --flow compile`，並用 `quartus_pgm` program `.sof`；若只要 generate/compile，可加 `-SkipProgram`。
- `scripts/nios/02-bsp-generate.ps1`：依目前 `nios.sopcinfo` 重新產生 `software/niosapp_bsp`。
- `scripts/nios/03-build-run-nios.ps1`：在 `software/niosapp` 執行 `make QSYS=0 MAKEABLE_LIBRARY_ROOT_DIRS= all`，再用 `nios2-download -g` run `niosapp.elf`；若只要 build，可加 `-NoRun`，若只要原本 app target，可加 `-MakeTarget app`。

改 Qsys/memory map 後建議順序為 `01` -> `02` -> `03`。只改 Nios C app 時通常只需要跑 `03`；若只改 C 且 hardware bitstream 沒變，`03` 會假設板上已經是相容的 `.sof`。

每次改 Qsys：

1. 在 Qsys/Platform Designer 修改 `nios.qsys`。
2. 確認所有元件 clock/reset 已接上。
3. CPU instruction master/data master 接 On-Chip Memory。
4. CPU data master 接所有 PIO/JTAG/System ID slave。
5. 設定 CPU Reset Vector / Exception Vector 到 On-Chip Memory。
6. 執行 `System -> Assign Base Addresses`。
7. Generate HDL。
8. 回 Quartus 確認 `nios/synthesis/nios.qip` 已在專案中。
9. 若 Qsys export interface 改變，同步更新 `top.v` instance port。
10. Compile Quartus。
11. 回 Nios II Eclipse 更新 BSP，確認 `system.h` macro。
12. Build/run C application。

PS/2 Verilog 相關檔案若尚未在 Quartus GUI 中出現，需加入以下 project files：

- `ps2_keyboard_controller.v`
- `ps2_receiver.v`
- `ps2_scancode_parser.v`
- `ps2_ascii_mapper.v`
- `keyboard_fifo.v`
- `keyboard_pio_interface.v`

每次改 `top.v`：

1. 確認 port 名稱與 `EP4.qsf` pin assignment 完全一致。
2. 確認 Qsys instance port 名稱和 `qsys_verilog_example.v` / generated HDL 一致。
3. 不要破壞 `SW[14]` reset，除非同步更新 AGENTS.md 和操作說明。
4. Compile Quartus，至少確認 analysis/synthesis 與 full flow 沒有 fatal error。

每次改 C 程式：

1. 先確認 `system.h` 裡的 PIO macro 名稱。
2. 一般情況下只 build app；本機已驗證指令如下：

```bash
cd /cygdrive/d/quartus/quartusFinalProject/software/niosapp
make QSYS=0 MAKEABLE_LIBRARY_ROOT_DIRS= app
```

3. 若真的改了 Qsys/BSP，再回 Eclipse 或 Nios tools 更新 BSP；不要手改 BSP generated files。
4. 在 JTAG UART console 保留基本 debug output，但不要讓主迴圈大量 `printf` 影響互動。
5. 上板驗證 SW/KEY/LED/HEX，再驗 LCD，最後驗 EEPROM。

## 驗證清單

最低驗證順序：

- Quartus compile 成功，`output_files/EP4.sof` 更新。
- Eclipse app build 成功，`software/niosapp/niosapp.elf` 更新。
- JTAG UART 可執行基本 `printf`。
- 開機 LCD 顯示共用選單：第一列為目前選項名稱，第二列中央為十進位 `1/2` 類 counter；若左側仍有選項，第二列第二格顯示 `<`；若右側仍有選項，第二列倒數第二格顯示 `>`。
- 開機選單按 `KEY3` / `KEY2` 可左右切換 `EEPROM EDITOR`、`SD EDITOR`、`SD QUESTIONS` 與 `TYPING GAME`，按 `KEY1` 確認。
- 開機選單確認 `EEPROM EDITOR` 進入原本 EEPROM/editor 流程，若 EEPROM 有有效文件會載入，否則空白文件開始。
- 開機選單確認 `SD QUESTIONS` 會讀 SD 卡 FAT16/FAT32 根目錄短檔名 `QUESTION.TXT`；成功後 LCD 以兩列顯示內容，`KEY2/KEY3` 捲動，`KEY1` 可重新讀檔，`KEY0` 返回首頁選單；讀取失敗時使用錯誤訊息 UI 與 5 Hz 錯誤閃爍。
- 開機選單確認 `SD EDITOR` 會讀 SD 卡 FAT16/FAT32 根目錄短檔名 `EDITOR.TXT`；若不存在則以空白文件開始並顯示 `No SD file`。SD editor 主畫面使用一般 editor 輸入、移動、command mode 與選單，top marker 為 `SD`。
- 從 EEPROM editor、SD 題目檢視、SD editor 或打字遊戲返回首頁選單時，HEX0~HEX7 會由 `main.c` 的外層首頁 transition 統一清空，不保留功能畫面的行號、ASCII、題號或秒表狀態。
- 開機選單確認 `TYPING GAME` 會先進大小寫模式選單：`Capitalized`、`Default`、`Random Caps`、`Quit`；再進題數選單：`5 Questions` 到 `50 Questions`，以 5 題為級距，最後一項 `Quit`。選完後顯示 `SW6-0 OFF` 與 `KEY1GO KEY0EXIT`；`SW[6:0]` 全關後按 `KEY1` 讀取 SD `QUESTION.TXT` 並隨機抽取所選題數，讀取期間 LEDR 使用 activity marquee。若題目不足所選題數，顯示錯誤訊息。
- 打字遊戲中 LCD 第一列顯示輸入 viewport 與 cursor、第二列顯示題目 viewport；使用者可用 SW[6:0]+KEY1 或 PS/2 鍵盤輸入，Backspace/Delete 與左右移動可修正答案，LF/Enter 不會建立新行；答案完全相同後立即自動進入下一題。
- 打字遊戲中秒表從第一個 `SW[6:0]` 變化或第一個實際輸入動作開始；LEDR 依目前題號顯示進度，LEDG0/1/6 保持 Insert / 移動模式 / overflow，LEDG2/3/4 保持 PS/2 detected/activity/Caps Lock，獨立 LEDG8 每秒閃爍作為冒號；HEX7~HEX6 每四秒循環顯示三秒目前題號與一秒總題數，HEX5~HEX4 顯示分鐘，HEX3~HEX2 顯示秒數，HEX1~HEX0 顯示 `SW[6:0]` 十六進位。
- 打字遊戲中若輸入字數已達到或超過目前題目長度但內容不相符，LEDR 會暫時切到既有 5 Hz 錯誤閃爍 2 秒；使用者可繼續用 Backspace/Delete 或移動游標修正輸入。
- 打字遊戲中按 `KEY0` 進入 `Restart` / `Continue` / `Quit` 選單；`Restart` 保留目前抽出的題目並歸零時間，`Continue` 回遊戲且不累加暫停期間時間，`Quit` 回首頁。
- 打字遊戲完成後以一般 UI 訊息顯示 `CPM n.nn` 與置中的 `KEY0 OK`；CPM 由本輪所選題目總字元數與完成秒表計算並四捨五入到小數點後兩位，按 `KEY0` 回首頁。
- editor 主畫面按 `KEY0` 進入 `:VI COMMAND` command mode；空指令返回 editor，`w` 存到目前 editor 的 primary storage，`q` 離開，`wq` / `x` 存檔後離開，`e!` restore whole。若 dirty 時執行 `q`，會以確認訊息提示 `Quit no save?`。
- command mode 按 `KEY2` 進入 editor 選單；EEPROM editor 選單可用 `KEY3` / `KEY2` 左右切換 `Save to ROM`、`Save as SD`、`Quit`、`Restore whole`、`Clear this line`、`Clear All`、`Move to head`、`Move to end`、`Cancel`，按 `KEY1` 確認；在 `Save to ROM` 按 `KEY3` 會回到 command mode。SD editor 選單第一項為 `Save`，第二項為 `Save as EEPROM`，其餘操作相同。
- editor 選單確認 `Quit` 時，若 dirty 會以確認訊息提示 `Quit no save?`；確認 `Restore whole` 會從目前 editor 的 primary storage 重新讀取整份文件；確認 `Cancel` 會直接返回 editor。確認 `Clear this line` 會清空目前行並將游標移到該行開頭；確認 `Clear All` 會重設成單一空白行；確認 `Move to head` / `Move to end` 只移動游標，不設定 dirty。
- EEPROM editor 選單確認 `Save as SD` 會寫入 SD 根目錄 `EDITOR.TXT`；如果檔案已存在，先顯示 `Overwrite SD?`，`KEY1` 確認覆寫、`KEY0` 取消。成功後顯示 `Saved to SD`，但不清除 EEPROM editor 對 EEPROM primary storage 的 dirty 狀態。
- SD editor 選單確認 `Save` 會覆寫 SD 根目錄 `EDITOR.TXT` 並清除 dirty；確認 `Save as EEPROM` 會先顯示 `Overwrite ROM?`，`KEY1` 確認覆寫 EEPROM、`KEY0` 取消。成功後顯示 `Saved to EEPROM`，但不清除 SD editor 對 SD primary storage 的 dirty 狀態。
- `SW[6:0]` 可讀出並在 HEX1~HEX0 顯示 ASCII hex。
- `SW[6:0] = 0x08` 時 KEY1 執行 BS，且游標在行頭時會刪除上一行 LF 並合併行；`0x0A` 時 KEY1 執行 LF，`0x7F` 時 KEY1 執行 DEL。
- `SW[14]` 為 reset：`0` reset，`1` run。
- `SW[15]` 為 PS/2 鍵盤輸入 enable：`0` 忽略，`1` 接受。
- `SW[16]` 可即時切換 Overwrite / Insert，且 LEDG0 反映目前模式。
- KEY active-low 反相與 edge detection 正確，一次按下只觸發一次。
- HEX active-low 顯示正確，未用位可 blank。
- LEDG0/LEDG1/LEDG5/LEDG6/LEDG7 軟體狀態符合計畫；LEDG6 只在目前 LCD 視窗右側仍有當前行文字未顯示時亮。PS/2 reset 後收到有效 frame 時 LEDG2 保持亮，持續輸入時 LEDG3 以約 100 ms 可視 pulse 閃動，Caps Lock 開啟時 LEDG4 亮。
- EEPROM editor 游標在第 0 行時，LCD 第一列顯示置中的閃爍 `-----EEPROM-----` marker，第二列顯示文件第 0 行；游標移到其他行後 marker 消失，畫面回到目前行/下一行 layout。
- LCD 游標在目前編輯位置；EEPROM editor 第 0 行中游標位於第二列，其他行位於一般 editor layout 的目前行列，Insert 模式為軟體閃爍底線游標，Overwrite 模式為 LCD 內建整格閃爍游標。
- 一般訊息 / 確認訊息 / 錯誤訊息的第二列提示文字與 `KEY0` / `KEY1` 返回邏輯正確，且 LEDR 分別以 2 Hz / 5 Hz 全燈閃爍。
- PS/2 鍵盤可輸入英文字母、數字、空白與常用標點；Shift/Caps Lock 會影響大小寫，Shift 會產生符號。
- PS/2 Enter / Backspace / Delete 分別觸發 LF、BS、DEL；方向鍵觸發游標左、右、上、下移動。Esc 在 editor 主畫面或 VI command 開啟 editor 選單，在打字遊戲輸入畫面開啟 `Restart` / `Continue` / `Quit`，首頁與其他非輸入畫面不響應。
- editor 選單確認 `Save to ROM`、`Save`、`Save as SD` 或 `Save as EEPROM` 並實際存檔時，`LEDR17..LEDR1` 會顯示單燈跑馬燈；這不是進度條，存檔結束後恢復目前行進度顯示。
- editor 選單確認 `Save to ROM` 後 reset/重新上電可從 EEPROM 讀回文件。
- SD editor 選單確認 `Save` 後重新進入 `SD EDITOR` 可從 SD `EDITOR.TXT` 讀回文件；EEPROM editor `Save as SD` 後也可用 `SD EDITOR` 讀回同一檔案。
- `HEX7~HEX6` 目前行號、`HEX5~HEX4` 游標位置、`HEX3~HEX2` 文件總行數為十進位；`HEX1~HEX0` ASCII 撥桿狀態維持十六進位。
- 打字遊戲秒表使用 Qsys `timer` 產生的 HAL tick；`system.h` 應維持 `ALT_SYS_CLK TIMER` 且 `TIMER_TICKS_PER_SEC 1000.0`。

## 常見陷阱

- 不要手改 `software/niosapp_bsp/system.h`；它是 generated file。
- 改 Qsys 後若 C 找不到 base macro，通常是 BSP 沒更新或 PIO 名稱改了。
- DE2-115 `KEY[3:0]` 是 active-low，沒反相會導致邏輯顛倒。
- HEX 是 active-low，直接寫 binary digit 不會顯示預期數字。
- LCD command/data timing 不能太快；clear/home 等待時間要比一般資料寫入長。
- I2C SDA 只能 open-drain：drive low 或 release，不能 drive high。
- Nios app stack/heap 預算很緊；`EditorDocument` 與 EEPROM/SD 文字暫存都是 3KB 等級，主 editor 文件應維持 static storage，避免同時放多份大型 local buffer，並以 linker 的 free stack/heap 報告確認。
- 目前已有 Qsys `timer` peripheral 並設定為 HAL system clock；若重新產生 BSP 後 `ALT_SYS_CLK` 變回 `none`，需用 `nios2-bsp-update-settings --set hal.sys_clk_timer timer` 修正後再 build。
- `UART_RXD` / `UART_TXD` 目前不是 Nios UART；除錯先用 JTAG UART。
- `SW[14]` 是 reset；`SW[15]` 是 PS/2 鍵盤輸入 enable；`SW[16]` 是 Insert / Overwrite；文字編輯器也使用 `SW[17]` 和 `SW[6:0]`。
- Qsys 新增或重新排序 PIO 後一定要更新 BSP；只重新 Generate HDL 不會更新 `software/niosapp_bsp/system.h`。
- 新增 `pio_out_ledr_flag` 後需重新 Generate HDL 並更新 BSP；否則 `top.v` 會找不到新的 Qsys wrapper port，或 C 端沒有 `PIO_OUT_LEDR_FLAG_BASE` 而只能使用 fallback 軟體跑馬燈。
- 若 SD 測試只顯示 `Update BSP first`，代表 C app 是用尚未包含 `SPI_SDCARD_BASE` 的舊 BSP 編出來；先更新 `software/niosapp_bsp` 再 build app。
