# DE2-115 文字編輯器專題計畫

## 使用 Nios II + C 語言實作

## 1. 專題目標

本專題預計在 ALTERA DE2-115 FPGA 平台上，使用 **Nios II + C 語言** 實作一個簡易文字編輯器。系統透過板上的 `SW` 輸入 ASCII 字元與模式，使用 `KEY` 控制寫入、換行、游標移動與存檔，並透過 `LCD` 顯示目前編輯行內容。`HEX`、`LEDR`、`LEDG` 則用於顯示目前行號、字元位置、總行數與系統狀態。

文字資料會先存放在 C 程式中的 `Document Buffer`。編輯後設定未儲存狀態，使用者按下 `KEY0` 進入 editor 選單，EEPROM editor 可選擇 `Save to ROM` 寫入 EEPROM，也可用 `Save as SD` 匯出到 SD `EDITOR.TXT`；SD editor 則可用 `Save` 覆寫 SD `EDITOR.TXT`，或用 `Save as EEPROM` 在確認後覆寫 EEPROM。

---

## 2. 系統架構

```text
使用者輸入
SW / KEY
    ↓
Nios II + C 主控制程式
    ↓
文字編輯邏輯
ASCII 輸入 / LF 換行 / 游標控制 / Insert / Overwrite
    ↓
Document Buffer
    ↓
LCD / HEX / LED 顯示
    ↓
EEPROM 儲存
```

---

## 3. 硬體與功能對應表

| 硬體資源         | 對應功能                  | 說明                            |
| ------------ | --------------------- | ----------------------------- |
| `CLOCK_50`   | 系統時脈                  | 提供 Nios II 與周邊運作時脈            |
| `SW[6:0]`    | ASCII 字元輸入            | 輸入 7-bit ASCII，包含空白、BS、LF、DEL |
| `SW15`       | Nios II reset          | `0`：reset，`1`：run               |
| `SW16`       | Insert / Overwrite 切換 | `0`：Overwrite，`1`：Insert        |
| `SW17`       | 移動模式切換                | `0`：左右移動，`1`：上下移動             |
| `KEY0`       | Editor menu           | 在 editor 主畫面開啟共用選單；依目前 editor 來源執行 EEPROM / SD 儲存選項 |
| `KEY1`       | Write                 | 寫入目前 ASCII；`0x08` BS，`0x0A` LF，`0x7F` DEL |
| `KEY3`       | 往前 / 往上               | 依照 `SW17` 決定游標左移或上一行          |
| `KEY2`       | 往後 / 往下               | 依照 `SW17` 決定游標右移或下一行          |
| `LCD`        | 文字顯示                  | 顯示目前正在編輯的一行文字、下一行與游標；最後一行時第二列以接近游標的頻率閃爍顯示 END 標記 |
| `HEX7~HEX6`  | 目前行號                  | 以十進位顯示目前所在行數                  |
| `HEX5~HEX4`  | 字元位置                  | 以十進位顯示目前游標在行內的位置              |
| `HEX3~HEX2`  | 總行數                   | 以十進位顯示目前文件總行數                 |
| `HEX1~HEX0`  | 目前 ASCII 值            | 可顯示目前 `SW[6:0]` 的 ASCII 十六進位值 |
| `LEDR[17:0]` | 目前行進度條 / blocking activity | 平時依目前行 / 文件總行數，從 `LEDR17` 往 `LEDR0` 亮；最後一行才亮 `LEDR0`。EEPROM 讀寫與 SD 讀取期間暫時使用 `LEDR17..LEDR1` 單燈跑馬燈 |
| `LEDG0`      | Insert / Overwrite 狀態 | 反映 `SW16`，`0`：Overwrite，`1`：Insert |
| `LEDG1`      | 移動模式狀態                | `0`：左右，`1`：上下                 |
| `LEDG5`      | Storage error         | 目前 editor 來源的 EEPROM / SD 讀寫或文字截斷錯誤 |
| `LEDG6`      | LCD 右側未顯示提示         | 目前 LCD 視窗右側仍有當前行文字未顯示           |
| `LEDG7`      | Unsaved 狀態            | 文件內容已修改但尚未完成儲存                |

---

## 4. Qsys / Platform Designer 規劃

