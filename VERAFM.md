# A2VERA FM Audio (Yamaha YM2151) Emulation — Implementation Record

> **Status: COMPLETE (2026-10-16).** The FM chain is implemented, verified
> end-to-end and audible (full-scale ~440 Hz tone from the `veramusic`
> `fmtest.po` disk, both real-time and headless runs).
> Technical details and debugging history live in `AGENTS.md`
> (`### FM daughterboard (VERAYM)` and `### FM WAV dump (-fm-wav-dump)`).

## 1. Project Context & Goal
* **Target Platforms**: AppleWin (C++) and apple2ts.com (TypeScript).
* **Objective**: Add cycle-accurate support for the A2VERA FM Audio daughterboard (Yamaha YM2151) running at ~1.023 MHz.
* **Hardware note**: the A2VERA board (prototype hardware "EV1" release) carries a Yamaha YM2151 (OPM interface) — the socket ships with the YM2151-compatible **OPM2151** drop-in replacement and can also take a genuine YM2151. The emulated chip is the YM2151 either way.

## 2. Hardware Architecture & Register Mapping
* **Host Interface**: Installed in Slot 2 (`$C200`) or Slot 4 (`$C400`), exposing `YM_REG` (select register, `$Cs20`) and `YM_DATA` (data write / status read, `$Cs21`).
* **Clock & Timers**: Native YM2151 clock runs at 3.579545 MHz, synchronized with Apple II CPU cycles during `UpdateSound()`.

## 3. Emulation Core Selection
* **Chosen Core**: MAME / `ymfm` over Nuked-OPM.
* **Reasoning**: Nuked-OPM offers gate-level simulation but is too heavy, causing stuttering in JS/TypeScript; MAME/`ymfm` uses optimized algorithms ensuring high performance, 60 FPS, and easy Wasm/TS portability.

## 4. Implementation Checklist (all phases done)
* **Phase 1 (Slot I/O)** — DONE: `$C2xx`/`$C4xx` handlers map to `ymfm::ym2151`; status reads reflect the BUSY window (`VERAYM::ymfm_set_busy_end` / `ymfm_is_busy`, driven by `m_clockCount` in `Generate()`). A guest must poll `$Cs21` bit 7.
* **Phase 2 (Mixing)** — DONE: `Generate()` advances 64 clocks per native sample (~55,930 Hz); `VERAYM::Render()` linearly resamples to 44.1 kHz and mixes into the VERA DS voice buffer. Host-side FM switch (`FM Card Enabled`, per-slot registry) falls `$Cs20`/`$Cs21` through to regular VERA registers when off.
* **Phase 3 (Portability)** — DONE for now: the C++ wrapper (`source/VERACard/VERAYM.h/.cpp`) is isolated around `source/ymfm/` (BSD-3-Clause) for a future Wasm/TS port.
* **Verification (added)** — `fmtest.po` (ProDOS disk from `veramusic`) plays a full-scale FM tone; `-fm-wav-dump` captures the first 20 s of the mixed buffer to `VERA-fm.wav`; headless `-run-cycles` must be ≥ ~8M cycles (guest key-on at ~6.2M).
