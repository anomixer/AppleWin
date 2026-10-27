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

### Address Mapping Comparison (Legacy vs Current Hardware Mapping)

| Property | Legacy Implementation | Correct Hardware Specification |
| :--- | :--- | :--- |
| **Design Basis** | Assumed register offset from Commander X16 (`VERA + $20`) | **Michael Morrison KansasFest 2026 Talk (28:36) & EV1 Prototype Card** |
| **Bus Select Signal** | **/IO SELECT** (Pin 1, 256 bytes per slot) | **/DEVICE SELECT** (Pin 41, 16 bytes per slot) |
| **Generic Formula** | `$Cs20` / `$Cs21` ($s$ = slot number) | **`$C0(8+s)0` / `$C0(8+s)1`** ($s$ = slot number) |
| **Slot 2 - `YM_SEL` (Register Select)** | `$C220` (`VERA_BASE + $20`) | **`$C0A0`** (`$C080 + 2*$10 + 0`) |
| **Slot 2 - `YM_DATA` (Data / Status)** | `$C221` (`VERA_BASE + $21`) | **`$C0A1`** (`$C080 + 2*$10 + 1`) |
| **Slot 4 - `YM_SEL` (Register Select)** | `$C420` (`VERA_BASE + $20`) | **`$C0C0`** (`$C080 + 4*$10 + 0`) |
| **Slot 4 - `YM_DATA` (Data / Status)** | `$C421` (`VERA_BASE + $21`) | **`$C0C1`** (`$C080 + 4*$10 + 1`) |
| **VERA Video Module Base** | `$Cs00`~`$Cs1F` (`$C200` / `$C400`) | **`$Cs00`~`$Cs1F` (`$C200` / `$C400`)** *(Unchanged)* |
| **Register Naming** | `YM_REG` / `YM_DATA` | **`YM_SEL` / `YM_DATA`** |

* **Hardware Finding (KansasFest 2026, min 28:36)**:
  Michael Morrison's talk confirms that the physical A2VERA board (EV1) maps:
  - **VERA**: `/IO SELECT` (`$Cs00` - `$CsFF`, e.g. Slot 2 `$C200`, Slot 4 `$C400`).
  - **OPM (YM2151)**: `/DEVICE SELECT` (`$C0(8+s)0` - `$C0(8+s)F`):
    - **Slot 2**: **`$C0A0`** = `YM_SEL` (Register Select), **`$C0A1`** = `YM_DATA` (Data Write / Status Read)
    - **Slot 4**: **`$C0C0`** = `YM_SEL` (Register Select), **`$C0C1`** = `YM_DATA` (Data Write / Status Read)
* **Migration**: Previous emulator versions temporarily intercepted `$Cs20` / `$Cs21` (`$C220` / `$C420`). AppleWin handlers now migrate directly to `$C0A0` / `$C0C0` (`$C0A1` / `$C0C1`).
* **Clock & Timers**: Native YM2151 clock is 3.579545 MHz. `UpdateSound()` generates audio from the DirectSound play cursor; BUSY advances when native samples are generated (including the key-off latch sample).

## 3. Emulation Core Selection
* **Chosen Core**: MAME / `ymfm` over Nuked-OPM.
* **Reasoning**: Nuked-OPM offers gate-level simulation but is too heavy, causing stuttering in JS/TypeScript; MAME/`ymfm` uses optimized algorithms ensuring high performance, 60 FPS, and easy Wasm/TS portability.

