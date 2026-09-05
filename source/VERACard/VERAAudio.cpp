/*
  VERAAudio.cpp - Commander X16 VERA audio core (PSG + PCM)

  Port of apple2ts's src/worker/devices/vera/pcm.ts and
  public/worklet/vera-psg.js
  (Commander X16 Emulator: (c) 2019 Michael Steil, (c) 2020 Frank van den Hoef,
   TypeScript port & mods by Michael Morrison) to C++ for AppleWin.

  License: 2-clause BSD
*/
#include "VERAAudio.h"

#include <cmath>
#include <cstdint>

const double VERAAudio::PSG_CLOCK = 25000000.0 / 512.0;

// PSG volume lookup table (64 entries)
static const uint16_t s_psgVolumeLut[64] = {
	0, 4, 8, 12,
	16, 17, 18, 20, 21, 22, 23, 25, 26, 28, 30, 31,
	33, 35, 37, 40, 42, 45, 47, 50, 53, 56, 60, 63,
	67, 71, 75, 80, 85, 90, 95, 101, 107, 113, 120, 127,
	135, 143, 151, 160, 170, 180, 191, 202, 214, 227, 241, 255,
	270, 286, 303, 321, 341, 361, 382, 405, 429, 455, 482, 511,
};

// PCM volume lookup table (16 entries)
static const uint8_t s_pcmVolumeLut[16] = { 0, 1, 2, 3, 4, 5, 6, 8, 11, 14, 18, 23, 30, 38, 49, 64 };

VERAAudio::VERAAudio()
{
	m_noiseState = 1;
	m_phaseScale = 0;
	Reset();
}

VERAAudio::~VERAAudio()
{
}

void VERAAudio::SetSampleRate(double sampleRate)
{
	m_phaseScale = PSG_CLOCK / sampleRate;
}

void VERAAudio::Reset()
{
	for (int i = 0; i < NUM_CHANNELS; i++)
	{
		Channel& ch = m_channels[i];
		ch.freq = 0;
		ch.volume = 0;
		ch.left = false;
		ch.right = false;
		ch.pw = 0;
		ch.waveform = 0;
		ch.noiseval = 0;
		ch.phase = 0;
	}
	m_noiseState = 1;

	fifoReset();
	m_ctrl = 0;
	m_rate = 0;
	m_loop = false;
	m_cur_l = 0;
	m_cur_r = 0;
	m_phase = 0;
}

void VERAAudio::fifoReset()
{
	m_fifo_wridx = 0;
	m_fifo_rdidx = 0;
	m_fifo_cnt = 0;
}

void VERAAudio::fifoRestart()
{
	m_fifo_rdidx = 0;
	m_fifo_cnt = m_fifo_wridx;
}

void VERAAudio::WritePSGReg(uint8_t reg, uint8_t value)
{
	reg &= 0x3f;
	value &= 0xff;

	const int ch = reg / 4;
	const int idx = reg & 3;
	if (ch >= NUM_CHANNELS)
		return;

	switch (idx)
	{
	case 0:
		m_channels[ch].freq = (m_channels[ch].freq & 0xff00) | value;
		break;
	case 1:
		m_channels[ch].freq = (m_channels[ch].freq & 0x00ff) | (value << 8);
		break;
	case 2:
		m_channels[ch].right = (value & 0x80) != 0;
		m_channels[ch].left = (value & 0x40) != 0;
		m_channels[ch].volume = s_psgVolumeLut[value & 0x3f];
		break;
	case 3:
		m_channels[ch].pw = value & 0x3f;
		m_channels[ch].waveform = value >> 6;
		break;
	}
}

void VERAAudio::WritePcmCtrl(uint8_t value)
{
	if ((value & 0xc0) == 0xc0)
	{
		m_loop = true;
	}
	else
	{
		m_loop = false;
		if (value & 0x80)
			fifoReset();
	}
	if (value & 0x40)
		fifoRestart();
	m_ctrl = value & 0x3f;
}

uint8_t VERAAudio::ReadPcmCtrl() const
{
	uint8_t result = m_ctrl;
	if (m_fifo_cnt == FIFO_SIZE - 1)
		result |= 0x80;	// full
	if (m_fifo_cnt == 0)
		result |= 0x40;	// empty
	return result;
}

void VERAAudio::WritePcmRate(uint8_t value)
{
	m_rate = (value > 128) ? (256 - value) : value;
}

uint8_t VERAAudio::ReadPcmRate() const
{
	return m_rate;
}

void VERAAudio::WritePcmFifo(uint8_t value)
{
	if (m_fifo_cnt < FIFO_SIZE - 1)
	{
		m_fifo[m_fifo_wridx++] = value;
		if (m_fifo_wridx == FIFO_SIZE)
			m_fifo_wridx = 0;
		m_fifo_cnt++;
	}
}

bool VERAAudio::IsFifoAlmostEmpty() const
{
	return m_fifo_cnt < 1024;
}

uint8_t VERAAudio::readFifo()
{
	if (m_fifo_cnt == 0)
		return 0;
	const uint8_t result = m_fifo[m_fifo_rdidx++];
	if (m_fifo_rdidx == FIFO_SIZE)
		m_fifo_rdidx = 0;
	m_fifo_cnt--;
	return result;
}

