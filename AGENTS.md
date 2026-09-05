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

## Build workflow

MSBuild (Visual Studio 2022) is used from the command line:

```powershell
$msbuild = "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe"
& $msbuild "AppleWin-VS2022.vcxproj" /p:Configuration=Debug   /p:Platform=Win32 /v:normal /nologo
& $msbuild "AppleWin-VS2022.vcxproj" /p:Configuration=Release /p:Platform=Win32 /v:normal /nologo
```

Gotchas:

- **Build single-threaded.** The `/m` (parallel) flag has produced transient
  "Build FAILED" with **0 errors** — run without `/m` for reliable results.
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

The correct video-timing approach (current implementation) decouples VERA from
the batches: advance **exactly one full VERA frame per Apple display frame**,
detected via the monotonic cumulative cycle counter `g_nCumulativeCycles`
(incremented by `CpuExecute`, see `source\CPU.cpp`). See
`VERACard::Update` in `source\VERACard\VERACard.cpp`.

## VERA card architecture

Files in `source\VERACard\`:

- `VERACard.h/.cpp` — slot-card glue: IO handlers, `Update()`, audio buffer
  management, save-state.
- `VERAVideo.h/.cpp` — the video core: `Step()` (scanline advance),
  `render_line()`, layers, sprites, palette, FX registers, IRQ.
- `VERAAudio.h/.cpp` — PSG (16 ch) + PCM FIFO mixer, `Render(buf, nSamples)`.

Key integration points:

- **IO region**: registers live in the Cx region `$Cs00–$CsFF` for the slot's
  bank; `addr & 0xff` selects the register. Registered via
  `RegisterIoHandler(slot, IO_Null, IO_Null, &IOReadCx, &IOWriteCx, ...)`.
- **IRQ**: `eIRQSRC` has an `IS_VERA` value (`source\Common.h`); asserted via
  `CpuIrqAssert(IS_VERA)` / `CpuIrqDeassert(IS_VERA)`.
- **CardManager**: single `m_pVERACard` instance, `GetVERACard()` accessor,
  and a slot drop-down entry (`CT_VERA` in `source\Card.h`).
- **CmdLine**: `-s<slot> vera` selects the card; hard disk image args are
  `-h1/-h2` for slot 7 (`source\CmdLine.cpp`).

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

### NTSC centring (Apple II image "偏上 / 偏左" fix)

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
   directly (later replaced entirely by the per-frame approach).
6. **16× too fast / scrolling** — forcing a full 16800-cycle frame **per batch**
   advanced ~16 frames per Apple frame; and per-batch advance caused drift.
   Final fix: one full VERA frame per Apple display frame via
   `g_nCumulativeCycles` frame-boundary detection.
7. **Choppy audio** — buffer was only 1 frame; raised to 3 frames.
8. **Blue refresh interference / blanking** — only call `UpdateDisplay()` /
   `ClearFrameBuffer()` when `IsVideoOutputEnabled()`.
9. **Text-mode blue scrolling up** — scan position drifted across frames;
   solved by the per-frame full-scan approach.
10. **Apple II text offset up** — NTSC image anchored at top of a 480-tall
    borderless area; solved by the centring offset above.
11. **Apple II image off-centre (偏下 / 偏左)** — vertical was shifted down by
    VidHD's `GetFrameBufferCentringValue()` stacking on the VERA centring, and
    horizontal centring only applied when VidHD was present. Fixed in
    `NTSC_VideoInit` by subtracting `GetFrameBufferCentringOffsetY()` from the
    vertical offset and adding a horizontal centring offset only for the
    only-VERA case (`!HasVidHD()`).
12. **Sandy/scratchy audio (沙沙聲) — root cause.** The VERA ring buffer is
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
14. **Random "flash crash" (閃退) after minutes — VERA-installation dependent.**
    The diagnostic that isolates it: the crash happens whenever the VERA card is
    installed *and any app is running*, including a **non-VERA** app (e.g.
    Mockingboard music). When VERA is removed, no crash (10+ min). This means
    the crash is in code that runs whenever VERA is installed — `Update()`,
    `UpdateSound()` or `VERAVideo::Step()` — **not** in the VERA display/audio
    *app* path (`UpdateDisplay` only runs when video output is enabled, so it
    is NOT the culprit for the non-VERA-app crash).
    **Critical:** an access violation (0xC0000005) is an **SEH** exception and is
    **NOT caught** by `WinMain`'s C++ `try/catch (std::exception)` — the process
    simply terminates ("閃退"). To locate it, add a
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

## Conventions

- The primary user communicates in **Traditional Chinese**; reply in kind.
- Keep temporary profiling/debug code out of the tree when done (the old
  `QueryPerformanceCounter` profiling block in `VERACard::Update` was removed).
