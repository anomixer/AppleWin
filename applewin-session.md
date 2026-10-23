# AppleWin VERA 專案 — Session 交接筆記

> 這份文件是給新 session 接手用的完整脈絡。工作目錄：`C:\dev\applewin`。
> 架構、build 流程、除錯記錄一律以 **`AGENTS.md`** 為權威來源，本筆記
> 只補 git 狀態、進行中的工作與待辦。

---

## 0. 專案是什麼

- **AppleWin** — Apple II 模擬器（Windows），加上了 **Commander X16 VERA**
  擴充卡移植（`source\VERACard\`），含 SD 卡 SPI、FM 女兒板（YM2151）。
- 使用者主要用**繁體中文**溝通。
- 相關 repo：`C:\dev\veramusic`（Apple II 音樂工具鏈，見 §2 / §3）。

---

## 1. 目前 git 狀態（2026-10-23 更新）

```
branch = feat/verafm（repo 預設分支），HEAD 與 fork/feat/verafm 完全同步
工作樹：clean；feat/vera 已刪除（本地與 remote）
落後 upstream/master commits: 0
```

2026-10-23 已透過正式 merge commit `60f23eaf` 將上游 **master**（`35366c56`，含 spng PNG 存檔、VS2026 專案、
CONTRIBUTING AI 政策）完整記錄在 commit graph 中。解決了因先前 squash 導致落後 4 commits 的假象，
程式碼內容 100% 相同且無 conflict。
Win32 / x64 Release 建置均 0 error 通過。

近期 commit（本 branch 頂端往下）：

```
60f23eaf  Merge branch 'master' of https://github.com/AppleWin/AppleWin into feat/verafm
47a008ec  feat(verafm): YM retrigger, X16 mix balance, speed-aware VERA time + PCM docs
1a7eac32  docs(verafm): record zsmstream TITLE-noise status; update session notes
1f375f9c  docs(verafm): update TITLE-noise status ...; add melodius ground-truth workbench
1a7eac32  docs(verafm): record zsmstream TITLE-noise status; update session notes
58aea9c7  docs(verafm): record zsmstream player debugging lessons
0b1a0ff5  docs(verafm): fix the chip/socket wording (YM2151 in the socket, OPM2151 as FPGA replacement)
d694ffd7  docs(verafm): name the emulated chip Yamaha YM2151 (A2VERA EV1 hardware)
4ccde5a5  docs(verafm): record FM verification across all docs
ae61f14f  feat(verafm): verify FM audio end-to-end; add opt-in -fm-wav-dump capture
23ba89a7  feat(verafm): A2VERA FM daughterboard (YM2151) via ymfm + VERAYM wrapper
d4366ebc  docs: update docs for upstream v1.32.1.0 merge
8cbc0d41  Merge upstream master into feat/vera   ← 已同步上游 v1.32.1.0
```

> 已完成並 commit 的里程碑：上游 v1.32.1.0 合併、SD 卡 SPI（歷史壓縮時期）、
> FM 女兒板 + 端到端驗證、`-fm-wav-dump`、AGENTS.md 除錯教訓記錄。
> TITLE 修復、FM retrigger、混音平衡、VSYNC 換算與文件同步已於
> `4e8f14ec` 收錄並 push 到 `origin`（fork `anomixer/AppleWin`）。

**Remote**：`origin` = 使用者的 fork `anomixer/AppleWin`（push 都到這裡，
預設分支 `feat/verafm`，remote 上只剩這一個分支）。上游 `AppleWin/AppleWin`
沒有設成 remote；需要同步時用
`git fetch https://github.com/AppleWin/AppleWin.git master`。

---

## 2. 已完成 — veramusic zsmstream 播放器 + TITLE 雜音（主線，2026-10-22 結案）

`C:\dev\veramusic` 是 Apple II 音樂播放鏈（獨立 repo），播放器為 Apple //e
的 ZSM 串流播放器（MLI 讀 ProDOS HDV、雙 frame buffer FB_A=$7300 /
FB_B=$7500、VSYNC IRQ 逐幀播），含按鈕控制與 STARTUP 選歌選單。修復已由
commit `3ee0462` 收錄並 push（`master == origin/master`；改寫前的本地備份
分支 `backup/master-pre-signature-clean` 已刪）：

