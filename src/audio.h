// What the other parts use from audio.cpp.
#pragma once
#include "common.h"

extern bool g_inRestartAudio;
void InstallAudioModeFix();
void EAXFixTick(LONGLONG now);
