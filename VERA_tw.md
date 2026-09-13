# VERA 卡（AppleWin 擴充卡）說明文件

本文說明本專案將 **Commander X16 的 VERA（Versatile Embedded Retro Adapter）**
晶片移植成 **AppleWin 插槽擴充卡**的規格、與 AppleWin 整合的方式、以及使用
方法。核心是 `apple2ts` 的 TypeScript 實作（`src/worker/devices/vera/`）的
C++ 移植。

> 與 VERA 相關的開發架構、建置與除錯記錄請見 `AGENTS.md`。

---

## 1. 概觀

- VERA 卡可插入 **Slot 2 或 Slot 4**。
- 提供完整 VERA 顯示核心（640×480 VGA / NTSC 掃描、雙圖層、128 個精靈、
  256 色調色盤、FX 暫存器）與音效核心（16 通道 PSG + PCM FIFO）。
- 提供一條 IRQ 中斷線。
- 軟體原始碼位於 `source\VERACard\`：
  - `VERAVideo.h/.cpp` — 顯示核心（video.ts 移植）
  - `VERAAudio.h/.cpp` — 音效核心（PSG + PCM）
  - `VERACard.h/.cpp` — 插槽卡整合、IO、Update、音效緩衝、存檔

### 實作狀態

- 完整 VERA 音效已完成（PSG 已實作 16 通道 + PCM）。
- SD 卡 SPI（SD/MMC）已實作 — 見 §4「SD 卡 SPI」。

---

## 2. 與 AppleWin 的整合

### 2.1 IO 定址

VERA 暫存器位於所選插槽的 **Cx 區域**：`$Cs00–$CsFF`，其中 `addr & 0xff`
選定暫存器。透過 `RegisterIoHandler(slot, IO_Null, IO_Null, &IOReadCx,
&IOWriteCx, ...)` 註冊（見 `VERACard::InitializeIO`）。

### 2.2 顯示（雙螢幕設計）

依使用者的設計理念，本實作採用「二合一」顯示：**平常顯示 Apple II 原始畫面，
只有當 VERA 軟體啟用 VERA 顯示輸出時才切換成 VERA 畫面**。

- 當 VERA 卡存在時，Video framebuffer 放大為 **680×516**
  （無邊框 **640×480**，邊框 20 寬 / 18 高）。
- `VERACard::Update()` 只在 `IsVideoOutputEnabled()` 為真時呼叫
  `UpdateDisplay()` 把 VERA 640×480 framebuffer 複製進 AppleWin framebuffer；
  否則保留 Apple II（NTSC）畫面。
- `VideoRefreshBuffer` 在 `IsVidVERAActive()` 時跳過 NTSC 重繪。
- framebuffer 為 **bottom-up**（row 0 = 底部）；VERA 與 NTSC 都用
  `(framebufferHeight - 1) - y - borderHeight` 對映。

### 2.3 IRQ

`eIRQSRC` 新增 `IS_VERA`；`VERACard::Update()` 依 `m_video.GetIRQOut()`
呼叫 `CpuIrqAssert(IS_VERA)` / `CpuIrqDeassert(IS_VERA)`。

### 2.4 音效

- SSI263 式環形緩衝（`DSGetSoundBuffer` / `DSGetLock`）。
- `kDSBufferByteSize = 44100 / 60 * 3 * 2 * kNumChannels`（約 3 幀立體聲），
  避免覆蓋正在播放的音訊。
- 取樣率 44100 Hz、立體聲；`UpdateSound()` 依 `g_nCumulativeCycles` 產生取樣。

---

## 3. 顯示規格

| 項目 | 值 |
|---|---|
| framebuffer（VERA） | 640 × 480（RGBA） |
| 掃描線 / 幀 | 525（VGA） |
| 每線像素時脈 | 800（`VGA_SCAN_WIDTH`），PIXEL_FREQ = 25 MHz |
| 完整一幀週期 | 525 × 800 / 25 = **16800** |
| 圖層 | 2 |
| 精靈 | 128 |
| 調色盤 | 256 色（每色 8-bit RGB） |
| VRAM | 128 KB（0x00000–0x1FFFF），含 PSG / 調色盤 / 精靈資料區 |

### 顯示時序（AppleWin 整合）

`VERACard::Update()` 被呼叫約 **16 次 / 幀**（每次約 1000 週期的批次）。為避免
每批次推進不足造成掃描位置漂移（捲動），以及每批次強制完整掃描造成 16 倍速，
目前做法是**每個 Apple 顯示幀只推進完整一幀**，用累積週期計數器
`g_nCumulativeCycles` 偵測 Apple 幀邊界：

```cpp
const uint64_t curCycles = g_nCumulativeCycles;
if (m_lastFrameCycles == 0)
    m_lastFrameCycles = curCycles;
