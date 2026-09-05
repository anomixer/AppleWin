/*
  VERAAudio.h - Commander X16 VERA audio core (PSG + PCM)

  Port of apple2ts's src/worker/devices/vera/pcm.ts and
  public/worklet/vera-psg.js
  (Commander X16 Emulator: (c) 2019 Michael Steil, (c) 2020 Frank van den Hoef,
   TypeScript port & mods by Michael Morrison) to C++ for AppleWin.

  License: 2-clause BSD
*/
#pragma once

#include <cstdint>
#include <cstring>

class VERAAudio
{
public:
	VERAAudio();
	~VERAAudio();

	void Reset();
	void SetSampleRate(double sampleRate);

	// PSG register write (addr in [0x00..0x3F])
	void WritePSGReg(uint8_t reg, uint8_t value);

	// PCM control
	void WritePcmCtrl(uint8_t value);
	uint8_t ReadPcmCtrl() const;
	void WritePcmRate(uint8_t value);
	uint8_t ReadPcmRate() const;
	void WritePcmFifo(uint8_t value);
	bool IsFifoAlmostEmpty() const;

	// Render 'numSamples' stereo samples into buf (interleaved L/R). Returns buf as is.
	void Render(int16_t* buf, int numSamples);

private:
	// PSG channel
	struct Channel
	{
		uint16_t freq = 0;
		uint16_t volume = 0;
		bool left = false;
		bool right = false;
		uint8_t pw = 0;
		uint8_t waveform = 0;
		uint8_t noiseval = 0;
		double phase = 0;
	};

	static const int NUM_CHANNELS = 16;
	static const int FIFO_SIZE = 4096;
	static const double PSG_CLOCK;	// 25000000/512

	// PSG state
	Channel m_channels[NUM_CHANNELS];
	uint16_t m_noiseState;
	double m_phaseScale;

	// PCM state
	uint8_t m_fifo[FIFO_SIZE];
	int m_fifo_wridx;
	int m_fifo_rdidx;
	int m_fifo_cnt;
	uint8_t m_ctrl;
	uint8_t m_rate;
	bool m_loop;
	int32_t m_cur_l;
	int32_t m_cur_r;
	double m_phase;

	void fifoReset();
	void fifoRestart();
	uint8_t readFifo();
	void renderSample(int16_t* outL, int16_t* outR);
	void renderPcmSample(int16_t* outL, int16_t* outR);
};