需要建立一個 Nios II 系統，包含以下元件：

| 元件                    | 用途                              |
| --------------------- | ------------------------------- |
| Nios II Processor     | 執行 C 語言文字編輯器主程式                 |
| On-Chip Memory        | 存放程式與執行時資料                      |
| JTAG UART             | 除錯與 Eclipse console 輸出          |
| System ID             | BSP 辨識系統                        |
| Timer                 | 目前未加入；若未來需要精準定時或中斷式 debounce 再新增 |
| SW PIO                | 讀取 `SW[17:0]`                   |
| KEY PIO               | 讀取 `KEY[3:0]`                   |
| LEDR PIO              | 控制 `LEDR[17:0]`                 |
| LEDG PIO              | 控制 `LEDG[7:0]`                  |
| HEX0~HEX7 PIO         | 控制 8 個七段顯示器                     |
| LCD DATA PIO          | 控制 LCD 資料線                      |
| LCD CTRL PIO          | 控制 LCD RS / RW / EN / ON / BLON |
| EEPROM PIO 或 I2C 控制模組 | 用於儲存與讀取文件資料                     |

---

## 5. C 程式資料結構規劃

建議先使用固定大小的文件資料區：

```c
#define MAX_LINES 32
#define LINE_LEN  99

char document[MAX_LINES][LINE_LEN + 1];
int line_len[MAX_LINES];

int current_line = 0;
int cursor_col = 0;
int total_lines = 1;

int insert_mode = 0;
int nav_mode = 0;

int dirty_flag = 0;
```

其中：

| 變數              | 功能                    |
| --------------- | --------------------- |
| `document`      | 儲存整份文件內容              |
| `line_len`      | 儲存每一行目前使用長度           |
| `current_line`  | 目前所在行                 |
| `cursor_col`    | 目前行內游標位置              |
| `total_lines`   | 目前文件總行數               |
| `insert_mode`   | Insert / Overwrite 模式 |
| `nav_mode`      | 左右 / 上下移動模式           |
| `dirty_flag`    | 是否有未儲存修改              |

`LEDG6` 不由文件資料結構儲存，而是在顯示層依目前 LCD viewport 起點與目前行長度即時計算：若目前行右側仍有文字未顯示，則亮燈。

---

## 6. 程式功能模組規劃

### 6.1 主程式流程

```text
1. 初始化系統
2. 初始化 LCD / HEX / LED
3. 顯示共用開機選單，`KEY3` / `KEY2` 左右切換，`KEY1` 確認
4. 若選擇 `EEPROM EDITOR`，從 EEPROM 讀取文件資料並載入 Document Buffer
5. 若選擇 `SD QUESTIONS`，讀取 SD 卡根目錄 `QUESTION.TXT` 做題庫檢視
6. 若選擇 `SD EDITOR`，讀取 SD 卡根目錄 `EDITOR.TXT` 到 Document Buffer；若不存在則以空白文件開始
7. 進入主迴圈
8. 讀取 SW / KEY
9. 判斷使用者操作
10. 更新文件資料
11. 若按下 KEY0，顯示 editor 選單；primary save 寫回目前來源，secondary save 可在 EEPROM 與 SD `EDITOR.TXT` 之間另存
12. 更新 LCD / HEX / LED 顯示
```

---

### 6.2 建議 C 程式模組

| 模組                          | 功能               |
| --------------------------- | ---------------- |
| `main.c`                    | 主程式流程與狀態控制       |
| `menu.c / menu.h`           | 共用 LCD 選單狀態機        |
| `editor.c / editor.h`       | 文字編輯邏輯           |
| `display.c / display.h`     | LCD、HEX、LED 顯示更新 |
| `key.c / key.h`             | KEY 去彈跳與邊緣偵測     |
| `eeprom.c / eeprom.h`       | EEPROM 讀寫        |
| `seven_seg.c / seven_seg.h` | 七段顯示器編碼          |

---

### 6.3 共用選單 UI

