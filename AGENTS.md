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

- SSI263-style ring buffer via `DSGetSoundBuffer` / `DSGetLock` /
  `DSZeroVoiceBuffer` (`source\SoundCore.cpp`).
- `kDSBufferByteSize = 44100 / 60 * 3 * 2 * kNumChannels` (~3 frames of stereo
  audio) — this avoids overwriting audio that is still playing (a 1-frame
  buffer caused choppy sound).
- Sample rate 44100 Hz, stereo. `UpdateSound()` is called each `Update()` and
  generates samples based on elapsed `g_nCumulativeCycles`.

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
