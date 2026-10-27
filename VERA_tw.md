# VERA 卡（AppleWin 擴充卡）說明文件

本文說明本專案將 **Commander X16 的 VERA（Versatile Embedded Retro Adapter）**
晶片移植成 **AppleWin 插槽擴充卡**的規格、與 AppleWin 整合的方式、以及使用
方法。核心是 `apple2ts` 的 TypeScript 實作（`src/worker/devices/vera/`）的
C++ 移植。

> 與 VERA 相關的開發架構、建置與除錯記錄請見 `AGENTS.md`。
> **註：** 本專案已與 AppleWin 上游 **master** 同步（v1.32.1.0 之後：spng
> PNG 存檔、VS2026 專案、CONTRIBUTING AI 政策），2026-10-22 合併。
>
> **開發模式：** 本專案之 VERA / VERAFM 擴充卡功能由人工規劃整體架構、硬體規格與測試驗證，並由 AI 工具輔助程式碼實作，符合上游 [AI Tool Use Policy](CONTRIBUTING.md#ai-tool-use-policy) 之規範。

---

> **2026-10-27 FM I/O 遷移（AI 輔助實作）：** FM 改用 `/DEVICE SELECT`，
> Slot 2 為 `$C0A0/$C0A1`，Slot 4 為 `$C0C0/$C0C1`。使用舊
> `$Cs20/$Cs21` 對映的客端程式／磁片須更新位址並重建，才能驗證 FM 播放。

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
- FM 女兒板（Yamaha YM2151；插座可裝真 YM2151 或 OPM2151 FPGA 替換件）已實作並完成端到端驗證（2026-10-16）— 見 §5「FM 女兒板」。
- SD 卡 SPI（SD/MMC）已實作 — 見 §4「SD 卡 SPI」。
- 客端音樂播放（veramusic 的 Apple //e zsmstream 播放器）已驗證：TITLE.ZSM
  與 BOTHPSG.ZSM 可正常播放並乾淨地 loop，PSG 與 FM 都聽得到 — 見
  `AGENTS.md` → Testing 的「zsmstream player — TITLE.ZSM noise + loop
  DISK ERR: RESOLVED」條目。

---

## 2. 與 AppleWin 的整合

### 2.1 IO 定址

VERA 暫存器位於所選插槽的 **Cx 區域**：`$Cs00–$CsFF`，其中 `addr & 0xff`
選定暫存器。透過 `RegisterIoHandler(slot, &IOReadC0, &IOWriteC0, &IOReadCx,
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

- 獨立的環形緩衝（`DSGetSoundBuffer` / `DSGetLock`）；
  `kDSBufferByteSize = 44100 / 60 * 3 * 2 * kNumChannels`（約 3 幀立體聲
  ≈ 50 ms），初始化時建立 **1/2 緩衝的前導量**。
- 取樣率 44100 Hz、立體聲。`UpdateSound()` 由 **DS 播放游標**驅動（不是
  CPU 時鐘）：只產生硬體實際消耗的取樣數（小數累加器），寫入速率與播放
  完全同步、零漂移。
- **混音平衡**：`mixed = (PSG + PCM) * 2 + YM`，再夾到 int16 — X16
  Developer Board 的 VERA 匯流排相對 YM2151 是兩倍（x16emu `src/audio.c`）。
  單一全音量 PSG 聲道只有滿刻度的 1/8，若 1:1 相加，PSG 會被 FM 蓋掉。

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

`VERACard::Update()` 被呼叫約 **16 次 / 幀**（每次約 1000 週期的批次）。
目前做法是**依 CPU 週期即時推進**（對應 apple2ts 的 `syncVera()`）：
`VERACard::SyncVideo()` 依 `g_nCumulativeCycles` 的增量呼叫
`m_video.Step(cpuMhz, delta)`，且 `IOReadCx`/`IOWriteCx` 在**每次 VERA 暫存器
存取前**都會先 `SyncVideo()`，所以掃描線暫存器（`$08`）在當下週期就是即時值。
`g_nCumulativeCycles` 只在 CPU 批次邊界更新，因此 IO handler 會先呼叫
`CpuCalcCycles(nExecutedCycles)` 校正到精確的讀寫週期，否則掃描線探測會看到
批次化的值，增量會不均（`64/32` 而非 `32/32`）：

```cpp
void VERACard::SyncVideo()
{
    const uint64_t now = g_nCumulativeCycles;
    if (m_lastVideoCycles == 0) m_lastVideoCycles = now;
    const uint64_t delta = now - m_lastVideoCycles;
    m_lastVideoCycles = now;
    if (delta == 0) return;
    // VERA 的 25 MHz 像素時鐘與 6502 無關：用實際（依速度調整的）CPU
    // 時鐘換算週期增量，任何 Emulation Speed 下 VSYNC 都維持 ~59.5 Hz。
    const double cpuMhz = g_fCurrentCLK6502 / 1000000.0;
    const bool newFrame = m_video.Step(cpuMhz, (int)delta, false);
    if (newFrame)
    {
        m_video.Update();
        if (m_video.IsVideoOutputEnabled())
            UpdateDisplay();
    }
}
```

> 曾嘗試「每個 Apple 幀只推進完整一幀」並已回退：那會讓掃描線在幀內凍結，
> 而 SMB1 移植版的 VERA-alive 探測會連讀 3 次掃描線並要求平滑變化 — 凍結的
> 掃描線會讓探測失敗、回報 `NO VERA`。依週期推進才能讓掃描線保持活動。
>
> `cpuMhz` 必須取自 `g_fCurrentCLK6502`，不能寫死 1 MHz。本機 //e 跑 3 MHz，
> 寫死 1 MHz 會讓 VERA 掃描快三倍（VSYNC 181 Hz）；用實際時鐘才會維持
> ~59.5 Hz。

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
byte）。`VERACard::SyncSPI()` 在每次存取 VERA 暫存器（`IOReadCx`/`IOWriteCx`）與 `Update()`
時依據週期增量即時推進 SPI，因此傳輸在 1～2 CPU 週期內即可完成。Guest 程式仍應
**poll SD_STATUS bit 7** 等候傳輸完成。

**支援指令**（SPI 模式）：CMD0、CMD8、CMD9（SEND_CSD）、ACMD41（CMD55+CMD41）、
CMD12、CMD13、CMD16、CMD17、CMD18、CMD24（寫入）、CMD25（多區塊寫入）、CMD55、CMD58（READ_OCR）。
- **CMD58（READ_OCR）**：依 SD SPI 規範回傳 5 位元組 `[R1, 0xC0, 0xFF, 0x80, 0x00]`，首位元組為卡片狀態（閒置為 `0x01`，已初始化為 `0x00`）。
- **CMD24（WRITE_BLOCK）**：接收起始 token（`0xFE`）、512 位元組資料與 CRC16 後，回覆標準 Data Response Token：寫入成功回覆 `0x05`（接受），超出邊界或唯讀錯誤回覆 `0x0D`（拒絕）。
- **CMD25（WRITE_MULTIPLE_BLOCK）**：每個 `$FC` token 後接一個 512 位元組資料區塊，完成後自動前進至下一個 LBA；`$FD` 結束連續寫入。CMD24 每個區塊立即 flush，CMD25 在 `$FD` 才 flush 一次，避免 FAT32 格式化時每個 sector 都同步寫入。
- **寫入保護**：VERA SD Card 對話框提供 **Write Protected** 勾選框，設定會依 slot 保存；啟用後 CMD24/CMD25 回覆 `0x0D`，guest 可辨識 SD 為唯讀。
- **狀態重設**：實作 `ResetSpiState()`，在卸載 SD 卡或更換映像檔時自動清空 SPI 緩衝區與傳輸狀態。

**掛載 / GUI：** `Configuration -> Slots` → 選 VERA 卡 → 「Configure...」開啟
`IDD_VERA_SD_CARD`「VERA SD Card」對話框（`PageSlots.cpp`），有「Select Image...」
與「Unmount」及 **Write Protected** 勾選框。對話框**不要求** VERA 卡已裝好：選取的路徑會存到
`CConfigNeedingRestart::m_VERASDImagePath[slot]`，因此可以一次選好 VERA + SD
影像再重啟，`ApplyConfigAfterClose()` 會在卡片（重新）插入後以
`VERACard::SetSDImagePath()` 掛載。`SetSDImagePath()` 掛載影像並把路徑存到
registry（每 slot 區段，`REGVALUE_VERA_SD_IMAGE` = "SD Card Image"）；`UnmountSD()`
清除。建構子用 `GetSDImagePathFromRegistry()` 還原已存的路徑。只改 SD 影像時
立即套用（不需強制重啟）。`VERACard::Update()` 也會執行一次 `TestSDRead()`
自我測試（讀 LBA 2048，把 FAT32 開機簽名寫入 `VERA.log`）；完整協定驗證由
`test\VERATest\VERATest.cpp`（Test 7）涵蓋。

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

### FM 女兒板（YM2151）

- A2VERA 板（原型硬體「EV1」版）增加一顆 Yamaha YM2151 FM 晶片，經由
  `/DEVICE SELECT` 定址：`$C080 + slot*$10 + 0` = `YM_SEL`（暫存器選擇），
  `+1` = `YM_DATA`（資料寫入 / 狀態讀取）。Slot 2 使用 `$C0A0/$C0A1`，
  Slot 4 使用 `$C0C0/$C0C1`。舊的 `$Cs20/$Cs21` FM 對映已移除；
  VERA 影像與 SD 暫存器仍使用 Cx 區域。
- 由 `source/VERACard/VERAYM.h/.cpp` 包裝 `ymfm::ym2151` 核心（
  `source/ymfm/`，BSD-3-Clause）模擬：插座可以裝真正的 YM2151，或改用
  OPM2151（為老舊 YM2151 設計的現代 FPGA 替換件）—— 無論哪種，模擬的都是
  YM2151。原生 ~55,930 Hz 輸出（晶片時脈 3,579,545 / 64）經線性重取樣到
  44.1 kHz，混入 VERA 的 DS 音效 buffer。
- **混音平衡**：VERA 匯流排（PSG + PCM）乘 2、YM2151 維持 1× —
  `mixed = (PSG + PCM) * 2 + YM`，再夾到 int16（見 §2.4）。
- **ZSM retrigger**：對 `$08` 寫入 operator mask 為 0（`(val & 0x78) == 0`）
  時，`VERAYM::WriteData` 會時鐘一個原生 YM 取樣（`ClockKeyOff()`），讓緊接
  而來的 key-on 能重新起音。ymfm 只在每次產生取樣時取樣 key 狀態；少了這個
  時鐘，同一 tick 內的 key-off + key-on 會互相抵消，第一音之後的每個音都只會
  衰減。
- 狀態暫存器會反映晶片的 BUSY 視窗（每次資料寫入後約 32 個晶片時脈 bit 7
  為設置狀態）；客端程式必須**輪詢 `YM_DATA` bit 7**。
- 可逐插槽啟用／停用（VERA 設定對話框的 `FM Card Enabled`，持久化到該插槽
  的 registry）。停用時 FM 埠使用空插槽 I/O 行為（`IO_Null`）。
- 已完成端到端驗證（2026-10-16）：客端程式寫入標準 init + key-on 序列後
  模擬器輸出全幅 FM 音。自動化驗證：`-fm-wav-dump` 把混合 VERA buffer 的前
  20 秒（PSG + PCM + FM 總和，立體聲 16-bit @ 44100）擷取到 exe 旁的
  `VERA-fm.wav`；選項性，未給 flag 不寫任何東西。headless `-run-cycles`
  需 ≥ 約 8M 週期（測試磁片的 key-on 在約 6.2M 週期）：
  ```
  AppleWin.exe -s2 vera -s6 diskii -d1 fmtest.po -power-on -log -run-cycles 12000000 -fm-wav-dump
  ```
  （`fmtest.po` 沒有收進 `veramusic` 的 repo；要先用
  `node veramusic/tools/build_fmtest_po.mjs` 建出來。）
- 客端音樂播放用 `C:\dev\veramusic` 的 Apple //e ZSM 串流播放器
  （`zsmstream.asm`，經女兒板輸出 PSG + FM + PCM），以 94 首的
  `melodius.hdv` 點歌台形式交付：
  `AppleWin.exe -s2 vera -s7 hdc -h1 C:\dev\veramusic\melodius.hdv -power-on`。
  已用 TITLE.ZSM 與 BOTHPSG.ZSM 驗證（PSG + FM、loop seek 乾淨），非 60 Hz
  的檔案也正常：播放器會把 ZSM header 的 tick rate 正規化成 60 Hz 輸出影格
  （zsmkit 語意），所以 240 ticks/s 的 Headbanger! 以正確速度播放。除錯歷史
  與剩餘限制見 `AGENTS.md` → Testing →
  "zsmstream player — TITLE.ZSM noise + loop DISK ERR: RESOLVED"。

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

### 自動化測試與驗證

為了方便自動化腳本或進階除錯測試，AppleWin 支援在指定的週期後自動結束並匯出狀態：

```powershell
Release\AppleWin.exe -s2 vera -run-cycles 5000000 -vera-dump "dump.bin" -save-state "state.aws"
```

- `-run-cycles <N>`：讓模擬器執行 `N` 個 CPU 週期後自動關閉。
- `-vera-dump <檔案>`：在自動關閉時，將 128KB 的 VERA VRAM 完整傾印到指定二進位檔。
- `-save-state <檔案>`：在自動關閉時，自動儲存 AppleWin 的狀態快照。

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
& $msbuild "AppleWin-VS2022.sln" /p:Configuration=Release /p:Platform=Win32 /v:normal /nologo
```

> 注意：務必經 **solution**（不要只建 `.vcxproj`），讓 `TestCPU6502` 的建置
> 順序正確。單執行緒較穩（`/m` 可能導致「0 Error 但 Build FAILED」）；
> 建置前若 `AppleWin.exe` 仍在執行會造成 LNK1104，需先 `Stop-Process`。

---

## 8. 存檔（Save State）

- `SaveSnapshot` 保存 128 KB VRAM 與全部 VERA IO/Composer/Layer 暫存器。
- `LoadSnapshot` 先 `Reset(false)` 再還原 VRAM 與暫存器。
- 快照單位版本 `kUNIT_VERSION = 1`。

---

## 9. 已知限制

- VERA 時間由 CPU 週期換算，換算係數取自目前的模擬速度
  （`g_fCurrentCLK6502`）。1 MHz 與 3 MHz 下 VSYNC 都維持 ~59.5 Hz 實時，
  但執行中切換模擬速度會造成掃描相位偏移。
- CPU 批次之間（`Update()` 約每 1 ms 一次）影片時間是一次推進的。任何
  VERA 暫存器存取都會先重新同步，所以掃描線探測看到的是即時值；但真正的
  逐週期中線光柵特效仍是近似。

## 10. 驗證與直條紋修正（2026-10-27）

VERA 重設現在會清空圖層、精靈及合成器的掃描線快取，並從重設後的暫存器重建
衍生屬性，避免停用圖層的殘留像素蓋到圖片和 OSD 文字。此修正由 AI 輔助實作；
測試 8／9 分別涵蓋已啟用的 Layer 1 與精靈跨越重設後的狀態。

Win32／x64 Release solution 建置與 CPU 前置測試成功，VERA 核心測試 **26/26**
通過。新 FM 對映的執行測試為 Win32 **26/26**、x64 **2/2**；直條紋修正後另有
RGB／完整掃描線模式 **8/8** 通過，涵蓋兩個插槽的音效 demo、Sonic、slideshow
及停用 FM 的 slideshow。

已重建 `C:\dev\veratest\veratest.po`、`slideshow.hdv` 與對應 ZIP。BASIC 選單和
FM 播放器共用 BUSY 偵測程式；使用者修改的 ABOUT 排版保留為 24 行、最長
39 字元，最後提示不換行。自動測試不取代音色、互動操作與實機驗證；測試內容
及暫存 QA 映像與交付映像的差異見 `VERAFM.md` 第 6／7 節。
