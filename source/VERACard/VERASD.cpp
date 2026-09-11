/*
  VERASD.cpp - Commander X16 VERA SD card (SPI) emulation for AppleWin

  Port of apple2ts's src/worker/devices/vera/sdcard.ts
  (Commander X16 Emulator: (c) 2019 Michael Steil, (c) 2020 Frank van den Hoef,
   TypeScript port & mods by Michael Morrison) to C++ for AppleWin.

  License: 2-clause BSD
*/
#include "VERASD.h"

#include <cstring>

#include "Log.h"

static const int SPI_CLOCK_RATE_MHZ = 12;	// really 12.5, but it won't matter
static const int XSEEK_SET = 0;

VERASD::VERASD()
{
	Reset();
}

VERASD::~VERASD()
{
	CloseFile();
}

void VERASD::Reset()
{
	m_selected = false;
	m_busy = false;
	m_autotx = false;
	m_received_byte = 0xff;
	m_sending_byte = 0xff;
	m_outcounter = 0;

	m_sdcard_file = nullptr;
	m_sdcard_path.clear();
	m_sdcard_attached = false;
	m_is_acmd = false;
	m_is_idle = true;
	m_is_initialized = false;
	m_ongoing_multiblock_read = false;

	m_rxbuf_idx = 0;
	m_lba = 0;
	m_last_cmd = 0;
	m_response_length = 0;
	m_response_counter = 0;
}

bool VERASD::Init()
{
	return true;
}

// ---------------------------------------------------------------------------
// Mount / unmount
// ---------------------------------------------------------------------------

void VERASD::SetPath(const std::string& path)
{
	Unmount();
	m_sdcard_path = path;
	OpenFile();
}

void VERASD::Unmount()
{
	CloseFile();
	m_sdcard_path.clear();
}

bool VERASD::OpenFile()
{
	if (m_sdcard_attached)
		return true;
	if (m_sdcard_path.empty())
		return false;

	m_sdcard_file = fopen(m_sdcard_path.c_str(), "r+b");
	if (!m_sdcard_file)
	{
		LogOutput("VERA SD: cannot open SD image '%s'\n", m_sdcard_path.c_str());
		return false;
	}

	m_sdcard_attached = true;
	m_is_initialized = false;
	LogOutput("VERA SD: SD card attached (%s)\n", m_sdcard_path.c_str());
	return true;
}

void VERASD::CloseFile()
{
	if (m_sdcard_file)
	{
		fclose(m_sdcard_file);
		m_sdcard_file = nullptr;
	}
	m_sdcard_attached = false;
}

bool VERASD::SeekBlock(uint32_t lba)
{
	if (!m_sdcard_file)
		return false;
	return fseek(m_sdcard_file, (long)((uint64_t)lba * 512), XSEEK_SET) == 0;
}

uint32_t VERASD::FileSizeBytes()
{
	if (!m_sdcard_file)
		return 0;
	long pos = ftell(m_sdcard_file);
	fseek(m_sdcard_file, 0, SEEK_END);
	long size = ftell(m_sdcard_file);
	fseek(m_sdcard_file, pos, XSEEK_SET);
	return (uint32_t)size;
}

bool VERASD::ReadBlock(uint32_t lba, uint8_t* dest512)
{
	if (!SeekBlock(lba))
		return false;
	return fread(dest512, 1, 512, m_sdcard_file) == 512;
}

bool VERASD::WriteBlock(uint32_t lba, const uint8_t* src512)
{
	if (!SeekBlock(lba))
		return false;
	if (fwrite(src512, 1, 512, m_sdcard_file) != 512)
		return false;
	fflush(m_sdcard_file);	// persist immediately (SD card write semantics)
	return true;
}

// ---------------------------------------------------------------------------
// SPI register access
// ---------------------------------------------------------------------------

