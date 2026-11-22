// What the other parts use from ticks.cpp.
#pragma once
#include "common.h"

extern std::atomic<unsigned long> g_tickFires, g_tickResyncs;
void PublishGrid(LONGLONG anchor, LONGLONG period);
void InstallTickPatch();
extern unsigned long g_tpfHist[5];
void SampleTicksPerFrame(bool countIt);