## 4. Implementation Checklist (all phases done)
* **Phase 1 (Slot I/O)** — DONE (2026-10-27, AI-assisted): `/DEVICE SELECT` C0 handlers map to `ymfm::ym2151` (Slot 2 `$C0A0/$C0A1`, Slot 4 `$C0C0/$C0C1`); status reads reflect the BUSY window (`VERAYM::ymfm_set_busy_end` / `ymfm_is_busy`, driven by `m_clockCount` in `Generate()`). A guest must poll `YM_DATA` bit 7.
* **Phase 2 (Mixing)** — DONE: `Generate()` advances 64 clocks per native sample (~55,930 Hz); `VERAYM::Render()` linearly resamples to 44.1 kHz and mixes into the VERA DS voice buffer. The final mix follows the X16 Developer Board: `(PSG + PCM) * 2 + YM`. Host-side FM switch (`FM Card Enabled`, per-slot registry) uses empty-slot I/O (`IO_Null`) on the FM ports when off.
* **Phase 3 (Portability)** — DONE for now: the C++ wrapper (`source/VERACard/VERAYM.h/.cpp`) is isolated around `source/ymfm/` (BSD-3-Clause) for a future Wasm/TS port.
* **Verification (added)** — `fmtest.po` (ProDOS disk from `veramusic`) plays a full-scale FM tone; `-fm-wav-dump` captures the first 20 s of the mixed buffer (PSG/PCM ×2 + FM) to `VERA-fm.wav`; headless `-run-cycles` must be ≥ ~8M cycles (guest key-on at ~6.2M). The Apple //e `zsmstream` player was verified with TITLE.ZSM and BOTHPSG.ZSM (PSG + FM, clean loops); its FM melody now matches the x16emu reference after the retrigger clocking fix.

## 5. I/O migration (2026-10-27)

AppleWin now registers separate C0 FM handlers and Cx VERA handlers. Only
C0 offsets 0/1 access YM2151; unused offsets and disabled FM use `IO_Null`.
The legacy Cx FM interception is removed, so `$Cs20/$Cs21` follow normal
VERA register mirroring. VERA video, SD, audio timing, and mixing are unchanged.
This migration was implemented with AI assistance. The playback evidence
above predates the migration: guest programs and disks using the legacy
addresses must be rebuilt for the new mapping before repeating those tests.

## 6. New-mapping verification (2026-10-27)

Both Release solution builds (Win32 and x64) succeed. The VERA core smoke
suite now passes 26/26 after the reset regressions in section 7. An isolated
handler harness checked both slot decoders,
BUSY status, unused ports, disabled FM, legacy VERA aliases and slot isolation.

`C:\dev\veratest` now builds both slots with the new FM ports. Its BASIC
launchers and all FM players share a bounded BUSY transition probe rather
than the obsolete Cx address-latch signature. `veratest.po`, `slideshow.hdv`
and `slideshow.hdv.zip` were rebuilt; all 31,147 music ticks remain unchanged.
The Win32 runtime matrix passes 26/26: all seven demos in both slots with FM,
plus menus, sprite/sound, Sonic and slideshow without FM. Save-state flags,
VRAM contents and nonzero WAV captures were checked. Sonic used 50M cycles
for its longer floppy load. Slideshow QA used temporary autoplay images with
CANYON first to exercise FM immediately; the delivered image retains SB-INTRO
first (its FM enters around 114 seconds). Physical hardware, listening and
interactive controls still need human verification.

Additional x64 runtime smoke tests passed 2/2: Slot 2 BASIC menu/FM detection
and Slot 4 slideshow FM playback (nonzero WAV capture).

## 7. Display reset follow-up (2026-10-27)

The vertical stripes reported in `veratest/err.png` exposed missing raster
cache initialization in `VERAVideo::Reset`. Reset now clears all layer,
sprite and compositor scanline buffers and rebuilds derived properties.
The reset regression tests verify all 307,200 pixels after previously
rendering Layer 1 or a sprite; the core suite passes 26/26. Both Release
builds succeed. This display fix was AI-assisted and does not change FM I/O.
The user's 24-line BASIC ABOUT page was preserved and `veratest.po` rebuilt
(maximum 39 characters; no final newline on the key prompt).

Both slots passed sprite/sound, Sonic and slideshow
checks in RGB mode with full scanlines (8/8 runtime cases, including FM-off
slideshow cases). Core reset regressions pass 26/26.