多選項 LCD 畫面使用共用 `menu.c / menu.h` 狀態機。外部流程只提供 null-terminated option list，例如開機選單的 `EEPROM EDITOR`、`SD EDITOR`、`SD QUESTIONS`、`TYPING GAME`，或 editor 選單的 `Save to ROM` / `Save as SD` / `Save as EEPROM`、`Clear this line`、`Clear All`、`Move to head`、`Move to end`；`menu_update()` 統一處理 `KEY3` 向左、`KEY2` 向右、`KEY1` 確認，並回傳 zero-based option index。

LCD 顯示規格：

```text
第一列：目前選項名稱
第二列第二格：左側仍有選項時顯示 <
第二列倒數第二格：右側仍有選項時顯示 >
第二列中央：十進位 目前/總數，例如 1/2，支援到 99/99
```

---

## 7. 文字編輯功能規劃

### 7.1 ASCII 輸入

`SW[6:0]` 作為 ASCII 輸入。

```text
0x20 ~ 0x7E：一般可顯示字元，但 0x7F 作為 DEL
0x08：BS，刪除游標左側字元
0x0A：LF，執行換行
0x7F：DEL，刪除游標所在字元
其他控制字元：暫時忽略
```

---

### 7.2 Write 功能

按下 `KEY1` 時：

```text
讀取 SW[6:0]
    ↓
判斷是否為 BS 0x08 / LF 0x0A / DEL 0x7F
    ↓
控制字元：執行刪除或換行
一般字元：依照目前模式寫入字元
```

---

### 7.3 Insert / Overwrite 模式

`SW16` 直接決定目前模式，不使用 KEY toggle：

| 模式        | 功能                 |
| --------- | ------------------ |
| Overwrite | 直接覆蓋目前游標位置的字元      |
| Insert    | 在目前游標位置插入字元，後方字元右移 |

若目前行已滿，寫入失敗且不改動文件內容。`LEDG6` 不代表行滿或文件滿；它只表示目前 LCD 視窗右側仍有當前行文字未顯示。

LCD 游標顯示：

```text
Insert 模式：LCD 內建底線游標，表示字元會從游標位置插入並往右推。
Overwrite 模式：LCD 內建整格閃爍游標，表示目前格會被覆寫。
```

---

### 7.4 LF 換行功能

當輸入 ASCII `0x0A` 並按下 `KEY1` 時：

```text
1. 將目前行從游標位置切開
2. 游標後方文字移到下一行
3. current_line + 1
4. cursor_col 歸零
5. total_lines 更新
6. 設定 dirty_flag
7. 更新 LCD / HEX / LED
```

### 7.5 BS / DEL 刪除功能

```text
BS 0x08：
1. 若 cursor_col > 0，刪除游標左側字元
2. 後方字元左移
3. cursor_col - 1
4. 設定 dirty_flag

DEL 0x7F：
1. 若游標所在位置有字元，刪除該字元
2. 後方字元左移
3. cursor_col 不變
4. 設定 dirty_flag
```

---

## 8. EEPROM 儲存規劃

EEPROM 用來保存文件內容，使 reset 或斷電後仍可恢復資料。

### 8.1 儲存內容

建議 EEPROM 儲存：

```text
1. magic/version
2. total_lines
3. current_line
4. cursor_col
5. insert_mode 或保留欄位
6. line_len[32]
7. document[32][99]
8. checksum
```

目前實作使用固定 3210-byte EEPROM layout：

```text
0..1       magic: 0x54 0x45
2          version: 0x02
3          total_lines
4          current_line
5          cursor_col
6          insert_mode
7          reserved
8..39      line_len[32]
40..3207   document[32][99]
3208..3209 additive checksum, little-endian
```

### 8.2 儲存時機

第一版可以採用：

```text
內容修改後只設定 dirty_flag
按下 KEY0 開啟 editor 選單，primary save 寫回目前來源；EEPROM editor 可 `Save as SD` 到 `EDITOR.TXT`，SD editor 可在確認後 `Save as EEPROM`
EEPROM 讀寫或 SD 讀寫期間以 LEDR17..LEDR1 顯示單燈跑馬燈；這只是 blocking activity 視覺效果，不代表讀寫進度
primary save 成功後清除 dirty_flag
若 dirty_flag = 0，primary save 可略過實際儲存；secondary save 仍可寫入另一個儲存後端
```

---

## 9. 顯示更新規劃

### 9.1 LCD

