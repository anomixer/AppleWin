/*
  VERATest.cpp - Console smoke test for the VERA core (VERAVideo + VERAAudio)

  Verifies the VERA I/O register behaviour against apple2ts's vera.test.ts
  cases (VRAM write/read, auto-increment strides, dual data ports, palette).
*/
#include <cstdio>
#include <cstdlib>

#include "../../source/VERACard/VERAVideo.h"
#include "../../source/VERACard/VERASD.h"

void LogOutput(const char* fmt, ...) {}
void LogWriteVERALog(const char* fmt, ...) {}

static int g_failures = 0;
static int g_passes = 0;

#define CHECK(cond, msg) \
	do { \
		if (cond) { fprintf(stderr, "  PASS: %s\n", msg); g_passes++; } \
		else { fprintf(stderr, "  FAIL: %s\n", msg); g_failures++; } \
		fflush(stderr); \
	} while (0)

int main()
{
	fprintf(stderr, "VERA core smoke test\n");
	fflush(stderr);

	VERAAudio audio;
	VERAVideo video;
	video.SetAudio(&audio);
	audio.SetSampleRate(44100);

	// --- Test 1: VRAM write/read with auto-increment +1 ---
	{
		video.Write(0x00, 0x00); // ADDR_L
		video.Write(0x01, 0x00); // ADDR_M
		video.Write(0x02, 0x10); // ADDR_H: bank0, inc +1 (stride index 2)
		video.Write(0x03, 0xAA); // DATA0 -> $00000
		video.Write(0x03, 0xBB); // DATA0 -> $00001
		video.Write(0x03, 0xCC); // DATA0 -> $00002

		// Reset address back to $00000 with auto-inc +1
		video.Write(0x00, 0x00);
		video.Write(0x01, 0x00);
		video.Write(0x02, 0x10);

		CHECK(video.Read(0x03, false) == 0xAA, "Test1: read 0xAA");
		CHECK(video.Read(0x03, false) == 0xBB, "Test1: read 0xBB");
		CHECK(video.Read(0x03, false) == 0xCC, "Test1: read 0xCC");
	}

	// --- Test 2: Auto-increment with stride +2/+4/+8 ---
	{
		video.Write(0x00, 0x00);
		video.Write(0x01, 0x00);
		video.Write(0x02, 0x30); // stride index 6 -> inc +4

		video.Write(0x03, 0x11); // writes to $00000, next is $00004
		video.Write(0x03, 0x22); // writes to $00004, next is $00008

		// Read without auto-increment (stride 0)
		video.Write(0x00, 0x00);
		video.Write(0x01, 0x00);
		video.Write(0x02, 0x00);
		CHECK(video.Read(0x03, false) == 0x11, "Test2: read $00000 = 0x11");

		video.Write(0x00, 0x04);
		video.Write(0x01, 0x00);
		video.Write(0x02, 0x00);
		CHECK(video.Read(0x03, false) == 0x22, "Test2: read $00004 = 0x22");
	}

	// --- Test 3: Dual data ports with CTRL.ADDRSEL ---
	{
		video.Write(0x05, 0x00); // CTRL = 0 (ADDR0 selected)
		video.Write(0x00, 0x00);
		video.Write(0x01, 0x01);
		video.Write(0x02, 0x10); // inc +1
		video.Write(0x03, 0x55); // write $55 to $00100

		video.Write(0x05, 0x01); // CTRL = 1 (ADDR1 selected)
		video.Write(0x00, 0x00);
		video.Write(0x01, 0x02);
		video.Write(0x02, 0x10);
		video.Write(0x04, 0x99); // write $99 to $00200

		// Verify $00100 holds $55
		video.Write(0x05, 0x00); // select ADDR0
		video.Write(0x00, 0x00);
		video.Write(0x01, 0x01);
		video.Write(0x02, 0x00);
		CHECK(video.Read(0x03, false) == 0x55, "Test3: ADDR0 -> $00100 = 0x55");

		// Verify $00200 holds $99
		video.Write(0x05, 0x01); // select ADDR1
		video.Write(0x00, 0x00);
		video.Write(0x01, 0x02);
		video.Write(0x02, 0x00);
		CHECK(video.Read(0x04, false) == 0x99, "Test3: ADDR1 -> $00200 = 0x99");
	}

	// --- Test 4: Palette writing and reading at VRAM $1FA00 ---
	{
		video.Write(0x05, 0x00);
		video.Write(0x00, 0x00);
		video.Write(0x01, 0xFA);
		video.Write(0x02, 0x11); // bank1, auto-inc +1

		video.Write(0x03, 0x00); // entry0 low
		video.Write(0x03, 0x00); // entry0 high
		video.Write(0x03, 0x00); // entry1 low (GB=$00)
		video.Write(0x03, 0x0F); // entry1 high (0R=$0F)

		// Read back entry 1
		video.Write(0x00, 0x02);
		video.Write(0x01, 0xFA);
		video.Write(0x02, 0x11);
		CHECK(video.Read(0x03, false) == 0x00, "Test4: palette entry1 low = 0x00");
		CHECK(video.Read(0x03, false) == 0x0F, "Test4: palette entry1 high = 0x0F");
	}

	// --- Test 5: step the video engine across several frames ---
	{
		video.Write(0x09, 0x03);	// DCSEL=0: enable video output
		int frames = 0;
		for (int i = 0; i < 3000; i++)
		{
			if (video.Step(1, 100, false))
				frames++;
		}
		fprintf(stderr, "  INFO: stepped 3000x, frames=%d\n", frames);
		fflush(stderr);
		const int fbW = video.GetFramebufferWidth();
		const int fbH = video.GetFramebufferHeight();
		CHECK(fbW == 640 && fbH == 480, "Test5: framebuffer is 640x480");
		CHECK(video.GetFramebuffer() != nullptr, "Test5: framebuffer ptr valid");
		CHECK(frames > 0, "Test5: produced at least one frame");
	}

	// --- Test 6: step with video output disabled (out_mode=0), as during DOS boot ---
	{
		video.Reset();
		int frames = 0;
		for (int i = 0; i < 200000; i++)
		{
			if (video.Step(1, 100, false))
				frames++;
		}
		fprintf(stderr, "  INFO: out_mode=0 stepped, frames=%d\n", frames);
		fflush(stderr);
		CHECK(frames > 0, "Test6: out_mode=0 produces frames");
	}

	// --- Test 7: VERA SD SPI protocol (CMD0, CMD8, CMD58 R3, ACMD41, CMD24 write tokens) ---
	{
		VERASD sd;
		const char* tmp_path = "test_vera_sd_tmp.img";
		FILE* f = fopen(tmp_path, "w+b");
		if (f)
		{
			uint8_t zero512[512] = { 0 };
			fwrite(zero512, 1, 512, f);
			fwrite(zero512, 1, 512, f); // 2 sectors = 1024 bytes
			fclose(f);
		}

		sd.SetPath(tmp_path);
		CHECK(sd.IsMounted(), "Test7: SD card mounted");

		auto spi_write = [&sd](uint8_t b) { sd.SpiWrite(0, b); sd.SpiStep(10); };
		auto spi_read = [&sd]() -> uint8_t { sd.SpiWrite(0, 0xFF); sd.SpiStep(10); return sd.SpiRead(0); };

		sd.SpiWrite(1, 0x01); // Select card (SS=1)

		// CMD0: GO_IDLE_STATE -> Expect R1 = 0x01
		spi_write(0x40); // CMD0
		spi_write(0); spi_write(0); spi_write(0); spi_write(0); spi_write(0x95);
		uint8_t r1 = spi_read();
		CHECK(r1 == 0x01, "Test7: CMD0 returns R1 = 0x01 (idle)");

		// CMD58: READ_OCR in idle state -> Expect 5 bytes: [0x01, 0xC0, 0xFF, 0x80, 0x00]
		spi_write(0x7A); // CMD58 (0x40 | 58)
		spi_write(0); spi_write(0); spi_write(0); spi_write(0); spi_write(0xFF);
		uint8_t r3_idle[5];
		for (int i = 0; i < 5; i++) r3_idle[i] = spi_read();
		CHECK(r3_idle[0] == 0x01 && r3_idle[1] == 0xC0 && r3_idle[2] == 0xFF && r3_idle[3] == 0x80 && r3_idle[4] == 0x00,
			"Test7: CMD58 returns 5 bytes with idle R1 [01 C0 FF 80 00]");

		// ACMD41: CMD55 then CMD41 -> initialize card
		spi_write(0x77); // CMD55
		spi_write(0); spi_write(0); spi_write(0); spi_write(0); spi_write(0xFF);
		spi_read(); // R1
		spi_write(0x69); // ACMD41
		spi_write(0); spi_write(0); spi_write(0); spi_write(0); spi_write(0xFF);
		uint8_t acmd_r1 = spi_read();
		CHECK(acmd_r1 == 0x00, "Test7: ACMD41 leaves card in non-idle state (R1 = 0x00)");

		// CMD58: READ_OCR in active state -> Expect 5 bytes: [0x00, 0xC0, 0xFF, 0x80, 0x00]
		spi_write(0x7A); // CMD58
		spi_write(0); spi_write(0); spi_write(0); spi_write(0); spi_write(0xFF);
		uint8_t r3_active[5];
		for (int i = 0; i < 5; i++) r3_active[i] = spi_read();
		CHECK(r3_active[0] == 0x00 && r3_active[1] == 0xC0 && r3_active[2] == 0xFF && r3_active[3] == 0x80 && r3_active[4] == 0x00,
			"Test7: CMD58 returns 5 bytes with active R1 [00 C0 FF 80 00]");

		// CMD24: WRITE_BLOCK sector 0
		spi_write(0x58); // CMD24 (0x40 | 24)
		spi_write(0); spi_write(0); spi_write(0); spi_write(0); spi_write(0xFF);
		uint8_t cmd24_r1 = spi_read();
		CHECK(cmd24_r1 == 0x00, "Test7: CMD24 command phase returns R1 = 0x00");

		// Send data packet: start token (0xFE) + 512 bytes payload + 2 CRC bytes
		spi_write(0xFE);
		for (int i = 0; i < 512; i++) spi_write((uint8_t)(i & 0xFF));
		spi_write(0xFF); spi_write(0xFF); // CRC16

		// Read data response token: 0x05 = accepted
		uint8_t wr_token = spi_read();
		CHECK(wr_token == 0x05, "Test7: CMD24 write accepted returns 0x05");

		// Test read back with TestReadBlock
		uint8_t readback[512] = { 0 };
		bool read_ok = sd.TestReadBlock(0, readback);
		CHECK(read_ok && readback[1] == 0x01 && readback[255] == 0xFF, "Test7: TestReadBlock verifies written data");

		// CMD24: WRITE_BLOCK to out-of-range sector (LBA 999999) -> expect 0x0D rejection token
		spi_write(0x58);
		spi_write(0x00); spi_write(0x0F); spi_write(0x42); spi_write(0x3F); spi_write(0xFF); // LBA 999999
		spi_read();
		spi_write(0xFE);
		for (int i = 0; i < 512; i++) spi_write(0xAA);
		spi_write(0xFF); spi_write(0xFF);
		uint8_t wr_fail_token = spi_read();
		CHECK(wr_fail_token == 0x0D, "Test7: CMD24 write out-of-range returns 0x0D rejection token");

		sd.Unmount();
		remove(tmp_path);
	}

	fprintf(stderr, "\nResult: %d passed, %d failed\n", g_passes, g_failures);
	fflush(stderr);
	return g_failures == 0 ? 0 : 1;
}
