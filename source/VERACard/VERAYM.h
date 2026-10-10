/*
  VERAYM.h - A2VERA FM audio (Yamaha YM2151 / OPM2151) wrapper around ymfm

  The OPM2151 daughterboard plugs into the VERA card and is accessed through
  the Cx region at offset $20 (YM_REG, register select) and $21 (YM_DATA,
  read/write data, read returns the status register), mirroring the X16
  ($9F20 + 0x20 = $9F40, $9F20 + 0x21 = $9F41).

  The wrapper owns a ymfm::ym2151 core (BSD licensed, Aaron Giles) and
  produces 16-bit stereo samples at the chip's native sample rate, resampled
  to the emulator's 44100 Hz output rate by the caller's consumer.

  License: BSD-3-Clause (see source/ymfm/LICENSE)
*/
#pragma once

#include "ymfm_opm.h"

#include <cstdint>

class VERAYM : public ymfm::ymfm_interface
{
public:
	VERAYM();
	~VERAYM();

	void Reset();

	// Host register interface (YM_REG = $20, YM_DATA = $21)
	void WriteRegSelect(uint8_t value);	// write to address port
	void WriteData(uint8_t value);		// write to data port
	uint8_t ReadData();				// read status port

	// Native YM2151 clock (Hz); the OPM2151 module runs at 3.579545 MHz
	static const uint32_t kYMClock = 3579545;

	// Generate one native-rate stereo sample (interleaved L,R via outL/outR).
	void Generate(int16_t* outL, int16_t* outR);

	// Native sample rate produced by the YM2151 at kYMClock.
	uint32_t GetSampleRate() const;

	// Render 'numSamples' stereo samples (interleaved L/R) at 44100 Hz by
	// resampling the native-rate output with linear interpolation.
	void Render(int16_t* buf, int numSamples);

	// ymfm_interface hooks: track the busy window so status reads reflect BUSY.
	void ymfm_set_busy_end(uint32_t clocks) override;
	bool ymfm_is_busy() override;

private:
	ymfm::ym2151 m_chip;
	ymfm::ym2151::output_data m_output;
	uint64_t m_clockCount;	// native-clock counter, advanced per generated sample
	uint64_t m_busyEnd;	// native-clock count at which BUSY clears

	// Resampling state (native rate -> 44100)
	double m_resamplePhase;
	int16_t m_prevL, m_prevR;
	int16_t m_curL, m_curR;
};
