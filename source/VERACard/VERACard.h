/*
  VERACard.h - Commander X16 VERA expansion card for AppleWin

  Emulates the Commander X16 VERA (Versatile Embedded Retro Adapter) card
  as an Apple II slot card (slot 2 or 4). Provides graphics (640x480), PSG+PCM
  audio and IRQ.

  Port of apple2ts's VERA implementation:
  - video core:   src/worker/devices/vera/video.ts
  - SD card:      src/worker/devices/vera/sdcard.ts      (not yet integrated)
  - PCM audio:    src/worker/devices/vera/pcm.ts
  - PSG audio:    public/worklet/vera-psg.js
  License: 2-clause BSD
*/
#pragma once

#include "Card.h"
#include "SoundCore.h"
#include "VERAAudio.h"
#include "VERAVideo.h"

#include <vector>

class VERACard : public Card
{
public:
	VERACard(UINT slot);
	virtual ~VERACard();

	// Card interface
	virtual void InitializeIO(LPBYTE pCxRomPeripheral);
	virtual void Destroy();
	virtual void Reset(const bool powerCycle);
	virtual void Update(const ULONG nExecutedCycles);
	virtual void SaveSnapshot(YamlSaveHelper& yamlSaveHelper);
	virtual bool LoadSnapshot(YamlLoadHelper& yamlLoadHelper, UINT version);

	// IO handlers (Cx region: $Cs00-$CsFF)
	static BYTE __stdcall IOReadCx(WORD pc, WORD addr, BYTE bWrite, BYTE value, ULONG nExecutedCycles);
	static BYTE __stdcall IOWriteCx(WORD pc, WORD addr, BYTE bWrite, BYTE value, ULONG nExecutedCycles);

	// True when VERA video output is enabled (used by Video to decide display override)
	bool IsActive() const { return m_video.IsVideoOutputEnabled(); }

	// SD card mount/unmount (drives the VERA SPI SD image).
	// SetSDImagePath also persists the path to the registry (per-slot).
	void SetSDImagePath(const std::string& path);
	void UnmountSD();
	bool IsSDMounted() const { return m_video.IsSDMounted(); }
	// Read the persisted SD image path from the registry (empty if none).
	std::string GetSDImagePathFromRegistry() const;

	// Self-test: read block 0 (and the FAT32 boot signature) through the SPI,
	// logging the result to VERA.log. Returns true if the SPI read succeeds.
	bool TestSDRead();

	static const std::string& GetSnapshotCardName();

	// Display: called when a new frame is ready. Copies VERA framebuffer into the
	// AppleWin video framebuffer (as bgra_t).
	void UpdateDisplay();

private:
	void UpdateSound();
	void InitAudio();

	// Audio state
	static VOICE m_veraVoice;
	static const UINT kNumChannels = 2;
	static const UINT kSampleRate = 44100;
	static const UINT kDSBufferByteSize = 44100 / 60 * 3 * 2 * kNumChannels;	// ~3 frames of stereo samples (~50 ms)

	ULONG m_lastVideoUpdateCycle;
	bool m_bSDTested;	// SD self-test done once after mount
	uint64_t m_lastFrameCycles;	// cumulative cycle count when the last full VERA frame was advanced
	ULONGLONG m_lastSoundTick;	// real-time ms when UpdateSound last ran (stall detection)
	uint32_t m_byteOffset;
	uint32_t m_lastPlayCursor;	// last DS play-cursor position (drives sample gen)
	double m_sampleAccum;	// fractional sample accumulator (exact sample gen)
	bool m_bFrameCleared;
	std::vector<short> m_mixBuffer;

	// Audio must be constructed before video (video holds a pointer to audio)
	VERAAudio m_audio;
	VERAVideo m_video;
};
