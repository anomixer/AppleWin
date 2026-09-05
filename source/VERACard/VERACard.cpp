/*
  VERACard.cpp - Commander X16 VERA expansion card for AppleWin

  License: 2-clause BSD
*/
#include "StdAfx.h"

#include <algorithm>

#include "VERACard.h"

#include "Common.h"
#include "Core.h"
#include "CPU.h"
#include "Interface.h"
#include "Log.h"
#include "Memory.h"
#include "NTSC.h"
#include "Video.h"
#include "YamlHelper.h"

// ---------------------------------------------------------------------------

static const UINT kUNIT_VERSION = 1;

VOICE VERACard::m_veraVoice;

const std::string& VERACard::GetSnapshotCardName()
{
	static const std::string name("VERA");
	return name;
}

VERACard::VERACard(UINT slot)
	: Card(CT_VERA, slot)
	, m_lastVideoUpdateCycle(0)
	, m_lastFrameCycles(0)
	, m_lastSoundUpdateCycle(0)
	, m_byteOffset(0)
	, m_numSamplesError(0)
	, m_bFrameCleared(false)
{
	if (m_slot == SLOT0)
		ThrowErrorInvalidSlot();

	m_audio.SetSampleRate(kSampleRate);
	m_video.SetAudio(&m_audio);

	m_mixBuffer.resize(kDSBufferByteSize / sizeof(short));

	// Request the larger (VERA-sized) framebuffer from the Video subsystem.
	GetVideo().SetVidVERA(true);
}

VERACard::~VERACard()
{
	GetVideo().SetVidVERA(false);
	DSReleaseSoundBuffer(&m_veraVoice);
}

void VERACard::InitializeIO(LPBYTE pCxRomPeripheral)
{
	// VERA has no ROM; registers live in the Cx region ($Cs00-$CsFF)
	RegisterIoHandler(m_slot, IO_Null, IO_Null, &VERACard::IOReadCx, &VERACard::IOWriteCx, this, NULL);
}

void VERACard::Destroy()
{
	DSReleaseSoundBuffer(&m_veraVoice);
}

void VERACard::Reset(const bool powerCycle)
{
	m_video.Reset();	// also resets audio (m_audio)
	m_audio.SetSampleRate(kSampleRate);

	m_lastVideoUpdateCycle = 0;
	m_lastFrameCycles = 0;
	m_lastSoundUpdateCycle = 0;
	m_byteOffset = 0;
	m_numSamplesError = 0;
	m_bFrameCleared = false;
}

void VERACard::Update(const ULONG nExecutedCycles)
{
	(void)nExecutedCycles;

	// Clear the framebuffer once when VERA first becomes active, so leftover
	// Apple II / border pixels don't show through.
	if (!m_bFrameCleared && m_video.IsVideoOutputEnabled())
	{
		GetVideo().ClearFrameBuffer();
		m_bFrameCleared = true;
	}

	// --- Video step ---
	//
	// VERA's video timing must be decoupled from the CPU cycle batches that
	// drive this Update(). Update() is called ~16 times per Apple frame (once
	// per ~1000-cycle batch), so advancing VERA by the batch count here would
	// render only a fraction of a frame per batch and let the scan position
	// drift, causing scrolling. Instead we advance exactly one complete VERA
	// frame (525 lines = 16800 pixel-clock steps) per Apple display frame,
	// detected via the monotonic cumulative cycle counter. This keeps the
	// emulated VERA frame rate matching the Apple II frame rate (~1 frame per
	// frame) with no scrolling, no tearing and no 16x speedup.
	const uint64_t curCycles = g_nCumulativeCycles;
	if (m_lastFrameCycles == 0)
		m_lastFrameCycles = curCycles;	// first call: establish a baseline

	const uint64_t frameCycles = NTSC_GetCyclesPerFrame();
	while (curCycles >= m_lastFrameCycles + frameCycles)
	{
		m_lastFrameCycles += frameCycles;

		// One full VERA frame: 525 lines x 800 pixels / 25 pixels-per-step.
		const int kFullFrameCycles = 525 * 800 / 25;	// 16800
		m_video.Step(1, kFullFrameCycles, false);
		m_video.Update();

		// Only paint the VERA framebuffer when the VERA video output is
		// actually enabled; otherwise leave the Apple II (NTSC) screen.
		if (m_video.IsVideoOutputEnabled())
			UpdateDisplay();
	}

	// --- IRQ ---
	const bool irq = m_video.GetIRQOut();
	if (irq)
		CpuIrqAssert(IS_VERA);
	else
		CpuIrqDeassert(IS_VERA);

	// --- Audio ---
	UpdateSound();
}

