# DE2-115 Nios II 文字編輯器

此專案使用 Quartus II 13.1 Web Edition、Qsys 與 Nios II Software Build Tools for Eclipse。

## 在新電腦開啟

1. 安裝 Quartus II 13.1 Web Edition，並確認包含 Nios II EDS。
2. Clone 此 repository。Quartus 13.1 工具較舊，建議路徑不要包含中文或空白。
3. 從 Quartus／開始功能表開啟 **Nios II Software Build Tools for Eclipse**，不要使用一般 Eclipse。
4. 選擇一個該電腦專用的新 workspace；不要把 repository 內的資料夾當成共享 workspace。
5. 選擇 `File -> Import -> General -> Existing Projects into Workspace`。
6. Root directory 指向 repository 的 `software`，匯入：
   - `niosapp_bsp`
   - `niosapp`
7. 不要勾選 `Copy projects into workspace`。

匯入後，`niosapp_bsp` 應有 Nios II 的 BSP 選項，`niosapp` 應有 `Run As -> Nios II Hardware`。若沒有，通常是開啟了普通 Eclipse，或沿用了其他電腦提交的 `.metadata`。

## 第一次編譯

Eclipse 流程：

1. 對 `niosapp_bsp` 執行 Nios II 的 Generate BSP。
2. Build `niosapp_bsp`。
3. Build `niosapp`。
4. 先將 `output_files/EP4.sof` program 到 FPGA，確認 `SW14=1`，再對 `niosapp` 執行 `Run As -> Nios II Hardware`。

PowerShell 自動流程：

```powershell
Copy-Item scripts/nios/local.env.example.ps1 scripts/nios/local.env.ps1
# 編輯 local.env.ps1，填入該電腦的 Nios II Command Shell.bat 路徑

# fresh clone：自動 Generate/Build BSP，再 build app
.\scripts\nios\03-build-run-nios.ps1 -NoRun

# 板上已有相容的 EP4.sof 時，build 並下載執行
.\scripts\nios\03-build-run-nios.ps1
```

若硬體設計也需要重新產生、編譯及 program：

```powershell
.\scripts\nios\01-qsys-quartus-program.ps1
.\scripts\nios\02-bsp-generate.ps1
.\scripts\nios\03-build-run-nios.ps1
```

`eclipse-C/`、BSP 的 `HAL/`、`drivers/`、object、library 與 ELF 都是每台電腦可重新產生的本機產物，因此不會提交到 Git。