```
3ee0462  fix(audio): X16 mixer balance, FM retriggers, zsmstream TITLE fixes
3805029  feat(zsmstream): add direct-BRUN autostart for headless --autoplay runs
de61f59  feat(zsmstream): in-song button controls + startup song-select menu
43f24b4  feat(zsmstream): Apple //e ZSM stream player (PSG + YM2151 FM, 11-song menu)
```

> hash 於 2026-10-22 因清除 bot 署名改寫過；舊 hash `894daed` /
> `f360b2e` / `889dc26` / `2d9b9fc` 已不存在於 remote。

原本症狀：TITLE.ZSM 播放時 PSG 段雜音、歌變慢，FM 段聽起來像雜音/只有 PSG
出聲（使用者：「歌慢，FM播放有一堆雜音」「感覺只有psg而已，fm沒出來」）。
三個獨立 bug：

1. **PSG flood（雜音＋變慢）** — `TICK4` 的 `STA TCNT1` 不設 flags，
   `BEQ TICK_YM` 測到的是 `CMP #$FF` 的殘留 Z；psgCnt=0 的 frame 落進
   PSG 路徑，`DEC TCNT1` 下溢成 $FF，IRQ 寫出 256 組假 PSG pair（位址
   跑出 PSG 窗口到 palette/sprite VRAM）。修法：branch 前加 `CMP #$00`。
   PSG walk 同時改成 16-bit（`FB_RD_L/H = FB_ACT`、`LDA (FB_RD_L),Y`）。
   先前「TICK_PSG 走原始 stream buffer」的診斷其實是這個 flood 的症狀。
2. **EOD 迴圈 DISK ERR $04** — `SETMARK_PARAMS` 參數數目寫成 4，但 ProDOS
   MLI SET_MARK 是 **2**（refnum + 24-bit position）；改成 2。
3. **FM retrigger 失效（後續音符無聲/衰減）** — ZSM 在同一 tick 內先
   `$08=ch`（key-off）再 `$08=$78|ch`（key-on），ymfm 每生成一個 sample
   才取樣一次 key state，中間沒有 sample 就互相抵消。修法：key-off 寫入後
   clock 一個 native YM sample — AppleWin `VERAYM::WriteData`
   （`m_regSelect == 0x08 && (val & 0x78) == 0` → `ClockKeyOff()`）與
   melodius `ym_clock_keyoff()` 各一份。

**混音平衡**（X16 Developer Board 硬體）：`mix = (PSG + PCM) * 2 + YM`。
原本 1:1 相加讓 FM 壓過 PSG（使用者：「FM很大聲，PSG小貓叫」）。AppleWin
在 `VERACard::UpdateSound()` 把 VERA bus 乘 2 再加 YM（線性硬夾，
melodius 同樣改法）。使用者耳朵驗證「這樣ok了」。

驗證：TITLE 第一輪正常、約 171M cycles 回到迴圈點；BOTHPSG 迴圈 3.28×；
headless `-fm-wav-dump` 各段峰值 0.13–0.26（舊 dump 卡在鼓聲底噪 0.08）。
**其餘 10 首尚未逐一實時聽測。**

> 注意：AGENTS.md 的舊 pc-hook 對映（TMPFB=0x2C0A 等）描述的是已刪除的
> debug build；probes 已從 `VERACard.cpp` 移除，現行 autoplay build 的
> zp 對映見 AGENTS.md（`ERR=$370C` …），且位址會隨播放器重組譯漂移。

---

## 3. veramusic repo 目前狀態（2026-10-22 更新）

- **Melodius 點唱機上線**：`C:\dev\veramusic\melodius.hdv`（+ `.zip`）取代
  舊的 `zsmstream.hdv` / `zsmstream_a.hdv`。內容是 X16 `MELODIUS` 全集
  **94 首 ZSM**（93 + `TREE.ZSM`，MIDI 不收），兩層選單（17 個
  category → 歌單），仿 `jukebox.hdv`。建置：
  `node tools/build_melodius.mjs`（`--list` 列歌單索引、
  `--autoplay <idx>` 在 `%TEMP%\melodius_autoplay.hdv` 產 headless 測試片）。