uint8_t VERASD::SpiRead(int reg)
{
	switch (reg)
	{
	case 0:
		if (m_autotx && m_selected && !m_busy)
		{
			// autotx mode will automatically send $FF after each read
			m_sending_byte = 0xff;
			m_busy = true;
			m_outcounter = 0;
		}
		LogWriteVERALog("VERASD SPI read  -> $%02X\n", m_received_byte);
		return m_received_byte;
	case 1:
		return (uint8_t)(((m_busy ? 1 : 0) << 7) | ((m_autotx ? 1 : 0) << 2) | (m_selected ? 1 : 0));
	}
	return 0;
}

void VERASD::SpiWrite(int reg, uint8_t value)
{
	switch (reg)
	{
	case 0:
		LogWriteVERALog("VERASD SPI write $%02X (selected=%d busy=%d)\n", value, m_selected, m_busy);
		if (m_selected && !m_busy)
		{
			m_sending_byte = value;
			m_busy = true;
			m_outcounter = 0;
		}
		break;
	case 1:
		if ((m_selected ? 1 : 0) != (value & 1))
		{
			m_selected = (value & 1) != 0;
			if (m_selected)
			{
				m_rxbuf_idx = 0;
			}
		}
		m_autotx = (value & 4) != 0;
		break;
	}
}

void VERASD::SpiStep(int clocks)
{
	if (m_busy)
	{
		m_outcounter += clocks * SPI_CLOCK_RATE_MHZ;
		if (m_outcounter >= 10)
		{
			// 10 cycles here should be safe and won't succeed in emulation
			// while failing on hardware.
			m_busy = false;
			if (m_sdcard_attached)
				m_received_byte = HandleByte(m_sending_byte);
			else
				m_received_byte = 0xff;
		}
	}
}

bool VERASD::TestReadBlock(uint32_t lba, uint8_t* dest512)
{
	if (!m_sdcard_attached)
		return false;

	// Reset SPI state and run the SD init sequence.
	m_busy = false;
	m_autotx = false;
	m_selected = false;
	m_is_idle = true;
	m_is_initialized = false;
	m_is_acmd = false;
	m_ongoing_multiblock_read = false;
	m_rxbuf_idx = 0;
	m_response_length = 0;
	m_response_counter = 0;

	auto spi_write = [this](uint8_t b) { SpiWrite(0, b); SpiStep(10); };
	auto spi_read = [this]() -> uint8_t { SpiWrite(0, 0xFF); SpiStep(10); return SpiRead(0); };

	// CMD0 (GO_IDLE) -> R1 = 0x01
	SpiWrite(1, 0x01);	// ss=1
	spi_write(0x40);	// start+transmit bit, CMD0
	spi_write(0); spi_write(0); spi_write(0); spi_write(0); spi_write(0xFF);
	spi_read();

	// CMD8 (SEND_IF_COND)
	spi_write(0x48);	// CMD8
	spi_write(0); spi_write(0); spi_write(0x01); spi_write(0xAA); spi_write(0x87);
	spi_read();

	// ACMD41 (CMD55 then CMD41)
	spi_write(0x77);	// CMD55
	spi_write(0); spi_write(0); spi_write(0); spi_write(0); spi_write(0xFF);
	spi_read();
	spi_write(0x69);	// CMD41 (with ACMD bit via 0x80|41)
	spi_write(0); spi_write(0); spi_write(0); spi_write(0); spi_write(0xFF);
	spi_read();

	// CMD16 (SET_BLOCKLEN) -> 512
	spi_write(0x50);	// CMD16
	spi_write(0); spi_write(0); spi_write(0x02); spi_write(0x00); spi_write(0xFF);
	spi_read();

	// CMD17 (READ_SINGLE_BLOCK) lba
	const uint8_t cmd17 = 0x51;	// 0x40 | 17
	spi_write(cmd17);
	spi_write((uint8_t)((lba >> 24) & 0xFF));
	spi_write((uint8_t)((lba >> 16) & 0xFF));
	spi_write((uint8_t)((lba >> 8) & 0xFF));
	spi_write((uint8_t)(lba & 0xFF));
	spi_write(0xFF);

	// Response: R1 (should be 0x00), then data token 0xFE, then 512 data bytes.
	uint8_t r1 = spi_read();
	if (r1 != 0x00)
		return false;

	// Advance to the data token (may take a few 0xFF reads).
	uint8_t token = 0;
	for (int i = 0; i < 16; i++)
	{
		token = spi_read();
		if (token == 0xFE)
			break;
	}
	if (token != 0xFE)
		return false;

	for (int i = 0; i < 512; i++)
		dest512[i] = spi_read();

	return true;
}

