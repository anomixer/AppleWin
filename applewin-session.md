# AppleWin VERA 專案 — Session 交接筆記

> 這份文件是給新 session 接手用的完整脈絡。舊 session（AppleWin VERA，id `session-5102e95a-...`）因 context 塞爆 + 反覆 output token limit 卡住，故在此交接。工作目錄：`C:\dev\AppleWin`。

---

## 0. 專案是什麼

- **AppleWin** — Apple II 模擬器（Windows），加上了 **Commander X16 VERA** 擴充卡移植（`source\VERACard\`）。
- 架構、build 流程、VERA 卡 / SD SPI / 顯示 / 音效等技術細節，一律以 **`AGENTS.md`** 為權威來源，本筆記不重複。
- 使用者主要用**繁體中文**溝通。

---

## 1. 目前 git 狀態（重要）

```
fork/master = <squash commit>   (https://github.com/anomixer/AppleWin)   ← 已 push（hash 見 `git log`）
local master = <squash commit>
HEAD         = master
工作樹：乾淨
```

> 歷史已 squash：`c1972253` 之後的全部 commit 已壓成**單一 squash commit**
> （VERA SD hex sector viewer + editor + docs）。

fork 歷史（最近）：
```
<squash commit>  VERA SD hex sector viewer, editor + docs   （squash）
c1972253  VERA: add SD/MMC SPI SD card emulation + GUI mount + docs
91c283f1  Docs: rename VERA spec - VERA.md is now English, Chinese spec moved to VERA_tw.md
7d9452c4  AGENTS: document building both Win32 (AppleWin.exe) and x64 (AppleWin-x64.exe) via the solution
499edd94  Log: write VERA.log in Release only when -log given (Debug always writes)
...
```

> 先前 session 曾把 `master` 弄亂（停在舊音效版 `4918e7a7`）。已把本機 `master` 重新指向正確提交並 push 到 fork。若再遇 detached HEAD，用 `git branch -f master <commit>` 修。

**Remote**：`origin` = 上游 `AppleWin/AppleWin`，`fork` = 使用者 fork `anomixer/AppleWin`（push 都到 fork）。

---

## 2. 已完成的工作 — VERA SD 功能（commit `c1972253` 內）

SD/MMC SPI 功能已實作並 commit（`VERASD.h/.cpp`、`VERAVideo.cpp`/`VERACard.cpp` 整合、GUI mount、registry 持久化、`VERA.md`/`VERA_tw.md` 更新）。**技術細節一律見 `AGENTS.md` 的 `### SD card (VERA SPI SD)`**，此處不重複。

---

## 3. 未完成 / 待辦（按優先序）

1. **（已完成）push fork**（local master = squash commit）。✅
2. **（可選）x16-rom / ProDOS 工具** — 舊 session 曾評估做一個 ProDOS 下可讀 VERA SD SPI 資訊的 6502 工具。相關 repo：`x16-rom`（X16Community）。

> 已完成項目（VERACard diagnostic 移除、SD SPI 顯示/輸入修復、歷史 squash）均已完成。

---

## 4. 舊 session 卡住的原因（給新 session 參考）

- Session「AppleWin VERA」跑了 ~100 回合、1500+ 步驟，context 塞到 ~75%（~197k/262k tokens），**反覆被 max output token 截斷**（turn 23、47、99 都是）。
- 看起來「卡住」其實是**反覆截斷 → 使用者 continue / 抱怨 → 又被截斷**的循環，不是死當。
- 主目標（SD SPI + GUI mount）已標記 complete。
- **教訓**：這種長 session 不要繼續堆；新 session 直接接手目前狀態（本筆記 + AGENTS.md 已涵蓋）。

---

## 5. 重要技術參考（簡錄）

- **執行迴圈時序**（`AGENTS.md` 有詳述）：`ContinueExecution` 每 1ms batch 跑 `CpuExecute` + `GetCardMgr().Update`。VERA 幀用 `g_nCumulativeCycles` 幀邊界偵測，每 Apple 幀推完整一幀（16800 週期）。**勿**用 per-batch 數直接推 VERA 幀。
- **Build**（務必經 solution，單執行緒）：
  ```powershell
  $msbuild = "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe"
  & $msbuild "AppleWin-VS2022.sln" /p:Configuration=Release /p:Platform=Win32 /v:minimal /nologo
  # x64: /p:Platform=x64
  ```
- **音效**：VERA 獨立卡，不借 Mockingboard/SSI263 模型；`UpdateSound()` 由 DS play cursor 驅動（分數樣本累加器），buffer ~3 幀。
- **顯示**：雙螢幕設計——平常 Apple II，VERA 軟體啟用輸出才切 VERA。framebuffer 680×516，bottom-up。
- **除錯**：崩潰是 SEH（0xC0000005），C++ try/catch 抓不到；用 `LogWriteVERALog()`（寫 exe 旁 `VERA.log`）。

---

## 6. 環境 / 工具

- 工作目錄：`C:\dev\AppleWin`。
- 相關目錄：`C:\dev\apple2ts`（apple2ts 原始碼）、`C:\dev\veratest`（asm6502.mjs / applebasic.mjs 組譯工具，上游來源）、`C:\dev\emu\ap2\prodos243.po`（外部 ProDOS 基底磁片，與 repo 內 `bin/ProDOS_2_4_3.po` 相同）。
- Release exe：`C:\dev\AppleWin\Release\AppleWin.exe`。
- 測試磁片：`C:\dev\Time-Pilot\TimePilot-IIvera\TimePilot-IIvera.hdv`。

---

## 7. 提醒新 session

- 使用者用**繁體中文**；回覆用繁中。
- 若 push 到 fork 需要 credential helper（GCM），在 sandbox 下會失敗——需用 `danger-full-access`（本 session 目前已是 danger-full-access + approval 停用，可直接執行）。
- 臨時/截圖檔（test_out.txt、Release zip）**已 gitignore，勿 commit**。