- **PCM 打通**（原本是「PCM deferred」）：播放器現在解 `$40` EXTCMD 的 PCM
  子事件，並把取樣從**同檔名的 `.PCM` 附檔**（ProDOS 8 對已開檔的第二個
  OPEN 回 `$50`，所以 build 時把 PCM 表切出來）以 2 KB 視窗邊播邊讀
  （`PCM_FILL_WINDOW` / `FP_PUSH_ONE` 比對 `PCM_WIN`/`PCM_WINEND`）。
  舊的 bulk load 上限 7 KB，28 首有 20 首取樣被截斷，Crockett 的 125 KB
  更因為 cap 只比 high byte 而溢位、直接把該歌秒退。
- 驗證（headless + `VERA.log` 的 `$C21D` 寫入）：Crockett 20 s 內 357k
  FIFO bytes（≈17.8 kB/s，rate 64 對得上），直方圖是 signed 8-bit 音訊
  形狀（mean −0.17、峰在 `$00`/`$FF`）；TREE 的 1.47 MB 取樣 508k bytes、
  播到 `T:00:16.5` 無 DISK ERR；正式片開機到 17 類選單正常。
  AppleWin 端**沒改**：`VERAAudio` 的 rate/format/volume 與參考渲染器
  `veramusic/tools/melodius.cpp` 完全一致。
- 已刪（連同 `melodius.hdv(.zip)`、`src/MELODIUS{,4}.BIN` 一起收錄在 veramusic
  `5a7558f`，已 push）：`zsmstream.hdv(.zip)`、`zsmstream_a.hdv(.zip)`、
  `src/ZSMSTREAM{,4,A}.BIN`、`src/zsmstream_{startup,autostart}.bas`、
  `tools/build_zsmstream{,_hdv}.mjs`；`build.bat` 的 `zsmstream` 目標改跑
  `build_melodius.mjs`（目標名 `melodius` 亦可）。`src/zsmstream.asm` 保留為引擎。
- veramusic 工作樹的**未追蹤**檔目前只剩使用者的除錯暫存（`tools/*.py`、
  `dis6502.*`、`state_fmfix.aws`、
  `AppleWin.log`、`33A7.`、`PRODOS#FF0000`、`_extract/` 等）。
  **使用者說要自己清 `tools/*.py`**，勿代刪、勿 commit。
  本 session 自己產的截圖/WAV 已清掉（驗證截圖改放 `C:\temp_pcm\shots_*`）。

---

### 2026-10-23 追加：Apple //e PCM 雜音結案（修正都在 veramusic 端）
- **雜音根因在 build 工具，不在 AppleWin**：`tools/zsm_pcm_resample.mjs` 的
  `parseZsmPcm` 把 16-bit PCM 的 frame 大小算成 1 byte（只看有沒有 stereo），
  mono16 的 Crockett 鼓聲因此被逐 byte 抽樣成 hash。修成
  `channels × sampleBytes` 後，單擊鼓波形與參考 `melodius.exe` corr **0.999**。
- **FIFO 供給預算**：27 首混音歌的鼓聲原本高達 97.7 kB/s，1.02 MHz 的 6502
  供不起（~22 cycles/byte）。新增 `capZsmPcmByteRate(buf, 16000)`，把所有
  PCM 歌的位元組率統一到 **16,022 B/s**（TREE 27,847→rate 42、
  Crockett 97,656→16,022），用「速率等比例縮放 + 資料反向內插」保值長度與音高。
- **驗證**（AppleWin headless 1.02 MHz，無 `-log`）：TREE
  `pushed 16022 / popped 16022 / underrun 0`、envelope corr 0.9948；
  Crockett 重建後 RMS 2,272→3,896、peak 近滿刻度、無靜音。
  `VERAAudio`／`VERAVideo` 無需修改。
- **追加（同日）：全面降到 8 kHz**。使用者反映 Crockett 在 16 kB/s 仍「有點小頓」，
  要求比照 `jukebox.hdv` 的 8 kHz。`capZsmPcmByteRate` 改為
  `capZsmPcmSampleRate(buf, 21)`，並把 16-bit 樣本以 TPDF dither 重新量化成
  8-bit（幾何位元組一併改寫）。結果：單聲道歌 **8,011 B/s**、5 首立體聲歌
  16,022 B/s；`PCM01.PCM` 422,872 B、`MOO03.PCM` 6,765 B、zip 3,630 KB。
  轉換品質（Crockett 對 16 kHz 版，兩邊都把 `.PCM` 接回後用 `melodius.exe
  --wav` 渲染）：envelope corr 0.9999、RMS −0.04 dB；單獨量 16→8 bit 這一步
  為 SNR 29.0 dB。headless 1.02 MHz 重跑（影像已釋放）記錄到 `$C21C` 在
  `$15`（rate 21）與 `$00` 之間交替（54 次）、56 次 `$C21B`；dump 在開機沉默
  之後沒有任何零樣本區段。
  `build_melodius.mjs` 新增 `--out <path>`，可在 `melodius.hdv` 被 emulator
  掛載時先建到別處。
