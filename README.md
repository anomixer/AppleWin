AppleWin
========

#### Apple II emulator for Windows

AppleWin is a fully-featured emulator supporting different Apple II models and clones. A variety of peripheral cards and video display modes are supported (eg. NTSC, RGB); and there's an extensive built-in symbolic debugger.

Apple II models supported include:

* II (original)
* II Plus
* //e (original)
* //e (enhanced)
* Various clones (Pravets, TK3000, Base 64)

There is currently no support for the //c, //c+, Laser 128, Laser 128EX, Laser 128EX2, or Apple IIgs.

Peripheral cards and add-on hardware supported:

- Mockingboard, Phasor and SAM sound cards
- Disk II interface for floppy disk drives
- Hard disk controller
- Super Serial Card (SSC)
- Parallel printer card
- Mouse interface
- Apple IIe Extended 80-Column Text Card and RamWorks III (8MB)
- RGB cards: Apple's Extended 80-Column Text/AppleColor Adaptor Card and 'Le Chat Mauve' Féline.
- CP/M SoftCard
- Uthernet I and II (ethernet cards)
- Language Card and Saturn 64/128K for Apple II/II Plus (and Saturn 128K for any Apple II in any slot)
- 4Play and SNES MAX joystick cards
- VidHD card (functionality limited to IIgs' Super Hi-Res video modes)
- Commander X16 VERA card (slot 2 or 4): 640×480 VGA/NTSC graphics, 2 layers,
  128 sprites, 256-colour palette, 16-channel PSG + PCM audio, IRQ
- No Slot Clock (NSC)
- Game I/O Connector copy protection dongles 

Commander X16 VERA card
=======================

The VERA expansion card (port of `apple2ts`'s TypeScript VERA) is installed in
**slot 2 or 4** (`-s2 vera` / `-s4 vera`). It provides a 640×480 VGA/NTSC
graphics core (2 layers, 128 sprites, 256-colour palette), a 16-channel PSG +
PCM audio core, and an IRQ line.

Behaviour:

- **Dual screen** — when the VERA video output is *disabled* the Apple II
  (NTSC) image is shown; the VERA framebuffer is only painted when the software
  enables VERA video output (`IsVideoOutputEnabled`). This lets VERA software
  switch between the Apple II screen and the VERA 640×480 display.
- **Audio** — the VERA audio is self-contained (it does **not** borrow the
  Mockingboard/SSI263 audio model). `UpdateSound()` is driven directly by the
  DS play cursor so the write rate exactly tracks real-time playback — no
  drift, no periodic glitch. The DS ring buffer is ~3 frames (~50 ms) with a
  1/2-buffer lead.
- **Diagnostics** — on a stall (emulator hang) the DS buffer is flushed so no
  lingering audio survives. Heartbeat + unhandled-exception info is written to
  `VERA.log` next to the exe (independent of the `-log` `AppleWin.log`).

Running
=======

Download latest (stable) release: [AppleWin v1.31.0.0](https://github.com/AppleWin/AppleWin/releases/download/v1.31.0.0/AppleWin1.31.0.0.zip)

Release Notes: [v1.31.0.0](https://github.com/AppleWin/AppleWin/releases/tag/v1.31.0.0)


Building
========
To compile for Windows from source see:

* [docs/compiling.txt](https://github.com/AppleWin/AppleWin/blob/master/docs/compiling.txt)

Debugging
=========
If you are experencing a system-wide input unresponsiveness when debugging under MSVC 2022 then use the command-line argument `-no-hook-system-key` to prevent an input thread for system keys. See `g_bHookSystemKey`, `HookFilter::HookFilterForKeyboard()` and `HookFilter::HookThread()`.

Alternative work arounds include:

* Debug with MSVC 2026,
* Debug with MSVC 2019, or
* Manually use the older v5.01 Windows Common Controls.

Unofficial Ports
================

These ports will allow you to build and run AppleWin on other platforms:

* [Linux](https://github.com/audetto/AppleWin)
* [macOS](https://github.com/sh95014/AppleWin)

Contributing
============
Please see the [CONTRIBUTING](https://github.com/AppleWin/AppleWin/blob/master/CONTRIBUTING.md) document before raising new bugs, features and _especially_ PRs (Pull Requests).


Next Version
============
Experimental build: pending

Please report [new issues](https://github.com/AppleWin/AppleWin/issues/new)


Previous Versions
=================

Last version supporting Windows XP: [AppleWin v1.30.21.0](https://github.com/AppleWin/AppleWin/releases/tag/v1.30.21.0)

Last version supporting Windows 2000: [AppleWin v1.29.16.0](https://github.com/AppleWin/AppleWin/releases/tag/v1.29.16.0)

Last version supporting Windows 98/ME: [AppleWin v1.25.0.4](https://github.com/AppleWin/AppleWin/releases/tag/v1.25.0.4)