// ---------------------------------------------------------------------------
// IO handlers
// ---------------------------------------------------------------------------

BYTE __stdcall VERACard::IOReadCx(WORD pc, WORD addr, BYTE bWrite, BYTE value, ULONG nExecutedCycles)
{
	const UINT slot = (addr >> 8) & 0xf;
	VERACard* pCard = (VERACard*)MemGetSlotParameters(slot);
	(void)pc; (void)bWrite; (void)value; (void)nExecutedCycles;
	if (!pCard)
		return 0;
	return pCard->m_video.Read(static_cast<uint8_t>(addr & 0xff), false);
}

BYTE __stdcall VERACard::IOWriteCx(WORD pc, WORD addr, BYTE bWrite, BYTE value, ULONG nExecutedCycles)
{
	const UINT slot = (addr >> 8) & 0xf;
	VERACard* pCard = (VERACard*)MemGetSlotParameters(slot);
	(void)pc; (void)bWrite; (void)nExecutedCycles;
	if (!pCard)
		return 0;
	pCard->m_video.Write(static_cast<uint8_t>(addr & 0xff), value);
	return 0;
}

// ---------------------------------------------------------------------------
// Display
// ---------------------------------------------------------------------------

void VERACard::UpdateDisplay()
{
	uint8_t* pFb = GetVideo().GetFrameBuffer();
	if (!pFb)
		return;

	const uint8_t* src = m_video.GetFramebuffer();	// RGBA
	const int w = m_video.GetFramebufferWidth();		// 640
	const int h = m_video.GetFramebufferHeight();		// 480
	const int fbW = static_cast<int>(GetVideo().GetFrameBufferWidth());	// 680
	const int fbH = static_cast<int>(GetVideo().GetFrameBufferHeight());	// 516
	const int borderW = static_cast<int>(GetVideo().GetFrameBufferBorderWidth());	// 20
	const int borderH = static_cast<int>(GetVideo().GetFrameBufferBorderHeight());	// 18

	const int visibleW = fbW - 2 * borderW;
	const int visibleH = fbH - 2 * borderH;
	const int drawW = (w < visibleW) ? w : visibleW;
	const int drawH = (h < visibleH) ? h : visibleH;

	// The Video framebuffer is bottom-up (row 0 = bottom) with the visible area
	// offset by the border. Mirror NTSC's mapping so the VERA image isn't
	// vertically flipped and lands inside the visible (borderless) region.
	for (int y = 0; y < drawH; y++)
	{
		const uint8_t* s = src + static_cast<size_t>(y) * w * 4;
		uint8_t* d = pFb + (static_cast<size_t>(fbH - 1 - y - borderH) * fbW + borderW) * 4;
		for (int x = 0; x < drawW; x++)
		{
			// src is RGBA; dest is bgra_t {b,g,r,a}
			d[0] = s[2];	// blue
			d[1] = s[1];	// green
			d[2] = s[0];	// red
			d[3] = 0xFF;	// alpha
			s += 4;
			d += 4;
		}
	}
}

// ---------------------------------------------------------------------------
// Audio (SSI263-style ring buffer)
// ---------------------------------------------------------------------------

void VERACard::InitAudio()
{
	if (m_veraVoice.lpDSBvoice && m_veraVoice.bActive)
		return;

	HRESULT hr = DSGetSoundBuffer(&m_veraVoice, kDSBufferByteSize, kSampleRate, kNumChannels, "VERA");
	if (FAILED(hr))
	{
		LogOutput("VERA: DSGetSoundBuffer failed (0x%08X)\n", hr);
		return;
	}

	DSZeroVoiceBuffer(&m_veraVoice, kDSBufferByteSize);
	m_byteOffset = 0;
	m_numSamplesError = 0;
}

