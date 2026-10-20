# AppleWin VERA 專案 — Session 交接筆記

> 這份文件是給新 session 接手用的完整脈絡。工作目錄：`C:\dev\applewin`。
> 架構、build 流程、除錯記錄一律以 **`AGENTS.md`** 為權威來源，本筆記
> 只補 git 狀態、進行中的工作與待辦。

---

## 0. 專案是什麼

- **AppleWin** — Apple II 模擬器（Windows），加上了 **Commander X16 VERA**
  擴充卡移植（`source\VERACard\`），含 SD 卡 SPI、FM 女兒板（YM2151）。
- 使用者主要用**繁體中文**溝通。
- 相關 repo：`C:\dev\veramusic`（Apple II 音樂工具鏈，見 §4）。

---

## 1. 目前 git 狀態（2026-10-20 更新）

```
branch = feat/verafm，HEAD = 19234c37，工作樹乾淨
```

近期 commit（本 branch 頂端往下）：

```
19234c37  docs(verafm): record zsmstream player debugging lessons
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

**Remote**：`origin` = 上游 `AppleWin/AppleWin`，`fork` = 使用者 fork
`anomixer/AppleWin`（push 都到 fork）。

---

## 2. 進行中的工作 — veramusic zsmstream 播放器 + TITLE 雜音（主線）

`C:\dev\veramusic` 是 Apple II 音樂播放鏈（分支獨立、與 applewin repo 分開），
最近三個 commit：

```
f360b2e  feat(zsmstream): add direct-BRUN autostart for headless --autoplay runs
889dc26  feat(zsmstream): in-song button controls + startup song-select menu
2d9b9fc  feat(zsmstream): Apple //e ZSM stream player (PSG + YM2151 FM, 11-song menu)
```

即：Apple //e 的 ZSM 串流播放器（MLI 讀 ProDOS HDV、雙 frame buffer
FB_A=$7300/FB_B=$7500、VSYNC IRQ 逐幀播），已含**按鈕控制與 STARTUP 選歌選單**。

**開著的 bug（主線）**：第 2 首 TITLE.ZSM 播放時 PSG 段雜音、歌變慢；FM 段
聽起來像雜音/只有 PSG 出聲。使用者原話：「2 title 那首 歌慢，FM播放有一堆
雜音... 後面psg部份又正常」「感覺只有psg而已，fm沒出來」。

已釘住的 root cause（headless `-run-cycles 60000000 -fm-wav-dump` capture，
`Release\VERA.log` run 4 = `lines[231330:]` 起）：**TICK_PSG 的
`(FB_ACT),Y` 在走原始 TITLE.ZSM stream buffer，不是 frame buffer** —
66 字節連續對齊鐵證（`win[5:71]==stream[1:67]`）、每 walk cycle 恰 128 組、
X≥8 寫出界落到 palette/sprite VRAM（144,599 筆 = 55%）⇒ 雜音機制；FM 側
確認正常。**下一步**（照序）：(1) 驗證窗口首字節 TCNT1=0x80 假設；
(2) 查 PUBLISH_FRAME/FRAME_START 的 FB_ACT 賦值；(3) 查 ProDOS MLI 是否
踩 zp $16/$17（FB_ACT）或 $27/$28（WRB_BASE）；(4) 修 `zsmstream.asm`、
重建磁片、headless 重驗。完整事實與卡點見 session 記憶檔：
`~\.claude\projects\C--dev-applewin\memory\` 下的
`HANDOFF-title-noise.md`、`zsmstream-title-psg-walk-noise.md`（其餘相關：
`zsmstream-fm-16bit-decrement-fix.md`、`zsmstream-setmark-paramcount-fix.md`、
`zsmstream-controls-and-startup-menu.md`）。

> 已知 caveat：AGENTS.md 的 pc-hook 對映（TMPFB=0x2C0A 等）描述的是
> **已部署的 debug build**（VERAZP 字串只存在於 Release\AppleWin.exe /
> .pdb / VERACard.obj，不在任何 commit 的 source 裡）；這份 capture 用的
> probe build 來源不在 git tree，位址會隨播放器重組譯漂移。

---

## 3. veramusic repo 未 commit 的狀態（需與使用者確認）

- `src/ZSMSTREAMA.BIN`、`zsmstream_a.hdv`、`zsmstream_a.hdv.zip` 為
  modified（autosong=1 直接放 TITLE 的 debug build）——待使用者決定
  commit 或還原 autosong=0。
- 未追蹤的除錯暫存檔：`tools/run4*.py`、`winalign.py`、
  `titlebytes/frames/pan/pcm/psg/tick.py`、`scanff.py`、`patfind.py`、
  `retest.py`、`dis6502.*`、`state_fmfix.aws`、`run4pickle.bin`、
  `AppleWin.log`、零散 `33A7.` / `PRODOS#FF0000` / `_FileInformation.txt`
  / `_extract/` 等。**使用者說要自己清 `tools/*.py`**，勿代刪、勿 commit。

---

## 4. 待辦（按優先序）

1. **TITLE 雜音修復**（§2 的下一步 4 步）→ 修好用繁中回報，請使用者
   實時測 TITLE + 其餘 10 首。
2. （§3）與使用者確認 autosong=1 debug build 的去留。
3. TITLE 修好後：更新 AGENTS.md 的 pc-hook 對映與除錯教訓。
4. （可選）x16-rom / ProDOS 工具 — 舊 session 曾評估做一個 ProDOS 下可讀
   VERA SD SPI 資訊的 6502 工具（repo：`x16-rom`，X16Community）。

---

## 5. 環境 / 工具

- 工作目錄：`C:\dev\applewin`；Release exe：`C:\dev\applewin\Release\AppleWin.exe`。
- 相關目錄：`C:\dev\apple2ts`（apple2ts 原始碼）、`C:\dev\veratest`
  （asm6502.mjs / applebasic.mjs 組譯工具）、`C:\dev\veramusic`
  （ZSM 播放器磁片 `zsmstream_a.hdv`、FM 測試 `fmtest.po`、自家
  AGENTS.md）、`C:\dev\emu\ap2\prodos243.po`（外部 ProDOS 基底磁片）。
- 測試磁片：`C:\dev\Time-Pilot\TimePilot-IIvera\TimePilot-IIvera.hdv`。
- Build 務必經 solution、單執行緒（勿 `/m`）；LNK1104 先殺
  `AppleWin.exe`。詳見 `AGENTS.md` 的 build 小節。

---

## 6. 提醒新 session

- 使用者用**繁體中文**；回覆用繁中。
- git commit 結尾 `Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>`；
  PR 描述加 🤖 Generated with [Claude Code](https://claude.com/claude-code)。
- 臨時/截圖檔（test_out.txt、Release zip）已 gitignore，勿 commit。
- 臨時除錯碼用完移出 tree；`Release\VERA.log` 會跨 run append（多 run
  並存，用最新 restart 標記過濾）。
- 長 session：context 塞爆前先依使用者指示 compress（記憶檔 + 本筆記）。
