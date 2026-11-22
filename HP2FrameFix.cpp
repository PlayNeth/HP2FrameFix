// HP2FrameFix.asi
// Frame pacing, precise game timing, smoother animation, interpolation, HD car
// reflections, widescreen, memory pools and fixes for Need for Speed: Hot
// Pursuit 2 (PC). Version history: CHANGELOG.md.
// Widescreen section: based on HP2WSFix (MIT, Lovro Pleše) - see that section.
// HD car reflections section: GPL-3.0 (derived from NFSHP2 HD Reflections by
// Arctic94 / NFS HD Reflections by Aero_) - see the "HD car reflections" section.
//
// Addresses below were found by disassembling NFSHP2.exe
// (SHA-1 63147101c2ca814e964f31c06730b1292093d7f4, PE timestamp 0x3DDDE96C).
// Every patch checks the original bytes first and disables itself on a
// different exe, so the ASI is still safe to load on other versions.
//
// What this ASI does:
//  - Frame limiter: deadline-based, on a high-resolution waitable timer
//    (enableLimiter, targetFPS); timeBeginPeriod(1); hiResGameTimers answers
//    the game's GetTickCount / timeGetTime from QueryPerformanceCounter.
//  - preciseGameTicks: the game simulates on a 120 Hz tick thread driven by a
//    chain of one-shot timeSetEvent timers (callback 0x5DBA10 -> SetEvent ->
//    tick thread increments 0x84C38C and runs the sim handlers), delivered late
//    and unevenly. A high-resolution scheduler replaces it; with a frame cap
//    that divides the tick rate the ticks are phase-locked to the frame
//    schedule (tickPhase). Re-locking never fires a tick earlier than one full
//    tick after the last, and ticks free-run while the limiter keeps missing
//    its deadlines, so the simulation never runs fast.
//  - fixEAXOnStartup (any audio mode): the sound engine reads the mode only
//    when it starts, before the saved profile is applied; the profile apply
//    (0x536BA0) copies the mode but never restarts the engine (the options menu
//    does, 0x446360). If a loaded profile changes the mode of the running
//    engine, it is restarted once.
//  - speedUpLoading: every load step (Progress::Advance, 0x479C70) renders and
//    presents a loading-screen frame; it is drawn at most every
//    loadScreenRenderMs, and the limiter is bypassed while loading.
//  - smoothDriverSteering: the steering-wheel pose (VehicleDriver, 0x42E7C8)
//    updated every 4th physics step; it updates every step with a quarter of
//    the step (0.025 instead of the shared 0.1).
//  - smoothSlowMotionAnims: AnimChannelSet::Advance (0x545030) advances by
//    fractional steps taken from race time instead of whole 60 Hz steps, so
//    characters animate every frame in slow motion too (bust camera, 0.25x).
//  - fixDriverShiftAnim: the clip selector (0x42E6B0) ends a shift when the
//    channel set's "looped" flag is set; the flag is a latch (set when a clip
//    wraps, cleared only when the selector starts a new clip). A gear change
//    during a shift extends it; the hand returns ~0.5 s after the last change.
//  - smoothCharacterAnims: the world update (0x4D00A0) runs character actors
//    only on odd steps ("and bl, 1"); their UpdateAll (0x406FB0) also runs on
//    even steps, hooked at 0x4D0217.
//  - smoothParticles: the particle/debris pool (0x6D42B0) integrates in half
//    steps in 0x41D760; it moves every physics step at the same speed. Its
//    collision/limit pass (0x41D020) keeps its rate.
//  - smoothProps: knocked props (rigid bodies from the pool at 0x6E27A0) are
//    synced (0x4D8850 -> 0x433900) and integrated (0x444440, dt 1/60) every
//    step, and re-filed into the grid cells the renderer draws (0x444F80).
//    Collisions keep their odd-step rate.
//  - interpolate: racing cars, the camera and the reflection probe are drawn
//    between the 60 Hz physics steps (see the Interpolation section).
//  - hdReflections: see the HD car reflections section.
//  - widescreen: see the Widescreen section.
//  - memoryPools: see the Memory pools section.

#include "src/common.h"
#include "src/config.h"
#include "src/log.h"
#include "src/helpers.h"
#include "src/timing.h"
#include "src/loading.h"
#include "src/portable.h"
#include "src/audio.h"
#include "src/ticks.h"
#include "src/animation.h"
#include "src/interp.h"
#include "src/stats.h"
#include "src/d3d8.h"
#include "src/reflections.h"
#include "src/widescreen.h"
#include "src/memory.h"

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------
BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID reserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        InitializeCriticalSection(&g_logLock);
        g_logLockInit = true;
        QueryPerformanceFrequency(&g_qpcFreq);
        LoadConfig();
        OpenLog();

        g_knownExe = CheckExeSignatures();

        if (g_cfg.raiseTimerResolution) {
            g_timerResRaised = (timeBeginPeriod(1) == TIMERR_NOERROR);
            Logf("timeBeginPeriod(1): %s\n", g_timerResRaised ? "OK" : "FAILED");
        }
        PatchIAT_Timers();
        InstallLoadingPatch();
        InstallAudioModeFix();
        InstallSaveFolder();
        InstallPortableRegistry();
        InstallTickPatch();
        InstallSteeringPatch();
        InstallAnimClockPatch();
        InstallPropsPatch();
        InstallParticlesPatch();
        InstallCharacterAnimPatch();
        InstallInterpolation();
        InstallReflections();
        InstallWidescreen();
        InstallMemoryPools();

        if (PatchIAT_Direct3DCreate8())
            Logf("D3D IAT patch OK\n");
        else
            Logf("D3D IAT patch FAILED - Direct3DCreate8 not found in exe imports. "
                 "Limiter inactive.\n");
    } else if (reason == DLL_PROCESS_DETACH) {
        if (reserved) g_logLockInit = false;   // process exit: don't take locks
        if (g_log) {
            LogStats();
            Logf("=== shutdown ===\n");
            fclose(g_log);
            g_log = nullptr;
        }
        if (g_waitTimer) CloseHandle(g_waitTimer);
        if (g_timerResRaised) timeEndPeriod(1);
    }
    return TRUE;
}
