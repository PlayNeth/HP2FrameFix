// What the other parts use from animation.cpp.
#pragma once
#include "common.h"

void InstallSteeringPatch();
namespace hp2 {
    const DWORD fnAnimAdvance = 0x545030;
    const DWORD pStepCounter  = 0x7221DC;   // int, 60 Hz physics steps
    const DWORD pRaceTime     = 0x7221F0;   // float seconds, scaled by game speed
    const DWORD pTimeScale    = 0x7221F4;   // float, 1.0 normal, <1 slow motion
}
void AnimDiagFlush(double seconds);
// The driver's clip selector (0x42E6B0) starts clips through these six calls
// to Actor::PlayAnim (0x402740, thiscall(id, param), ret 8). Clearing the
// looped latch here means a new shift clip can't inherit an old "ended".
namespace hp2 {
    const DWORD fnPlayAnim = 0x402740;
    const DWORD callSelectorPlayAnim[6] = { 0x42E761, 0x42E775, 0x42E795, 0x42E7A9, 0x42E7BD, 0x42E83A };
}
// Shift animation pacing. The selector (0x42E6B0, this = CarModel+0x358) starts
// a ~1 s shift clip (0x16317 / 0x1631B) whenever the gear differs from the one it
// last animated ([sel+0x1C]); changes made during a shift used to queue up, so
// quick up/down shifting kept replaying the clip long after the last change.
// Now a change during a shift extends it instead: the gear is taken as
// animated, and if the clip is already past its middle (hand at the shifter)
// it's held there, so the hand returns to the wheel ~0.5 s after the last change.
namespace hp2 { const DWORD callSelector = 0x42BEEE; const DWORD fnSelector = 0x42E6B0; }
void InstallAnimClockPatch();
void LogTimeScaleChanges();
namespace hp2 {
    const DWORD callWorldStepMid = 0x4D0217;   // call 0x4CA850 inside WorldStep, before the odd-step block
    const DWORD fnWorldStepMid   = 0x4CA850;   // ignores ecx, no stack args
    const DWORD fnActorsUpdate   = 0x406FB0;   // thiscall(ActorManager*): UpdateAll
}
void InstallParticlesPatch();
void InstallPropsPatch();
void InstallCharacterAnimPatch();