LCD 顯示目前行內容，並在第二列顯示下一行：

```text
row 0: document[current_line]
row 1: document[current_line + 1]
```

每行文件最多 99 個字元，LCD 只顯示 16 欄 viewport；當游標接近視窗左右邊界時，viewport 會水平捲動。若目前行或下一行不足 LCD 長度，後方補空白。若目前行已是文件最後一行，第二列顯示 `------END-------`，並在顯示 / 空白之間以接近 LCD 內建游標的頻率閃爍，避免與真實文件文字混淆。

LCD cursor 會放在 `document[current_line][cursor_col]` 的位置。Insert 模式使用底線游標；Overwrite 模式使用整格閃爍游標。

---

### 9.2 HEX

| HEX         | 顯示內容         |
| ----------- | ------------ |
| `HEX7~HEX6` | 目前行號，十進位       |
| `HEX5~HEX4` | 目前字元位置，十進位     |
| `HEX3~HEX2` | 文件總行數，十進位      |
| `HEX1~HEX0` | 目前 ASCII 輸入值，十六進位 |

---

### 9.3 LED

| LED          | 顯示內容               |
| ------------ | ------------------ |
| `LEDR[17:0]` | 平時顯示目前行 / 文件總行數進度，從 LEDR17 亮到 LEDR0；EEPROM 讀寫與 SD 讀取時暫時以 LEDR17..LEDR1 顯示跑馬燈 |
| `LEDG0`      | Insert / Overwrite |
| `LEDG1`      | 左右 / 上下移動          |
| `LEDG5`      | Storage error      |
| `LEDG6`      | 目前 LCD 視窗右側仍有當前行文字未顯示 |
| `LEDG7`      | Unsaved            |

---

## 10. 開發階段規劃

### 第一階段：基本輸入與顯示

```text
1. 建立 Nios II 系統
2. 完成 SW / KEY / LED / HEX PIO
3. C 程式讀取 SW[6:0]
4. HEX 顯示目前 ASCII
5. LEDG 顯示基本狀態
```

---

### 第二階段：LCD 顯示

```text
1. 建立 LCD PIO
2. 完成 LCD 初始化
3. LCD 顯示固定文字
4. LCD 顯示 document[current_line]
```

---

### 第三階段：文字編輯邏輯

```text
1. 建立 Document Buffer
2. KEY1 寫入字元
3. KEY2 / KEY3 控制左右移動
4. SW16 切換 Insert / Overwrite
5. 支援 BS 0x08、LF 0x0A、DEL 0x7F 控制字元
6. LCD 顯示 Insert/Overwrite 對應游標樣式
```

---

### 第四階段：多行與狀態顯示

```text
1. SW17 切換左右 / 上下移動
2. 支援 current_line 上下移動
3. HEX 以十進位顯示行號、字元位置、總行數，並以十六進位顯示 ASCII 輸入值
4. LEDR 顯示目前行 / 文件總行數進度條，最後一行才亮 LEDR0
5. LEDG 顯示目前行右側未顯示 / unsaved 狀態
```

---

### 第五階段：EEPROM 儲存

```text
1. 完成 EEPROM 寫入功能
2. 完成 EEPROM 讀取功能
3. 開機時自動載入文件
4. 編輯後設定 dirty_flag
5. KEY0 開啟 editor 選單，確認 Save to ROM 時手動儲存文件
6. EEPROM 讀寫 blocking 期間顯示 LEDR17..LEDR1 單燈跑馬燈視覺效果
7. 存檔成功後清除 dirty_flag
8. reset 後確認資料可恢復
```

---

## 11. 預期成果

完成後，系統可在 DE2-115 上獨立運作，使用者能透過開關輸入 ASCII 字元並切換輸入模式，利用按鍵控制游標、換行，並透過 editor 選單手動儲存文件；LCD 會顯示目前編輯內容與選單。七段顯示器與 LED 可即時顯示目前文件狀態，EEPROM 則負責保存文件資料，使系統具備基本文字編輯與非揮發性儲存能力。

若未來判斷 LCD、EEPROM、debounce、timer 或其他功能應改成 Verilog/Qsys/IP 並行實作，必須先提出變更計畫並取得確認，不直接變更硬體架構。