- **ESC 殘音**：`zsmstream.asm` 離場時 flush PCM FIFO + `PCM_KEY_OFF`（`$80`）
  + FM key-off + PSG 靜音；實測 12 s 中斷後剩餘 8 s 的 dump 全零。
- **melodius.exe 狀態列**改為使用者指定格式：
  `mm:ss/mm:ss [===============>] [FM ........] [PSG ......#....] [PCM --] V:15 FM:1.00 PSG:2.00 PCM:2.00 [1SHT]`。
- **非 60 Hz tick rate 正規化（Headbanger! 聽起來 4× 太慢）**：`zsmstream.asm`
  現在比照 zsmkit — `DECODE_FRAME` 每個 60 Hz 輸出影格吃掉
  `TICKS_PER_FRM = rate/60` 個來源 tick，`TICK_REM = rate%60` 進位累積在
  `FRAC`；rate < 60 輸出 hold 影格。`FRM` 改數來源 tick，5 s 跳轉因此用
  `5 * TICKRATE`。選單時長（`build_melodius.mjs`）與 `melodius.exe` 的
  mm:ss 也改成 `ticks ÷ header tick rate`（Headbanger 240 Hz：17:34 →
  **4:24**，離線 WAV 263.6 s 且與舊渲染的前 263.6 s 完全相同）。
  94 首裡 11 首非 60 Hz。驗證：3 MHz 與預設 1.02 MHz 都连续 RMS 2400–3200、
  零樣本比 0.00004，60/64/50 Hz 歌曲迴歸正常（合併影格最大 510 B < FB 512 B）。
- AppleWin 本體唯一改動：`VERACard.cpp` 的 VERA.log 過濾新增
  `(addr & 0x1f) != 0x1d`（PCM FIFO 資料埠），避免 `-log` 拖慢量測。
  Release **Win32 與 x64 皆已重建**、探針已移除。

---

## 4. 待辦（按優先序）

1. ~~決定 `melodius.hdv` 要不要進 git~~ — 已隨 2026-10-23 修正一起提交。
2. 實時聽測：Crockett / TREE 的 PCM 已有系統性驗證；其餘 PCM 歌 與
   66 首純 FM/PSG 歌尚未逐一試聽。
3. （可選）x16-rom / ProDOS 工具 — 舊 session 曾評估做一個 ProDOS 下可讀
   VERA SD SPI 資訊的 6502 工具（repo：`x16-rom`，X16Community）。

---

## 5. 環境 / 工具

- 工作目錄：`C:\dev\applewin`；Release exe：`C:\dev\applewin\Release\AppleWin.exe`。
- 相關目錄：`C:\dev\apple2ts`（apple2ts 原始碼）、`C:\dev\veratest`
  （asm6502.mjs / applebasic.mjs 組譯工具）、`C:\dev\veramusic`
  （ZSM 點唱機 `melodius.hdv`、FM 測試 `fmtest.po`、自家
  AGENTS.md）、`C:\dev\emu\ap2\prodos243.po`（外部 ProDOS 基底磁片）。
- 測試磁片：`C:\dev\Time-Pilot\TimePilot-IIvera\TimePilot-IIvera.hdv`。
- Build 務必經 solution、單執行緒（勿 `/m`）；LNK1104 先殺
  `AppleWin.exe`。詳見 `AGENTS.md` 的 build 小節。

---

## 6. 提醒新 session

- 使用者用**繁體中文**；回覆用繁中。
- **commit / PR 一律不加 bot 或工具署名**（不要 `Co-Authored-By`、
  不要 "Generated with ..."）——使用者明確要求。
- 臨時/截圖檔（test_out.txt、Release zip）已 gitignore，勿 commit。
- 臨時除錯碼用完移出 tree；`Release\VERA.log` 會跨 run append（多 run
  並存，用最新 restart 標記過濾）。
- 長 session：context 塞爆前先依使用者指示 compress（記憶檔 + 本筆記）。
