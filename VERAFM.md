# A2VERA FM Audio (Yamaha YM2151) Emulation — Implementation Record

> **Status: COMPLETE (2026-10-16).** The FM chain is implemented, verified
> end-to-end and audible (full-scale ~440 Hz tone from the `veramusic`
> `fmtest.po` disk, both real-time and headless runs).
> Technical details and debugging history live in `AGENTS.md`
> (`### FM daughterboard (VERAYM)` and `### FM WAV dump (-fm-wav-dump)`).

> **Update 2026-10-22 — guest playback verified; retrigger + mix fixed.**
> The guest-side TITLE.ZSM fault was two `zsmstream.asm` bugs, now fixed and
> verified: a PSG-flood flag test (`STA` does not set flags) and the ProDOS
> `SET_MARK` parameter count (2, not 4). TITLE.ZSM and BOTHPSG.ZSM play and
> loop cleanly.
>
> Two FM-path refinements landed with it:
> - **Retrigger** — `VERAYM::WriteData` clocks one native YM sample after a
>   full key-off write to `$08` (`ClockKeyOff()`), so a ZSM key-off + key-on
>   inside one tick starts a fresh attack. Verified against the x16emu
>   ground-truth TITLE.ZSM render (all FM groups re-attack).
> - **Mixer balance** — `VERACard::UpdateSound` now mixes
>   `(PSG + PCM) * 2 + YM`, matching the X16 Developer Board. A 1:1 sum buried
>   the PSG under the FM (「FM 很大聲, PSG 小貓叫」); the ×2 build was
>   confirmed by ear.
>
> Technical details in `AGENTS.md` (`### FM daughterboard (VERAYM)`,
> `### Audio model`, and Testing → "zsmstream player — TITLE.ZSM noise +
> loop DISK ERR: RESOLVED").

> **Update 2026-10-23 — non-60 Hz ZSM tick rates normalized; the jukebox is
> the test disk.** `zsmstream.asm` now consumes `TICKS_PER_FRM = tickRate/60`
> source ticks (plus a `tickRate%60` carry) per 60 Hz output frame, the way
> zsmkit does, so Headbanger!.ZSM (header rate 240) plays at tempo instead of
> ~4× slow. Duration labels in `build_melodius.mjs` and `melodius.exe` divide
> the tick count by the header tick rate, not by 60 (Headbanger: 4:24).
> Guest playback is verified through `C:\dev\veramusic\melodius.hdv`, the
> 94-song jukebox:
> `AppleWin.exe -s2 vera -s7 hdc -h1 C:\dev\veramusic\melodius.hdv -power-on`.
> (`fmtest.po` is a locally built artifact — it is not committed in
> `veramusic`; regenerate it with `node veramusic/tools/build_fmtest_po.mjs`.)

## 1. Project Context & Goal
* **Target Platforms**: AppleWin (C++) and apple2ts.com (TypeScript).
* **Objective**: Add cycle-accurate support for the A2VERA FM Audio daughterboard (Yamaha YM2151) running at ~1.023 MHz.
* **Hardware note**: the A2VERA board (prototype hardware "EV1" release) carries a Yamaha YM2151 (OPM interface) — the socket can take a genuine YM2151 or the **OPM2151**, a modern FPGA drop-in replacement for the vintage YM2151 (the original is no longer practical to source). The emulated chip is the YM2151 either way.

## 2. Hardware Architecture & Register Mapping
* **Host Interface**: Installed in Slot 2 (`$C200`) or Slot 4 (`$C400`), exposing `YM_REG` (select register, `$Cs20`) and `YM_DATA` (data write / status read, `$Cs21`).
* **Clock & Timers**: Native YM2151 clock runs at 3.579545 MHz, synchronized with Apple II CPU cycles during `UpdateSound()`.

## 3. Emulation Core Selection
* **Chosen Core**: MAME / `ymfm` over Nuked-OPM.
* **Reasoning**: Nuked-OPM offers gate-level simulation but is too heavy, causing stuttering in JS/TypeScript; MAME/`ymfm` uses optimized algorithms ensuring high performance, 60 FPS, and easy Wasm/TS portability.

## 4. Implementation Checklist (all phases done)
* **Phase 1 (Slot I/O)** — DONE: `$C2xx`/`$C4xx` handlers map to `ymfm::ym2151`; status reads reflect the BUSY window (`VERAYM::ymfm_set_busy_end` / `ymfm_is_busy`, driven by `m_clockCount` in `Generate()`). A guest must poll `$Cs21` bit 7.
* **Phase 2 (Mixing)** — DONE: `Generate()` advances 64 clocks per native sample (~55,930 Hz); `VERAYM::Render()` linearly resamples to 44.1 kHz and mixes into the VERA DS voice buffer. The final mix follows the X16 Developer Board: `(PSG + PCM) * 2 + YM`. Host-side FM switch (`FM Card Enabled`, per-slot registry) falls `$Cs20`/`$Cs21` through to regular VERA registers when off.
* **Phase 3 (Portability)** — DONE for now: the C++ wrapper (`source/VERACard/VERAYM.h/.cpp`) is isolated around `source/ymfm/` (BSD-3-Clause) for a future Wasm/TS port.
* **Verification (added)** — `fmtest.po` (ProDOS disk from `veramusic`) plays a full-scale FM tone; `-fm-wav-dump` captures the first 20 s of the mixed buffer (PSG/PCM ×2 + FM) to `VERA-fm.wav`; headless `-run-cycles` must be ≥ ~8M cycles (guest key-on at ~6.2M). The Apple //e `zsmstream` player was verified with TITLE.ZSM and BOTHPSG.ZSM (PSG + FM, clean loops); its FM melody now matches the x16emu reference after the retrigger clocking fix.