// ---------------------------------------------------------------------------
// Response helpers
// ---------------------------------------------------------------------------

void VERASD::SetResponseR1()
{
	uint8_t r1 = m_is_idle ? 1 : 0;
	m_response[0] = r1;
	m_response_length = 1;
}

void VERASD::SetResponseR2()
{
	if (m_is_initialized)
	{
		m_response[0] = 0x00;
		m_response[1] = 0x00;
		m_response_length = 2;
	}
	else
	{
		m_response[0] = 0x1F;
		m_response[1] = 0xFF;
		m_response_length = 2;
	}
}

void VERASD::SetResponseR3()
{
	m_response[0] = 0xC0;
	m_response[1] = 0xFF;
	m_response[2] = 0x80;
	m_response[3] = 0x00;
	m_response_length = 4;
}

void VERASD::SetResponseR7()
{
	m_response[0] = 1;
	m_response[1] = 0x00;
	m_response[2] = 0x00;
	m_response[3] = 0x01;
	m_response[4] = 0xAA;
	m_response_length = 5;
}

void VERASD::SetResponseCSD()
{
	// CSD register (21 bytes).
	static const uint8_t csd[21] = {
		0xff, 0xff, 0x00, 0xff, 0xfe,
		0x40, 0x0e, 0x00, 0x32, 0x5b,
		0x59, 0x00, 0x00, 0x00, 0x00,
		0x7f, 0x80, 0x0a, 0x40, 0x00,
		0x01
	};
	memcpy(m_response, csd, sizeof(csd));
	const uint32_t c_size = (FileSizeBytes() >> 19) - 1;
	m_response[12] |= (uint8_t)((c_size >> 16) & 0x3f);
	m_response[13] = (uint8_t)((c_size >> 8) & 0xff);
	m_response[14] = (uint8_t)(c_size & 0xff);
	m_response_length = 21;
}

// Return length of reply (1+512+2 for a data block, or 1 for error token).
uint32_t VERASD::LoadBlock(uint8_t* dest)
{
	dest[0] = 0xFE;	// Data token for CMD17/18
	if ((uint64_t)m_lba * 512 >= FileSizeBytes())
	{
		dest[0] = 0x08;	// Error token: out of range
		return 1;
	}

	ReadBlock(m_lba, dest + 1);
	return 1 + 512 + 2;
}

// ---------------------------------------------------------------------------
// Byte handler
// ---------------------------------------------------------------------------

