# A2VERA FM Audio (YM2151/OPM2151) Emulation Implementation Plan

## 1. Project Context & Goal
* **Target Platforms**: AppleWin (C++) and apple2ts.com (TypeScript).
* **Objective**: Add cycle-accurate support for the A2VERA FM Audio daughterboard (Yamaha YM2151 / OPM2151) running at ~1.023 MHz.

## 2. Hardware Architecture & Register Mapping
* **Host Interface**: Installed in Slot 2 (\(C200) or Slot 4 (\)C400), exposing `YM_REG` (select register) and `YM_DATA` (read/write data/status).
* **Clock & Timers**: Native YM2151 clock runs at 3.579545 MHz, synchronized with Apple II CPU cycles during `UpdateSound()`.

## 3. Emulation Core Selection
* **Chosen Core**: MAME / `ymfm` over Nuked-OPM.
* **Reasoning**: Nuked-OPM offers gate-level simulation but is too heavy, causing stuttering in JS/TypeScript; MAME/`ymfm` uses optimized algorithms ensuring high performance, 60 FPS, and easy Wasm/TS portability.

## 4. Implementation Checklist for the AI Assistant
* **Phase 1 (Slot I/O)**: Hook `$C2xx` / `$C4xx` handlers and map to `ymfm::ym2151`, handling status reads to prevent lockups.
* **Phase 2 (Mixing)**: Drive clock stepping in `UpdateSound()`, mix stereo output into the DirectSound ring buffer.
* **Phase 3 (Portability)**: Keep the C++ wrapper isolated for future WebAssembly or TypeScript adaptation.



