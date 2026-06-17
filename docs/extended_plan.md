# Extended Implementation Plan

本文件用來記錄文字編輯器平台下一階段預計擴充的技術方向。

目前基礎功能已完成，包含文字編輯、資料儲存與基本輸入流程。接下來的目標是讓系統更接近完整的獨立文字輸入平台，並為後續打字遊戲、題庫載入與軟硬體協同設計做準備。

---

## 1. PS/2 鍵盤輸入

### 1.1 目標

目前系統已經可以接受標準 ASCII 輸入，也可以處理部分控制字元。下一步希望透過 DE2-115 上的 PS/2 介面接入實體鍵盤，讓使用者可以直接使用鍵盤輸入文字。

目標如下：

- 使用 PS/2 鍵盤作為主要文字輸入來源。
- 將 PS/2 scan code 轉換成 ASCII 字元。
- 支援基本英文字母、數字、空白與常用符號。
- 支援必要控制鍵，例如 Enter、Backspace、方向鍵等。
- 不支援的按鍵或組合鍵先忽略，不影響系統運作。
- 轉換後的 ASCII 或控制字元要能接到現有文字編輯流程。

---

### 1.2 實作方向

PS/2 鍵盤輸出的不是 ASCII，而是 scan code，因此需要新增一層鍵盤解碼邏輯。

建議使用 Verilog 來接收與解碼 PS/2 訊號，再將結果提供給 Nios II C 程式讀取。

```text
PS/2 Keyboard
    ↓
PS/2 CLK / DATA
    ↓
Verilog PS/2 Receiver
    ↓
Scan Code Parser
    ↓
ASCII / Control Code Mapper
    ↓
FIFO or PIO Interface
    ↓
Nios II C Program
    ↓
Existing Text Editor Logic
```

這部分建議使用 Verilog 的原因是 PS/2 鍵盤有自己的 clock，由硬體接收資料會比 C 語言輪詢更穩定，也比較不容易漏接按鍵。

---

### 1.3 預計新增模組

預計可以拆成以下幾個 Verilog 模組：

| 模組 | 功能 |
|---|---|
| `ps2_receiver.v` | 接收 PS/2 serial data，組成 8-bit scan code |
| `ps2_scancode_parser.v` | 判斷 make code、break code、extended code |
| `ps2_ascii_mapper.v` | 將 scan code 轉成 ASCII 或控制字元 |
| `keyboard_fifo.v` | 暫存鍵盤輸入，避免 Nios II 忙碌時漏字 |
| `keyboard_pio_interface.v` | 將 FIFO 狀態與輸入資料提供給 Nios II 讀取 |

---

### 1.4 需要處理的 PS/2 狀況

PS/2 鍵盤輸入需要注意以下狀況：

- **Make Code**：按鍵按下時送出的 scan code。
- **Break Code**：按鍵放開時通常會先送出 `F0`，初期可忽略。
- **Extended Code**：方向鍵、Delete 等特殊鍵可能會有 `E0` 前綴。
- **Shift 狀態**：需要判斷大小寫與符號。
- **Caps Lock 狀態**：可作為進階功能，初期可先不完整支援。
- **不支援按鍵**：直接忽略即可。

初期優先支援：

- 英文字母 `A` ~ `Z`
- 數字 `0` ~ `9`
- 空白鍵
- Enter
- Backspace
- 方向鍵
- 常用標點符號

---

### 1.5 與現有系統整合

因為目前文字編輯器已經以 ASCII / 控制字元作為輸入核心，所以 PS/2 鍵盤模組不需要直接改動文字編輯邏輯。

Nios II C 程式只需要從鍵盤 FIFO 或 PIO 讀取輸入資料，然後送進原本的輸入處理函式。

```text
while (1) {
    if (keyboard_has_data()) {
        ch = keyboard_read();
        editor_handle_input(ch);
    }

    editor_update_display();
}
```

這樣可以讓 PS/2 鍵盤成為新的輸入來源，同時保留原本 SW / KEY 測試輸入方式。

---

## 2. SDHC 文字檔讀取

### 2.1 目標

第二個擴充方向是讓系統可以從 SD 卡讀取文字檔案。初期用途是作為打字遊戲的題目來源，之後也可以擴充成文章載入、範本文字載入或其他外部資料來源。

目前先以 **SDHC** 為主要目標，不優先支援 SDXC。原因是 SDHC 容量常見、取得容易，且通常使用 FAT32，較適合自行實作簡化版 reader。

初期目標如下：

- 使用 4GB ~ 32GB SDHC 卡。
- SD 卡格式化為 FAT32。
- 從根目錄讀取固定文字檔。
- 檔名先採用 8.3 格式，例如 `QUESTIONS.TXT`。
- 文字內容先限制為英文 ASCII。
- 一行代表一筆題目。
- 初期只做 read-only，不做寫入。

---

### 2.2 建議資料格式

初期建議文字檔內容採用簡單格式：

```text
hello world
fpga text editor
nios ii typing game
practice makes perfect
```

每一行視為一個題目。

暫時不建議使用：

- 中文 UTF-8 題目
- 長檔名
- 多層資料夾
- 複雜標記格式
- SD 卡寫入功能
- SDXC / exFAT

