#pragma once

#include <cstdio>

#include "StrFormat.h"

#ifndef _VC71	// __VA_ARGS__ not supported on MSVC++ .NET 7.x
	#ifdef _DEBUG
		#define LOG(format, ...) LogOutput(format, __VA_ARGS__)
	#else
		#define LOG(...)
	#endif
#endif

extern FILE* g_fh;	// File handle for log file

void LogInit();
void LogDone();

void LogOutput(const char* format, ...) ATTRIBUTE_FORMAT_PRINTF(1, 2);
void LogFileOutput(const char* format, ...) ATTRIBUTE_FORMAT_PRINTF(1, 2);
// Write to VERA.log next to the exe (always findable, independent of the
// -log AppleWin.log which opens in the CWD). Used for VERA diagnostics.
void LogWriteVERALog(const char* format, ...) ATTRIBUTE_FORMAT_PRINTF(1, 2);