uint8_t VERASD::HandleByte(uint8_t inbyte)
{
	if (!m_selected || !m_sdcard_attached)
		return 0xFF;

	uint8_t outbyte = 0xFF;
	if (m_rxbuf_idx == 0 && inbyte == 0xFF)
	{
		// send response data
		if (m_response_length > 0)
		{
			outbyte = m_response[m_response_counter++];
			if (m_response_counter == m_response_length)
			{
				if (m_ongoing_multiblock_read)
				{
					// Prepare next multiblock reply (no R1: just data token + data + CRC).
					m_lba++;
					uint8_t read_multiblock_next_response[3 + 512 + 2];
					m_response_length = LoadBlock(read_multiblock_next_response);
					// Stop multiblock read if error
					if (m_response_length == 1)
						m_ongoing_multiblock_read = false;
					memcpy(m_response, read_multiblock_next_response, m_response_length);
					m_response_counter = 0;
				}
				else
				{
					m_response_length = 0;
					m_ongoing_multiblock_read = false;
				}
			}
		}
	}
	else
	{
		m_rxbuf[m_rxbuf_idx++] = inbyte;
		if ((m_rxbuf[0] & 0xC0) == 0x40 && m_rxbuf_idx == 6)
		{
			m_rxbuf_idx = 0;
			// Check for start-bit + transmission bit
			if ((m_rxbuf[0] & 0xC0) != 0x40)
			{
				m_response_length = 0;
				return 0xFF;
			}
			m_rxbuf[0] &= 0x3F;
			// Use upper command bit to indicate this is an ACMD
			if (m_is_acmd)
			{
				m_rxbuf[0] |= 0x80;
				m_is_acmd = false;
			}

			m_last_cmd = m_rxbuf[0];
			switch (m_rxbuf[0])
			{
			case CMD0:
				// GO_IDLE_STATE: Resets the SD Memory Card
				m_is_idle = true;
				SetResponseR1();
				break;
			case CMD8:
				// SEND_IF_COND
				SetResponseR7();
				break;
			case CMD9:
				// SEND_CSD
				SetResponseCSD();
				break;
			case ACMD41:
				// SD_SEND_OP_COND
				m_is_idle = false;
				m_is_initialized = true;
				SetResponseR1();
				break;
			case CMD12:
				// STOP_TRANSMISSION: Abort ongoing multiple block read
				m_ongoing_multiblock_read = false;
				SetResponseR1();
				break;
			case CMD13:
				// SEND_STATUS
				SetResponseR2();
				break;
			case CMD16:
				// SET_BLOCKLEN
				SetResponseR1();
				break;
			case CMD18:
				// READ_MULTIPLE_BLOCK
				m_ongoing_multiblock_read = true;
				m_lba = (uint32_t)(((uint32_t)m_rxbuf[1] << 24) | ((uint32_t)m_rxbuf[2] << 16) | ((uint32_t)m_rxbuf[3] << 8) | m_rxbuf[4]);
				{
					uint8_t read_block_response[3 + 512 + 2];
					read_block_response[0] = 0;	// R1 response to command
					m_response_length = 1 + LoadBlock(read_block_response + 1);
					if (m_response_length == 2)
						m_ongoing_multiblock_read = false;
					memcpy(m_response, read_block_response, m_response_length);
				}
				break;
			case CMD17:
				// READ_SINGLE_BLOCK
				m_lba = (uint32_t)(((uint32_t)m_rxbuf[1] << 24) | ((uint32_t)m_rxbuf[2] << 16) | ((uint32_t)m_rxbuf[3] << 8) | m_rxbuf[4]);
				{
					uint8_t read_block_response[3 + 512 + 2];
					read_block_response[0] = 0;	// R1 response to command
					m_response_length = 1 + LoadBlock(read_block_response + 1);
					if (m_response_length == 2)
						m_ongoing_multiblock_read = false;
					memcpy(m_response, read_block_response, m_response_length);
				}
				break;
			case CMD24:
				// WRITE_BLOCK
				m_lba = (uint32_t)(((uint32_t)m_rxbuf[1] << 24) | ((uint32_t)m_rxbuf[2] << 16) | ((uint32_t)m_rxbuf[3] << 8) | m_rxbuf[4]);
				if (m_rxbuf_idx > 4 && (uint64_t)m_lba * 512 >= FileSizeBytes())
				{
					m_response[0] = 0x00;
					m_response[1] = 0x08;
					m_response_length = 2;
				}
				else
				{
					SetResponseR1();
				}
				break;
			case CMD55:
				// APP_CMD: Next command is an application specific command
				m_is_acmd = true;
				SetResponseR1();
				break;
			case CMD58:
				// READ_OCR
				SetResponseR3();
				break;
			default:
				SetResponseR1();
				break;
			}
			m_response_counter = 0;
		}
		else if (m_rxbuf_idx == 515)
		{
			m_rxbuf_idx = 0;
			// Check for 'start block' byte
			if (m_last_cmd == CMD24 && m_rxbuf[0] == 0xFE)
			{
				if ((uint64_t)m_lba * 512 < FileSizeBytes())
					WriteBlock(m_lba, m_rxbuf + 1);
			}
		}
	}
	return outbyte;
}