// Render one PSG sample into outL/outR (16-bit signed, combined across all 16 channels)
void VERAAudio::renderSample(int16_t* outL, int16_t* outR)
{
	int32_t l = 0;
	int32_t r = 0;

	// Advance the shared noise LFSR ONCE per output sample. The noise generator
	// is clocked at the PSG/sample rate, not once per channel — clocking it
	// 16x per sample made the noise far too fast (a hiss that sounded like sand).
	m_noiseState = (m_noiseState << 1) |
		((((m_noiseState >> 1) ^ (m_noiseState >> 2) ^ (m_noiseState >> 4) ^ (m_noiseState >> 15)) & 1));
	m_noiseState &= 0xffff;

	for (int i = 0; i < NUM_CHANNELS; i++)
	{
		Channel& ch = m_channels[i];
		const uint32_t oldPhase = static_cast<uint32_t>(ch.phase);
		double newPhase = 0;
		if (ch.left || ch.right)
		{
			newPhase = ch.phase + ch.freq * m_phaseScale;
			// % 0x20000
			newPhase = fmod(newPhase, 0x20000);
		}
		const uint32_t newPhaseInt = static_cast<uint32_t>(newPhase);
		if ((oldPhase & 0x10000) && !(newPhaseInt & 0x10000))
		{
			ch.noiseval = (m_noiseState >> 1) & 0x3f;
		}
		ch.phase = newPhase;

		int32_t v = 0;
		switch (ch.waveform)
		{
		case 0: // pulse
			v = ((newPhaseInt >> 10) > ch.pw) ? 0 : 0x3f;
			break;
		case 1: // sawtooth
			v = (newPhaseInt >> 11) ^ ((ch.pw ^ 0x3f) & 0x3f);
			break;
		case 2: // triangle
			v = ((newPhaseInt & 0x10000) ? (~(newPhaseInt >> 10) & 0x3f) : ((newPhaseInt >> 10) & 0x3f)) ^
				((ch.pw ^ 0x3f) & 0x3f);
			break;
		case 3: // noise
			v = ch.noiseval;
			break;
		}

		// sv = v ^ 0x20; if (sv & 0x20) sv |= 0xffc0; sv = (sv << 16) >> 16  => sign-extend 6 bits
		int32_t sv = v ^ 0x20;
		if (sv & 0x20)
			sv |= 0xffc0;
		sv = (sv << 16) >> 16;	// sign extend to 16-bit

		const int32_t val = sv * static_cast<int32_t>(ch.volume);
		if (ch.left)
			l += val >> 3;
		if (ch.right)
			r += val >> 3;
	}

	*outL = static_cast<int16_t>((l << 16) >> 16);
	*outR = static_cast<int16_t>((r << 16) >> 16);
}

// Render one PCM sample into outL/outR
void VERAAudio::renderPcmSample(int16_t* outL, int16_t* outR)
{
	const uint32_t oldPhase = static_cast<uint32_t>(m_phase);
	m_phase = fmod(m_phase + m_rate * m_phaseScale, 256.0);
	const uint32_t newPhase = static_cast<uint32_t>(m_phase);

	if ((oldPhase & 0x80) != (newPhase & 0x80))
	{
		if (m_fifo_cnt == 0)
		{
			m_cur_l = 0;
			m_cur_r = 0;
		}
		else
		{
			switch ((m_ctrl >> 4) & 3)
			{
			case 0: // mono 8-bit
				m_cur_l = static_cast<int16_t>((readFifo() << 8));	// sign-extend 8-bit
				m_cur_r = m_cur_l;
				break;
			case 1: // stereo 8-bit
				if (m_fifo_cnt < 2)
				{
					fifoReset();
					m_cur_l = m_cur_r = 0;
				}
				else
				{
					m_cur_l = static_cast<int16_t>((readFifo() << 8));
					m_cur_r = static_cast<int16_t>((readFifo() << 8));
				}
				break;
			case 2: // mono 16-bit
				if (m_fifo_cnt < 2)
				{
					fifoReset();
					m_cur_l = m_cur_r = 0;
				}
				else
				{
					const int32_t l = readFifo() | (readFifo() << 8);
					m_cur_l = static_cast<int16_t>(l);
					m_cur_r = m_cur_l;
				}
				break;
			case 3: // stereo 16-bit
				if (m_fifo_cnt < 4)
				{
					fifoReset();
					m_cur_l = m_cur_r = 0;
				}
				else
				{
					const int32_t l = readFifo() | (readFifo() << 8);
					m_cur_l = static_cast<int16_t>(l);
					const int32_t r = readFifo() | (readFifo() << 8);
					m_cur_r = static_cast<int16_t>(r);
				}
				break;
			}
			if (m_loop && m_fifo_cnt == 0)
				fifoRestart();
		}
	}

	const int32_t volume = s_pcmVolumeLut[m_ctrl & 0x0f];
	*outL = static_cast<int16_t>((m_cur_l * volume) / 64);
	*outR = static_cast<int16_t>((m_cur_r * volume) / 64);
}

void VERAAudio::Render(int16_t* buf, int numSamples)
{
	for (int i = 0; i < numSamples; i++)
	{
		int16_t psgL, psgR, pcmL, pcmR;
		renderSample(&psgL, &psgR);
		renderPcmSample(&pcmL, &pcmR);

		// Mixed output, clamped to 16-bit range (worklet clamps to [-1,1] then *32768)
		int32_t l = static_cast<int32_t>(psgL) + static_cast<int32_t>(pcmL);
		int32_t r = static_cast<int32_t>(psgR) + static_cast<int32_t>(pcmR);
		if (l > 32767) l = 32767;
		else if (l < -32768) l = -32768;
		if (r > 32767) r = 32767;
		else if (r < -32768) r = -32768;

		buf[i * 2 + 0] = static_cast<int16_t>(l);
		buf[i * 2 + 1] = static_cast<int16_t>(r);
	}
}