const uint64_t frameCycles = NTSC_GetCyclesPerFrame();
while (curCycles >= m_lastFrameCycles + frameCycles)
{
    m_lastFrameCycles += frameCycles;
    m_video.Step(1, 16800, false);   // 完整一幀
    m_video.Update();
    if (m_video.IsVideoOutputEnabled())
        UpdateDisplay();
}
```

---

## 4. IO 暫存器對映

VERA 暫存器由 `addr & 0x1F` 選定（`VERAVideo::Write/Read`）。

| reg | 功能 |
|---|---|
| 0x00–0x02 | 資料/位址指標（ADDRL/ADDRM/ADDRH + INCR） |
| 0x03, 0x04 | VRAM 資料讀寫（DATA0 / DATA1） |
| 0x05 | 控制（`DCSEL`、`ADDRSEL`、bit7 = 軟體重置） |
| 0x06 | `IEN`（IRQ 致能） |
| 0x07 | `ISR`（IRQ 狀態清除） |
| 0x08 | `IRQL`（IRQ 比較線） |
| 0x09–0x0C | Composer 暫存器（依 `DCSEL` 選定） |
| 0x0D–0x13 | Layer 0 暫存器（7 bytes） |
| 0x14–0x1A | Layer 1 暫存器（7 bytes） |
| 0x1B–0x1D | PCM（0x1B 控制、0x1C 速率、0x1D FIFO 寫入） |
| 0x1E–0x1F | SD 卡 SPI：0x1E = SD_DATA、0x1F = SD_STATUS |

### Composer（DCSEL=0，offset 進入 `m_reg_composer`）

| offset | 功能（依 `render_line` 程式碼） |
|---|---|
| 0 | DC_VIDEO：bits 0–1 = out_mode（0=停用，非 0=啟用）、bit 3 = 交錯、bit 4/5/6 = Layer0/1/精靈致能 |
| 1 | 水平縮放（scale） |
| 2 | 垂直縮放（用於 eff_y 累積） |
| 3 | 邊框顏色（border_color） |
| 4 | hstart（<<2） |
| 5 | hstop（<<2） |
| 6 | vstart（<<1） |
| 7 | vstop（<<1） |
| 8+ | 色度 hscale |
| 9+ | FX 暫存器：DCSEL 2–6（位址/增量/像素位置/快取等） |

### Layer 暫存器（每層 7 bytes）

| byte | 說明 |
|---|---|
| 0 | 設定：color_depth（bits 0–1）、bitmap_mode（bit 2）、text_256c（bit 3）、mapw（bits 4–5）、maph（bits 6–7） |
| 1 | map_base（<<9） |
| 2 | tile_base（bits 0–5，<<9）、tile 寬高（bits 0–1） |
| 3 | HSCROLL 低 8 bits |
| 4 | HSCROLL 高 4 bits（`& 0xF`） |
| 5 | VSCROLL 低 8 bits |
| 6 | VSCROLL 高 4 bits（`& 0xF`） |

### SD 卡 SPI

`VERASD.h/.cpp` 實作 SD/MMC **SPI 狀態機**（apple2ts `src/worker/devices/vera/sdcard.ts`
的移植）。底層儲存為原始 512-byte-block 影像，用真實 stdio 開啟
（`fopen("r+b")` / `fseek` / `fread` / `fwrite`）。

**暫存器**（插槽區，`addr & 0x1F`；slot 2 → `$C21E`/`$C21F`）：

| reg | 名稱 | 功能 |
|---|---|---|
| 0x1E | SD_DATA | 寫入即送出一個 byte（開始傳輸）；讀取回傳收到的 byte；autotx 開啟時讀取會自動送出 `$FF` |
| 0x1F | SD_STATUS | bit 0 = SD_SEL（卡片選定 / CS）、bit 2 = SD_AUTOTX、bit 7 = SD_BUSY（byte 傳輸進行中） |

**SPI 時序：** `SPI_CLOCK_RATE_MHZ = 12`。`SpiStep(clocks)` 把 `clocks * 12`
加進 busy 計數器，累積到 `>= 10` 即完成一個 byte（模擬中約 1 CPU 週期完成一個
byte）。`VERACard::Update()` 每個 1 ms 批次呼叫 `m_video.StepSPI(...)` —
SPI 只在批次邊界推進，所以 guest 程式要等傳輸完成，必須 **poll SD_STATUS bit 7**，
不能用固定延遲。

**支援指令**（SPI 模式）：CMD0、CMD8、CMD9（SEND_CSD）、ACMD41（CMD55+CMD41）、
CMD12、CMD13、CMD16、CMD17、CMD18、CMD24（寫入）、CMD55、CMD58（READ_OCR）。

**掛載 / GUI：** `Configuration -> Slots` → 選 VERA 卡 → 「Configure...」開啟
`IDD_VERA_SD_CARD`「VERA SD Card」對話框（`PageSlots.cpp`），有「Select Image...」
與「Unmount」。對話框**不要求** VERA 卡已裝好：選取的路徑會存到
`CConfigNeedingRestart::m_VERASDImagePath[slot]`，因此可以一次選好 VERA + SD
影像再重啟，`ApplyConfigAfterClose()` 會在卡片（重新）插入後以
`VERACard::SetSDImagePath()` 掛載。`SetSDImagePath()` 掛載影像並把路徑存到
registry（每 slot 區段，`REGVALUE_VERA_SD_IMAGE` = "SD Card Image"）；`UnmountSD()`
清除。建構子用 `GetSDImagePathFromRegistry()` 還原已存的路徑。只改 SD 影像時
立即套用（不需強制重啟）。`VERACard::Update()` 也會執行一次 `TestSDRead()`
自我測試（讀 LBA 2048，把 FAT32 開機簽名寫入 `VERA.log`）。

---

## 5. 音效規格

### PSG

- 16 通道，時脈 `PSG_CLOCK = 25000000 / 512`。
- 每通道：頻率、音量、左右、脈寬、波形（PWM / 鋸齒 / 雜訊）。
- 寫入位址 `0x1F9C0–0x1F9FF`（PSG 區）。

### PCM

- 4096 bytes FIFO，支援迴圈。
- 控制 / 速率 / FIFO 寫入經由 `$9F3B–$9F3D`（reg 0x1B–0x1D）。
- 支援 16-bit 線性、立體聲混音。

---

## 6. 在 AppleWin 中使用（命令列）

VERA 卡由命令列指定插槽：

```
AppleWin.exe -s2 vera
```

或插到 slot 4：

```
AppleWin.exe -s4 vera
```

### 搭配 Slot 7 硬碟（例如 TimePilot）

```powershell
Release\AppleWin.exe -s2 vera -s7 hdc -h1 "C:\dev\Time-Pilot\TimePilot-IIvera\TimePilot-IIvera.hdv"
```

- `-s7 hdc`：Slot 7 安裝硬碟控制器（HardDisk Controller）。
- `-h1 <image>`：Slot 7 硬碟第 1 顆影像（`-h2` 為第 2 顆）。
- 啟動後先顯示 Apple II 畫面（DOS/ProDOS），軟體啟用 VERA 後自動切到 VERA 畫面。

### 圖形介面

在「Slot」設定頁的下拉選單中選擇 VERA 卡（`CT_VERA`）即可。

### 掛載 SD 影像（SD 卡 SPI）

開啟 `Configuration -> Slots`，選擇 VERA 卡（即使該 slot 原本是 empty）並對
VERA 卡按「Configure...」。「VERA SD Card」對話框可讓你 **Select Image...**
（原始 512-byte-block 影像，例如 FAT32 `.img`）或 **Unmount**。選取的影像會
掛載為 VERA SD 卡，路徑會存到 registry（每 slot），下次啟動自動還原。
可以在同一次設定好 VERA 卡與 SD 影像後一次重啟，兩者會同時生效。

---

## 7. 建置與執行

```powershell
$msbuild = "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe"
& $msbuild "AppleWin-VS2022.vcxproj" /p:Configuration=Release /p:Platform=Win32 /v:normal /nologo
```

> 注意：單執行緒建置較穩（`/m` 可能導致「0 Error 但 Build FAILED」）；
> 建置前若 `AppleWin.exe` 仍在執行會造成 LNK1104，需先 `Stop-Process`。

---

## 8. 存檔（Save State）

- `SaveSnapshot` 保存 128 KB VRAM 與全部 VERA IO/Composer/Layer 暫存器。
- `LoadSnapshot` 先 `Reset(false)` 再還原 VRAM 與暫存器。
- 快照單位版本 `kUNIT_VERSION = 1`。

---

## 9. 已知限制

- VERA 幀率以 Apple 幀率驅動（約 1 幀 / Apple 幀），與真實 59.5 fps 有
  ~0.8% 差異，實務上不可察覺。
- 每幀完整一幀的推進方式使 VSYNC/LINE IRQ 在同一時刻觸發，中線光柵特效
  時序不精確（對多數軟體無影響）。
