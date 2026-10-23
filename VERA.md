# VERA Card (AppleWin expansion card) documentation

This document describes the port of the **Commander X16 VERA (Versatile
Embedded Retro Adapter)** chip into an **AppleWin slot expansion card** —
its specification, how it integrates with AppleWin, and how to use it. The
core is a C++ port of `apple2ts`'s TypeScript implementation
(`src/worker/devices/vera/`).

> For the development architecture, build workflow and debugging notes,
> see `AGENTS.md`.

> **Note:** This fork is synced with AppleWin upstream **master** (post
> v1.32.1.0: PNG saved images via spng, VS2026 projects, CONTRIBUTING AI
> policy), merged 2026-10-22.
>
> **Development Model:** The VERA and FM expansion card features in this fork
> were architected, designed, and verified by human contributors, with implementation
> assistance from AI tools in compliance with upstream's [AI Tool Use Policy](CONTRIBUTING.md#ai-tool-use-policy).

---

## 1. Overview

- The VERA card can be installed in **Slot 2 or Slot 4**.
- It provides the full VERA display core (640×480 VGA / NTSC scan, 2 layers,
  128 sprites, 256-colour palette, FX registers) and audio core
  (16-channel PSG + PCM FIFO).
- It provides one IRQ line.
- Source code is in `source\VERACard\`:
  - `VERAVideo.h/.cpp` — display core (port of `video.ts`)
  - `VERAAudio.h/.cpp` — audio core (PSG + PCM)
  - `VERACard.h/.cpp` — slot-card integration, IO, Update, audio buffer, save-state

### Implementation status

- Full VERA audio is done (16-channel PSG + PCM implemented).
- The FM daughterboard (Yamaha YM2151) is implemented and verified
  end-to-end (2026-10-16) — see §5 "FM daughterboard".
- The SD card SPI (SD/MMC) is implemented — see §4 "SD card SPI".
- Guest music playback (veramusic's Apple //e zsmstream player) is verified:
  TITLE.ZSM and BOTHPSG.ZSM play and loop cleanly, with both PSG and FM
  audible. See `AGENTS.md` → Testing → "zsmstream player — TITLE.ZSM noise
  + loop DISK ERR: RESOLVED".

---

## 2. Integration with AppleWin

### 2.1 IO addressing

The VERA registers live in the **Cx region** of the selected slot:
`$Cs00–$CsFF`, where `addr & 0xff` selects the register. Registered via
`RegisterIoHandler(slot, IO_Null, IO_Null, &IOReadCx, &IOWriteCx, ...)`
(see `VERACard::InitializeIO`).

### 2.2 Display (dual-screen design)

Following the user's design intent, this implementation uses a "two-in-one"
display: **the Apple II native screen is shown normally; the display only
switches to the VERA screen when the VERA software enables VERA video
output.**

- When the VERA card is present, the Video framebuffer is resized to
  **680×516** (borderless **640×480**, border 20 wide / 18 tall).
- `VERACard::Update()` calls `UpdateDisplay()` only while
  `IsVideoOutputEnabled()` is true, copying the VERA 640×480 framebuffer into
  the AppleWin framebuffer; otherwise the Apple II (NTSC) screen is kept.
- `VideoRefreshBuffer` skips the NTSC redraw when `IsVidVERAActive()`.
- The framebuffer is **bottom-up** (row 0 = bottom); both VERA and NTSC map
  with `(framebufferHeight - 1) - y - borderHeight`.

### 2.3 IRQ

`eIRQSRC` gains `IS_VERA`; `VERACard::Update()` calls
`CpuIrqAssert(IS_VERA)` / `CpuIrqDeassert(IS_VERA)` based on
`m_video.GetIRQOut()`.

### 2.4 Audio

- Self-contained ring buffer (`DSGetSoundBuffer` / `DSGetLock`);
  `kDSBufferByteSize = 44100 / 60 * 3 * 2 * kNumChannels` (~3 frames of
  stereo audio ≈ 50 ms) with a **1/2-buffer lead** on init.
- Sample rate 44100 Hz, stereo. `UpdateSound()` is driven by the **DS play
  cursor**, not the emulated CPU clock: it generates exactly the samples the
  hardware consumed (fractional accumulator), so the write rate tracks
  playback with zero drift.
- **Mixer balance**: `mixed = (PSG + PCM) * 2 + YM`, clamped to int16 — the
  X16 Developer Board doubles the VERA bus relative to the YM2151 (x16emu
  `src/audio.c`). A full-volume PSG voice is only 1/8 of full scale, so a 1:1
  sum buries the PSG under the FM.

---

## 3. Display specification

| Item | Value |
|---|---|
| framebuffer (VERA) | 640 × 480 (RGBA) |
| scanlines / frame | 525 (VGA) |
| pixel clock per line | 800 (`VGA_SCAN_WIDTH`), PIXEL_FREQ = 25 MHz |
| full frame period | 525 × 800 / 25 = **16800** |
| layers | 2 |
| sprites | 128 |
| palette | 256 colours (8-bit RGB each) |
| VRAM | 128 KB (0x00000–0x1FFFF), including PSG / palette / sprite data areas |

### Display timing (AppleWin integration)

`VERACard::Update()` is called about **16 times / frame** (each a ~1000-cycle
batch). The current approach advances VERA **cycle-count driven** (mirrors
apple2ts's `syncVera()`): `VERACard::SyncVideo()` advances
`m_video.Step(cpuMhz, delta)` by the `g_nCumulativeCycles` delta since the last
sync, and `IOReadCx`/`IOWriteCx` call `SyncVideo()` **before every VERA
register access** so the scanline register (`$08`) is live at the exact current
cycle. `g_nCumulativeCycles` is only updated at CPU batch boundaries, so the IO
handlers first call `CpuCalcCycles(nExecutedCycles)` to make it accurate at the
exact read/write cycle — otherwise the port's scanline probe sees the batched
value and the deltas come out uneven (`64/32` instead of `32/32`):

```cpp
void VERACard::SyncVideo()
{
    const uint64_t now = g_nCumulativeCycles;
    if (m_lastVideoCycles == 0) m_lastVideoCycles = now;
    const uint64_t delta = now - m_lastVideoCycles;
    m_lastVideoCycles = now;
    if (delta == 0) return;
    // VERA's 25 MHz pixel clock is independent of the 6502: convert the
    // CPU-cycle delta with the actual (speed-adjusted) CPU clock so VSYNC
    // stays ~59.5 Hz at any Emulation Speed.
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

> A once-per-full-frame advance was tried and reverted: it freezes the scanline
> inside a frame, and the SMB1 port's VERA-alive probe reads the scanline 3×
> and requires it to change smoothly — a frozen scanline fails that probe and
> the port reports `NO VERA`. Cycle-count-driven stepping keeps the scanline
> live and is what makes the probe pass.
>
> The `cpuMhz` argument must come from `g_fCurrentCLK6502`, not a fixed 1 MHz.
> This machine runs the //e at 3 MHz, where a fixed 1 MHz made VERA scan three
> times too fast (VSYNC 181 Hz). With the real clock it stays at ~59.5 Hz.

---

## 4. IO register map

VERA registers are selected by `addr & 0x1F` (`VERAVideo::Write/Read`).

| reg | function |
|---|---|
| 0x00–0x02 | data/address pointer (ADDRL/ADDRM/ADDRH + INCR) |
| 0x03, 0x04 | VRAM data read/write (DATA0 / DATA1) |
| 0x05 | control (`DCSEL`, `ADDRSEL`, bit7 = software reset) |
| 0x06 | `IEN` (IRQ enable) |
| 0x07 | `ISR` (IRQ status clear) |
| 0x08 | `IRQL` (IRQ compare line) |
| 0x09–0x0C | Composer registers (selected by `DCSEL`) |
| 0x0D–0x13 | Layer 0 registers (7 bytes) |
| 0x14–0x1A | Layer 1 registers (7 bytes) |
| 0x1B–0x1D | PCM (0x1B control, 0x1C rate, 0x1D FIFO write) |
| 0x1E–0x1F | SD card SPI: 0x1E = SD_DATA, 0x1F = SD_STATUS |

### Composer (DCSEL=0, offset into `m_reg_composer`)

| offset | function (per `render_line` code) |
|---|---|
| 0 | DC_VIDEO: bits 0–1 = out_mode (0=disabled, nonzero=enabled), bit 3 = interlace, bits 4/5/6 = Layer0/1/sprite enable |
| 1 | horizontal scale (scale) |
| 2 | vertical scale (used for eff_y accumulation) |
| 3 | border colour (border_color) |
| 4 | hstart (<<2) |
| 5 | hstop (<<2) |
| 6 | vstart (<<1) |
| 7 | vstop (<<1) |
| 8+ | chroma hscale |
| 9+ | FX registers: DCSEL 2–6 (address/increment/pixel position/cache, etc.) |

### Layer registers (7 bytes per layer)

| byte | description |
|---|---|
| 0 | settings: color_depth (bits 0–1), bitmap_mode (bit 2), text_256c (bit 3), mapw (bits 4–5), maph (bits 6–7) |
| 1 | map_base (<<9) |
| 2 | tile_base (bits 0–5, <<9), tile width/height (bits 0–1) |
| 3 | HSCROLL low 8 bits |
| 4 | HSCROLL high 4 bits (`& 0xF`) |
| 5 | VSCROLL low 8 bits |
| 6 | VSCROLL high 4 bits (`& 0xF`) |

### SD card SPI

`VERASD.h/.cpp` implements an SD/MMC **SPI state machine** (port of apple2ts
`src/worker/devices/vera/sdcard.ts`). The backing store is a raw
512-byte-block image opened with real stdio (`fopen("r+b")` / `fseek` /
`fread` / `fwrite`).

**Registers** (slot region, `addr & 0x1F`; slot 2 → `$C21E`/`$C21F`):

| reg | name | function |
|---|---|---|
| 0x1E | SD_DATA | write sends a byte (starts a transfer); read returns the received byte; with autotx enabled a read auto-sends `$FF` |
| 0x1F | SD_STATUS | bit 0 = SD_SEL (card selected / CS), bit 2 = SD_AUTOTX, bit 7 = SD_BUSY (a byte transfer is in progress) |

**SPI timing:** `SPI_CLOCK_RATE_MHZ = 12`. `SpiStep(clocks)` adds `clocks * 12`
to a busy counter, and a byte completes once it reaches `>= 10` (~1 CPU cycle
per byte in emulation). `VERACard::SyncSPI()` advances the SPI by the cumulative
cycle delta before every VERA register access (`IOReadCx`/`IOWriteCx`) as well
as in `Update()`, so transfers complete within 1–2 CPU cycles. Guest programs must
still **poll SD_STATUS bit 7** to wait for transfer completion.

**Commands** (SPI mode): CMD0, CMD8, CMD9 (SEND_CSD), ACMD41 (CMD55+CMD41),
CMD12, CMD13, CMD16, CMD17, CMD18, CMD24 (write), CMD55, CMD58 (READ_OCR).
- **CMD58 (READ_OCR)**: Returns 5 bytes according to SD SPI specification:
  `[R1, 0xC0, 0xFF, 0x80, 0x00]` with R1 indicating card status (`0x01` idle, `0x00` active).
- **CMD24 (WRITE_BLOCK)**: Receives start token (`0xFE`), 512-byte payload, and
  CRC16, responding with Data Response Token `0x05` (accepted) or `0x0D` (write error / write protection).
  A guest (e.g. `verasdedit`) reads this token to detect write-protection: `0x0D`
  means the image is read-only (`OpenFile` fell back to `rb` when `r+b` failed,
  so `WriteBlock`'s `fwrite` fails), or the SD write-protect checkbox is checked
  (`VERASD::WriteBlock` returns false when `m_write_protected` is set), and the
  guest reports the SD as write-protected.
- **State reset**: `ResetSpiState()` clears SPI shift buffers, command phase,
  and transfer state upon card unmount or image replacement.

**Mount / GUI:** `Configuration -> Slots` → select VERA → "Configure..." opens
the `IDD_VERA_SD_CARD` "VERA SD Card" dialog (`PageSlots.cpp`) with
"Select Image..." / "Unmount" and a **"Write Protected"** checkbox (forces
CMD24 → `0x0D`, persisted per-slot via `REGVALUE_VERA_SD_WRITEPROTECT`).
The dialog does **not** require the VERA card to
be installed yet: the selected path is stored in
`CConfigNeedingRestart::m_VERASDImagePath[slot]`, so you can pick VERA + SD
image in one go and restart. `ApplyConfigAfterClose()` then mounts it via
`VERACard::SetSDImagePath()` after the card is (re)inserted. `SetSDImagePath()`
mounts the image and persists the path to the registry (per-slot section,
`REGVALUE_VERA_SD_IMAGE` = "SD Card Image"); `UnmountSD()` clears it. The
constructor restores the persisted image via `GetSDImagePathFromRegistry()`.
Changing only the SD image applies immediately (no forced restart).
`VERACard::Update()` also runs a one-time `TestSDRead()` self-test (reads
LBA 2048 and logs the FAT32 boot signature to `VERA.log`). Full SPI protocol
verification is covered in `test\VERATest\VERATest.cpp` (Test 7).

---

## 5. Audio specification

### PSG

- 16 channels, clocked at `PSG_CLOCK = 25000000 / 512`.
- Per channel: frequency, volume, left/right, pulse width, waveform
  (PWM / saw / noise).
- Write address `0x1F9C0–0x1F9FF` (PSG area).

### PCM

- 4096-byte FIFO, supports looping.
- Control / rate / FIFO write via `$9F3B–$9F3D` (reg 0x1B–0x1D).
- Supports 16-bit linear, stereo mixing.

### FM daughterboard (YM2151)

- The A2VERA board (prototype hardware "EV1" release) adds a Yamaha YM2151
  FM chip addressed through the VERA Cx region: `$Cs20` = `YM_REG` (register
  select), `$Cs21` =
  `YM_DATA` (data write / status read), mirroring the X16's `$9F40`/`$9F41`.
- Emulated by `source/VERACard/VERAYM.h/.cpp` wrapping the `ymfm::ym2151`
  core (`source/ymfm/`, BSD-3-Clause): the socket can take a genuine YM2151
  or the OPM2151 (a modern FPGA drop-in replacement for the vintage YM2151)
  — the emulated chip is the YM2151 either way. Native ~55,930 Hz
  output (chip clock
  3,579,545 / 64) is linearly resampled to 44.1 kHz and mixed into the VERA
  DS voice buffer.
- **Mixer balance**: the VERA bus (PSG + PCM) is doubled and the YM2151 added
  at 1× — `mixed = (PSG + PCM) * 2 + YM`, clamped to int16 (see §2.4).
- **ZSM retrigger**: a `$08` write with operator mask 0 (`(val & 0x78) == 0`)
  clocks one native YM sample in `VERAYM::WriteData` (`ClockKeyOff()`), so an
  immediately following key-on starts a fresh attack. ymfm only samples the
  key state once per generated sample; without this, a key-off + key-on in the
  same tick cancels and every note after the first decays.
- The status register reflects the chip's BUSY window (bit 7 set for ~32 chip
  clocks after each data write); a guest program must **poll `$Cs21` bit 7**.
- Enable/disable per slot (`FM Card Enabled` in the VERA configuration
  dialog, persisted in the per-slot registry). When disabled, `$Cs20`/`$Cs21`
  fall through to the regular VERA register handling.
- Verified end-to-end (2026-10-16): a guest program writing the standard
  init + key-on sequence produces a full-scale FM tone. Automated
  verification: `-fm-wav-dump` captures the first 20 s of the mixed VERA
  buffer (PSG + PCM + FM sum, stereo 16-bit @ 44100) to `VERA-fm.wav` next
  to the exe; opt-in, nothing is written without the flag. With headless
  `-run-cycles` use ≥ ~8M cycles (the test disk's key-on is at ~6.2M cycles):
  ```
  AppleWin.exe -s2 vera -s6 diskii -d1 fmtest.po -power-on -log -run-cycles 12000000 -fm-wav-dump
  ```
  (`fmtest.po` is not committed in `veramusic`; build it first with
  `node veramusic/tools/build_fmtest_po.mjs`.)
- Guest-side music playback uses `C:\dev\veramusic`'s Apple //e ZSM
  stream player (`zsmstream.asm`, PSG + FM + PCM via the daughterboard),
  shipped as the 94-song `melodius.hdv` jukebox:
  `AppleWin.exe -s2 vera -s7 hdc -h1 C:\dev\veramusic\melodius.hdv -power-on`.
  Verified with TITLE.ZSM and BOTHPSG.ZSM (PSG + FM, clean loop seeks) and
  with the non-60 Hz files: the player normalizes a ZSM header tick rate to
  60 Hz output frames (zsmkit semantics), so Headbanger! at 240 ticks/s
  plays at tempo. The debugging history and residual limitations are in
  `AGENTS.md` → Testing → "zsmstream player — TITLE.ZSM noise + loop DISK
  ERR: RESOLVED".

---

## 6. Using VERA in AppleWin (command line)

The VERA card is selected by slot on the command line:

```
AppleWin.exe -s2 vera
```

Or in slot 4:

```
AppleWin.exe -s4 vera
```

### With a Slot 7 hard disk (e.g. TimePilot)

```powershell
Release\AppleWin.exe -s2 vera -s7 hdc -h1 "C:\dev\Time-Pilot\TimePilot-IIvera\TimePilot-IIvera.hdv"
```

- `-s7 hdc`: install a hard disk controller in Slot 7.
- `-h1 <image>`: Slot 7 hard disk image #1 (`-h2` for #2).
- On startup the Apple II screen is shown (DOS/ProDOS); when software enables
  VERA it switches to the VERA screen.

### Automated Testing and Verification

For automated scripts or advanced testing, AppleWin supports running for a specified number of cycles and then auto-exiting to export state:

```powershell
Release\AppleWin.exe -s2 vera -run-cycles 5000000 -vera-dump "dump.bin" -save-state "state.aws"
```

- `-run-cycles <N>`: Run the emulator for `N` CPU cycles, then auto-exit.
- `-vera-dump <file>`: On auto-exit, dump the entire 128KB VERA VRAM to a binary file.
- `-save-state <file>`: On auto-exit, automatically save the AppleWin snapshot state.

### GUI

Select the VERA card (`CT_VERA`) in the "Slot" settings page dropdown.

### Mount an SD image (SD card SPI)

Open `Configuration -> Slots`, select VERA (even if the slot was previously
empty) and press "Configure..." for the VERA card. The "VERA SD Card" dialog
lets you **Select Image...** (a raw 512-byte-block image, e.g. a FAT32 `.img`)
or **Unmount**. The selected image is mounted as the VERA SD card and the path
is persisted to the registry (per-slot), so it is restored on the next launch.
You can select the VERA card and its SD image in one session and restart once —
both take effect together.

---

## 7. Build and run

```powershell
$msbuild = "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe"
& $msbuild "AppleWin-VS2022.sln" /p:Configuration=Release /p:Platform=Win32 /v:normal /nologo
```

> Note: build via the **solution** (not the individual `.vcxproj`) so the
> `TestCPU6502` dependency is ordered correctly. Single-threaded is more
> reliable (`/m` can produce "Build FAILED" with 0 errors); if `AppleWin.exe`
> is still running the link fails with LNK1104 — stop it first.

---

## 8. Save state

- `SaveSnapshot` saves the 128 KB VRAM and all VERA IO/Composer/Layer registers.
- `LoadSnapshot` calls `Reset(false)` first, then restores the VRAM and registers.
- Snapshot unit version `kUNIT_VERSION = 1`.

---

## 9. Known limitations

- VERA time is derived from CPU cycles converted with the configured
  emulation speed (`g_fCurrentCLK6502`). VSYNC stays ~59.5 Hz real-time at
  1 MHz and 3 MHz, but changing the emulation speed mid-run shifts the scan
  phase.
- Between CPU batches (`Update()` is called every ~1 ms), video time advances
  in one step. Any guest access to a VERA register resyncs first, so the
  scanline probe sees live values; true cycle-by-cycle midline raster effects
  are still approximated.
