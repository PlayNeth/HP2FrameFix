// What the other parts use from interp.cpp.
#pragma once
#include "common.h"

extern std::atomic<bool> g_firstPresentSeen;
extern LONGLONG g_lastPresentQpc;
extern LONGLONG g_nextDeadline;
extern LONGLONG g_lastReanchor;
extern bool g_gridUnstable;
extern unsigned long long g_presentCount, g_intervalCount;
extern double g_minDelta, g_maxDelta, g_sumDelta;
namespace hp2 {
    const DWORD callWorldStep   = 0x4D0370;  const DWORD fnWorldStep   = 0x4D00A0;  // cdecl(step)
    const DWORD callCarSetOwner = 0x4CA968;  const DWORD fnCarSetOwner = 0x42B880;  // this=CarModel (veh)
    const DWORD callCarSetXform = 0x4CA978;  const DWORD fnCarSetXform = 0x42B940;  // this=CarModel (view,pos*,rot*)
    const DWORD callBuildView1  = 0x4D7734;  const DWORD callBuildView2 = 0x4D7813;
    const DWORD fnBuildView     = 0x40D620;  // this=view owner (a,b,c), ret 0xC
    const DWORD callDrawListB   = 0x4CAA7F;  const DWORD callDrawSimCar = 0x44D6D0;
    const DWORD fnCarDraw       = 0x42C010;  // this=CarModel (view), ret 4
    const DWORD pReplayFlag     = 0x7221EC;  // BYTE, world steps skipped while set
}
extern int g_ipNVehs;
extern int g_ipNOwners;
extern unsigned long g_ipCutFrames;
extern unsigned long g_ipFrames, g_ipFramesBlended, g_ipAhead, g_ipBehind,
 g_ipCarsBlended, g_ipCarsPass, g_ipCarMismatch, g_ipCarJump,
 g_ipCamsBlended, g_ipCamCut, g_ipCamMismatch, g_ipResets,
 g_ipClockSnaps, g_ipSteps, g_ipDrawListB, g_ipDrawSimCar;
extern double g_ipAlphaSum;
void RestoreCamOverrides();
// Car reflections: the world render (0x4D7985) updates the reflection cubemap
// of the view's car from the car's physics body position - the raw newest step,
// while the car itself is drawn blended. The probe then jumps in 60 Hz steps
// against the smoothly moving car. Give it the same blended position.
namespace hp2 { const DWORD callReflUpdate = 0x4D7985; const DWORD fnReflUpdate = 0x53E7E0; }
extern unsigned long g_ipReflBlended, g_ipReflPass;
extern unsigned long g_ipReflCamBlended;
void InstallInterpolation();
