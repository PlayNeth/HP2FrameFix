// What the other parts use from loading.cpp.
#pragma once
#include "common.h"

extern unsigned long g_loadSteps, g_loadRenders, g_loadPresents;
bool LoadingNow(LONGLONG now);
void EndLoadIfIdle(LONGLONG now);
void InstallLoadingPatch();