這樣可以降低檔案系統與文字解析的複雜度。

---

### 2.3 SDHC 讀取架構

建議架構如下：

```text
SDHC Card
    ↓
SPI Mode SD Driver
    ↓
512-byte Sector Reader
    ↓
FAT32 Reader
    ↓
Find QUESTIONS.TXT
    ↓
Read Text Content
    ↓
Question Buffer
    ↓
Typing Game / Text Display
```

SD 卡底層可以使用 SPI mode 讀取，初期不需要使用完整 SDIO 模式。對本專題來說，資料量不大，SPI mode 的速度已足夠應付文字題目讀取。

---

### 2.4 SDHC 規格注意事項

SDHC 與傳統 SDSC 最大的差異之一是定址方式。

| 類型 | 容量 | 定址方式 | 常見檔案系統 |
|---|---:|---|---|
| SDSC | 2GB 以下 | Byte Addressing | FAT12 / FAT16 |
| SDHC | 4GB ~ 32GB | Block Addressing | FAT32 |
| SDXC | 64GB 以上 | Block Addressing | exFAT |

本專案初期以 SDHC 為主，因此讀取 sector 時應使用 block addressing。

也就是讀取第 `N` 個 512-byte sector 時，送出的 address 應為：

```text
address = N
```

而不是：

```text
address = N * 512
```

後續如果要同時相容 SDSC，可以在初始化後判斷卡片類型，再決定 address 轉換方式。

---

### 2.5 初始化與讀取流程

SDHC 初始化大致流程如下：

```text
Power On
    ↓
Send Dummy Clocks
    ↓
CMD0: Enter Idle State
    ↓
CMD8: Check SD Version
    ↓
CMD55 + ACMD41: Initialize Card
    ↓
CMD58: Read OCR / Check Card Type
    ↓
CMD17: Read Single Block
```

初期可以先完成最低限度的 read-only 功能：

1. 初始化 SDHC。
2. 成功讀取 sector 0。
3. 解析 MBR / Boot Sector。
4. 找到 FAT32 root directory。
5. 搜尋 `QUESTIONS.TXT`。
6. 讀取檔案內容。
7. 將文字資料放入 RAM buffer。
8. 顯示第一行題目到 LCD。

---

### 2.6 與 Nios II 的分工

SD 卡與 FAT32 的處理流程比較接近軟體邏輯，因此初期建議主要由 Nios II C 程式負責。

| 功能 | 建議實作方式 | 原因 |
|---|---|---|
| SD SPI 傳輸 | Verilog 或 C 皆可 | 初期可先用 C 控制 SPI PIO，之後再硬體化 |
| SD 初始化 | C | 流程判斷多，C 較好維護 |
| Sector 讀取 | C + SPI driver | 初期 read-only 較容易測試 |
| FAT32 解析 | C | 檔案系統邏輯較適合軟體 |
| 題目解析 | C | 一行一題，使用字串處理較方便 |
| 題目顯示 | C | 可接現有 LCD 顯示流程 |

若後續要提高穩定性，可以再將 SPI controller 改成 Verilog 模組，Nios II 只負責發出讀取命令與接收 sector buffer。

---

## 3. 預計開發順序

### 階段一：PS/2 鍵盤輸入

目標：

```text
實體鍵盤輸入
→ Verilog 解碼
→ Nios II 讀取 ASCII
→ 文字編輯器正常輸入
```

驗收條件：

- 按下字母鍵可以在 LCD 或文字 buffer 中顯示。
- Enter / Backspace 可以觸發原本的控制流程。
- 不支援按鍵不會造成系統錯誤。
- 連續輸入不容易漏字。

---

### 階段二：SDHC 題庫讀取

目標：

```text
SDHC FAT32
→ 讀取 QUESTIONS.TXT
→ 載入題目 buffer
→ LCD 顯示題目
```

驗收條件：

- SDHC 可以成功初始化。
- 可以讀取 512-byte sector。
- 可以找到根目錄中的 `QUESTIONS.TXT`。
- 可以讀出第一行題目並顯示。
- 題目資料可以提供給後續打字遊戲使用。

---

## 4. 長期擴充方向

完成 PS/2 與 SDHC 後，後續可以擴充：

- 打字遊戲模式。
- 隨機選題。
- 計時與分數統計。
- 錯字提示。
- HEX 顯示速度、分數或剩餘時間。
- LED 顯示進度或答題狀態。
- EEPROM 儲存最高分或使用者設定。
- Verilog 硬體計時器與鍵盤 FIFO。
- SD 卡題庫更新。

---

## 5. 初步結論

PS/2 鍵盤輸入是最適合下一步實作的功能，因為目前系統已經以 ASCII 作為輸入核心，只需要新增 scan code 到 ASCII 的轉換層即可。此部分建議使用 Verilog 接收 PS/2 訊號，再透過 FIFO 或 PIO 交給 Nios II。

SDHC 文字檔讀取則適合放在第二階段。初期建議使用 SDHC + FAT32 + 固定 8.3 檔名 + ASCII 文字檔，先完成 read-only 題庫載入，再逐步擴充成完整打字遊戲資料來源。
