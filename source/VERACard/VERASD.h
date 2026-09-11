/*
  VERASD.h - Commander X16 VERA SD card (SPI) emulation for AppleWin

  Port of apple2ts's src/worker/devices/vera/sdcard.ts
  (Commander X16 Emulator: (c) 2019 Michael Steil, (c) 2020 Frank van den Hoef,
   TypeScript port & mods by Michael Morrison) to C++ for AppleWin.

  Emulates the SD/MMC SPI protocol spoken through the VERA SPI port
  ($9F3E/$9F3F). apple2ts uses stub C-API calls (x16open/read/write/seek/size)
  for the backing storage; here those are implemented with real stdio so the
  SD image is a raw 512-byte-block file (e.g. a FAT32 image).

  License: 2-clause BSD
*/
#pragma once

#include <cstdint>
#include <cstdio>
#include <string>

class VERASD
{
public:
	VERASD();
	~VERASD();

	void Reset();
	bool Init();

	// Mount / unmount a raw SD image (512-byte blocks).
	void SetPath(const std::string& path);
	void Unmount();
	bool IsMounted() const { return m_sdcard_attached; }
	const std::string& GetPath() const { return m_sdcard_path; }

	// SPI register access (reg = $9F3E & 1 selects DATA, $9F3F & 1 selects STATUS).
	uint8_t SpiRead(int reg);
	void SpiWrite(int reg, uint8_t value);

	// Advance the SPI timing by 'clocks' CPU cycles (drives the byte-transfer
	// busy counter, which completes a transfer every 10 SPI clock cycles).
	void SpiStep(int clocks);

	// Self-test: run the SD init sequence and read block 'lba' (0-based) into
	// dest512. Returns true if the SPI read completed with a valid data token.
	bool TestReadBlock(uint32_t lba, uint8_t* dest512);

private:
	// SD/MMC commands (SPI mode)
	static const int CMD0 = 0;
	static const int CMD8 = 8;
	static const int CMD9 = 9;
	static const int ACMD41 = 0x80 | 41;
	static const int CMD12 = 12;
	static const int CMD13 = 13;
	static const int CMD16 = 16;
	static const int CMD17 = 17;
	static const int CMD18 = 18;
	static const int CMD24 = 24;
	static const int CMD55 = 55;
	static const int CMD58 = 58;

	// File I/O helpers (replacement for apple2ts's x16* stubs).
	bool OpenFile();
	void CloseFile();
	bool SeekBlock(uint32_t lba);
	uint32_t FileSizeBytes();
	bool ReadBlock(uint32_t lba, uint8_t* dest512);
	bool WriteBlock(uint32_t lba, const uint8_t* src512);

	uint8_t HandleByte(uint8_t inbyte);
	void SetResponseR1();
	void SetResponseR2();
	void SetResponseR3();
	void SetResponseR7();
	void SetResponseCSD();
	uint32_t LoadBlock(uint8_t* dest);

	// State
	FILE* m_sdcard_file;
	std::string m_sdcard_path;
	bool m_sdcard_attached;
	bool m_selected;
	bool m_busy;
	bool m_autotx;
	bool m_is_acmd;
	bool m_is_idle;
	bool m_is_initialized;
	bool m_ongoing_multiblock_read;

	uint8_t m_sending_byte;
	uint8_t m_received_byte;
	int m_outcounter;

	uint8_t m_rxbuf[3 + 512];
	int m_rxbuf_idx;
	uint32_t m_lba;
	int m_last_cmd;

	uint8_t m_response[3 + 1 + 512 + 2];	// max response size (R3=4, CSD=21, block=515)
	int m_response_length;
	int m_response_counter;
};
