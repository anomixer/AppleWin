/*
  VERAYM.cpp - A2VERA FM audio (YM2151 / OPM2151) core wrapper around ymfm
*/
#include "VERAYM.h"

VERAYM::VERAYM()
	: m_chip(*this)
	, m_clockCount(0)
	, m_busyEnd(0)
	, m_resamplePhase(0)
	, m_prevL(0), m_prevR(0)
	, m_curL(0), m_curR(0)
{
	m_chip.reset();
}

VERAYM::~VERAYM()
{
}

void VERAYM::Reset()
{
	m_chip.reset();
	m_clockCount = 0;
	m_busyEnd = 0;
	m_resamplePhase = 0;
	m_prevL = m_prevR = m_curL = m_curR = 0;
}

void VERAYM::WriteRegSelect(uint8_t value)
{
	m_chip.write(0, value); // address port
}

void VERAYM::WriteData(uint8_t value)
{
	m_chip.write(1, value); // data port — latches the selected register
}

uint8_t VERAYM::ReadData()
{
	return m_chip.read(1); // status port
}

void VERAYM::Generate(int16_t* outL, int16_t* outR)
{
	m_chip.generate(&m_output, 1);
	*outL = (int16_t)m_output.data[0];
	*outR = (int16_t)m_output.data[1];
	m_clockCount += 64;
}

uint32_t VERAYM::GetSampleRate() const
{
	return m_chip.sample_rate(kYMClock);
}

void VERAYM::Render(int16_t* buf, int numSamples)
{
	const double ratio = static_cast<double>(GetSampleRate()) / 44100.0;
	for (int i = 0; i < numSamples; ++i)
	{
		m_resamplePhase += ratio;
		while (m_resamplePhase >= 1.0)
		{
			m_resamplePhase -= 1.0;
			m_prevL = m_curL;
			m_prevR = m_curR;
			Generate(&m_curL, &m_curR);
		}
		const double f = m_resamplePhase;
		buf[2 * i]     = static_cast<int16_t>(m_prevL + (m_curL - m_prevL) * f);
		buf[2 * i + 1] = static_cast<int16_t>(m_prevR + (m_curR - m_prevR) * f);
	}
}

void VERAYM::ymfm_set_busy_end(uint32_t clocks)
{
	m_busyEnd = m_clockCount + clocks;
}

bool VERAYM::ymfm_is_busy()
{
	return m_clockCount < m_busyEnd;
}