void VERACard::UpdateSound()
{
	if (!m_veraVoice.lpDSBvoice)
	{
		InitAudio();	// audio buffer may not be created yet (lazy init)
		if (!m_veraVoice.lpDSBvoice)
			return;	// audio not available yet
	}

	// Generate samples based on elapsed cycles.
	if (m_lastSoundUpdateCycle == 0)
	{
		m_lastSoundUpdateCycle = g_nCumulativeCycles;
		return;
	}

	const double updateInterval = (double)(g_nCumulativeCycles - m_lastSoundUpdateCycle);
	if (updateInterval < kMinUpdateIntervalCycles)
		return;
	m_lastSoundUpdateCycle = g_nCumulativeCycles;

	// Convert cycles to sample count.
	const double nIrqFreq = g_fCurrentCLK6502 / updateInterval + 0.5;	// round-up
	int nNumSamples = (int)((double)kSampleRate / nIrqFreq) + m_numSamplesError;
	if (nNumSamples <= 0)
		nNumSamples = 0;
	const UINT kMaxSamples = kDSBufferByteSize / (sizeof(short) * kNumChannels);	// frames of stereo
	if (nNumSamples > (int)kMaxSamples)
		nNumSamples = (int)kMaxSamples;

	if (nNumSamples == 0)
	{
		if (m_numSamplesError)
			m_byteOffset = 0;
		return;
	}

	// Generate the samples from the VERA audio core.
	m_audio.Render(m_mixBuffer.data(), nNumSamples);

	// Write into the ring buffer.
	DWORD dwCurrentPlayCursor, dwCurrentWriteCursor;
	HRESULT hr = m_veraVoice.lpDSBvoice->GetCurrentPosition(&dwCurrentPlayCursor, &dwCurrentWriteCursor);
	if (FAILED(hr))
		return;

	if (m_byteOffset == 0)
		m_byteOffset = dwCurrentWriteCursor;

	if (SoundCore_ValidateAndAlignWriteOffset(m_byteOffset, dwCurrentPlayCursor, dwCurrentWriteCursor))
		m_numSamplesError = 0;

	short* pLocked0;
	short* pLocked1;
	DWORD dwLockedSize0, dwLockedSize1;
	hr = DSGetLock(m_veraVoice.lpDSBvoice, m_byteOffset,
		(uint32_t)nNumSamples * sizeof(short) * kNumChannels,
		&pLocked0, &dwLockedSize0, &pLocked1, &dwLockedSize1);
	if (FAILED(hr))
		return;

	memcpy(pLocked0, m_mixBuffer.data(), dwLockedSize0);
	if (pLocked1)
		memcpy(pLocked1, m_mixBuffer.data() + dwLockedSize0 / sizeof(short), dwLockedSize1);

	m_veraVoice.lpDSBvoice->Unlock(pLocked0, dwLockedSize0, pLocked1, dwLockedSize1);

	m_byteOffset = (m_byteOffset + (uint32_t)nNumSamples * sizeof(short) * kNumChannels) % kDSBufferByteSize;
}

// ---------------------------------------------------------------------------
// Save state
// ---------------------------------------------------------------------------

void VERACard::SaveSnapshot(YamlSaveHelper& yamlSaveHelper)
{
	// Save the VERA video RAM so the display contents survive a save-state.
	YamlSaveHelper::Slot slot(yamlSaveHelper, GetSnapshotCardName(), m_slot, kUNIT_VERSION);

	YamlSaveHelper::Label state(yamlSaveHelper, "%s:\n", SS_YAML_KEY_STATE);

	// Video RAM (128KB)
	YamlSaveHelper::Label mem(yamlSaveHelper, "%s:\n", "Video RAM");
	yamlSaveHelper.SaveMemory(m_video.GetVideoRAM(), (UINT)m_video.GetVideoRAMSize(), 0);

	// VERA IO/Composer/Layer registers (fixed-size buffer)
	std::vector<uint8_t> regs;
	m_video.SerializeRegisters(regs);
	YamlSaveHelper::Label regsLabel(yamlSaveHelper, "%s:\n", "Registers");
	yamlSaveHelper.SaveMemory(regs.data(), (UINT)regs.size(), 0);
}

bool VERACard::LoadSnapshot(YamlLoadHelper& yamlLoadHelper, UINT version)
{
	if (version < 1 || version > kUNIT_VERSION)
		ThrowErrorInvalidVersion(version);

	// Reset core first, then restore video RAM.
	Reset(false);

	if (!yamlLoadHelper.GetSubMap(SS_YAML_KEY_STATE))
		throw std::runtime_error("VERA: Missing state");

	if (!yamlLoadHelper.GetSubMap("Video RAM"))
		throw std::runtime_error("VERA: Missing Video RAM");

	std::vector<uint8_t> vram;
	yamlLoadHelper.LoadMemory(vram, m_video.GetVideoRAMSize(), 0);
	if (vram.size() == m_video.GetVideoRAMSize())
		memcpy(m_video.GetVideoRAM(), vram.data(), vram.size());

	yamlLoadHelper.PopMap();	// Video RAM

	// Registers (optional for backward compatibility with older save-states)
	if (yamlLoadHelper.GetSubMap("Registers"))
	{
		std::vector<BYTE> regs;
		yamlLoadHelper.LoadMemory(regs, VERAVideo::GetRegisterStateSize(), 0);
		if (regs.size() == VERAVideo::GetRegisterStateSize())
			m_video.DeserializeRegisters(regs);
		yamlLoadHelper.PopMap();
	}

	yamlLoadHelper.PopMap();	// state
	return true;
}
