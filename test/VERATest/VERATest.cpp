/*
  VERATest.cpp - Console smoke test for the VERA core (VERAVideo + VERAAudio)

  Verifies the VERA I/O register behaviour against apple2ts's vera.test.ts
  cases (VRAM write/read, auto-increment strides, dual data ports, palette).
*/
#include <cstdio>
#include <cstdlib>

#include "../../source/VERACard/VERAVideo.h"

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

	fprintf(stderr, "\nResult: %d passed, %d failed\n", g_passes, g_failures);
	fflush(stderr);
	return g_failures == 0 ? 0 : 1;
}
