// What the other parts use from timing.cpp.
#pragma once
#include "common.h"

extern LARGE_INTEGER g_qpcFreq;
extern HANDLE g_waitTimer;
extern bool g_timerResRaised;
double QpcToMs(LONGLONG delta);
LONGLONG QpcNow();
extern std::atomic<unsigned long> g_gtcCalls, g_tgtCalls;
void PatchIAT_Timers();
void InitWaitTimer();
void WaitUntil(LONGLONG deadline);
