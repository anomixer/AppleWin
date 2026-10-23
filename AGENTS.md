# AGENTS.md

This file is a working guide for coding agents and contributors working in this
repository. It captures the architectural facts, build workflow and the
hard-won debugging lessons from the VERA expansion-card port so future work
doesn't re-trace the same mistakes.

## What this repository is

This is **AppleWin** — the Apple II emulator for Windows — with a
**Commander X16 VERA** expansion-card port added in the `source\VERACard`
directory. The VERA card can be installed into **slot 2 or slot 4** and
provides a 640×480 VGA/NTSC graphics core, a 16-channel PSG + PCM audio core,
and an IRQ line. The core is a C++ port of `apple2ts`'s TypeScript VERA
(`src/worker/devices/vera/video.ts`, `pcm.ts`, `vera-psg.js`).

> **Note:** This fork is synced with upstream AppleWin **master** (post
> v1.32.1.0: PNG saved images via spng, VS2026 projects, CONTRIBUTING AI
> policy). Merged 2026-10-22 into `feat/verafm`.
>
> **Development Model:** The VERA and FM expansion card features in this repository
> are architected, designed, and verified by human contributors, with implementation
> and maintenance assisted by AI tools in accordance with upstream's
> [AI Tool Use Policy](CONTRIBUTING.md#ai-tool-use-policy).

## Build workflow

MSBuild (Visual Studio 2022) is used from the command line. **Always build via
the solution** (`AppleWin-VS2022.sln`), not the individual `.vcxproj` — the
solution resolves the `TestCPU6502` dependency ordering for both Win32 and x64.

```powershell
$msbuild = "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe"
# Win32 (x86) Release  ->  Release\AppleWin.exe
& $msbuild "AppleWin-VS2022.sln" /p:Configuration=Release /p:Platform=Win32 /v:minimal /nologo
# x64 Release         ->  x64\Release\AppleWin-x64.exe
& $msbuild "AppleWin-VS2022.sln" /p:Configuration=Release /p:Platform=x64   /v:minimal /nologo
```

The project already defines both platforms (`Release|Win32` and `Release|x64`);
the x64 configuration sets `TargetName = AppleWin-x64`, so the two builds
produce `Release\AppleWin.exe` and `x64\Release\AppleWin-x64.exe` without
colliding. Debug builds are likewise `Debug\AppleWin.exe` and
`x64\Debug\AppleWin-x64.exe`.

Gotchas:

- **Build single-threaded.** The `/m` (parallel) flag has produced transient
  "Build FAILED" with **0 errors** — run without `/m` for reliable results.
- **Build via the solution, not the vcxproj.** Building `AppleWin-VS2022.vcxproj`
  alone for x64 fails: its `PreBuildEvent` runs `$(OutDir)TestCPU6502.exe`,
  which isn't built yet for the x64 platform (the x64 `OutDir` is
  `x64\Release\`, and `TestCPU6502.exe` only exists there once the solution
  builds it first). The solution handles the ordering.
- **LNK1104 / locked exe.** If `Release\AppleWin.exe` (or `Debug\...`) is
  still running, the link fails with `LNK1104: cannot open ... AppleWin.exe`.
  Kill it first:
  ```powershell
  Get-Process AppleWin -ErrorAction SilentlyContinue | Stop-Process -Force
  ```
- The project has a `PreBuildEvent` that runs `$(OutDir)TestCPU6502.exe`
  (a CPU unit test). If that returns non-zero the build fails **silently**
  (0 errors). Run it directly to diagnose.
- Windows `min`/`max` macros clash with `std::min`/`std::max` (error C2589);
  use the ternary operator instead.
- The `VERAAudio.cpp` / `VERAVideo.cpp` files use `<PrecompiledHeader>NotUsing</PrecompiledHeader>`.

## Execution-loop timing model (critical)

The single most important thing to understand before touching `VERACard::Update`.

- `ContinueExecution()` (`source\Windows\AppleWin.cpp`) runs once per
  **1 ms batch** (`nExecutionPeriodUsec = 1000`), so `fExecutionPeriodClks`
  ≈ 1000 CPU cycles per batch.
- Each batch: `CpuExecute(uCyclesToExecute)` then
  `GetCardMgr().Update(uActualCyclesExecuted)`.
- `uActualCyclesExecuted` is the **per-batch** executed-cycle count (~1000),
  **not cumulative**. There are ~16 batches per Apple display frame.
- Therefore `VERACard::Update` is called **~16 times per frame**, each with
  `nExecutedCycles` ≈ 1000. **Do not drive the VERA frame from the per-batch
  count directly** — that renders only a fraction of a frame per batch.

The correct video-timing approach (current implementation) advances VERA by the
CPU cycles executed, **cycle-count driven**, mirroring apple2ts's
`syncVera()`/`video_step(1, cycleDelta)`. `VERACard::Update` calls
`SyncVideo()`, which advances `m_video.Step(cpuMhz, delta)` by the
`g_nCumulativeCycles` delta since the last sync (incremented by `CpuExecute`,
see `source\CPU.cpp`), and `IOReadCx`/`IOWriteCx` call `SyncVideo()` **before
every VERA register access** so the scanline register (`$08`) reflects the exact
current cycle.
`g_nCumulativeCycles` is only updated at CPU batch boundaries, so the IO
handlers first call `CpuCalcCycles(nExecutedCycles)` to make it accurate at the
exact read/write cycle — otherwise the port's scanline probe sees the batched
value and the deltas come out uneven (`64/32` instead of `32/32`).

**`cpuMhz` must be the real CPU clock, not a hard-coded 1.0:**
`const double cpuMhz = g_fCurrentCLK6502 / 1000000.0;`. `VERAVideo::Step` uses
it to convert CPU cycles into VERA pixel time. With a fixed 1 MHz on a 3 MHz
//e configuration, VSYNC fired ~3× too fast (181 Hz instead of ~59.5 Hz) and
the zsmstream IRQ ran away. Deriving it from `g_fCurrentCLK6502` keeps VERA at
~59.5 Hz real time at any emulation speed.

> Why not advance one full VERA frame per Apple frame? A once-per-frame advance
> freezes the scanline inside a frame, and the SMB1 port's VERA-alive probe
> reads the scanline 3× (with delays) and requires it to *change smoothly*
> (`|(s3-s2)-(s2-s1)| < $11`). A frozen scanline fails that probe and the port
> reports `NO VERA`. Cycle-count-driven stepping keeps the scanline live, which
> is what makes the probe pass. See `VERACard::SyncVideo`.

## VERA card architecture

Files in `source\VERACard\`:

- `VERACard.h/.cpp` — slot-card glue: IO handlers, `Update()`, audio buffer
  management, save-state, SD-image registry persistence.
- `VERAVideo.h/.cpp` — the video core: `Step()` (scanline advance),
  `render_line()`, layers, sprites, palette, FX registers, IRQ.
- `VERAAudio.h/.cpp` — PSG (16 ch) + PCM FIFO mixer, `Render(buf, nSamples)`.
- `VERAYM.h/.cpp` — FM daughterboard wrapper around the `ymfm::ym2151` core
  (`source\ymfm\`, BSD-3-Clause): emulates the **Yamaha YM2151** (the OPM
  interface chip on the A2VERA board; the socket can take a genuine YM2151
  or the OPM2151 FPGA replacement). Host registers are
  `$Cs20` = `YM_REG` (register select) and `$Cs21` = `YM_DATA` (data write,
  read = status), mirroring the X16's `$9F40`/`$9F41`. Native ~55,930 Hz
  output (`kYMClock` = 3,579,545 / 64) is resampled to 44.1 kHz by
  `VERAYM::Render()` and mixed into the VERA DS voice in `UpdateSound()`.
- `VERASD.h/.cpp` — SD/MMC SPI protocol emulation (port of apple2ts
  `src/worker/devices/vera/sdcard.ts`), speaking through VERA `$9F3E/$9F3F`.
  Uses real stdio for the backing raw 512-byte-block image.

Key integration points:

- **IO region**: registers live in the Cx region `$Cs00–$CsFF` for the slot's
  bank; `addr & 0xff` selects the register. Registered via
  `RegisterIoHandler(slot, IO_Null, IO_Null, &IOReadCx, &IOWriteCx, ...)`.
  The FM branch in `IOReadCx`/`IOWriteCx` intercepts `addr & 0xff` `$20`/`$21`
  *before* the video-register path, and only when `m_bFMEnabled` (default on;
  per-slot `REGVALUE_VERA_FM_ENABLED`, GUI checkbox in the VERA Configure
  dialog).
- **IRQ**: `eIRQSRC` has an `IS_VERA` value (`source\Common.h`); asserted via
  `CpuIrqAssert(IS_VERA)` / `CpuIrqDeassert(IS_VERA)`.
- **CardManager**: single `m_pVERACard` instance, `GetVERACard()` accessor,
  and a slot drop-down entry (`CT_VERA` in `source\Card.h`).
- **CmdLine**: `-s<slot> vera` selects the card; hard disk image args are
  `-h1/-h2` for slot 7 (`source\CmdLine.cpp`).

### FM daughterboard (VERAYM)

`VERAYM` implements the `ymfm_interface` hooks so the status register reflects
the chip's BUSY window:

- `ymfm_set_busy_end(clocks)` sets `m_busyEnd = m_clockCount + clocks`; ymfm
  requests `32 * clock_prescale()` after each data write.
- `ymfm_is_busy()` returns `m_clockCount < m_busyEnd`. **`m_clockCount` only
  advances in `Generate()`** (+64 per native sample), which runs from
  `VERAYM::Render()` inside `UpdateSound()`. A guest that polls `$Cs21` bit 7
  therefore only sees BUSY clear while audio is being generated.
- `VERAYM::Render(buf, numSamples)` resamples with linear interpolation
  (`m_resamplePhase`, `m_prevL/R`, `m_curL/R`) at `ratio = sampleRate / 44100`.
- **Key-off must be clocked before an immediate key-on.** ymfm samples the key
  state once per generated native sample (`clock_keystate` only starts an
  attack on a 0→1 change of `m_key_state`). ZSM retriggers a channel with
  `$08=ch` (key-off) immediately followed by `$08=$78|ch` (key-on) inside one
  tick; with no sample in between, the two writes cancel and every note after
  the first decays instead of re-attacking. `VERAYM::WriteData` therefore
  tracks `m_regSelect` and calls `ClockKeyOff()` (one native sample) when
  `$08` is written with `(val & 0x78) == 0`. Verified against the x16emu
  TITLE.ZSM reference (all groups peak 0.13–0.26 instead of the 0.08 drum
  floor).

> **CONFIRMED WORKING (2026-10-16): the FM chain plays sound end-to-end.**
> The user heard the tone with a real-time run of the `veramusic` `fmtest.po`
> disk. The chain fmtest → `$Cs20/$Cs21` writes → ymfm core → DirectSound
> voice is verified. The whole chain plays a full-scale (~440 Hz) FM tone.
>
> Verification evidence (headless + real-time runs):
> - Real-time run (12 s, normal speed, no `-run-cycles`) of `-s2 vera -s6
>   diskii -d1 C:\dev\veramusic\fmtest.po -power-on -log`: the log shows the
>   complete 68-write guest sequence, `nativeNonzero=73234`, max L/R = 32640
>   (near full scale), and the emulator's audio output contains a
>   full-scale ~440 Hz FM tone (1.29–2.07 s of the dump).
> - Headless auto-exit run (`-run-cycles N`) reproduces the same audio in
>   the `-fm-wav-dump` capture — but **`N` must be ≥ ~8M cycles**: the guest
>   key-on happens at ~6.2M CPU cycles, so `-run-cycles 6000000` exits
>   before the beep and captures silence (all-zero dump).
>
> Historical debugging record (2026-10-16, `veramusic` fmtest.po):
> - `fmtest.po` is a **ProDOS 2.4.3 disk** (280 x 512-byte blocks), built by
>   `veramusic/tools/build_fmtest_po.mjs` (the `.po` itself is a local
>   artifact — it is not committed in `veramusic`, so build it before
>   reproducing these runs). The player files
>   (`FMTEST.BIN`/`FMTEST4.BIN`) are added as type BIN (0x06) with
>   **aux = 0x2000** and raw program bytes (no DOS 3.3 4-byte addr/len header).
>   `fmtest_startup.bas` runs them via ProDOS BASIC.SYSTEM's
>   `PRINT CHR$(4);"BRUN FMTEST.BIN"`. The earlier "BRUN misreads the raw
>   file as addr=$D878 / len=169" hypothesis is **eliminated**: the loaded code
>   executes coherently at $2000 and its `$20D5` ADC table reads back
>   `{0,8,16,24}` correctly.
> - Guest write sequence (intended, from `fmtest.asm`): init
>   `R8 D120, R8 D0, R32 D199` (`$C7`: ch0 CON=7), then per operator N=0..3
>   six writes `($40+$8N, $01) ($60+$8N, $00) ($80+$8N, $1F) ($A0+$8N, $00)
>   ($C0+$8N, $00) ($E0+$8N, $07)`, then play `R40 D74, R48 D0, R8 D120` ×2,
>   keyoff `R8 D0`. The executing log matches this exactly (line for line).
> - **Register anomaly — ROOT CAUSE FOUND + FIXED (guest bug, not emulator)**:
>   pre-fix the five non-MUL register values came out either **immediate + 48**
>   (`$90/$B0/$D0/$F0/$10`) or bare (`$60/$80/$A0/$C0/$E0`), randomly per
>   write, and the table offset (+8N) vanished. Root cause is
>   `fmtest.asm`'s `WAIT_BUSY` helper ($2112) — two bugs: (1) it clobbers X
>   (`LDX #$10` with only A saved via PHA/PLA), and (2) its `BMI` is
>   reversed: bit 7 **set** (BUSY) exits immediately, bit 7 clear spins 16×.
>   So the TL write's `LDA #$60; CLC; ADC OP_OFF,X` inherits X from the
>   previous `WAIT_BUSY` call: X=16 → the ADC reads `$20D5+16 = $20E5`,
>   whose RAM value is `$30` (the immediate byte of PLAY_NOTE's `LDA #$30`)
>   → reg = imm+$30 (+48, offset lost); X=0 → reads `$20D5+0 = $00` → reg =
>   bare immediate. Fix (applied + verified): save/restore X around the poll
>   and keep spinning **while bit 7 is set** (DEX timeout):
>   `PHA / TXA / PHA / LDX #$10 / loop: LDA YM_DATA / BMI busy / exit;
>   DEX / BNE loop / exit: PLA / TAX / PLA / RTS`.
> - VERA.log **appends across runs** (several runs' lines coexist in one
>   file, distinguished by `VERAYM(this=XXXX)` ids; a run's FM section
>   restarts at `write YM_REG=8 (writes=1, ...)`) — always filter by the
>   latest restart marker before concluding.
> - Headless runs of this disk **require** both `-s6 diskii` (default slot 6
>   has no Disk II → no boot) and `-log` (without it VERA.log grows zero
>   lines): `-s2 vera -s6 diskii -d1 fmtest.po -power-on -log -run-cycles N`.
> - Host environment: this machine exposes only VB-Audio Virtual Cable
>   endpoints; DirectSound is silent there while waveOut works. Delivery plan
>   chosen with the user: capture FM output to a WAV file to listen directly
>   (`-fm-wav-dump`, see **Testing** below).

### FM WAV dump (`-fm-wav-dump`)

- `-fm-wav-dump` (new cmd-line flag, `CmdLine.h` `fmWavDump`) makes
  `VERACard::UpdateSound()` dump the first **20 s** of the mixed VERA buffer
  (PSG + PCM + FM sum, stereo 16-bit @ 44100) to `VERA-fm.wav` next to the
  exe. Opt-in only: without the flag nothing is written, so a previous
  capture is not clobbered.
- The 44-byte WAV header placeholder is laid out so the two patched fields
  (`RIFF` size at offset 4 and the `data`-chunk size at offset 40) align:
  offsets 36–39 hold the literal `data` chunk id (`'d','a','t','a'`), offsets
  40–43 hold the data-size placeholder. **Gotchas learned the hard way:**
  the initial header version had offsets 36–39 zeroed — players can't find a
  `data` chunk and refuse to play (the dump looked "silent" even though the
  audio was real), and the average-byte-rate field (offset 28–31) was
  written as `373504` (2× the true 176400). Both are fixed; a WAV whose
  header is broken can still be repaired in place by patching those bytes.
- `-run-cycles` interplay: the dump captures only audio produced before
  auto-exit. For fmtest's two beeps use `-run-cycles 12000000` or larger.
  Note the guest's `WAIT_2S` busy-wait loops are CPU-count driven, so under
  headless auto-exit the beep timeline is compressed relative to real time
  (beep at ~1.3 s in the capture instead of ~5 s); a real-time run shows the
  natural timing.



### SD card (VERA SPI SD)

`VERASD.h/.cpp` is an SD/MMC **SPI state machine** (port of apple2ts
`src/worker/devices/vera/sdcard.ts`). The backing store is a raw
512-byte-block image opened with real stdio (`fopen("r+b")` / `fseek` /
`fread` / `fwrite`).

- **Registers** (slot region, `addr & 0x1F`; slot 2 → `$C21E`/`$C21F`):
  - `$Cs1E` (reg 0x1E) — SD_DATA: write sends a byte (starts a transfer),
    read returns the received byte; with autotx enabled a read auto-sends `$FF`.
  - `$Cs1F` (reg 0x1F) — SD_STATUS: bit 0 = SD_SEL (card selected / CS),
    bit 2 = SD_AUTOTX, bit 7 = SD_BUSY (a byte transfer is in progress).
- **SPI timing**: `SPI_CLOCK_RATE_MHZ = 12`; `SpiStep(clocks)` adds
  `clocks * 12` to a busy counter, and a byte completes once it reaches `>= 10`
  (~1 CPU cycle per byte in emulation). `VERACard::SyncSPI()` advances the SPI
  by the `g_nCumulativeCycles` delta since the last advance, and is called from
  **every** VERA register access (`IOReadCx`/`IOWriteCx`) as well as from
  `Update()`, so a byte completes within ~1–2 CPU cycles of the guest writing
  it. A guest program must still **poll SD_STATUS bit 7**, not use a fixed
  delay, to wait for a transfer.
  > The SPI was originally advanced only at `Update()` batch boundaries
  > (~1000 cycles each), which made every byte take a whole batch: a bulk read
  > was ~1000× too slow, and a guest's bounded 256-spin BUSY poll could expire
  > before the byte completed and read a stale byte — intermittent corruption
  > (a horizontal break at a random row) in SD-SLIDES / SD-HIRES. Advancing per
  > register access fixes both.
- **Commands** (SPI mode): CMD0, CMD8, CMD9 (SEND_CSD), ACMD41 (CMD55+CMD41),
  CMD12, CMD13, CMD16, CMD17, CMD18, CMD24 (write), CMD25 (multi-block
  write), CMD55, CMD58 (READ_OCR).
  - **CMD58 (READ_OCR)**: Returns 5 bytes according to SD SPI specification:
    `[R1, 0xC0, 0xFF, 0x80, 0x00]` where the initial byte is R1 (`0x01` if idle,
    `0x00` if card initialized).
  - **CMD24 (WRITE_BLOCK)**: After receiving data packet (`0xFE` + 512 bytes +
    2-byte CRC), responds with standard Data Response Token: `0x05` for accepted
    write, or `0x0D` for rejected write (e.g. out-of-range LBA, read-only image,
    or the SD write-protect checkbox is checked).
  - **CMD25 (WRITE_MULTIPLE_BLOCK)**: Each `$FC` + 512-byte packet writes the
    current LBA and advances it; `$FD` ends the run. CMD25 buffers host writes
    across its blocks and calls `fflush()` once at `$FD`. CMD24 still flushes
    immediately. Do not restore an `fflush()` per CMD25 block: it eliminates the
    purpose of multi-block writes and makes FAT32 formatting dramatically slower.
  - **State reset**: `ResetSpiState()` resets all transmission buffers, counters,
    and flags (`m_selected`, `m_busy`, `m_rxbuf_idx`, `m_response_length`,
    `m_ongoing_multiblock_read`, etc.) upon `Reset()`, `Unmount()`, or attaching
    a new image in `OpenFile()`.
- **Mount / GUI**: `Configuration -> Slots` → select VERA → "Configure..."
  opens the `IDD_VERA_SD_CARD` "VERA SD Card" dialog (`PageSlots.cpp`) with
  "Select Image..." / "Unmount" and a **"Write Protected"** checkbox. Checking
  it forces CMD24 to return `0x0D` (write rejected) regardless of the image's
  actual write permission, so write-protection is testable from the GUI. The
  flag is persisted per-slot (`REGVALUE_VERA_SD_WRITEPROTECT` = "SD Card Write
  Protected") and applied via `VERACard::SetSDWriteProtected()`; `WriteBlock`
  returns false when set. The dialog does **not** require the VERA card
  to be installed yet — the selected path is stored in
  `CConfigNeedingRestart::m_VERASDImagePath[slot]` (not the live card), so you
  can pick VERA + SD image in one go and restart: `ApplyConfigAfterClose()`
  mounts it via `VERACard::SetSDImagePath()` after the card is (re)inserted.
  `SetSDImagePath()` mounts the image and persists the path to the registry
  (per-slot section, `REGVALUE_VERA_SD_IMAGE` = "SD Card Image"); `UnmountSD()`
  clears it. The constructor restores the persisted image via
  `GetSDImagePathFromRegistry()`. The path is **not** part of the restart-detect
  `operator==`, so changing only the SD image applies immediately without a
  restart (mirrors the harddisk pattern).
  > Note: `ApplyNewConfigForRestart` empties the slot before re-inserting it and
  > `RegSetConfigSlotNewCardType` deletes the whole slot registry section, so
  > storing the SD path only in the slot section (the old approach) made it
  > disappear on restart — hence the config-based storage above.
- **Self-test & Unit test**: `VERACard::Update()` runs `TestSDRead()` once after
  an image is mounted (reads LBA 2048 through the SPI and logs the FAT32 boot
  signature to `VERA.log`). In addition, `test\VERATest\VERATest.cpp` contains
  Test 7 covering the full SD SPI state machine: CMD0, CMD8, CMD58 (idle/active),
  ACMD41, CMD24 write tokens (`0x05`/`0x0D`), and block verification.

### Display / framebuffer model

- When the VERA card is present, the Video framebuffer is resized to
  **680×516** (borderless **640×480**, border **20** wide / **18** tall).
  See `Video::GetFrameBufferBorderless*/Border*` in `source\Video.cpp`.
- The framebuffer is **bottom-up** (row 0 = bottom) — NTSC and VERA both map
  with `(framebufferHeight - 1) - y - borderHeight`.
- The VERA 640×480 RGBA framebuffer is copied into the AppleWin framebuffer
  by `VERACard::UpdateDisplay()` **only while `IsVideoOutputEnabled()`** is
  true; otherwise the Apple II (NTSC) screen is shown. This implements the
  "dual-screen" behaviour the user requested: normally Apple II, switch to
  VERA only when the software enables VERA video output.
- `VideoRefreshBuffer` skips NTSC redraw when `IsVidVERAActive()`.
- **RGBA packing** is done so the little-endian memory layout is R,G,B,A
  (byte 0 = R). Do not reverse this.

### NTSC centring (Apple II image "upper left" fix)

When VERA forces a 640×480 borderless area, the NTSC Apple II image (560×384)
must be centred within it. `NTSC_VideoInit` (`source\NTSC.cpp`) positions each
scan line with two offsets:

- **Vertical** `centringOffset = (borderlessHeight - 384)/2 - GetFrameBufferCentringOffsetY()`
  — centres the image in the taller area, and subtracts VidHD's own vertical
  shift (which `GetFrameBufferCentringValue()` applies when VidHD is present) so
  the two mechanisms don't stack.
- **Horizontal** `hCentring = (borderlessWidth > 560 && !HasVidHD()) ? (borderlessWidth - 560)/2 : 0`
  — centres the image horizontally. This is needed **only when VERA is present
  without VidHD**, because the existing VidHD centring (`GetFrameBufferCentringValue`)
  already adds the horizontal offset when VidHD is installed.

Net result: the Apple II image is correctly centred for normal Apple II,
IIgs, only-VERA, and VERA+VidHD combinations.

### Audio model

- VERA is a **self-contained, independent card** — it does **not** borrow
  Mockingboard/SSI263's audio model (no fill-level feedback loop, no
  `SoundCore_ValidateAndAlignWriteOffset`). Audio is written to its own DS
  voice buffer via `DSGetSoundBuffer` / `DSGetLock` / `DSZeroVoiceBuffer`
  (`source\SoundCore.cpp`).
- `kDSBufferByteSize = 44100 / 60 * 3 * 2 * kNumChannels` (~3 frames of stereo
  audio ≈ 50 ms) with a **1/2-buffer lead** established on init, so the play
  cursor never catches up (no underrun). (A 6-frame buffer was tried to help
  the 14 s glitch but was reverted — the play-cursor approach is what actually
  fixed the glitch, and the larger buffer was suspected in a VERA-dependent
  crash.)
- Sample rate 44100 Hz, stereo. `UpdateSound()` is driven by the **DS play
  cursor**, not the emulated CPU clock: it generates exactly as many samples
  as the hardware consumed since the last update (a fractional accumulator
  `m_sampleAccum += bytesPlayed / (sizeof(short)*kNumChannels)` carries the
  remainder), so the audio is synchronised to real-time playback — zero drift,
  zero periodic glitch. (Generating from `g_nCumulativeCycles` drifted ~0.4%
  from the DS clock and caused a glitch every ~14 s.)
- **X16 Developer Board mix: `mixed = (PSG + PCM) * 2 + YM`.** `UpdateSound`
  doubles the VERA bus (`m_mixBuffer`) before adding the YM2151
  (`m_ymBuffer`), then clamps to int16. This mirrors x16emu `src/audio.c`
  (`((psg + pcm) << 14) >> 13` + `ym >> 15`). A full-volume PSG voice is only
  1/8 of full scale, so a 1:1 sum buries the PSG under the FM — the user
  described it as 「FM 很大聲，PSG 小貓叫」. The ×2 belongs at the mixer
  (`VERACard::UpdateSound`), not in `VERAAudio` (the PSG core itself is
  register- and formula-accurate). Confirmed by listening on 2026-10-22.

## Debugging lessons (ordered)

1. **vector subscript out of range** — a ULONG cycle-delta underflow made the
   VERA scan position negative → negative framebuffer index. Fixed by clamping
   in `render_line` + guarding framebuffer writes.
2. **Image vertically flipped / top residue** — `UpdateDisplay` originally
   wrote top-down at (0,0). Fixed by reversing Y (bottom-up) + border offset.
3. **Screen frozen** — `Step` used `if` (only one scanline per call) instead of
   `while`; changed to `while` for both VGA and NTSC paths.
4. **No sound** — `InitAudio()` wasn't called; made it lazy-init in
   `UpdateSound()`.
5. **Way too slow / barely advancing** — using a running difference of the
   per-batch count made `cycleDelta ≈ 0`. Fixed by using the batch count
   directly (later replaced by the cycle-count-driven approach).
6. **16× too fast / scrolling** — forcing a full 16800-cycle frame **per batch**
   advanced ~16 frames per Apple frame; and per-batch advance caused drift.
   Intermediate fix was one full VERA frame per Apple display frame via
   `g_nCumulativeCycles` frame-boundary detection; that was later superseded
   by the cycle-count-driven stepping described above (the per-frame approach
   freezes the scanline and fails the SMB1 VERA-alive probe).
7. **Choppy audio** — buffer was only 1 frame; raised to 3 frames.
8. **Blue refresh interference / blanking** — only call `UpdateDisplay()` /
   `ClearFrameBuffer()` when `IsVideoOutputEnabled()`.
9. **Text-mode blue scrolling up** — scan position drifted across frames;
   solved by keeping the scan position live with cycle-count-driven stepping
   (an intermediate per-frame full-scan approach also worked but froze the
   scanline).
10. **Apple II text offset up** — NTSC image anchored at top of a 480-tall
    borderless area; solved by the centring offset above.
11. **Apple II image off-centre (bottom, left)** — vertical was shifted down by
    VidHD's `GetFrameBufferCentringValue()` stacking on the VERA centring, and
    horizontal centring only applied when VidHD was present. Fixed in
    `NTSC_VideoInit` by subtracting `GetFrameBufferCentringOffsetY()` from the
    vertical offset and adding a horizontal centring offset only for the
    only-VERA case (`!HasVidHD()`).
12. **Sandy/scratchy audio — root cause.** The VERA ring buffer is
    only ~3 frames (8820 bytes ≈ 50 ms), but the SSI263/Mockingboard fill-level
    feedback loop (steering `m_numSamplesError` via `SoundCore_GetErrorInc()`,
    ±20 per batch) is tuned for a **65536-byte** buffer. On the tiny VERA buffer
    that ±20 correction makes the audio core generate samples faster/slower than
    real time → pitch warble that sounds like static and covers the music. The
    integer truncation (`(int)(44100/nIrqFreq)` = 43) vs the true rate (43.11)
    also makes the buffer drain and underrun every ~20 s.
    **FINAL fix (current):** no feedback loop at all. Use an exact **fractional
    sample accumulator** (`m_sampleAccum += updateInterval * kSampleRate /
    g_fCurrentCLK6502`, then take the integer part) so the write rate equals the
    play rate exactly — zero drift, zero under/overflow. Establish a **~1/3-buffer
    lead** on init so the play cursor never catches up. **Gotchas:** the
    first-call sentinel must be `(uint32_t)-1`, not `== 0` (0 is a valid ring
    position — the cumulative offset returns to exactly 0 ~every 50 ms); and
    `nBytesRemaining` must be wrapped if it's ever computed.
13. **PSG noise clocked 16× too fast** — `renderSample` advanced the shared
    noise LFSR once per *channel* (16× per sample) instead of once per sample,
    producing a very fast high-frequency hiss. Move the LFSR advance above the
    channel loop so it ticks once per output sample (the noise generator is
    clocked at the PSG/sample rate).
14. **Random "flash crash" after minutes — VERA-installation dependent.**
    The diagnostic that isolates it: the crash happens whenever the VERA card is
    installed *and any app is running*, including a **non-VERA** app (e.g.
    Mockingboard music). When VERA is removed, no crash (10+ min). This means
    the crash is in code that runs whenever VERA is installed — `Update()`,
    `UpdateSound()` or `VERAVideo::Step()` — **not** in the VERA display/audio
    *app* path (`UpdateDisplay` only runs when video output is enabled, so it
    is NOT the culprit for the non-VERA-app crash).
    **Critical:** an access violation (0xC0000005) is an **SEH** exception and is
    **NOT caught** by `WinMain`'s C++ `try/catch (std::exception)` — the process
    simply terminates. To locate it, add a
    `SetUnhandledExceptionFilter` handler that logs the exception code+address
    (`source\Windows\AppleWin.cpp`); `LogFileOutput`'s log file is unbuffered
    (`_IONBF`), so the line is flushed before termination. Note `LogInit()` only
    runs with `-log`, so `AppleWin.log` often does not exist — use
    `LogWriteVERALog()` which appends to `VERA.log` next to the exe.
15. **Music stuck / lingering audio (餘音) after a stall** — when the emulator
    stalls (a video/CPU hang), `UpdateSound` is not called and the DS buffer
    keeps looping the last audio, leaving a lingering tone even after the audio
    resumes. A heartbeat log (every ~600 `Update()` calls: `VERA alive: update N`,
    plus a `VERA audio ok` marker after `UpdateSound`) reveals the stall: the
    heartbeat stops at the last `VERA alive` (no `audio ok` after it) → the hang
    is in the video `Step()`; if it stops after `VERA audio ok`, it is in
    `UpdateSound`. **Fix:** track the real-time ms between `UpdateSound` calls
    (`GetTickCount64`); if the gap exceeds ~500 ms, `DSZeroVoiceBuffer` the voice
    and re-establish a fresh lead so no stale audio survives.

## Testing

- `test\VERATest\VERATest.cpp` — console smoke test of the video/audio cores
  (13/13 checks).
- Interactive smoke test (headless): launch the Release exe with the TimePilot
  disk image, confirm the process stays alive ~15 s with steady CPU, then kill:
  ```powershell
  $exe = "C:\dev\AppleWin\Release\AppleWin.exe"
  $p = Start-Process $exe -ArgumentList '-s2','vera','-s7','hdc','-h1','C:\dev\Time-Pilot\TimePilot-IIvera\TimePilot-IIvera.hdv' -PassThru
  Start-Sleep 15; Get-Process -Id $p.Id
  Stop-Process -Id $p.Id -Force
  ```
- Automated verification (headless): run the emulator for a fixed number of CPU cycles and auto-exit to dump VERA VRAM and save state. Useful for CI/CD or testing script automation.
  ```powershell
  & $exe -s2 vera -run-cycles 5000000 -vera-dump "dump.bin" -save-state "state.aws"
  ```
- FM audio verification (headless): run the `veramusic` fmtest disk and capture the FM
  output. `-fm-wav-dump` writes `VERA-fm.wav` (first 20 s of the mixed VERA buffer,
  stereo 16-bit @ 44100) next to the exe; opt-in, without the flag nothing is written.
  **`-run-cycles` must be ≥ ~8M** (fmtest key-on is at ~6.2M CPU cycles):
  ```powershell
  & $exe -s2 vera -s6 diskii -d1 C:\dev\veramusic\fmtest.po -power-on -log -run-cycles 12000000 -fm-wav-dump
  ```
  Validate the capture with Python (`import wave; wave.open(...)`): a playable
  capture shows full-scale ±32704 peaks during the guest's beeps; all-zero means
  either the cycles limit cut the run before key-on or the WAV header is broken
  (see the FM WAV dump section above). For a real-time check, run the same command
  line without `-run-cycles`, sleep ~12 s, then kill the process.
- **Bash quoting gotcha (paths in `-ArgumentList`-style runs):** when launching
  the exe from Git-Bash, double quotes eat `\d`/`\v`/`\z` escapes —
  `-h1 "C:\dev\veramusic\z.hdv"` arrives as `C:devveramusicz.hdv`,
  `DoHardDiskInsert` returns `res=0` and the GUI warns "Failed to insert
  harddisk(s)". Use single quotes or forward slashes (`'C:/dev/...'`).
  `AppleWin.log` (`Init: DoHardDiskInsert(HDD-0), res=…`) is the first thing to
  check when a headless music run is silent: `res=0` means no HD mounted, no
  ProDOS boot, the menu never appears — nothing else in the chain runs.
- **pc-hook addresses drift with every player rebuild.** The `TMPFB`/`TMPEXIT`
  pc probes in `VERACard.cpp` (`regs.pc == 0x…`) are byte offsets inside
  `ZSMSTREAM*.BIN`; after reassembling the player they land on wrong
  instructions and the dump silently never fires. Verify the hooked pc against
  the current BIN before concluding "the hook found nothing": locate the
  target instruction (e.g. `STA $C220` / the exit-path `STA DEBUG_MARKER`
  stores) with a small script scanning `8d 20 c2` / `8d e1 33` in
  `src/ZSMSTREAM*.BIN`. **AppleWin delivers IO-write pc as `STA`-address+3**
  (pc already advanced past the 3-byte `STA abs`), so hook the pc AFTER the
  store, not the store itself. Current mapping (2026-10-19): `TMPFB` at
  `0x2C0A` (pc shown during `STA $C220` at `0x2C07`), exit-path at `0x2AAC`
  (pc during `STA DEBUG_MARKER` at `0x2AA9` in DISK_FAIL), zp vars
  `ERR=$33DE`, `ERRCODE=$33DF`, `DONE=$33E0`, `DEBUG_MARKER=$33E1`,
  `IRQ_ACTIVE=$33EA`, `STREAM_END=$33EB`, `EOF_FLAG=$33EC`;
  `SETMARK_PARAMS` at `$3459` (count) / `$345A` (ref) / `$345B..$345D` (24-bit
  position) / `$345E` (REFNUM), `ERRSRC=$34A4`, stream zp `STREAM=$06/$07`.
  **Provenance caveat (2026-10-20): the `VERAZP`/`TMPFB` probe code lives
  ONLY in the deployed debug-capture build** — the strings appear in
  `Release\AppleWin.exe`, `Release\AppleWin.pdb`,
  `AppleWin\Release\VERACard.obj` and the captured `Release\VERA.log`
  (406 `VERAZP` lines, sample: `VERAZP ERR=$20 ERRCODE=$41 DONE=$50 …
  STREAM=$419B`), but **not in any committed source** (`git log --all -S
  VERAZP` is empty). The mapping above describes that deployed build.
  **Update 2026-10-22:** the TITLE issue is resolved, the probes were removed
  from `VERACard.cpp`, and the current autoplay build (`ZSMSTREAMA.BIN`,
  end $37D9) uses: `ERR=$370C`, `ERRCODE=$370D`, `DONE_FLAG=$370E`,
  `DEBUG_MARKER=$370F`, `IRQ_ACTIVE=$3718`, `STREAM_END=$3719`,
  `EOF_FLAG=$371A`, `TCNT1=$371C`, `SETMARK_PARAMS=$3787` (count 2, ref,
  24-bit position), `REF_NUM=$378C`, `ERRSRC=$37D2`; stream zp `STREAM=$06/$07`.
- **zsmstream player — TITLE.ZSM noise + loop DISK ERR: RESOLVED (2026-10-22).**
  The veramusic `zsmstream.asm` Apple //e player (`C:\dev\veramusic`) streams
  ZSM from a ProDOS HDV via MLI and publishes decoded frames into a double
  frame buffer (FB_A=$7300 / FB_B=$7500). Two independent player bugs caused
  the reported symptoms (slow motion + heavy noise, and intermittent
  「disk err - stream block xxx」); both are fixed and verified headless against
  the X16 `melodius` ground truth:
  1. **PSG flood → slow motion + noise.** `TICK4` did `STA TCNT1` then
     `BEQ TICK_YM`; `STA` does not set flags, so the stale Z from
     `CMP #$FF` was tested. A frame with psgCnt=0 fell through, `DEC TCNT1`
     underflowed to $FF and the IRQ wrote 256 bogus PSG pairs (addresses ran
     past the PSG window into palette/sprite VRAM). Fix: `CMP #$00` before
     the branch. The PSG walk is also 16-bit now (a frame can exceed 255 B):
     `FB_RD_L/H = FB_ACT`, `LDA (FB_RD_L),Y`. The earlier "TICK_PSG walks the
     raw stream buffer" diagnosis was this flood's symptom, not a separate
     bug.
  2. **EOD loop-seek DISK ERR $04.** `SETMARK_PARAMS` was `!byte 4,...`;
     ProDOS MLI SET_MARK takes a parameter count of **2** (refnum + 24-bit
     position). With 4 the MLI returns $04 (bad reference number) at the
     first EOD, `DISK_FAIL` prints "DISK ERR - STREAM BLOCK UNREADABLE $04"
     and closes the stream. Fix: `!byte 2,...` (matches `pcmstream.asm`).
  3. **FM melody: first chord plays, every later note is silent/decaying
     (YM2151 retrigger lost).** ZSM retriggers a channel with `$08=ch`
     (key-off, operator mask 0) immediately followed by `$08=$78|ch`
     (key-on) inside one tick. ymfm samples the key state once per generated
     sample (`clock_keystate` only calls `start_attack()` on a 0→1 change of
     `m_key_state`), and the players applied a whole tick's writes with no
     sample generated in between, so key-off+key-on cancelled: the envelope
     kept decaying and each new note started quieter (or not at all). The
     first note of each channel works because it starts from key state 0.
     Fix: after a full key-off write to `$08`, clock exactly one native YM
     sample so the release latches before the key-on arrives —
     `melodius.cpp` (`ym_clock_keyoff()` in `apply_frame`) and AppleWin
     `VERAYM::WriteData` (`m_regSelect == 0x08 && (val & 0x78) == 0` →
     `ClockKeyOff()`), both tracking the selected register.
     Verified: `tools/melodius.exe TITLE.ZSM --wav` vs the x16emu truth
     (aligned, 55 s): 0 loud-windows missing (was ~74% unaligned / audible
     collapse); `ch0` solo envelope per note 0.074→0.078→0.087 (was
     0.074→0.020→0.005). AppleWin `-fm-wav-dump` group peaks now
     0.13–0.26 for all groups (old dump stayed at the drum floor 0.08).
     x16emu uses the same ymfm but clocks the chip between CPU writes, which
     is why the X16 reference never had the bug.
  Verification (Enhanced //e at 3 MHz — see the timing note below):
  - TITLE: first pass plays normally (6052 real PSG pairs after tick 1024; 897
    host ADDR_L writes observed for ticks 1024–1433 vs 900 expected), then
    loops at ~171M cycles. State at 195M: FRM=3765, DEBUG_MARKER=$12, ERR=0,
    ERRCODE=0, REF_NUM=1, EOF_FLAG=0; the 512-byte stream buffer matches
    TITLE byte offset 0x2688 (loop point 0x888 + 15 blocks) with STREAM
    pointing at 0x27D8.
  - BOTHPSG: loops 3.28× to offset 0x13 (a valid command boundary
    mid-frame), PSG writes continue to the end, buffer matches offset 0x13.
  - The menu disk (`zsmstream.hdv`) boots and shows the song list; the
    autoplay disk (`zsmstream_a.hdv`, AUTOSONG=1) is the TITLE test disk.
  - **Follow-up 2026-10-22 (PCM):** the player now decodes the ZSM `$40` EXTCMD
    PCM sub-events and streams samples through the VERA FIFO, so the emulator's
    PCM path finally gets traffic. `VERAAudio::renderPcmSample` was verified
    against the x16-validated reference renderer and needed **no change**: the
    FIFO byte rate is `rate * 48828.125/128` (≈ `rate * 381.5` Hz), format
    `ctrl>>4 & 3` = mono8 / stereo8 / mono16 / stereo16 with **signed** 8-bit
    (`(int8)b << 8`), and `s_pcmVolumeLut[ctrl & 0x0f] / 64` gain — all identical
    to `veramusic/tools/melodius.cpp`. The verification signal is the count and
    distribution of `$C21D` writes in `VERA.log`: Crockett streams 357k FIFO
    bytes in 20 s (~17.8 kB/s, matching rate 64) with a signed-audio histogram
    (mean ≈ 0, peaks at `$00`/`$FF`), TREE 508k bytes. Silence there means the
    player never triggered, not that the emulator dropped it.
  - The veramusic disks were consolidated into one jukebox image: `zsmstream.hdv`
    / `zsmstream_a.hdv` are gone, replaced by `C:\dev\veramusic\melodius.hdv`
    (94 songs, two-level menu) and the temp autoplay image
    `%TEMP%\melodius_autoplay.hdv` built by
    `node tools/build_melodius.mjs --autoplay <songIdx>` (`--list` prints the
    indices). PCM samples live in companion `<name>.PCM` files on the same
    volume because ProDOS 8 refuses a second OPEN on an open file ($50).
  - The temporary TICK/PSG/IRQ probes and the `pc=` field in the generic IO
    log were removed from `VERACard.cpp`; only the real-time VSYNC fix
    remains. All `zsmstream*.hdv` images were rebuilt from the fixed source.
  - **PCM streaming is a CPU-budget problem, not an emulator bug (2026-10-22,
    TREE.ZSM).** TREE is one 1.47 MB mono-8 instrument at rate 73, i.e. the
    VERA FIFO must be fed 27,865 B/s. Each FIFO byte costs the guest
    `LDA ctrl / BMI / LDA (ptr),Y / STA $C21D / INY / CPY / BNE` ≈ 22 cycles,
    so the feed alone needs ~613 k cycles/s = 60 % of a 1.02 MHz //e, and with
    the tick decode + MLI window fills + status drawing on top the player only
    manages ~26 kB/s — the FIFO sits at 0-400 bytes and every missing byte
    becomes one silent sample, which is what "all noise" sounded like. At the
    user's 3 MHz setting the same code feeds 27,843 B/s with the FIFO pinned at
    3817 and zero underruns. Diagnose it with a temporary counter probe
    (`pushed` / `popped` / `underrun` / `fifo` / `rate` / `clk` logged once per
    44100 generated samples in `UpdateSound`): `fifo` hovering near 0 with
    `pushed < rate*381.47` is feed starvation; `fifo` full means the emulator
    is at fault. **`-clock-multiplier <n>` (CmdLine.cpp) is how to force the
    clock headlessly** — a plain headless run uses the registry speed, which on
    this machine measured 1,020,484 Hz, not the interactive 3 MHz.
  - **Measurement gotcha: `-log` throttles the emulation.** `IOWriteCx` logs
    every non-DATA0 write, and each line is an `fopen`/`fwrite`/`fclose`; at
    27.9 k FIFO bytes/s that is 27,900 log opens per second, which pushes the
    run below real time and makes a *feed-rate* measurement taken under `-log`
    meaningless. The bulk stream ports are now excluded from that log
    (`(addr & 0x1f) != 0x03` for DATA0, `!= 0x1d` for the PCM FIFO data port).
    `Log.cpp` only writes VERA.log when `-log` is set, so interactive runs were
    never affected.
  - Reference for the alignment test: `tools/melodius.exe TREE.ZSM --no-loop
    --time 20 --wav tree_truth.wav` (48 kHz, 53.4 s). Compare by cross-
    correlating 20 ms RMS envelopes over 2 s segments: a correct stream tracks
    `emu = truth + const` with corr > 0.95 per segment; a slope ≠ 1 means the
    feed is starving (silence stretches the timeline) and a *linear* slope is
    the emulator's clock, not the player.
  - **Crockett noise resolved (2026-10-23) — the bug was in the veramusic build
    tool, not the emulator.** `zsm_pcm_resample.mjs` walked 16-bit PCM with a
    1-byte frame (`bytesPerFrame` ignored the sample width), so the decimated
    mono16 drum became byte hash; and the songs still ran drums at up to
    97.7 kB/s, which starves the FIFO at 1.02 MHz. The build now caps every
    PCM song at **8,011 B/s mono / 16,022 B/s stereo** — the jukebox's 8 kHz
    VERA rate 21, with 16-bit instruments re-quantized to dithered 8-bit
    (`PCM01.PCM` 423 KB, `MOO03.PCM` 6.8 KB). Verified in AppleWin at the
    earlier 16 kB/s cap: TREE `pushed 16022 / popped 16022 / underrun 0`,
    envelope corr 0.9948; Crockett's mono16 drum single-hit corr 0.999 (peak
    8,020 vs 8,032). The 8 kHz build is verified two ways: offline, source vs
    8 kHz Crockett (both renders with the companion `.PCM` joined, 144 s) gives
    20 ms envelope corr 0.9999 and RMS −0.04 dB, with the 16→8-bit step alone
    at SNR 29.0 dB; in-emulator, a headless 1.02 MHz Crockett run logs `$C21C`
    alternating `$15` (rate 21) / `$00` and its dump has no zero sample runs
    after the boot silence. `VERAAudio::renderPcmSample` needed no change.
  - **Non-60 Hz ZSM tick rates now normalized in the player (2026-10-23).**
    Headbanger!.zsm (header tick rate **240**) played ~4× slow because the IRQ
    published one tick per 60 Hz VSYNC. `zsmstream.asm` now does what zsmkit
    does: `DECODE_FRAME` consumes `TICKS_PER_FRM = rate/60` ticks plus a
    `TICK_REM = rate%60` carry (merged into one frame), and rates <60 publish
    hold frames. `FRM` counts source ticks, so the 5 s seek uses
    `5 * TICKRATE`. The menu duration label and `tools/melodius.exe` were also
    dividing ticks by 60 (Headbanger showed 17:34); both now divide by the
    header rate (4:24, offline WAV 263.6 s and byte-identical to the old
    render's first 263.6 s). Verified at 3 MHz (reference's 12 s section lands
    at ~13 s) and at the default 1.02 MHz (continuous RMS, no dropouts), with
    60/64/50 Hz songs regression-tested — the merged 240 Hz frames cost no more
    per output frame than a 60 Hz song.
  **Timing note:** this machine's //e runs at 3 MHz (config speed slider), so
  `g_nCumulativeCycles` advances ~51k per 60 Hz VERA tick, not ~17k: TITLE's
  EOD is at ~171M cycles and a 75M-cycle run is only ~24 s of song time.
  `T:` on screen is published-frame based, so 55 s of music = ~3329 frames.
  The VSYNC fix (`Step(double mhz,...)` with `g_fCurrentCLK6502/1e6`) keeps
  VERA at ~59.5 Hz real time at any emulation speed; it is a real fix, not a
  probe.
- **melodius (X16 player) ground-truth workbench (2026-10-22).** To obtain
  a reference recording for the above comparison, the X16-native player
  `C:\dev\melodius` was instrumented in x16emu:
  - Built with `ca65 --cpu W65C02` (uppercase W; lowercase `65C02` rejects
    `wai`), linked via `ld65 -C melodius.cfg obj/*.o -m melodius.map -o
    melodius.PRG`.
  - TEMP short-circuit `loadtitle` added to `src\directory.s`: walks the
    DIR-bank `mfiles` list comparing each null-terminated entry against
    `"TITLE.ZSM"`, sets `cactive`/`cvis` to the hit and calls
    `dirlist_exec_dptr` — replaces the old `jsr loadrandom` in
    `main.s` (entry at `melodius.map` 000B9B).
  - x16emu keyboard injection capability (final): only down/tab reach the
    kernal; enter/space/letters silently do nothing (proven with a hex
    key-value probe at col 74 of melodius + the BASIC echo file).
    `pyautogui`+SetForegroundWindow is fine for screenshots, not keys.
  - **Ground truth captured**: `x16emu -prg melodius.PRG -run -fsroot .
    -ram 1024 -scale 1 -echo iso -wav title_truth.wav,auto` records the
    emulator's audio output (`-wav file[,auto]` starts on first non-zero
    audio). Killing x16emu truncates the WAV header; repair in place by
    patching RIFF size at offset 4 and data-chunk size at offset 40 (fmt
    chunk lives at 16, data starts at 44). Result:
    `C:\dev\melodius\x16run\title_truth_fixed.wav` = 209 s stereo 48828 Hz,
    music non-zero from ~5 s onward, TITLE content verified via per-second
    RMS scan (quiet dip ~54–56 s = TITLE's first-loop boundary). Use this
    as the reference track when comparing the Apple //e zsmstream output
    ("slow motion + noise") against the X16 native playback.

## Conventions

- The primary user communicates in **Traditional Chinese**; reply in kind.
- Keep temporary profiling/debug code out of the tree when done (the old
  `QueryPerformanceCounter` profiling block in `VERACard::Update` was removed).
- **Upstream AI Tool Use Policy (`CONTRIBUTING.md`):**
  Upstream AppleWin enforces an explicit AI policy (merged from commit `35366c56`):
  1. **Human in the loop:** A human must supervise and review all contributions (issues, patches, PRs, comments).
  2. **Explicit marking:** All AI-generated contributions must be explicitly marked as such.
  3. **Licensing & copyright:** Contributors are responsible for ensuring appropriate rights and licensing compliance under AppleWin's license terms.
  Any agent or contributor preparing PRs or patches for upstream AppleWin must strictly abide by these rules.
