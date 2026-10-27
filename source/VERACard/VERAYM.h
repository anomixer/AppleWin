/*
  VERAYM.h - A2VERA FM audio (Yamaha YM2151 / OPM2151) wrapper around ymfm

  The OPM2151 daughterboard plugs into the VERA card and is accessed through
  /DEVICE SELECT at $C080 + slot*$10: offset $0 is YM_SEL (register select),
  offset $1 is YM_DATA (data write / status read). Slot 2 uses $C0A0/$C0A1;
  slot 4 uses $C0C0/$C0C1.

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

	// Host register interface (YM_SEL = offset $0, YM_DATA = offset $1)
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
	// Advance the chip by one native sample. A register burst that turns a
	// channel off and back on (the ZSM key-off/key-on retrigger idiom) would
	// otherwise never be seen by ymfm's sample-clocked keystate sampler.
	void ClockKeyOff();

	ymfm::ym2151 m_chip;
	ymfm::ym2151::output_data m_output;
	uint8_t m_regSelect;	// last value written to the address port
	uint64_t m_clockCount;	// native-clock counter, advanced per generated sample
	uint64_t m_busyEnd;	// native-clock count at which BUSY clears

	// Resampling state (native rate -> 44100)
	double m_resamplePhase;
	int16_t m_prevL, m_prevR;
	int16_t m_curL, m_curR;
};
