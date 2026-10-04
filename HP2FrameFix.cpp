// HP2FrameFix.asi  v0.17.7
// Widescreen section: based on HP2WSFix (MIT, Lovro Pleše) - see that section.
// HD car reflections section: GPL-3.0 (derived from NFSHP2 HD Reflections by
// Arctic94 / NFS HD Reflections by Aero_) - see the "HD car reflections" section.
// Frame pacing, game clock fix, EAX-on-startup fix and loading speed-up
// for Need for Speed: Hot Pursuit 2 (PC)
//
// Addresses below were found by disassembling NFSHP2.exe
// (SHA-1 63147101c2ca814e964f31c06730b1292093d7f4, PE timestamp 0x3DDDE96C).
// Every patch checks the original bytes first and disables itself on a
// different exe, so the ASI is still safe to load on other versions.
//
// v0.17.7: Hor+ reworked: each 3D view keeps the look it had on a 4:3 screen
//    (its viewport clipped to 4:3 vs now) - whatever aspect it passes.
// v0.17.6: showroom cars were stretched sideways: their viewport is wider than
//    4:3 once the menus are centered - big 3D views of any shape get Hor+.
// v0.17.5: v0.17.4's FMV column crop removed (its viewport stayed set for the race).
// v0.17.3: centered text was off to the right: the text measuring code converts
//    a size with the screen->layout function, which must not subtract the
//    centering offset there. That call gets a scale-only version.
// v0.17.2: the minimap sits in the HUD root's parent (<HUD>.GWidget), not in
//    HudView - that container's side elements go to the edges too.
// v0.17.1: the minimap (made by code, so never seen loading) goes to the left edge
//    like the rest of the HUD; wsLog=2 logs every widget drawn.
// v0.17: widescreen support (widescreen and ws* keys), reworked from HP2WSFix:
//    centered undistorted menus/HUD at any aspect, full-screen backgrounds
//    filled, HUD side elements at the screen edges, Hor+ 3D FOV, menus at the
//    in-game resolution, aspect-correct FMVs, all display modes listed.
// v0.16.3: the blended camera now stays in the camera object for the whole frame
//    render instead of only during BuildView. Each car copies the camera eye in
//    CarSetXform (0x42B940 -> car+0xB60) for its sun specular, so the glints
//    were computed from the raw step camera - up to a step away at speed. The
//    raw camera is put back before the next physics step and at Present.
// v0.16.2: v0.16.1 also swapped the body rotation during the cubemap update;
//    the game caches the car's draw transform there, so the player car was
//    drawn unblended (cars mismatch ~1 per frame). Removed; camera part kept.
// v0.16.1: sun glints / reflections on the car no longer wobble with interpolation.
//    The cubemap update (0x53E380) aims the sphere map at the camera eye (read
//    through the view, 0x40CFD0) and, in one mode, along the body rotation
//    (body+0x2B4) - both were the raw physics-step values while the car and
//    view were drawn blended. The update now sees the blended camera and body.
// v0.16: HD car reflections built in (hdReflections and refl* keys), including
//    the fix for the cubemap's alternating halves (seam flicker).
// v0.15.6: the car reflection probe (cubemap update, 0x4D7985) gets the car's
//    blended position too; it used the raw 60 Hz physics position, so the
//    reflections stepped against the smoothly drawn car.
// v0.15.5: shift animation pacing reworked - every gear change is animated
//    again (v0.15.4 skipped quick back-and-forth shifts, like the vanilla game
//    skipping them). A change during a shift extends it: the hand stays at the
//    shifter and returns ~0.5 s after the last change, instead of queueing.
// v0.15.4: gear changes during a shift folded into it; quick back-and-forth
//    shifts not animated (replaced in v0.15.5).
// v0.15.3: fixed the driver sometimes repeating the gear-shift animation forever.
//    The clip selector (0x42E6B0) ends a shift when the anim's "looped" flag is
//    set; with fractional animation advances a later small advance could clear
//    the flag before the selector looked. The flag is now a latch: set when a
//    clip wraps, cleared only when the selector starts a new clip.
// v0.15.2-interp: a camera whose view direction turns > 45 deg (or rolls, or
//    whose fov jumps) within one step counts as a cut - look back on cameras
//    that flip the same camera object was blended through the car.
// v0.15.1-interp: a frame whose camera can't be blended (look back / camera
//    switch / jump) now draws the cars un-blended too - they were a step away
//    from the camera for a frame, which flickered when looking back.
// v0.15 (based on v0.13.2):
//  - Interpolation: racing cars and the camera are drawn between the game's
//    60 Hz physics steps, so motion is smooth at any frame rate above 60.
//    After every WorldStep (0x4D0370) the physics bodies of the cars drawn last
//    frame (vehicle+0x6E8: pos +0x2A8, 3x3 +0x2B4) and each view's active camera
//    (eye/target/fov/up) are snapshot. At render time CarModel::SetTransform
//    (0x42B940, called from 0x4CA860 with the body's pose) and BuildView
//    (0x40D620) get values blended between the snapshots around "display time
//    minus interpDelay steps"; the camera is restored right after the view is
//    built. Cars/cameras that don't match the newest snapshot (replays, resets,
//    cameras changed outside a step) are drawn exactly as the game had them.
//    The log counts car draws through two other paths (list B, sim-placed
//    cars) that are not blended yet.
//
// Changes from v0.13.1:
//  - Fixed the game speeding up when the frame cap can't be reached (e.g. a
//    480 cap running at ~270 fps). Every missed deadline re-anchored the
//    limiter, and every re-anchor let the tick scheduler fire the next tick as
//    early as half a tick after the previous one - with re-anchors every few
//    ms the 120 Hz sim ran up to ~2x. Now: re-locking never fires a tick
//    earlier than one full tick after the last; free-running ticks keep their
//    cadence across schedule changes; and when the limiter keeps missing its
//    deadlines the ticks free-run until frames are on time again (2 s).
//
// Changes from v0.13:
//  - Fixed knocked props flickering: the prop sync (0x433900) moves each prop
//    onto a pending list (0x444F20); the renderer only draws objects that are
//    in a grid cell, and the game re-files the pending list (0x444F80) right
//    after its own sync. The even-step sync now does the same.
//
// Changes from v0.12:
//  - smoothProps now targets the real knockable props. When hit, a prop gets a
//    rigid body from a 40-entry pool (0x6E27A0). Its rendered transform is
//    synced from the body (0x433900) only on odd physics steps, and bodies in
//    the simplified integrator (0x444440, dt = 1/30) also only step on odd
//    steps. The sync loop (0x4D8850) now also runs on even steps, and the
//    simplified integrator runs every step with dt = 1/60. Collisions keep
//    their original odd-step rate.
//  - The v0.12 patch (0x41D760) turned out to be the particle/debris pool
//    (smoke, sparks, crash debris); it is kept as smoothParticles.
//
// Changes from v0.11:
//  - smoothParticles (was mislabelled smoothProps): the particle/debris pool
//    at 0x6D42B0 integrates its motion in 0x41D760 using
//    "half steps" (stepCounter >> 1), i.e. 30 Hz. The two halvings are removed
//    and the 0.5 scale becomes 0.25, so they move every physics step at the
//    same speed. Their collision/limit pass (0x41D020) keeps its original rate.
//
// Changes from v0.10:
//  - smoothCharacterAnims: the per-physics-step world update (0x4D00A0) only
//    runs character actors (drivers, cops, dog, the busted driver) on odd
//    steps - "and bl, 1" - so every character animated at 30 Hz. The ASI now
//    also runs the actor UpdateAll (0x406FB0) on even steps, hooked at the
//    call at 0x4D0217. Other odd-step-only systems (particles etc.) keep their
//    original rate. Animation advances by elapsed steps, so speed is unchanged.
//  - Removed the experimental camera probe.
//
// Changes from v0.9:
//  - Animation clock rewrite (AnimChannelSet::Advance, 0x545030). Character
//    animations advanced in whole 60 Hz physics steps. Slow-motion cameras
//    (e.g. the bust sequence) scale game time down to 0.25x, so the step
//    counter - and every animation - only moved 15 times a second.
//    smoothSlowMotionAnims advances channels by fractional steps taken from
//    race time, so characters animate every frame at any game speed.
//  - fixDriverShiftAnim: the driver's clip selector (0x42E6B0) treats the
//    shift animation as finished when the channel set's "looped" flag is set.
//    Advance left that flag untouched when no time had passed, so a stale
//    flag (often from the steering clip) cancelled freshly started shift
//    animations - the hand never left the wheel. The flag is now cleared
//    whenever an update advances no time.
//  - Log records game-speed (time scale) changes.
//
// Changes from v0.8:
//  - smoothDriverSteering: the driver's steering-wheel pose (VehicleDriver,
//    0x42E7C8) only updated every 4th 60 Hz physics step, i.e. 15 Hz, moving
//    up to 0.1 of the range per update. It now updates every physics step
//    with the step scaled down (0.025), so the wheel turns at the same speed
//    but at 60 Hz. The shared 0.1 constant is left alone.
//
// Changes from v0.7.2:
//  - preciseGameTicks: the game simulates on a 120 Hz tick thread driven by a
//    chain of one-shot timeSetEvent timers (callback 0x5DBA10 -> SetEvent ->
//    tick thread increments 0x84C38C and runs the sim handlers). Windows'
//    multimedia timer delivers those callbacks late and unevenly, so some
//    rendered frames get 1 or 3 sim ticks instead of 2 (visible judder at a
//    steady 60 fps). The ASI now replaces that timer with its own
//    high-resolution scheduler. When the limiter runs at a rate that divides
//    the tick rate (60, 30, 120...), ticks are phase-locked to the frame
//    schedule so every frame gets exactly the same number of ticks.
//  - Stats now include a ticks-per-frame histogram (e.g. 2:600 = perfect).
//  - Correction: NFSHP2.exe does import Sleep (an earlier import listing was
//    truncated); it is not involved in the loading or timing issues.
//
// Changes from v0.7.1:
//  - Removed skipRapidPresents / rapidThresholdMs. Dropping presents that came
//    soon after the previous one discarded real frames (the check ran before
//    the limiter's wait), causing stutter; the limiter and clock fix cover it.
//
// Changes from v0.7:
//  - HP2FrameFix.ini and HP2FrameFix.log now live next to the .asi (e.g. in a
//    scripts\ folder) instead of next to the game exe.
//
// Changes from v0.6:
//  - EAX fix. The game only enables EAX when the audio system (re)starts and
//    only if the audio-mode option is 3 (EAX) at that moment. The menu calls
//    RestartAudio() (0x446360) when the mode changes; nothing calls it after the
//    saved profile is applied at startup. fixEAXOnStartup detects "option says
//    EAX, engine has EAX off" on the front end and calls RestartAudio() once,
//    which is exactly what the manual Stereo -> EAX toggle does.
//  - Loading speed-up. Every load step calls Progress::Advance() (0x479C70),
//    which renders and presents a full loading-screen frame each time, so load
//    time is bounded by frame time. The render call is redirected so the screen
//    is drawn at most every loadScreenRenderMs; the limiter is also bypassed
//    while loading. Per-load stats are logged.
//  - Removed the Sleep hooks (no effect on loading).
//
// Changes from v0.5:
//  - timeBeginPeriod(1) always raised; hiResGameTimers answers the game's
//    GetTickCount / timeGetTime from QueryPerformanceCounter.
//
// Changes from v0.4:
//  - Deadline-based limiter with high-resolution waitable timer; stats average
//    fixed; dead EAX code removed; checked VirtualProtect; serialized logging.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include <d3d8.h>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

#define HP2FF_VERSION "0.17.7"

// ---------------------------------------------------------------------------
// Game addresses (NFSHP2.exe, image base 0x400000, no relocations)
// ---------------------------------------------------------------------------
namespace hp2 {
    // Globals
    const DWORD pApp            = 0x83C024;  // App*; +0x48 options, +0x50 audio subsystem
    const DWORD optAudioModePtr = 0x19154;   // options + this -> int* (option 0x12 = audio mode)
    const DWORD bEaxActive      = 0x84ECE1;  // BYTE, set by sound lib when EAX listener is live
    const DWORD bSoundReady     = 0x7930D4;  // DWORD, non-zero once the sound system is up
    const int   kAudioModeEAX   = 3;

    // Functions
    const DWORD fnRestartAudio  = 0x446360;  // void(), same as changing audio mode in the menu
    const DWORD fnProgAdvance   = 0x479C70;  // Progress::Advance (thiscall)
    const DWORD callLoadRender  = 0x479CCE;  // E8 call -> fnRenderScreen inside Advance
    const DWORD fnRenderScreen  = 0x598920;  // int __cdecl(Screen*)

    // 120 Hz simulation ticker
    const DWORD fnTickCallback  = 0x5DBA10;  // TIMECALLBACK, reschedules itself via timeSetEvent
    const DWORD pTickCounter    = 0x84C38C;  // DWORD, incremented once per sim tick
    const DWORD pTickRate       = 0x84C388;  // int, ticks per second (120)

    struct Sig { DWORD addr; const char* bytes; int len; };
    const Sig kSigs[] = {
        { fnRestartAudio, "\x83\x3D\xE0\x0B\x7A\x00\xFC", 7 },
        { 0x5F2E36,       "\xC6\x05\xE1\xEC\x84\x00\x01", 7 },             // EAX flag set
        { 0x536270,       "\x55\x8B\xEC\x8B\x45\x08\x8B\x8C\x81\x0C\x91\x01\x00", 13 }, // GetOption
        { fnProgAdvance,  "\x8B\x11\x56\x8B\x71\x04\x46\x8B", 8 },
        { callLoadRender, "\xE8\x4D\xEC\x11\x00", 5 },
        { fnTickCallback, "\xA1\x44\xCC\x84\x00\x8B\x0D\x50\xCC\x84\x00", 11 },
        { 0x5DBE22,       "\xA1\x8C\xC3\x84\x00\x40\xA3\x8C\xC3\x84\x00", 11 }, // tick++
        { 0x42E7DC,       "\x83\xC2\x03", 3 },                         // steer: every 4 steps
        { 0x42E7F9,       "\xD8\x05\x24\xD8\x65\x00", 6 },            // fadd [0.1]
        { 0x42E810,       "\xD8\x25\x24\xD8\x65\x00", 6 },            // fsub [0.1]
        { 0x545030,       "\x55\x8B\xEC\x51\x8A\x41\x08\x53\x32\xDB", 10 }, // AnimChannelSet::Advance
        { 0x54504E,       "\x8B\x15\xDC\x21\x72\x00", 6 },            // mov edx,[stepCounter]
        { 0x4D00A9,       "\x8A\x5D\x08\xB1\x01\x22\xD9", 7 },         // bl = step & 1
        { 0x4D0217,       "\xE8\x34\xA6\xFF\xFF", 5 },                 // call 0x4CA850
        { 0x4D021C,       "\x84\xDB\x74\x35", 4 },                      // test bl,bl / je skip
        { 0x42BEEE,       "\xE8\xBD\x27\x00\x00", 5 },                 // call selector (in CarModel update)
        { 0x42E713,       "\x0F\xB6\x81\x30\x06\x00\x00", 7 },       // gear = byte [car+0x630]
        { 0x42E71F,       "\x89\x46\x1C", 3 },                           // last animated gear [sel+0x1C]
        { 0x42E6FA,       "\x8A\x47\x3E", 3 },                           // selector reads looped (+0x34+0xA)
        { 0x42E761,       "\xE8\xDA\x3F\xFD\xFF", 5 },                 // selector PlayAnim calls (6)
        { 0x42E775,       "\xE8\xC6\x3F\xFD\xFF", 5 },
        { 0x42E795,       "\xE8\xA6\x3F\xFD\xFF", 5 },
        { 0x42E7A9,       "\xE8\x92\x3F\xFD\xFF", 5 },
        { 0x42E7BD,       "\xE8\x7E\x3F\xFD\xFF", 5 },
        { 0x42E83A,       "\xE8\x01\x3F\xFD\xFF", 5 },
        { 0x406FB0,       "\x8B\x49\x5C\x85\xC9\x74\x05", 7 },         // ActorList::UpdateAll wrapper
        { 0x41D7E1,       "\x8B\xD1\xD1\xFA\x89\x15\xFC\x33\x6D\x00\xD1\xF9", 12 }, // props: half steps
        { 0x41D80B,       "\xD8\x0D\x94\xD6\x65\x00", 6 },            // particles: fmul [0.5]
        { 0x4D53B8,       "\x85\xDB\x74\x09", 4 },                      // simple integrator odd-only gate
        { 0x4D8850,       "\xA1\xA8\x9A\x6E\x00\x57\x33\xFF", 8 },     // sync flying props loop
        { 0x444F80,       "\x55\x8B\xEC\x83\xEC\x18\x56\x8B\x35\xC8\x1A\x70\x00", 13 }, // re-file pending objects
        { 0x444440,       "\x55\x8B\xEC\x83\xEC\x48\x53\x8B\x5D\x08", 10 }, // simple integrator
        { 0x444495,       "\xD8\x0D\x90\xD6\x65\x00", 6 },            // dt 1/30 sites (6)
        { 0x4444A9,       "\xD8\x0D\x90\xD6\x65\x00", 6 },
        { 0x4444B6,       "\xD8\x0D\x90\xD6\x65\x00", 6 },
        { 0x4444DE,       "\xD8\x0D\x90\xD6\x65\x00", 6 },
        { 0x4444F9,       "\xD8\x0D\x90\xD6\x65\x00", 6 },
        { 0x444517,       "\xD8\x0D\x90\xD6\x65\x00", 6 },
        // interpolation (v0.15)
        { 0x4D0370,       "\xE8\x2B\xFD\xFF\xFF", 5 },                 // call WorldStep
        { 0x4CA968,       "\xE8\x13\x0F\xF6\xFF", 5 },                 // call CarModel::SetOwner
        { 0x4CA978,       "\xE8\xC3\x0F\xF6\xFF", 5 },                 // call CarModel::SetTransform
        { 0x4CA8D7,       "\x8B\x88\x70\x01\x00\x00", 6 },            // vehicle = [x+0x170]
        { 0x4CA8E3,       "\x8B\x90\xA8\x02\x00\x00", 6 },            // body pos at +0x2A8
        { 0x4CA960,       "\x8B\xB1\x2C\x0D\x00\x00", 6 },            // CarModel = [veh+0xD2C]
        { 0x4D7734,       "\xE8\xE7\x5E\xF3\xFF", 5 },                 // call BuildView (main)
        { 0x4D7813,       "\xE8\x08\x5E\xF3\xFF", 5 },                 // call BuildView (2nd)
        { 0x40D626,       "\x8B\x81\x64\x01\x00\x00", 6 },            // active camera slot
        { 0x40C269,       "\x39\x7B\x68", 3 },                           // camera id at +0x68
        { 0x40C756,       "\x8D\x41\x04", 3 },                           // eye at cam+8
        { 0x4D7985,       "\xE8\x56\x6E\x06\x00", 5 },                 // call reflection cubemap update
        { 0x4D795A,       "\x8B\x88\xA8\x02\x00\x00", 6 },            // probe pos = body +0x2A8
        { 0x4CAA7F,       "\xE8\x8C\x15\xF6\xFF", 5 },                 // car draw, list B
        { 0x44D6D0,       "\xE8\x3B\xE9\xFD\xFF", 5 },                 // car draw, sim-placed cars
    };
}

// ---------------------------------------------------------------------------
// Config
// ---------------------------------------------------------------------------
struct Config {
    int   targetFPS            = 60;
    int   enableLimiter        = 1;
    int   passthrough          = 0;
    int   raiseTimerResolution = 1;
    int   hiResGameTimers      = 1;
    int   preciseGameTicks     = 1;
    int   smoothDriverSteering = 1;
    int   smoothSlowMotionAnims = 1;
    int   fixDriverShiftAnim   = 1;
    int   animDiag             = 0;
    int   smoothCharacterAnims = 1;
    int   smoothProps          = 1;
    int   smoothParticles      = 1;
    float tickPhase            = 0.75f; // where ticks sit inside each tick slot after a frame deadline
    int   fixEAXOnStartup      = 1;
    int   eaxFixStableFrames   = 90;    // frames the bad state must persist first
    int   speedUpLoading       = 1;
    int   loadScreenRenderMs   = 100;   // 0 = every step (original), -1 = never
    int   limiterDuringLoading = 0;
    int   logToFile            = 1;
    int   logEveryNFrames      = 600;
    // interpolation (v0.15)
    // HD car reflections (v0.16, from NFSHP2 HD Reflections by Arctic94 / Aero_, GPL-3)
    int   hdReflections        = 1;
    int   reflAutoRes          = 1;     // 1080p 512, 1440p 1024, 2160p 2048
    int   reflCubemapRes       = 512;
    int   reflEnviromapRes     = 512;
    int   reflPowerOfTwo       = 0;
    float reflBrightness       = 1.0f;
    int   reflFullDetail       = 1;     // all 6 faces even on lower in-game reflection detail
    int   reflAllFacesEveryFrame = 1;   // no alternating halves (seam flicker)
    int   interpolate          = 1;     // blend cars between 60 Hz physics steps
    int   interpolateCamera    = 1;     // blend the camera too
    float interpDelay          = 1.0f;  // steps behind the newest step that frames are drawn
    float maxStepDistance      = 10.0f; // don't blend a jump bigger than this per step (resets, cuts)
    float maxStepAngle         = 90.0f;
    // widescreen (v0.17)
    int   widescreen           = 1;
    int   wsCenterUI           = 1;     // menus/HUD: uniform scale, centered
    int   wsFixFOV             = 1;     // Hor+ 3D
    int   wsMenuResolution     = 1;     // 1 = in-game resolution, 2 = desktop, 0 = 800x600
    int   wsBackgrounds        = 2;     // 0 = 4:3 with bars, 1 = stretch, 2 = fill (no distortion)
    int   wsHudToEdges         = 1;
    int   wsMovies             = 1;
    int   wsUnlockResolutions  = 1;
    int   wsLog                = 1;
};

static Config g_cfg;
static void LogWidescreenStats();
static bool   g_cfgLoaded = false;
static char   g_gameDir[MAX_PATH] = {};

static char   g_asiDir[MAX_PATH] = {};

// Folder containing this .asi, found from an address inside the module so it
// works no matter where the ASI loader picked it up from.
static void InitAsiDir() {
    if (g_asiDir[0]) return;
    HMODULE self = nullptr;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(&InitAsiDir), &self) &&
        GetModuleFileNameA(self, g_asiDir, MAX_PATH)) {
        char* slash = strrchr(g_asiDir, '\\');
        if (slash) { *(slash + 1) = 0; return; }
    }
    g_asiDir[0] = 0;   // fall back to the current directory
}

static void InitGameDir() {
    if (g_gameDir[0]) return;
    if (!GetModuleFileNameA(nullptr, g_gameDir, MAX_PATH)) { g_gameDir[0] = 0; return; }
    char* slash = strrchr(g_gameDir, '\\');
    if (slash) *(slash + 1) = 0; else g_gameDir[0] = 0;
}

static const char kInterpBlock[] =
    "; --- interpolation: draw cars and camera between the game's 60 Hz physics steps,\n"
    ";     so motion is smooth at any frame rate above 60 ---\n"
    "interpolate=1\n"
    "interpolateCamera=1\n"
    "; how many 1/60 s steps behind the newest step frames are drawn (1.0 = least delay)\n"
    "interpDelay=1.0\n"
    "; don't blend jumps bigger than this per step (resets, teleports, camera cuts)\n"
    "maxStepDistance=10\n"
    "maxStepAngle=90\n";

static const char kReflBlock[] =
    "; --- HD car reflections (from NFSHP2 HD Reflections by Arctic94 / Aero_) ---\n"
    "; sharper car reflections: the reflection cubemap and environment map are 128x128 in the game\n"
    "hdReflections=1\n"
    "; pick the resolution from the display height (1080p = 512, 1440p = 1024, 2160p = 2048)\n"
    "reflAutoRes=1\n"
    "; used when reflAutoRes=0 (128 = game default, 256, 512, 1024, 2048)\n"
    "reflCubemapRes=512\n"
    "reflEnviromapRes=512\n"
    "; round the resolutions down to a power of two (old GPUs)\n"
    "reflPowerOfTwo=0\n"
    "; reflection intensity 0.0 - 1.0 (1.0 = game default)\n"
    "reflBrightness=1.0\n"
    "; render the full dynamic reflection even when the in-game reflection detail is lower\n"
    "reflFullDetail=1\n"
    "; render all 6 cubemap faces every frame (the game alternates halves -> seam flicker at HD)\n"
    "reflAllFacesEveryFrame=1\n";

static const char kWsBlock[] =
    "; --- widescreen (reworked from HP2WSFix by xan1242 / hdbeefup) ---\n"
    "; pick the resolution in the game's Options as usual; everything adapts to its aspect ratio\n"
    "widescreen=1\n"
    "; menus and HUD: draw the 800x600 layout with one scale, centered (no stretching)\n"
    "wsCenterUI=1\n"
    "; 3D view: keep the 4:3 vertical view and show more at the sides (Hor+)\n"
    "wsFixFOV=1\n"
    "; menu resolution: 1 = same as in-game, 2 = desktop, 0 = game default 800x600\n"
    "wsMenuResolution=1\n"
    "; full-screen backgrounds: 0 = 4:3 with side bars, 1 = stretch, 2 = fill without distortion\n"
    "wsBackgrounds=2\n"
    "; in-race HUD: move the elements on the left/right side to the screen edges\n"
    "wsHudToEdges=1\n"
    "; FMVs: scale to fit with their aspect ratio, centered\n"
    "wsMovies=1\n"
    "; list every display mode in the options (the game hides some)\n"
    "wsUnlockResolutions=1\n"
    "; log what the widescreen code adjusts (2 = also every widget drawn, for diagnostics)\n"
    "wsLog=1\n";

static void LoadConfig() {
    if (g_cfgLoaded) return;
    g_cfgLoaded = true;
    InitGameDir();
    InitAsiDir();
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%sHP2FrameFix.ini", g_asiDir);
    FILE* f = fopen(path, "r");
    if (!f) {
        f = fopen(path, "w");
        if (f) {
            fputs(
                "; HP2FrameFix.ini (v" HP2FF_VERSION ")\n"
                "; --- frame limiter ---\n"
                "enableLimiter=1\n"
                "targetFPS=60\n"
                "passthrough=0\n"
                "; --- game clock (fix for duplicated frames on Windows) ---\n"
                "raiseTimerResolution=1\n"
                "hiResGameTimers=1\n"
                "; replace the game's 120 Hz sim timer with a precise, frame-locked one\n"
                "preciseGameTicks=1\n"
                "tickPhase=0.75\n"
                "; update the driver's steering animation every physics step (60 Hz) instead of 15 Hz\n"
                "smoothDriverSteering=1\n"
                "; animate characters every frame even in slow motion (bust sequence)\n"
                "smoothSlowMotionAnims=1\n"
                "; stop the gear-shift animation from being cancelled by a stale flag\n"
                "fixDriverShiftAnim=1\n"
                "; update characters (drivers, cops, dog) every physics step instead of every other\n"
                "smoothCharacterAnims=1\n"
                "; move knocked props (signs, meters, cones) every physics step instead of every other\n"
                "smoothProps=1\n"
                "; move particles/debris (smoke, sparks) every physics step\n"
                "smoothParticles=1\n"
                "; log per-character animation update rates (diagnostics)\n"
                "animDiag=0\n"
                "; --- EAX: re-apply saved EAX mode after startup ---\n"
                "fixEAXOnStartup=1\n"
                "eaxFixStableFrames=90\n"
                "; --- loading ---\n"
                "speedUpLoading=1\n"
                "; draw the loading screen at most every N ms (0 = every step, -1 = never)\n"
                "loadScreenRenderMs=100\n"
                "limiterDuringLoading=0\n"
                "; --- logging ---\n"
                "logToFile=1\n"
                "logEveryNFrames=600\n", f);
            fputs(kInterpBlock, f);
            fputs(kReflBlock, f);
            fputs(kWsBlock, f);
            fclose(f);
        }
        return;
    }
    char line[256];
    bool sawInterp = false, sawRefl = false, sawWs = false;
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == ';' || line[0] == '#' || line[0] == '\r' || line[0] == '\n') continue;
        char key[64] = {}, val[64] = {};
        if (sscanf(line, " %63[^= \t] = %63s", key, val) < 2) continue;
        if      (!_stricmp(key, "targetFPS"))            g_cfg.targetFPS = atoi(val);
        else if (!_stricmp(key, "enableLimiter"))        g_cfg.enableLimiter = atoi(val);
        else if (!_stricmp(key, "passthrough"))          g_cfg.passthrough = atoi(val);
        else if (!_stricmp(key, "raiseTimerResolution")) g_cfg.raiseTimerResolution = atoi(val);
        else if (!_stricmp(key, "hiResGameTimers"))      g_cfg.hiResGameTimers = atoi(val);
        else if (!_stricmp(key, "preciseGameTicks"))     g_cfg.preciseGameTicks = atoi(val);
        else if (!_stricmp(key, "smoothDriverSteering")) g_cfg.smoothDriverSteering = atoi(val);
        else if (!_stricmp(key, "smoothSlowMotionAnims")) g_cfg.smoothSlowMotionAnims = atoi(val);
        else if (!_stricmp(key, "fixDriverShiftAnim"))   g_cfg.fixDriverShiftAnim = atoi(val);
        else if (!_stricmp(key, "animDiag"))             g_cfg.animDiag = atoi(val);
        else if (!_stricmp(key, "smoothCharacterAnims")) g_cfg.smoothCharacterAnims = atoi(val);
        else if (!_stricmp(key, "smoothProps"))          g_cfg.smoothProps = atoi(val);
        else if (!_stricmp(key, "smoothParticles"))      g_cfg.smoothParticles = atoi(val);
        else if (!_stricmp(key, "tickPhase"))            g_cfg.tickPhase = (float)atof(val);
        else if (!_stricmp(key, "fixEAXOnStartup"))      g_cfg.fixEAXOnStartup = atoi(val);
        else if (!_stricmp(key, "eaxFixStableFrames"))   g_cfg.eaxFixStableFrames = atoi(val);
        else if (!_stricmp(key, "speedUpLoading"))       g_cfg.speedUpLoading = atoi(val);
        else if (!_stricmp(key, "loadScreenRenderMs"))   g_cfg.loadScreenRenderMs = atoi(val);
        else if (!_stricmp(key, "limiterDuringLoading")) g_cfg.limiterDuringLoading = atoi(val);
        else if (!_stricmp(key, "logToFile"))            g_cfg.logToFile = atoi(val);
        else if (!_stricmp(key, "logEveryNFrames"))      g_cfg.logEveryNFrames = atoi(val);
        else if (!_stricmp(key, "hdReflections"))        { g_cfg.hdReflections = atoi(val); sawRefl = true; }
        else if (!_stricmp(key, "reflAutoRes"))          g_cfg.reflAutoRes = atoi(val);
        else if (!_stricmp(key, "reflCubemapRes"))       g_cfg.reflCubemapRes = atoi(val);
        else if (!_stricmp(key, "reflEnviromapRes"))     g_cfg.reflEnviromapRes = atoi(val);
        else if (!_stricmp(key, "reflPowerOfTwo"))       g_cfg.reflPowerOfTwo = atoi(val);
        else if (!_stricmp(key, "reflBrightness"))       g_cfg.reflBrightness = (float)atof(val);
        else if (!_stricmp(key, "reflFullDetail"))       g_cfg.reflFullDetail = atoi(val);
        else if (!_stricmp(key, "reflAllFacesEveryFrame")) g_cfg.reflAllFacesEveryFrame = atoi(val);
        else if (!_stricmp(key, "interpolate"))          { g_cfg.interpolate = atoi(val); sawInterp = true; }
        else if (!_stricmp(key, "interpolateCamera"))    g_cfg.interpolateCamera = atoi(val);
        else if (!_stricmp(key, "interpDelay"))          g_cfg.interpDelay = (float)atof(val);
        else if (!_stricmp(key, "maxStepDistance"))      g_cfg.maxStepDistance = (float)atof(val);
        else if (!_stricmp(key, "maxStepAngle"))         g_cfg.maxStepAngle = (float)atof(val);
        else if (!_stricmp(key, "widescreen"))           { g_cfg.widescreen = atoi(val); sawWs = true; }
        else if (!_stricmp(key, "wsCenterUI"))           g_cfg.wsCenterUI = atoi(val);
        else if (!_stricmp(key, "wsFixFOV"))             g_cfg.wsFixFOV = atoi(val);
        else if (!_stricmp(key, "wsMenuResolution"))     g_cfg.wsMenuResolution = atoi(val);
        else if (!_stricmp(key, "wsBackgrounds"))        g_cfg.wsBackgrounds = atoi(val);
        else if (!_stricmp(key, "wsHudToEdges"))         g_cfg.wsHudToEdges = atoi(val);
        else if (!_stricmp(key, "wsMovies"))             g_cfg.wsMovies = atoi(val);
        else if (!_stricmp(key, "wsUnlockResolutions"))  g_cfg.wsUnlockResolutions = atoi(val);
        else if (!_stricmp(key, "wsLog"))                g_cfg.wsLog = atoi(val);
    }
    fclose(f);
    if (!sawInterp) {                     // ini from an older version: add the new keys
        FILE* a = fopen(path, "a");
        if (a) { fputs("\n", a); fputs(kInterpBlock, a); fclose(a); }
    }
    if (!sawRefl) {
        FILE* a = fopen(path, "a");
        if (a) { fputs("\n", a); fputs(kReflBlock, a); fclose(a); }
    }
    if (!sawWs) {
        FILE* a = fopen(path, "a");
        if (a) { fputs("\n", a); fputs(kWsBlock, a); fclose(a); }
    }
}

// ---------------------------------------------------------------------------
// Logging (serialized across threads)
// ---------------------------------------------------------------------------
static FILE* g_log = nullptr;
static CRITICAL_SECTION g_logLock;
static bool g_logLockInit = false;

static void Logf(const char* fmt, ...) {
    if (!g_log) return;
    if (g_logLockInit) EnterCriticalSection(&g_logLock);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fflush(g_log);
    if (g_logLockInit) LeaveCriticalSection(&g_logLock);
}

static void OpenLog() {
    if (!g_cfg.logToFile || g_log) return;
    InitGameDir();
    InitAsiDir();
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%sHP2FrameFix.log", g_asiDir);
    g_log = fopen(path, "a");
    Logf("\n=== HP2FrameFix v" HP2FF_VERSION " started ===\n");
    Logf("asi folder: %s\ngame folder: %s\n", g_asiDir[0] ? g_asiDir : "(current dir)", g_gameDir);
    Logf("limiter=%d fps=%d passthrough=%d timerRes=%d hiResTimers=%d\n",
         g_cfg.enableLimiter, g_cfg.targetFPS, g_cfg.passthrough,
         g_cfg.raiseTimerResolution, g_cfg.hiResGameTimers);
    Logf("preciseGameTicks=%d tickPhase=%.2f smoothDriverSteering=%d smoothSlowMotionAnims=%d "
         "fixDriverShiftAnim=%d animDiag=%d smoothCharacterAnims=%d smoothProps=%d smoothParticles=%d\n", g_cfg.preciseGameTicks, g_cfg.tickPhase,
         g_cfg.smoothDriverSteering, g_cfg.smoothSlowMotionAnims, g_cfg.fixDriverShiftAnim,
         g_cfg.animDiag, g_cfg.smoothCharacterAnims, g_cfg.smoothProps, g_cfg.smoothParticles);
    Logf("fixEAX=%d speedUpLoading=%d loadScreenRenderMs=%d limiterDuringLoading=%d\n",
         g_cfg.fixEAXOnStartup, g_cfg.speedUpLoading, g_cfg.loadScreenRenderMs,
         g_cfg.limiterDuringLoading);
    Logf("interpolate=%d interpolateCamera=%d interpDelay=%.2f maxStepDistance=%.1f maxStepAngle=%.0f\n",
         g_cfg.interpolate, g_cfg.interpolateCamera, g_cfg.interpDelay, g_cfg.maxStepDistance,
         g_cfg.maxStepAngle);
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static bool PatchPointer(void** slot, void* newValue, void** oldValue) {
    DWORD oldProt;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &oldProt)) return false;
    if (oldValue) *oldValue = *slot;
    *slot = newValue;
    DWORD dummy;
    VirtualProtect(slot, sizeof(void*), oldProt, &dummy);
    return true;
}

static bool PatchCallRel32(DWORD at, void* target) {
    BYTE* p = reinterpret_cast<BYTE*>(at);
    DWORD oldProt;
    if (!VirtualProtect(p, 5, PAGE_EXECUTE_READWRITE, &oldProt)) return false;
    p[0] = 0xE8;
    *reinterpret_cast<DWORD*>(p + 1) = (DWORD)target - (at + 5);
    DWORD dummy;
    VirtualProtect(p, 5, oldProt, &dummy);
    FlushInstructionCache(GetCurrentProcess(), p, 5);
    return true;
}

template <typename Fn>
static void ForEachImport(const char* dllName, Fn cb) {
    HMODULE hMod = GetModuleHandleA(nullptr);
    if (!hMod) return;
    auto base = reinterpret_cast<BYTE*>(hMod);
    auto dos = reinterpret_cast<PIMAGE_DOS_HEADER>(base);
    auto nt  = reinterpret_cast<PIMAGE_NT_HEADERS>(base + dos->e_lfanew);
    DWORD importRva = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    if (!importRva) return;
    auto imp = reinterpret_cast<PIMAGE_IMPORT_DESCRIPTOR>(base + importRva);
    for (; imp->Name; ++imp) {
        if (_stricmp(reinterpret_cast<const char*>(base + imp->Name), dllName) != 0) continue;
        auto thunk = reinterpret_cast<PIMAGE_THUNK_DATA>(base + imp->FirstThunk);
        auto orig  = reinterpret_cast<PIMAGE_THUNK_DATA>(base +
                     (imp->OriginalFirstThunk ? imp->OriginalFirstThunk : imp->FirstThunk));
        for (; thunk->u1.Function; ++thunk, ++orig) {
            if (IMAGE_SNAP_BY_ORDINAL(orig->u1.Ordinal)) continue;
            auto byName = reinterpret_cast<PIMAGE_IMPORT_BY_NAME>(base + orig->u1.AddressOfData);
            cb(reinterpret_cast<void**>(&thunk->u1.Function),
               reinterpret_cast<const char*>(byName->Name));
        }
    }
}

template <typename T>
static bool SafeRead(DWORD addr, T* out) {
    if (!addr || IsBadReadPtr(reinterpret_cast<void*>(addr), sizeof(T))) return false;
    *out = *reinterpret_cast<T*>(addr);
    return true;
}

static bool g_knownExe = false;

static bool CheckExeSignatures() {
    if (GetModuleHandleA(nullptr) != reinterpret_cast<HMODULE>(0x400000)) {
        Logf("exe check: unexpected image base, game-specific fixes disabled\n");
        return false;
    }
    for (const auto& s : hp2::kSigs) {
        if (IsBadReadPtr(reinterpret_cast<void*>(s.addr), s.len) ||
            memcmp(reinterpret_cast<void*>(s.addr), s.bytes, s.len) != 0) {
            Logf("exe check: bytes at 0x%06lX differ - not the analysed NFSHP2.exe, "
                 "EAX and loading fixes disabled\n", (unsigned long)s.addr);
            return false;
        }
    }
    Logf("exe check: NFSHP2.exe matches, game-specific fixes available\n");
    return true;
}

// ---------------------------------------------------------------------------
// Timing
// ---------------------------------------------------------------------------
static LARGE_INTEGER g_qpcFreq = {};
static HANDLE g_waitTimer = nullptr;
static bool   g_timerResRaised = false;

static double QpcToMs(LONGLONG delta) {
    if (g_qpcFreq.QuadPart == 0) return 0.0;
    return (double)delta * 1000.0 / (double)g_qpcFreq.QuadPart;
}
static LONGLONG QpcNow() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }

// Game clock hooks: GetTickCount / timeGetTime backed by QPC
typedef DWORD (WINAPI *GetTickCount_t)(void);
typedef DWORD (WINAPI *timeGetTime_t)(void);
static GetTickCount_t g_origGetTickCount = nullptr;
static timeGetTime_t  g_origTimeGetTime  = nullptr;
static std::atomic<unsigned long> g_gtcCalls{0}, g_tgtCalls{0};
static LONGLONG g_clockQpcStart = 0;
static DWORD    g_gtcBase = 0, g_tgtBase = 0;

static DWORD HiResMs(DWORD base) {
    LONGLONG d = QpcNow() - g_clockQpcStart;
    LONGLONG f = g_qpcFreq.QuadPart;
    LONGLONG ms = (d / f) * 1000 + ((d % f) * 1000) / f;
    return base + (DWORD)ms;
}

static DWORD WINAPI Hook_GetTickCount() {
    g_gtcCalls++;
    if (g_cfg.hiResGameTimers) return HiResMs(g_gtcBase);
    return g_origGetTickCount ? g_origGetTickCount() : ::GetTickCount();
}

static DWORD WINAPI Hook_timeGetTime() {
    g_tgtCalls++;
    if (g_cfg.hiResGameTimers) return HiResMs(g_tgtBase);
    return g_origTimeGetTime ? g_origTimeGetTime() : ::timeGetTime();
}

static void PatchIAT_Timers() {
    g_clockQpcStart = QpcNow();
    g_gtcBase = ::GetTickCount();
    g_tgtBase = ::timeGetTime();
    bool gtc = false, tgt = false;
    ForEachImport("kernel32.dll", [&](void** slot, const char* name) {
        void* old = nullptr;
        if (!strcmp(name, "GetTickCount") && PatchPointer(slot, (void*)&Hook_GetTickCount, &old)) {
            g_origGetTickCount = reinterpret_cast<GetTickCount_t>(old);
            gtc = true;
        }
    });
    ForEachImport("winmm.dll", [&](void** slot, const char* name) {
        void* old = nullptr;
        if (!strcmp(name, "timeGetTime") && PatchPointer(slot, (void*)&Hook_timeGetTime, &old)) {
            g_origTimeGetTime = reinterpret_cast<timeGetTime_t>(old);
            tgt = true;
        }
    });
    Logf("game clock hooks: GetTickCount=%s timeGetTime=%s\n",
         gtc ? "hooked" : "not imported", tgt ? "hooked" : "not imported");
}

static void InitWaitTimer() {
    static bool done = false;
    if (done) return;
    done = true;
    typedef HANDLE (WINAPI *CWTEx_t)(LPSECURITY_ATTRIBUTES, LPCWSTR, DWORD, DWORD);
    auto pCreate = reinterpret_cast<CWTEx_t>((void*)
        GetProcAddress(GetModuleHandleA("kernel32.dll"), "CreateWaitableTimerExW"));
    if (pCreate)
        g_waitTimer = pCreate(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                              TIMER_ALL_ACCESS);
    if (!g_waitTimer && !g_timerResRaised) {
        timeBeginPeriod(1);
        g_timerResRaised = true;
    }
    Logf("wait method: %s\n", g_waitTimer ? "high-resolution waitable timer"
                                          : "Sleep with timeBeginPeriod(1)");
}

static void WaitUntil(LONGLONG deadline) {
    LONGLONG now = QpcNow();
    double remainingMs = QpcToMs(deadline - now);
    if (g_waitTimer) {
        if (remainingMs > 1.0) {
            LARGE_INTEGER due;
            due.QuadPart = -(LONGLONG)((remainingMs - 0.5) * 10000.0);
            if (SetWaitableTimer(g_waitTimer, &due, 0, nullptr, nullptr, FALSE))
                WaitForSingleObject(g_waitTimer, INFINITE);
        }
    } else if (remainingMs > 2.0) {
        ::Sleep((DWORD)(remainingMs - 1.5));
    }
    do { YieldProcessor(); now = QpcNow(); } while (now < deadline);
}

// ---------------------------------------------------------------------------
// Loading speed-up: throttle the loading-screen render inside Progress::Advance
// ---------------------------------------------------------------------------
typedef int (__cdecl *RenderScreen_t)(void* screen);
static RenderScreen_t g_origRenderScreen = reinterpret_cast<RenderScreen_t>(hp2::fnRenderScreen);

static LONGLONG g_loadLastStepQpc   = 0;
static LONGLONG g_loadStartQpc      = 0;
static LONGLONG g_loadLastRenderQpc = 0;
static bool     g_loadActive        = false;
static unsigned long g_loadSteps = 0, g_loadRenders = 0, g_loadPresents = 0;
static double   g_loadRenderMs = 0.0;
static int      g_loadCount = 0;

static bool LoadingNow(LONGLONG now) {
    return g_loadActive && QpcToMs(now - g_loadLastStepQpc) < 500.0;
}

static int __cdecl Hook_LoadRender(void* screen) {
    LONGLONG now = QpcNow();
    if (!g_loadActive) {
        g_loadActive = true;
        g_loadStartQpc = now;
        g_loadSteps = g_loadRenders = g_loadPresents = 0;
        g_loadRenderMs = 0.0;
        g_loadLastRenderQpc = 0;
    }
    g_loadLastStepQpc = now;
    g_loadSteps++;

    bool render = true;
    if (g_cfg.speedUpLoading) {
        if (g_cfg.loadScreenRenderMs < 0)
            render = false;
        else if (g_cfg.loadScreenRenderMs > 0 && g_loadLastRenderQpc &&
                 QpcToMs(now - g_loadLastRenderQpc) < (double)g_cfg.loadScreenRenderMs)
            render = false;
    }
    if (!render) return 0;   // original returns 0 on success; caller ignores it

    g_loadLastRenderQpc = now;
    g_loadRenders++;
    int r = g_origRenderScreen(screen);
    g_loadRenderMs += QpcToMs(QpcNow() - now);
    return r;
}

static void EndLoadIfIdle(LONGLONG now) {
    if (!g_loadActive || QpcToMs(now - g_loadLastStepQpc) < 1000.0) return;
    g_loadActive = false;
    g_loadCount++;
    double total = QpcToMs(g_loadLastStepQpc - g_loadStartQpc);
    Logf("load #%d: %lu steps, %lu screen renders (%.0f ms), %lu presents, "
         "first->last step %.0f ms\n",
         g_loadCount, g_loadSteps, g_loadRenders, g_loadRenderMs, g_loadPresents, total);
}

static void InstallLoadingPatch() {
    if (!g_knownExe) return;
    if (PatchCallRel32(hp2::callLoadRender, (void*)&Hook_LoadRender))
        Logf("loading: render call in Progress::Advance redirected (renderMs=%d, speedUp=%d)\n",
             g_cfg.loadScreenRenderMs, g_cfg.speedUpLoading);
    else
        Logf("loading: patch FAILED (VirtualProtect)\n");
}

// ---------------------------------------------------------------------------
// EAX-on-startup fix
// ---------------------------------------------------------------------------
static bool g_eaxFixDone = false;
static int  g_eaxBadFrames = 0;
static int  g_eaxLastMode = -99, g_eaxLastActive = -1, g_eaxLastReady = -1;
static bool g_inRestartAudio = false;

static bool ReadAudioState(int* mode, int* eaxActive, int* soundReady) {
    DWORD app = 0, options = 0, modePtr = 0;
    if (!SafeRead(hp2::pApp, &app) || !app) return false;
    if (!SafeRead(app + 0x48, &options) || !options) return false;
    if (!SafeRead(options + hp2::optAudioModePtr, &modePtr) || !modePtr) return false;
    int m = 0; BYTE a = 0; DWORD r = 0;
    if (!SafeRead(modePtr, &m) || !SafeRead(hp2::bEaxActive, &a) ||
        !SafeRead(hp2::bSoundReady, &r)) return false;
    *mode = m; *eaxActive = a; *soundReady = r ? 1 : 0;
    return true;
}

static void EAXFixTick(LONGLONG now) {
    if (!g_knownExe || !g_cfg.fixEAXOnStartup || g_eaxFixDone) return;
    int mode, active, ready;
    if (!ReadAudioState(&mode, &active, &ready)) return;

    if (mode != g_eaxLastMode || active != g_eaxLastActive || ready != g_eaxLastReady) {
        Logf("audio state: mode=%d (%s) eaxActive=%d soundReady=%d\n", mode,
             mode == hp2::kAudioModeEAX ? "EAX" : "not EAX", active, ready);
        g_eaxLastMode = mode; g_eaxLastActive = active; g_eaxLastReady = ready;
    }

    bool bad = mode == hp2::kAudioModeEAX && !active && ready && !LoadingNow(now);
    g_eaxBadFrames = bad ? g_eaxBadFrames + 1 : 0;
    if (g_eaxBadFrames < g_cfg.eaxFixStableFrames) return;

    g_eaxFixDone = true;   // one attempt per launch
    Logf("EAX fix: option is EAX but engine has EAX off - calling RestartAudio()\n");
    g_inRestartAudio = true;
    reinterpret_cast<void (*)()>(hp2::fnRestartAudio)();
    g_inRestartAudio = false;
    if (ReadAudioState(&mode, &active, &ready))
        Logf("EAX fix: after restart mode=%d eaxActive=%d%s\n", mode, active,
             active ? " - OK" : " - driver did not accept EAX (is DSOAL installed?)");
}

// ---------------------------------------------------------------------------
// Precise game ticks: replaces the one-shot timeSetEvent chain that drives the
// game's 120 Hz simulation thread with our own high-resolution scheduler.
// ---------------------------------------------------------------------------
typedef MMRESULT (WINAPI *timeSetEvent_t)(UINT, UINT, LPTIMECALLBACK, DWORD_PTR, UINT);
typedef MMRESULT (WINAPI *timeKillEvent_t)(UINT);
typedef void (CALLBACK *GameTimeProc_t)(UINT, UINT, DWORD_PTR, DWORD_PTR, DWORD_PTR);
static timeSetEvent_t  g_origTimeSetEvent  = nullptr;
static timeKillEvent_t g_origTimeKillEvent = nullptr;
static const UINT kFakeTimerId = 0x48503254;   // never a real winmm id

static bool               g_ticksActive = false;   // known exe + option on + hooks in place
static HANDLE             g_tickWake    = nullptr;
static HANDLE             g_tickThread  = nullptr;
static std::atomic<bool>  g_tickArmed{false};
static std::atomic<DWORD> g_tickUser{0};
static std::atomic<unsigned long> g_tickFires{0}, g_tickResyncs{0};

// Frame grid published by the limiter (one writer: render thread).
static CRITICAL_SECTION g_gridLock;
static bool     g_gridLockInit = false;
static LONGLONG g_gridAnchor = 0;    // any frame deadline (QPC)
static LONGLONG g_gridPeriod = 0;    // frame period (QPC), 0 = no grid
static int      g_gridTpf    = 0;    // ticks per frame, 0 = not an integer
static LONG     g_gridGen    = 0;

static int GameTickRate() {
    int r = 0;
    if (!SafeRead(hp2::pTickRate, &r) || r <= 0 || r > 10000) r = 120;
    return r;
}

static void PublishGrid(LONGLONG anchor, LONGLONG period) {
    if (!g_ticksActive || !g_gridLockInit) return;
    int rate = GameTickRate();
    int fps  = g_cfg.targetFPS;
    EnterCriticalSection(&g_gridLock);
    g_gridAnchor = anchor;
    g_gridPeriod = period;
    g_gridTpf    = (period > 0 && fps > 0 && rate % fps == 0) ? rate / fps : 0;
    g_gridGen++;
    LeaveCriticalSection(&g_gridLock);
}

static void WaitUntilOn(HANDLE timer, LONGLONG deadline) {
    LONGLONG now = QpcNow();
    double remainingMs = QpcToMs(deadline - now);
    if (timer) {
        if (remainingMs > 0.6) {
            LARGE_INTEGER due;
            due.QuadPart = -(LONGLONG)((remainingMs - 0.3) * 10000.0);
            if (SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE))
                WaitForSingleObject(timer, INFINITE);
        }
    } else if (remainingMs > 2.0) {
        ::Sleep((DWORD)(remainingMs - 1.5));
    }
    do { YieldProcessor(); now = QpcNow(); } while (now < deadline);
}

static DWORD WINAPI TickSchedulerThread(LPVOID) {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    typedef HANDLE (WINAPI *CWTEx_t)(LPSECURITY_ATTRIBUTES, LPCWSTR, DWORD, DWORD);
    auto pCreate = reinterpret_cast<CWTEx_t>((void*)
        GetProcAddress(GetModuleHandleA("kernel32.dll"), "CreateWaitableTimerExW"));
    HANDLE timer = pCreate ? pCreate(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                     TIMER_ALL_ACCESS) : nullptr;
    const LONGLONG freq = g_qpcFreq.QuadPart;

    LONG     gen = -1;
    LONGLONG base = 0, num = 1, den = 1, k = 0, lastFire = 0;

    for (;;) {
        if (!g_tickArmed.load()) { WaitForSingleObject(g_tickWake, 100); continue; }

        int rate = GameTickRate();
        LONGLONG tickLen = freq / rate;
        LONGLONG now = QpcNow();

        LONG curGen; LONGLONG ga, gp; int tpf;
        EnterCriticalSection(&g_gridLock);
        curGen = g_gridGen; ga = g_gridAnchor; gp = g_gridPeriod; tpf = g_gridTpf;
        LeaveCriticalSection(&g_gridLock);

        bool wantLock = gp > 0 && tpf > 0;
        bool freeRunning = base != 0 && num == freq && den == rate;
        if (curGen != gen && !wantLock && freeRunning) gen = curGen;   // keep cadence
        if (curGen != gen || base == 0) {
            gen = curGen;
            // Never fire two ticks closer than one tick apart: a schedule change
            // may delay the next tick (once), never bring it forward - otherwise
            // frequent re-anchors would speed the game up.
            LONGLONG earliest = lastFire ? lastFire + tickLen : now;
            if (earliest < now - tickLen) earliest = now - tickLen;
            if (wantLock) {
                // Phase-lock: tpf ticks per frame, offset into each tick slot.
                num = gp; den = tpf;
                base = ga + (LONGLONG)((double)g_cfg.tickPhase * (double)gp / tpf);
                while (base > earliest) base -= gp;
            } else {
                num = freq; den = rate;
                base = earliest;
            }
            LONGLONG diff = earliest - base;            // >= 0
            k = (diff * den + num - 1) / num;           // first slot at or after 'earliest'
        }

        LONGLONG t = base + (k * num) / den;
        if (now - t > 8 * tickLen) {                    // stalled (alt-tab, hitch): resync
            base = now; num = freq; den = rate; k = 0; t = now;
            gen = -1;                                    // re-lock on next pass
            g_tickResyncs++;
        }
        WaitUntilOn(timer, t);

        if (!g_tickArmed.exchange(false)) continue;    // killed while waiting
        lastFire = t;
        k++;
        g_tickFires++;
        reinterpret_cast<GameTimeProc_t>(hp2::fnTickCallback)(
            kFakeTimerId, 0, g_tickUser.load(), 0, 0);   // re-arms via Hook_timeSetEvent
    }
    return 0;
}

static MMRESULT WINAPI Hook_timeSetEvent(UINT delay, UINT res, LPTIMECALLBACK cb,
                                         DWORD_PTR user, UINT flags) {
    if (g_ticksActive && reinterpret_cast<DWORD>(cb) == hp2::fnTickCallback &&
        !(flags & TIME_PERIODIC)) {
        if (!g_tickThread) {
            g_tickThread = CreateThread(nullptr, 0, TickSchedulerThread, nullptr, 0, nullptr);
            Logf("game ticks: precise scheduler started (rate=%d Hz)\n", GameTickRate());
        }
        g_tickUser = (DWORD)user;
        g_tickArmed = true;
        SetEvent(g_tickWake);
        return kFakeTimerId;
    }
    return g_origTimeSetEvent ? g_origTimeSetEvent(delay, res, cb, user, flags)
                              : ::timeSetEvent(delay, res, cb, user, flags);
}

static MMRESULT WINAPI Hook_timeKillEvent(UINT id) {
    if (id == kFakeTimerId) { g_tickArmed = false; return TIMERR_NOERROR; }
    return g_origTimeKillEvent ? g_origTimeKillEvent(id) : ::timeKillEvent(id);
}

// ---------------------------------------------------------------------------
// Driver steering animation: 15 Hz -> 60 Hz
// ---------------------------------------------------------------------------
static float g_driverSteerStep = 0.025f;   // original 0.1 per 4 physics steps

static bool PatchBytes(DWORD at, const void* bytes, size_t len) {
    void* p = reinterpret_cast<void*>(at);
    DWORD oldProt;
    if (!VirtualProtect(p, len, PAGE_EXECUTE_READWRITE, &oldProt)) return false;
    memcpy(p, bytes, len);
    DWORD dummy;
    VirtualProtect(p, len, oldProt, &dummy);
    FlushInstructionCache(GetCurrentProcess(), p, len);
    return true;
}

static void InstallSteeringPatch() {
    if (!g_knownExe || !g_cfg.smoothDriverSteering) return;
    const BYTE every = 0x00;                       // add edx, 0 -> update when step counter advances
    DWORD stepAddr = reinterpret_cast<DWORD>(&g_driverSteerStep);
    bool ok = PatchBytes(0x42E7DE, &every, 1) &&
              PatchBytes(0x42E7FB, &stepAddr, 4) &&   // fadd dword ptr [g_driverSteerStep]
              PatchBytes(0x42E812, &stepAddr, 4);     // fsub dword ptr [g_driverSteerStep]
    Logf("driver steering: %s\n", ok ? "updates every physics step (60 Hz)" : "patch FAILED");
}

// ---------------------------------------------------------------------------
// Animation clock: replacement for AnimChannelSet::Advance (0x545030)
// ---------------------------------------------------------------------------
namespace hp2 {
    const DWORD fnAnimAdvance = 0x545030;
    const DWORD pStepCounter  = 0x7221DC;   // int, 60 Hz physics steps
    const DWORD pRaceTime     = 0x7221F0;   // float seconds, scaled by game speed
    const DWORD pTimeScale    = 0x7221F4;   // float, 1.0 normal, <1 slow motion
}

#pragma pack(push, 1)
struct AnimChannel {        // 0x14 bytes
    float rate;             // +0x00 clip time per physics step
    float time;             // +0x04
    float prevTime;         // +0x08
    float length;           // +0x0C
    int   active;           // +0x10
};
struct AnimChannelSet {
    int          lastStep;  // +0x00 step counter at last advance
    int          elapsed;   // +0x04 whole steps advanced by the last call
    BYTE         paused;    // +0x08
    BYTE         unk9;      // +0x09
    BYTE         looped;    // +0x0A a channel wrapped during the last call
    BYTE         unkB;
    AnimChannel* channels;  // +0x0C
    int          count;     // +0x10
};
#pragma pack(pop)
static_assert(sizeof(AnimChannel) == 0x14, "AnimChannel layout");
static_assert(offsetof(AnimChannelSet, channels) == 0x0C, "AnimChannelSet layout");

struct AnimClockEntry { const AnimChannelSet* key; int lastStep; double lastClock; };
static const int kAnimClockSlots = 2048;           // power of two
static AnimClockEntry g_animClock[kAnimClockSlots];
static int g_animClockUsed = 0;
static CRITICAL_SECTION g_animLock;

static double GameStepClock() {
    int   counter = *reinterpret_cast<volatile int*>(hp2::pStepCounter);
    float rt      = *reinterpret_cast<volatile float*>(hp2::pRaceTime);
    double frac = (double)rt * 60.0 - (double)counter;
    if (!(frac > 0.0)) frac = 0.0;                  // also catches NaN
    if (frac > 0.999) frac = 0.999;                 // counter capped / catching up
    return (double)counter + frac;
}

static AnimClockEntry* AnimClockFor(const AnimChannelSet* cs) {
    DWORD h = (reinterpret_cast<DWORD>(cs) >> 3) * 2654435761u;
    for (int i = 0; i < kAnimClockSlots; ++i) {
        AnimClockEntry& e = g_animClock[(h + i) & (kAnimClockSlots - 1)];
        if (e.key == cs) return &e;
        if (!e.key) {
            if (g_animClockUsed > kAnimClockSlots * 3 / 4) {      // stale actors piled up
                memset(g_animClock, 0, sizeof(g_animClock));
                g_animClockUsed = 0;
                return AnimClockFor(cs);
            }
            e.key = cs; e.lastStep = cs->lastStep; e.lastClock = cs->lastStep;
            g_animClockUsed++;
            return &e;
        }
    }
    return nullptr;
}

// --- animation diagnostics (animDiag=1) ---
struct AnimDiagEntry { const AnimChannelSet* cs; int animId; DWORD clipVt; float rate, length;
                       unsigned calls, advances; double steps; };
static AnimDiagEntry g_animDiag[32];
static int g_animDiagCount = 0;
static unsigned g_animDiagDropped = 0;

static void AnimDiagRecord(const AnimChannelSet* cs, double steps) {
    if (!g_cfg.animDiag) return;
    int animId = 0; DWORD clipVt = 0; float rate = 0, length = 0;
    const BYTE* actor = reinterpret_cast<const BYTE*>(cs) - 0x34;
    SafeRead(reinterpret_cast<DWORD>(actor) + 0x5C, &animId);
    if (cs->count > 0 && !IsBadReadPtr(cs->channels, sizeof(AnimChannel))) {
        const AnimChannel& c0 = cs->channels[0];
        rate = c0.rate; length = c0.length;
        DWORD clip = (DWORD)c0.active;               // +0x10 holds the clip object
        if (clip) SafeRead(clip, &clipVt);
    }
    int i = 0;
    for (; i < g_animDiagCount; ++i)
        if (g_animDiag[i].cs == cs && g_animDiag[i].animId == animId) break;
    if (i == g_animDiagCount) {
        if (g_animDiagCount == 32) { g_animDiagDropped++; return; }
        g_animDiag[i] = { cs, animId, clipVt, rate, length, 0, 0, 0.0 };
        g_animDiagCount++;
    }
    AnimDiagEntry& e = g_animDiag[i];
    e.calls++;
    if (steps > 1e-6) { e.advances++; e.steps += steps; }
    e.clipVt = clipVt; e.rate = rate; e.length = length;
}

static void AnimDiagFlush(double seconds) {
    if (!g_cfg.animDiag || !g_animDiagCount || seconds <= 0) return;
    Logf("anim diag (%.1fs): %d character clips%s\n", seconds, g_animDiagCount,
         g_animDiagDropped ? " (table full)" : "");
    for (int i = 0; i < g_animDiagCount; ++i) {
        const AnimDiagEntry& e = g_animDiag[i];
        Logf("  set=%p anim=0x%X clipVt=0x%06lX rate=%.4f len=%.1f | calls/s=%.1f advances/s=%.1f "
             "steps/s=%.1f\n", (const void*)e.cs, e.animId, (unsigned long)e.clipVt, e.rate, e.length,
             e.calls / seconds, e.advances / seconds, e.steps / seconds);
    }
    g_animDiagCount = 0; g_animDiagDropped = 0;
}

// __fastcall: ecx = this (AnimChannelSet*), edx unused. Returns "a channel looped".
static bool __fastcall Hook_AnimAdvance(AnimChannelSet* cs, void*) {
    if (cs->paused) {
        cs->elapsed = 0;
        return false;
    }
    const int counter = *reinterpret_cast<volatile int*>(hp2::pStepCounter);

    double steps;
    if (g_cfg.smoothSlowMotionAnims) {
        EnterCriticalSection(&g_animLock);
        AnimClockEntry* e = AnimClockFor(cs);
        double now = GameStepClock();
        if (!e) {
            steps = (double)(counter - cs->lastStep);
        } else {
            if (e->lastStep != cs->lastStep) {           // game re-synced this set (new clip/actor)
                e->lastStep = cs->lastStep;
                e->lastClock = cs->lastStep;
            }
            steps = now - e->lastClock;
            if (steps < 0.0 || steps > 600.0) {          // clock went backwards or huge jump: resync
                e->lastClock = now;
                e->lastStep = counter;
                steps = 0.0;
            } else if (steps > 1e-6) {
                e->lastClock = now;
                e->lastStep = counter;
            }
        }
        LeaveCriticalSection(&g_animLock);
    } else {
        steps = (double)(counter - cs->lastStep);
    }

    AnimDiagRecord(cs, steps);
    if (!(steps > 1e-6)) {
        cs->elapsed = 0;                                 // nothing advanced: flag left as is
        return false;
    }

    // Integer view for the game's other users of 'elapsed' (pose refresh gate,
    // root-motion wrap correction): at least 1 whenever time moved.
    int whole = (int)(steps + 0.5);
    cs->elapsed = whole < 1 ? 1 : whole;
    cs->lastStep = counter;

    bool looped = false;
    for (int i = 0; i < cs->count; ++i) {
        AnimChannel& ch = cs->channels[i];
        if (!ch.active) continue;
        ch.prevTime = ch.time;
        ch.time = (float)((double)ch.time + steps * (double)ch.rate);
        if (ch.time > ch.length) { ch.time = 0.0f; looped = true; }
    }
    if (g_cfg.fixDriverShiftAnim) {
        // Latch: once a clip wraps the flag stays set until the driver's clip
        // selector starts a new clip (Hook_SelectorPlayAnim). With fractional
        // advances a later small advance used to clear it before the selector
        // looked, so the shift clip never "ended" and replayed forever.
        if (looped) cs->looped = 1;
    } else {
        cs->looped = looped ? 1 : 0;
    }
    return looped;
}

// The driver's clip selector (0x42E6B0) starts clips through these six calls
// to Actor::PlayAnim (0x402740, thiscall(id, param), ret 8). Clearing the
// looped latch here means a new shift clip can't inherit an old "ended".
namespace hp2 {
    const DWORD fnPlayAnim = 0x402740;
    const DWORD callSelectorPlayAnim[6] = { 0x42E761, 0x42E775, 0x42E795, 0x42E7A9, 0x42E7BD, 0x42E83A };
}
typedef void (__fastcall *PlayAnim_t)(void* actor, void* edx, DWORD id, DWORD param);
static void __fastcall Hook_SelectorPlayAnim(void* actor, void*, DWORD id, DWORD param) {
    reinterpret_cast<AnimChannelSet*>(reinterpret_cast<BYTE*>(actor) + 0x34)->looped = 0;
    reinterpret_cast<PlayAnim_t>(hp2::fnPlayAnim)(actor, nullptr, id, param);
}

// Shift animation pacing. The selector (0x42E6B0, this = CarModel+0x358) starts
// a ~1 s shift clip (0x16317 / 0x1631B) whenever the gear differs from the one it
// last animated ([sel+0x1C]); changes made during a shift used to queue up, so
// quick up/down shifting kept replaying the clip long after the last change.
// Now a change during a shift extends it instead: the gear is taken as
// animated, and if the clip is already past its middle (hand at the shifter)
// it's held there, so the hand returns to the wheel ~0.5 s after the last change.
namespace hp2 { const DWORD callSelector = 0x42BEEE; const DWORD fnSelector = 0x42E6B0; }
typedef void (__fastcall *Selector_t)(void* sel, void* edx, int arg);
static void __fastcall Hook_Selector(void* selP, void*, int arg) {
    DWORD sel = reinterpret_cast<DWORD>(selP), actor = 0, car = 0;
    int id = -1, lastGear = -1; BYTE gear = 0;
    if (SafeRead(sel + 0x34, &actor) && actor && SafeRead(sel + 0x18, &car) && car &&
        SafeRead(actor + 0x5C, &id) && (id == 0x16317 || id == 0x1631B) &&
        SafeRead(car + 0x630, &gear) && SafeRead(sel + 0x1C, &lastGear) && (int)gear != lastGear) {
        *reinterpret_cast<int*>(sel + 0x1C) = gear;               // no queued second shift
        AnimChannelSet* cs = reinterpret_cast<AnimChannelSet*>(actor + 0x34);
        if (cs->count > 0 && cs->count <= 16 && cs->channels &&
            !IsBadWritePtr(cs->channels, cs->count * sizeof(AnimChannel))) {
            for (int i = 0; i < cs->count; ++i) {
                AnimChannel& ch = cs->channels[i];
                float mid = ch.length * 0.5f;
                if (ch.length > 0.0f && ch.time > mid) { ch.time = mid; ch.prevTime = mid; }
            }
        }
    }
    reinterpret_cast<Selector_t>(hp2::fnSelector)(selP, nullptr, arg);
}

static void InstallAnimClockPatch() {
    if (!g_knownExe || (!g_cfg.smoothSlowMotionAnims && !g_cfg.fixDriverShiftAnim && !g_cfg.animDiag)) return;
    InitializeCriticalSection(&g_animLock);
    BYTE jmp[5] = { 0xE9 };
    DWORD rel = reinterpret_cast<DWORD>(&Hook_AnimAdvance) - (hp2::fnAnimAdvance + 5);
    memcpy(jmp + 1, &rel, 4);
    bool ok = PatchBytes(hp2::fnAnimAdvance, jmp, sizeof(jmp));
    if (ok && g_cfg.fixDriverShiftAnim) {
        for (DWORD at : hp2::callSelectorPlayAnim) ok = ok && PatchCallRel32(at, (void*)&Hook_SelectorPlayAnim);
        ok = ok && PatchCallRel32(hp2::callSelector, (void*)&Hook_Selector);
    }
    Logf("animation clock: %s (fractional=%d, shiftFix=%d)\n", ok ? "replaced" : "patch FAILED",
         g_cfg.smoothSlowMotionAnims, g_cfg.fixDriverShiftAnim);
}

static void LogTimeScaleChanges() {
    if (!g_knownExe) return;
    static float last = -1.0f;
    float ts = 0.0f;
    if (!SafeRead(hp2::pTimeScale, &ts)) return;
    if (ts != last) {
        Logf("game speed: %.2fx\n", ts);
        last = ts;
    }
}

// ---------------------------------------------------------------------------
// Character actors every physics step (game only updates them on odd steps)
// ---------------------------------------------------------------------------
namespace hp2 {
    const DWORD callWorldStepMid = 0x4D0217;   // call 0x4CA850 inside WorldStep, before the odd-step block
    const DWORD fnWorldStepMid   = 0x4CA850;   // ignores ecx, no stack args
    const DWORD fnActorsUpdate   = 0x406FB0;   // thiscall(ActorManager*): UpdateAll
}
static bool g_propsSyncEveryStep = false;   // set by InstallPropsPatch
typedef void (__fastcall *ThisNoArgs_t)(void* self, void* edx);
static std::atomic<unsigned long> g_extraActorUpdates{0};

static void __fastcall Hook_WorldStepMid(void* self, void*) {
    reinterpret_cast<ThisNoArgs_t>(hp2::fnWorldStepMid)(self, nullptr);
    // WorldStep(step) is called with the current counter; the game updates actors
    // when (step & 1) != 0. Fill in the even steps.
    int step = *reinterpret_cast<volatile int*>(hp2::pStepCounter);
    if (step & 1) return;
    if (g_propsSyncEveryStep)
    {
        reinterpret_cast<void (*)()>(0x4D8850)();     // sync flying props' transforms
        reinterpret_cast<void (*)()>(0x444F80)();     // re-file them into grid cells (drawn per cell)
    }
    if (!g_cfg.smoothCharacterAnims) return;
    DWORD app = 0, mgr = 0;
    if (!SafeRead(hp2::pApp, &app) || !app || !SafeRead(app + 0x30, &mgr) || !mgr) return;
    reinterpret_cast<ThisNoArgs_t>(hp2::fnActorsUpdate)(reinterpret_cast<void*>(mgr), nullptr);
    g_extraActorUpdates++;
}

// Particles/debris: 0x41D760 integrates with h = (counter>>1) - last, scaled by 0.5.
static float g_propStepScale = 0.25f;

static void InstallParticlesPatch() {
    if (!g_knownExe || !g_cfg.smoothParticles) return;
    const BYTE nop2[2] = { 0x90, 0x90 };
    DWORD scaleAddr = reinterpret_cast<DWORD>(&g_propStepScale);
    bool ok = PatchBytes(0x41D7E3, nop2, 2) &&          // sar edx,1 (first-run init)
              PatchBytes(0x41D7EB, nop2, 2) &&          // sar ecx,1
              PatchBytes(0x41D80D, &scaleAddr, 4);      // fmul [0.5] -> [0.25]
    Logf("particles: %s\n", ok ? "move every physics step (60 Hz)" : "patch FAILED");
}

// Knocked props: rigid-body pool 0x6E27A0.
static float g_simpleBodyDt = 1.0f / 60.0f;   // simplified integrator was 1/30 per odd step

static void InstallPropsPatch() {
    if (!g_knownExe || !g_cfg.smoothProps) return;
    const BYTE nop2[2] = { 0x90, 0x90 };
    DWORD dtAddr = reinterpret_cast<DWORD>(&g_simpleBodyDt);
    const DWORD dtSites[] = { 0x444497, 0x4444AB, 0x4444B8, 0x4444E0, 0x4444FB, 0x444519 };
    bool ok = PatchBytes(0x4D53BA, nop2, 2);               // je skip -> run simple integrator every step
    for (DWORD a : dtSites) ok = ok && PatchBytes(a, &dtAddr, 4);
    g_propsSyncEveryStep = ok;
    Logf("props: %s\n", ok ? "knocked props simulated and synced every physics step (60 Hz)"
                            : "patch FAILED");
}

static void InstallCharacterAnimPatch() {
    if (!g_knownExe || (!g_cfg.smoothCharacterAnims && !g_propsSyncEveryStep)) return;
    bool ok = PatchCallRel32(hp2::callWorldStepMid, (void*)&Hook_WorldStepMid);
    Logf("even-step hook: %s (characters=%d, props=%d)\n", ok ? "installed" : "patch FAILED",
         g_cfg.smoothCharacterAnims, g_propsSyncEveryStep ? 1 : 0);
}

static void InstallTickPatch() {
    if (!g_knownExe || !g_cfg.preciseGameTicks) return;
    InitializeCriticalSection(&g_gridLock);
    g_gridLockInit = true;
    g_tickWake = CreateEventA(nullptr, FALSE, FALSE, nullptr);
    bool set = false, kill = false;
    ForEachImport("winmm.dll", [&](void** slot, const char* name) {
        void* old = nullptr;
        if (!strcmp(name, "timeSetEvent") && PatchPointer(slot, (void*)&Hook_timeSetEvent, &old)) {
            g_origTimeSetEvent = reinterpret_cast<timeSetEvent_t>(old); set = true;
        } else if (!strcmp(name, "timeKillEvent") && PatchPointer(slot, (void*)&Hook_timeKillEvent, &old)) {
            g_origTimeKillEvent = reinterpret_cast<timeKillEvent_t>(old); kill = true;
        }
    });
    g_ticksActive = set && kill && g_tickWake;
    Logf("game ticks: %s\n", g_ticksActive ? "timeSetEvent/timeKillEvent hooked"
                                           : "hook FAILED, using the game's own timer");
}

// Ticks-per-frame histogram, read at each Present.
static DWORD g_lastTickCount = 0;
static bool  g_haveTickCount = false;
static unsigned long g_tpfHist[5] = {};

static void SampleTicksPerFrame(bool countIt) {
    if (!g_knownExe) return;
    DWORD c = 0;
    if (!SafeRead(hp2::pTickCounter, &c)) return;
    if (g_haveTickCount && countIt) {
        DWORD d = c - g_lastTickCount;
        g_tpfHist[d > 4 ? 4 : d]++;
    }
    g_lastTickCount = c;
    g_haveTickCount = true;
}

// ---------------------------------------------------------------------------
// Present hook
// ---------------------------------------------------------------------------
static std::atomic<bool> g_firstPresentSeen{false};
static LONGLONG g_lastPresentQpc = 0;
static LONGLONG g_nextDeadline   = 0;
static LONGLONG g_lastReanchor   = 0;
static bool     g_gridUnstable   = false;
static unsigned long long g_presentCount = 0, g_intervalCount = 0;
static double g_minDelta = 1e9, g_maxDelta = 0.0, g_sumDelta = 0.0;

// ---------------------------------------------------------------------------
// Interpolation (v0.15)
//
// The world steps at 60 Hz (WorldStep 0x4D00A0, called from 0x4D0370) and the
// game draws whatever the last step left. Racing cars are re-posed at render
// time from their physics bodies (0x4CA860 -> CarModel::SetTransform 0x42B940
// with body pos +0x2A8 / 3x3 rotation +0x2B4, z flipped), and the view is built
// from the active camera object of each player view (0x40D620: eye +8,
// target +0x14, fov +0x20, up +0x40). The camera director reads the physics
// bodies, not the car models, so blended car models never feed back into the
// simulation.
//
// After every step we snapshot the bodies and cameras (time-stamped with the
// frame's display time); at render time each car's SetTransform and each view
// build get values blended between the two snapshots around "display time
// minus interpDelay steps". Anything that doesn't match the newest snapshot
// (replays, resets, cameras moved outside a step) is drawn as the game had it.
// ---------------------------------------------------------------------------
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
typedef void  (__cdecl    *WorldStep_t)(int step);
typedef void  (__fastcall *CarSetOwner_t)(void* car, void* edx, void* veh);
typedef void  (__fastcall *CarSetXform_t)(void* car, void* edx, void* view, const float* pos, const float* rot);
typedef DWORD (__fastcall *BuildView_t)(void* owner, void* edx, DWORD a, DWORD b, DWORD c);
typedef void  (__fastcall *CarDraw_t)(void* car, void* edx, void* view);

static const int kIpCars = 32, kIpCams = 4, kIpRing = 4;
struct IpCar { DWORD veh; float x[12]; };                 // body pos (3) + rotation (9), physics space
struct IpCam { DWORD owner, cam, activeId; float f[10]; }; // eye 3, target 3, fov, up 3
struct IpSnap { int idx; int nCars, nCams; IpCar cars[kIpCars]; IpCam cams[kIpCams]; };
static IpSnap g_ipRing[kIpRing];
static int    g_ipHead = 0, g_ipCount = 0, g_ipLastIdx = 0;
static LONGLONG g_ipLastStepQpc = 0;

struct IpVeh { DWORD veh, car; unsigned long long seen; };
static IpVeh  g_ipVehs[kIpCars]; static int g_ipNVehs = 0;
static DWORD  g_ipOwners[kIpCams]; static int g_ipNOwners = 0;
static DWORD  g_ipPendingCar = 0, g_ipPendingVeh = 0;
// A view built this frame with an un-blended camera (cut / switch / mismatch):
// draw the cars un-blended too, or they'd sit a step away from the camera.
static unsigned long long g_ipCutFrame = ~0ull;
static unsigned long g_ipCutFrames = 0;

// statistics
static unsigned long g_ipFrames = 0, g_ipFramesBlended = 0, g_ipAhead = 0, g_ipBehind = 0,
                     g_ipCarsBlended = 0, g_ipCarsPass = 0, g_ipCarMismatch = 0, g_ipCarJump = 0,
                     g_ipCamsBlended = 0, g_ipCamCut = 0, g_ipCamMismatch = 0, g_ipResets = 0,
                     g_ipClockSnaps = 0, g_ipSteps = 0, g_ipDrawListB = 0, g_ipDrawSimCar = 0;
static double g_ipAlphaSum = 0;

// --- frame display time --------------------------------------------------
// With the limiter on, a frame drawn now reaches the screen at the next
// deadline. Without it, follow the frame start times with a light filter.
static LONGLONG g_ftLast = 0, g_ftInterval = 0, g_frameTime = 0;
static unsigned long long g_frameTimeFor = ~0ull;
static LONGLONG SmoothedNow() {
    LONGLONG now = QpcNow();
    if (!g_ftLast) { g_ftLast = now; return now; }
    LONGLONG d = now - g_ftLast;
    if (!g_ftInterval) g_ftInterval = d;
    LONGLONG pred = g_ftLast + g_ftInterval, err = now - pred, ft;
    if (g_ftInterval > 0 && err < g_ftInterval / 2 && err > -g_ftInterval / 2) {
        ft = pred + err / 8;
        g_ftInterval += (d - g_ftInterval) / 16;
    } else {
        ft = now;
        if (d > 0 && d < g_qpcFreq.QuadPart / 10) g_ftInterval = d;
    }
    g_ftLast = ft;
    return ft;
}
static LONGLONG FrameDisplayTime() {
    if (g_frameTimeFor != g_presentCount) {
        g_frameTimeFor = g_presentCount;
        LONGLONG sm = SmoothedNow();
        g_frameTime = g_nextDeadline ? g_nextDeadline : sm;
    }
    return g_frameTime;
}

// --- step clock -------------------------------------------------------------
// E(t) = continuous step index, following step arrivals gently; its rate is
// 60 steps per second times the game's time scale (slow motion).
static bool     g_ipClockValid = false;
static double   g_ipE0 = 0;   static LONGLONG g_ipT0 = 0;
static double   g_ipPeriod = 0;               // QPC ticks per step
static double StepPeriodNow() {
    float sc = 1.0f;
    SafeRead(hp2::pTimeScale, &sc);
    if (!(sc > 0.05f)) sc = 0.05f;
    if (sc > 4.0f) sc = 4.0f;
    return (double)g_qpcFreq.QuadPart / 60.0 / (double)sc;
}
static double ClockAt(LONGLONG t) { return g_ipE0 + (double)(t - g_ipT0) / g_ipPeriod; }
static void ClockOnStep(int idx, LONGLONG t) {
    double p = StepPeriodNow();
    if (!g_ipClockValid) {
        g_ipE0 = idx; g_ipT0 = t; g_ipPeriod = p; g_ipClockValid = true; return;
    }
    double e = ClockAt(t);
    g_ipE0 = e; g_ipT0 = t;                       // rebase (also when the rate changes)
    if (fabs(p - g_ipPeriod) > g_ipPeriod * 0.02) g_ipPeriod = p;
    double err = (double)idx - e;
    if (err > 3.0 || err < -3.0) { g_ipE0 = idx; g_ipClockSnaps++; }
    else g_ipE0 += err / 32.0;
}

// --- snapshots ----------------------------------------------------------------
static bool ReadBody(DWORD veh, float* x) {
    DWORD body = 0;
    if (!SafeRead(veh + 0x6E8, &body) || !body) return false;
    if (IsBadReadPtr(reinterpret_cast<void*>(body + 0x2A8), 48)) return false;
    memcpy(x, reinterpret_cast<void*>(body + 0x2A8), 48);
    return true;
}
static DWORD ActiveCamera(DWORD owner, DWORD* idOut) {
    DWORD slot = 0, id = 0, list = 0, n = 0;
    if (!SafeRead(owner + 0x164, &slot) || slot > 10) return 0;
    if (!SafeRead(owner + 0x138 + slot * 4, &id)) return 0;
    if (!SafeRead(owner + 4, &list) || !SafeRead(owner + 8, &n) || !list || n > 64) return 0;
    for (DWORD i = 0; i < n; i++) {
        DWORD cam = 0, cid = 0;
        if (!SafeRead(list + i * 4, &cam) || !cam || !SafeRead(cam + 0x68, &cid)) continue;
        if (cid == id) { if (idOut) *idOut = id; return cam; }
    }
    return 0;
}
static bool ReadCam(DWORD cam, float* f) {
    if (IsBadReadPtr(reinterpret_cast<void*>(cam + 8), 0x44)) return false;
    memcpy(f, reinterpret_cast<void*>(cam + 8), 7 * 4);          // eye, target, fov
    memcpy(f + 7, reinterpret_cast<void*>(cam + 0x40), 3 * 4);   // up
    return true;
}
static void WriteCam(DWORD cam, const float* f) {
    memcpy(reinterpret_cast<void*>(cam + 8), f, 7 * 4);
    memcpy(reinterpret_cast<void*>(cam + 0x40), f + 7, 3 * 4);
}

// The blended camera stays written into the camera object for the whole frame
// render (not only during BuildView): CarSetXform (0x42B940) copies the camera
// eye into each car (+0xB60) for its sun specular, and the reflection update
// aims the sphere map at it. The raw camera goes back before the next physics
// step and after Present, so the game's own camera update never sees blended
// values. If the game wrote the camera itself meanwhile, its value is kept.
struct IpCamOverride { DWORD cam; float raw[10], blended[10]; bool active; };
static IpCamOverride g_ipCamOv[kIpCams];
static IpCamOverride* FindCamOverride(DWORD cam) {
    for (int i = 0; i < kIpCams; i++) if (g_ipCamOv[i].active && g_ipCamOv[i].cam == cam) return &g_ipCamOv[i];
    return nullptr;
}
static void RestoreCamOverride(IpCamOverride* o) {
    float now[10];
    if (ReadCam(o->cam, now) && !memcmp(now, o->blended, sizeof(now))) WriteCam(o->cam, o->raw);
    o->active = false;
}
static void RestoreCamOverrides() {
    for (int i = 0; i < kIpCams; i++) if (g_ipCamOv[i].active) RestoreCamOverride(&g_ipCamOv[i]);
}
static void ApplyCamOverride(DWORD cam, const float* raw, const float* bl) {
    IpCamOverride* o = FindCamOverride(cam);
    if (!o) for (int i = 0; i < kIpCams && !o; i++) if (!g_ipCamOv[i].active) o = &g_ipCamOv[i];
    if (!o) { RestoreCamOverrides(); o = &g_ipCamOv[0]; }
    o->cam = cam; memcpy(o->raw, raw, sizeof(o->raw)); memcpy(o->blended, bl, sizeof(o->blended));
    o->active = true;
    WriteCam(cam, bl);
}

static void OnStep(int idx) {
    g_ipSteps++;
    LONGLONG t = FrameDisplayTime();
    if (g_ipCount && (idx <= g_ipLastIdx || idx - g_ipLastIdx > 16)) {
        g_ipCount = 0; g_ipClockValid = false; g_ipResets++;     // restart / jump
    }
    g_ipLastIdx = idx;
    g_ipLastStepQpc = QpcNow();
    g_ipHead = (g_ipHead + 1) % kIpRing;
    IpSnap& s = g_ipRing[g_ipHead];
    s.idx = idx; s.nCars = 0; s.nCams = 0;
    for (int i = 0; i < g_ipNVehs; i++)
        if (ReadBody(g_ipVehs[i].veh, s.cars[s.nCars].x)) s.cars[s.nCars++].veh = g_ipVehs[i].veh;
    for (int i = 0; i < g_ipNOwners; i++) {
        IpCam& c = s.cams[s.nCams];
        c.owner = g_ipOwners[i];
        c.cam = ActiveCamera(c.owner, &c.activeId);
        if (c.cam && ReadCam(c.cam, c.f)) s.nCams++;
    }
    if (g_ipCount < kIpRing) g_ipCount++;
    ClockOnStep(idx, t);
}

static void __cdecl Hook_WorldStep(int step) {
    RestoreCamOverrides();
    reinterpret_cast<WorldStep_t>(hp2::fnWorldStep)(step);
    if (!*reinterpret_cast<volatile BYTE*>(hp2::pReplayFlag)) OnStep(step + 1);
}

// --- per-frame blend position -------------------------------------------------
struct IpFrame { bool ok; const IpSnap* A; const IpSnap* B; float t; };
static IpFrame g_ipFrame; static unsigned long long g_ipFrameFor = ~0ull;
static const IpFrame& FrameBlend() {
    if (g_ipFrameFor == g_presentCount) return g_ipFrame;
    g_ipFrameFor = g_presentCount;
    IpFrame& f = g_ipFrame; f.ok = false;
    g_ipFrames++;
    if (!g_cfg.interpolate || g_ipCount < 2 || !g_ipClockValid) return f;
    if (QpcNow() - g_ipLastStepQpc > g_qpcFreq.QuadPart / 4) return f;   // paused / replay
    double r = ClockAt(FrameDisplayTime()) - g_cfg.interpDelay;
    const IpSnap* newest = &g_ipRing[g_ipHead];
    if (r >= (double)newest->idx) { g_ipAhead++; return f; }           // newest is the best we have
    const IpSnap *A = nullptr, *B = nullptr;
    for (int k = 0; k + 1 < g_ipCount; k++) {
        const IpSnap* nb = &g_ipRing[(g_ipHead - k + kIpRing) % kIpRing];
        const IpSnap* na = &g_ipRing[(g_ipHead - k - 1 + kIpRing) % kIpRing];
        if (r >= (double)na->idx) { A = na; B = nb; break; }
    }
    if (!A) {
        int k = g_ipCount - 2;
        B = &g_ipRing[(g_ipHead - k + kIpRing) % kIpRing];
        A = &g_ipRing[(g_ipHead - k - 1 + kIpRing) % kIpRing];
        r = A->idx; g_ipBehind++;
    }
    int span = B->idx - A->idx;
    if (span <= 0 || span > 8) return f;
    double t = (r - A->idx) / span;
    f.t = (float)(t < 0 ? 0 : t > 1 ? 1 : t);
    f.A = A; f.B = B; f.ok = true;
    g_ipFramesBlended++; g_ipAlphaSum += f.t;
    return f;
}

// --- blending math -------------------------------------------------------------
struct IpQuat { float w, x, y, z; };
static IpQuat MatToQuat(const float* m) {   // row-major 3x3
    IpQuat q; float tr = m[0] + m[4] + m[8];
    if (tr > 0) { float s = sqrtf(tr + 1.0f) * 2; q.w = 0.25f * s; q.x = (m[7] - m[5]) / s;
                  q.y = (m[2] - m[6]) / s; q.z = (m[3] - m[1]) / s; }
    else if (m[0] > m[4] && m[0] > m[8]) { float s = sqrtf(1.0f + m[0] - m[4] - m[8]) * 2;
                  q.w = (m[7] - m[5]) / s; q.x = 0.25f * s; q.y = (m[1] + m[3]) / s; q.z = (m[2] + m[6]) / s; }
    else if (m[4] > m[8]) { float s = sqrtf(1.0f + m[4] - m[0] - m[8]) * 2;
                  q.w = (m[2] - m[6]) / s; q.x = (m[1] + m[3]) / s; q.y = 0.25f * s; q.z = (m[5] + m[7]) / s; }
    else { float s = sqrtf(1.0f + m[8] - m[0] - m[4]) * 2;
                  q.w = (m[3] - m[1]) / s; q.x = (m[2] + m[6]) / s; q.y = (m[5] + m[7]) / s; q.z = 0.25f * s; }
    return q;
}
static void QuatToMat(const IpQuat& q, float* m) {
    float xx = q.x*q.x, yy = q.y*q.y, zz = q.z*q.z, xy = q.x*q.y, xz = q.x*q.z, yz = q.y*q.z,
          wx = q.w*q.x, wy = q.w*q.y, wz = q.w*q.z;
    m[0] = 1 - 2*(yy+zz); m[1] = 2*(xy-wz);     m[2] = 2*(xz+wy);
    m[3] = 2*(xy+wz);     m[4] = 1 - 2*(xx+zz); m[5] = 2*(yz-wx);
    m[6] = 2*(xz-wy);     m[7] = 2*(yz+wx);     m[8] = 1 - 2*(xx+yy);
}
static bool IsRotation(const float* m) {
    for (int r = 0; r < 3; r++) {
        float l = m[r*3]*m[r*3] + m[r*3+1]*m[r*3+1] + m[r*3+2]*m[r*3+2];
        if (fabsf(l - 1.0f) > 0.02f) return false;
    }
    float det = m[0]*(m[4]*m[8]-m[5]*m[7]) - m[1]*(m[3]*m[8]-m[5]*m[6]) + m[2]*(m[3]*m[7]-m[4]*m[6]);
    return fabsf(det - 1.0f) < 0.05f;
}
// a, b: pos(3) + rot(9). Returns false if the pair shouldn't be blended.
static bool BlendXform(const float* a, const float* b, float t, float* out) {
    float dx = b[0]-a[0], dy = b[1]-a[1], dz = b[2]-a[2];
    if (dx*dx + dy*dy + dz*dz > g_cfg.maxStepDistance * g_cfg.maxStepDistance) return false;
    for (int i = 0; i < 3; i++) out[i] = a[i] + (b[i] - a[i]) * t;
    const float *ma = a + 3, *mb = b + 3; float* mo = out + 3;
    if (!memcmp(ma, mb, 36)) { memcpy(mo, mb, 36); return true; }
    if (!IsRotation(ma) || !IsRotation(mb)) {
        for (int i = 0; i < 9; i++) mo[i] = ma[i] + (mb[i] - ma[i]) * t;
        return true;
    }
    IpQuat qa = MatToQuat(ma), qb = MatToQuat(mb);
    float d = qa.w*qb.w + qa.x*qb.x + qa.y*qb.y + qa.z*qb.z;
    if (d < 0) { qb.w = -qb.w; qb.x = -qb.x; qb.y = -qb.y; qb.z = -qb.z; d = -d; }
    if (d > 1) d = 1;
    if (2.0f * acosf(d) * 57.29578f > g_cfg.maxStepAngle) return false;
    IpQuat q;
    if (d > 0.9995f) {
        q.w = qa.w + (qb.w-qa.w)*t; q.x = qa.x + (qb.x-qa.x)*t;
        q.y = qa.y + (qb.y-qa.y)*t; q.z = qa.z + (qb.z-qa.z)*t;
    } else {
        float th = acosf(d), s = sinf(th), wa = sinf((1-t)*th)/s, wb = sinf(t*th)/s;
        q.w = qa.w*wa + qb.w*wb; q.x = qa.x*wa + qb.x*wb; q.y = qa.y*wa + qb.y*wb; q.z = qa.z*wa + qb.z*wb;
    }
    float l = sqrtf(q.w*q.w + q.x*q.x + q.y*q.y + q.z*q.z);
    if (l < 1e-6f) return false;
    q.w /= l; q.x /= l; q.y /= l; q.z /= l;
    QuatToMat(q, mo);
    return true;
}
// physics space -> what 0x4CA860 hands to SetTransform (z flipped)
static void ToRenderSpace(const float* x, float* pos, float* rot) {
    pos[0] = x[0]; pos[1] = x[1]; pos[2] = -x[2];
    const float* m = x + 3;
    rot[0] = m[0];  rot[1] = m[1];  rot[2] = -m[2];
    rot[3] = m[3];  rot[4] = m[4];  rot[5] = -m[5];
    rot[6] = -m[6]; rot[7] = -m[7]; rot[8] = m[8];
}
static bool NearlyEqual(const float* a, const float* b, int n) {
    for (int i = 0; i < n; i++) {
        float d = fabsf(a[i] - b[i]), s = fabsf(a[i]) + fabsf(b[i]);
        if (d > 1e-4f * (1.0f + s)) return false;
    }
    return true;
}
static const IpCar* FindCar(const IpSnap* s, DWORD veh) {
    for (int i = 0; i < s->nCars; i++) if (s->cars[i].veh == veh) return &s->cars[i];
    return nullptr;
}
static const IpCam* FindCam(const IpSnap* s, DWORD owner) {
    for (int i = 0; i < s->nCams; i++) if (s->cams[i].owner == owner) return &s->cams[i];
    return nullptr;
}

// --- render-time hooks -------------------------------------------------------------
static void RegisterVehicle(DWORD veh, DWORD car) {
    for (int i = 0; i < g_ipNVehs; i++)
        if (g_ipVehs[i].veh == veh) { g_ipVehs[i].car = car; g_ipVehs[i].seen = g_presentCount; return; }
    // drop vehicles not drawn for 10 s worth of frames, then add
    int w = 0;
    for (int i = 0; i < g_ipNVehs; i++)
        if (g_presentCount - g_ipVehs[i].seen < 2000) g_ipVehs[w++] = g_ipVehs[i];
    g_ipNVehs = w;
    if (g_ipNVehs < kIpCars) g_ipVehs[g_ipNVehs++] = { veh, car, g_presentCount };
}

static void __fastcall Hook_CarSetOwner(void* car, void*, void* veh) {
    g_ipPendingCar = reinterpret_cast<DWORD>(car);
    g_ipPendingVeh = reinterpret_cast<DWORD>(veh);
    if (g_cfg.interpolate) RegisterVehicle(g_ipPendingVeh, g_ipPendingCar);
    reinterpret_cast<CarSetOwner_t>(hp2::fnCarSetOwner)(car, nullptr, veh);
}

static void __fastcall Hook_CarSetXform(void* car, void*, void* view, const float* pos, const float* rot) {
    if (g_cfg.interpolate && g_ipCutFrame == g_presentCount) { g_ipCarsPass++; }
    else if (g_cfg.interpolate && reinterpret_cast<DWORD>(car) == g_ipPendingCar && g_ipPendingVeh) {
        const IpFrame& f = FrameBlend();
        const IpCar* a = f.ok ? FindCar(f.A, g_ipPendingVeh) : nullptr;
        const IpCar* b = f.ok ? FindCar(f.B, g_ipPendingVeh) : nullptr;
        const IpCar* n = f.ok ? FindCar(&g_ipRing[g_ipHead], g_ipPendingVeh) : nullptr;
        if (a && b && n) {
            float np[3], nr[9];
            ToRenderSpace(n->x, np, nr);
            // only if the newest snapshot is still what the game is drawing
            if (NearlyEqual(np, pos, 3) && NearlyEqual(nr, rot, 9)) {
                float x[12];
                if (BlendXform(a->x, b->x, f.t, x)) {
                    float p[3], m[9];
                    ToRenderSpace(x, p, m);
                    g_ipCarsBlended++;
                    reinterpret_cast<CarSetXform_t>(hp2::fnCarSetXform)(car, nullptr, view, p, m);
                    return;
                }
                g_ipCarJump++;
            } else g_ipCarMismatch++;
        }
        g_ipCarsPass++;
    }
    reinterpret_cast<CarSetXform_t>(hp2::fnCarSetXform)(car, nullptr, view, pos, rot);
}

// A camera that flips or swings in one step (look back on cameras that turn the
// same camera object around, quick pans) must not be blended: halfway through a
// 180-degree flip the view sits inside the car.
static bool CameraJumped(const float* a, const float* b) {
    float dx = b[0]-a[0], dy = b[1]-a[1], dz = b[2]-a[2];
    if (dx*dx + dy*dy + dz*dz > g_cfg.maxStepDistance * g_cfg.maxStepDistance) return true;
    float da[3] = { a[3]-a[0], a[4]-a[1], a[5]-a[2] }, db[3] = { b[3]-b[0], b[4]-b[1], b[5]-b[2] };
    float la = sqrtf(da[0]*da[0] + da[1]*da[1] + da[2]*da[2]);
    float lb = sqrtf(db[0]*db[0] + db[1]*db[1] + db[2]*db[2]);
    if (la < 1e-4f || lb < 1e-4f) return true;
    float c = (da[0]*db[0] + da[1]*db[1] + da[2]*db[2]) / (la * lb);
    if (c < 0.7071f) return true;                                  // view turned > 45 deg
    float ua = sqrtf(a[7]*a[7] + a[8]*a[8] + a[9]*a[9]), ub = sqrtf(b[7]*b[7] + b[8]*b[8] + b[9]*b[9]);
    if (ua > 1e-4f && ub > 1e-4f &&
        (a[7]*b[7] + a[8]*b[8] + a[9]*b[9]) / (ua * ub) < 0.7071f) return true;   // rolled > 45 deg
    if (fabsf(b[6] - a[6]) > 0.2f * (fabsf(a[6]) + 1e-3f)) return true;          // fov jumped > 20 %
    return false;
}

// The blended camera used for this frame's view, per owner, so the reflection
// update (which reads the camera eye itself) can see the same camera.
struct IpViewCam { DWORD owner, cam; unsigned long frame; float f[10]; };
static IpViewCam g_ipViewCams[kIpCams];
static void RememberViewCam(DWORD owner, DWORD cam, const float* f) {
    int slot = 0;
    for (int i = 0; i < kIpCams; i++) {
        if (g_ipViewCams[i].owner == owner) { slot = i; break; }
        if (g_ipViewCams[i].frame < g_ipViewCams[slot].frame) slot = i;
    }
    IpViewCam& v = g_ipViewCams[slot];
    v.owner = owner; v.cam = cam; v.frame = g_presentCount; memcpy(v.f, f, sizeof(v.f));
}
static const IpViewCam* FindViewCam(DWORD owner) {
    for (int i = 0; i < kIpCams; i++)
        if (g_ipViewCams[i].owner == owner && g_ipViewCams[i].frame == g_presentCount && g_ipViewCams[i].cam)
            return &g_ipViewCams[i];
    return nullptr;
}

static void MarkCutFrame() {
    if (g_ipCutFrame != g_presentCount) { g_ipCutFrame = g_presentCount; g_ipCutFrames++; }
}

static DWORD __fastcall Hook_BuildView(void* ownerP, void*, DWORD a, DWORD b, DWORD c) {
    BuildView_t orig = reinterpret_cast<BuildView_t>(hp2::fnBuildView);
    DWORD owner = reinterpret_cast<DWORD>(ownerP);
    if (!g_cfg.interpolate || !g_cfg.interpolateCamera) return orig(ownerP, nullptr, a, b, c);
    bool known = false;
    for (int i = 0; i < g_ipNOwners; i++) if (g_ipOwners[i] == owner) known = true;
    if (!known && g_ipNOwners < kIpCams) g_ipOwners[g_ipNOwners++] = owner;
    const IpFrame& f = FrameBlend();
    if (f.ok) {
        const IpCam* ca = FindCam(f.A, owner);
        const IpCam* cb = FindCam(f.B, owner);
        const IpCam* cn = FindCam(&g_ipRing[g_ipHead], owner);
        DWORD id = 0, cam = ActiveCamera(owner, &id);
        float cur[10];
        bool have = false;
        if (cam) {
            if (IpCamOverride* o = FindCamOverride(cam)) {
                float now[10];
                if (ReadCam(cam, now) && !memcmp(now, o->blended, sizeof(now))) {
                    memcpy(cur, o->raw, sizeof(cur)); have = true;    // still ours: raw is the real state
                } else o->active = false;                             // the game moved it meanwhile
            }
            if (!have) have = ReadCam(cam, cur);
        }
        if (ca && cb && cn && cam && have) {
            if (ca->cam != cb->cam || cb->cam != cam || cn->cam != cam ||
                ca->activeId != cb->activeId || cb->activeId != id) {
                g_ipCamCut++; MarkCutFrame();                   // camera switched between the steps
            } else if (!NearlyEqual(cur, cn->f, 10)) {
                g_ipCamMismatch++; MarkCutFrame();              // moved outside a step
            } else {
                if (CameraJumped(ca->f, cb->f)) {
                    g_ipCamCut++; MarkCutFrame();
                } else {
                    float bl[10];
                    for (int i = 0; i < 10; i++) bl[i] = ca->f[i] + (cb->f[i] - ca->f[i]) * f.t;
                    ApplyCamOverride(cam, cur, bl);       // stays until the next step / Present
                    RememberViewCam(owner, cam, bl);
                    g_ipCamsBlended++;
                    return orig(ownerP, nullptr, a, b, c);
                }
            }
        }
    }
    if (DWORD cam0 = ActiveCamera(owner, nullptr))                  // drawn unblended: raw camera
        if (IpCamOverride* o = FindCamOverride(cam0)) RestoreCamOverride(o);
    return orig(ownerP, nullptr, a, b, c);
}

// Car reflections: the world render (0x4D7985) updates the reflection cubemap
// of the view's car from the car's physics body position - the raw newest step,
// while the car itself is drawn blended. The probe then jumps in 60 Hz steps
// against the smoothly moving car. Give it the same blended position.
namespace hp2 { const DWORD callReflUpdate = 0x4D7985; const DWORD fnReflUpdate = 0x53E7E0; }
typedef void (__fastcall *ReflUpdate_t)(void* mgr, void* edx, void* view, const float* pos);
static unsigned long g_ipReflBlended = 0, g_ipReflPass = 0;
static unsigned long g_ipReflCamBlended = 0;
static void __fastcall Hook_ReflUpdate(void* mgr, void*, void* view, const float* pos) {
    ReflUpdate_t orig = reinterpret_cast<ReflUpdate_t>(hp2::fnReflUpdate);
    if (g_cfg.interpolate && pos && g_ipCutFrame != g_presentCount) {
        const IpFrame& f = FrameBlend();
        if (f.ok) {
            const IpSnap* n = &g_ipRing[g_ipHead];
            for (int i = 0; i < n->nCars; i++) {
                float np[3], nr[9];
                ToRenderSpace(n->cars[i].x, np, nr);
                if (!NearlyEqual(np, pos, 3)) continue;
                const IpCar* a = FindCar(f.A, n->cars[i].veh);
                const IpCar* b = FindCar(f.B, n->cars[i].veh);
                if (!a || !b) break;
                float x[12];
                if (!BlendXform(a->x, b->x, f.t, x)) break;
                float bp[3], br[9];
                ToRenderSpace(x, bp, br);
                g_ipReflBlended++;
                // The update orients the sphere map toward the camera eye, read
                // through the view (0x40CFD0). (Don't touch the body: the game
                // caches the car's draw transform from it during this call.)
                // (The blended camera stays in place for the whole frame since v0.16.3.)
                const IpViewCam* vc = FindViewCam(reinterpret_cast<DWORD>(view));
                if (vc && FindCamOverride(vc->cam)) g_ipReflCamBlended++;
                orig(mgr, nullptr, view, bp);
                return;
            }
        }
    }
    g_ipReflPass++;
    orig(mgr, nullptr, view, pos);
}

static void __fastcall Hook_DrawListB(void* car, void*, void* view) {
    g_ipDrawListB++;
    reinterpret_cast<CarDraw_t>(hp2::fnCarDraw)(car, nullptr, view);
}
static void __fastcall Hook_DrawSimCar(void* car, void*, void* view) {
    g_ipDrawSimCar++;
    reinterpret_cast<CarDraw_t>(hp2::fnCarDraw)(car, nullptr, view);
}

static void InstallInterpolation() {
    if (!g_knownExe || !g_cfg.interpolate) { Logf("interpolation: off\n"); return; }
    bool ok = PatchCallRel32(hp2::callWorldStep,   (void*)&Hook_WorldStep) &&
              PatchCallRel32(hp2::callCarSetOwner, (void*)&Hook_CarSetOwner) &&
              PatchCallRel32(hp2::callCarSetXform, (void*)&Hook_CarSetXform) &&
              PatchCallRel32(hp2::callBuildView1,  (void*)&Hook_BuildView) &&
              PatchCallRel32(hp2::callBuildView2,  (void*)&Hook_BuildView) &&
              PatchCallRel32(hp2::callDrawListB,   (void*)&Hook_DrawListB) &&
              PatchCallRel32(hp2::callDrawSimCar,  (void*)&Hook_DrawSimCar) &&
              PatchCallRel32(hp2::callReflUpdate,  (void*)&Hook_ReflUpdate);
    Logf("interpolation: %s (camera=%d, delay=%.2f steps)\n", ok ? "installed" : "patch FAILED",
         g_cfg.interpolateCamera, g_cfg.interpDelay);
}

static void LogInterpStats() {
    if (!g_cfg.interpolate || !g_knownExe) return;
    Logf("  interp: steps=%lu frames=%lu blended=%lu (avg t %.2f) ahead=%lu behind=%lu | cars blended=%lu "
         "passed=%lu (mismatch %lu, jump %lu) | camera blended=%lu cuts=%lu mismatch=%lu (cut frames %lu) | resets=%lu "
         "clock snaps=%lu | tracked cars=%d views=%d | other car draws: listB=%lu sim-placed=%lu | "
         "reflection probe blended=%lu (camera %lu) passed=%lu\n",
         g_ipSteps, g_ipFrames, g_ipFramesBlended, g_ipFramesBlended ? g_ipAlphaSum / g_ipFramesBlended : 0.0,
         g_ipAhead, g_ipBehind, g_ipCarsBlended, g_ipCarsPass, g_ipCarMismatch, g_ipCarJump,
         g_ipCamsBlended, g_ipCamCut, g_ipCamMismatch, g_ipCutFrames, g_ipResets, g_ipClockSnaps, g_ipNVehs,
         g_ipNOwners, g_ipDrawListB, g_ipDrawSimCar, g_ipReflBlended, g_ipReflCamBlended, g_ipReflPass);
    g_ipReflBlended = g_ipReflPass = g_ipReflCamBlended = 0;
    g_ipSteps = g_ipFrames = g_ipFramesBlended = g_ipAhead = g_ipBehind = g_ipCarsBlended =
        g_ipCarsPass = g_ipCarMismatch = g_ipCarJump = g_ipCamsBlended = g_ipCamCut =
        g_ipCamMismatch = g_ipResets = g_ipClockSnaps = g_ipDrawListB = g_ipDrawSimCar = g_ipCutFrames = 0;
    g_ipAlphaSum = 0;
}

static void LogStats() {
    if (!g_log) return;
    if (g_intervalCount) {
        double avg = g_sumDelta / (double)g_intervalCount;
        Logf("stats: presents=%llu | last %llu frames: min=%.2fms avg=%.2fms "
             "(%.1f fps) max=%.2fms | clock calls GetTickCount=%lu timeGetTime=%lu\n",
             g_presentCount, g_intervalCount, g_minDelta, avg,
             avg > 0.0 ? 1000.0 / avg : 0.0, g_maxDelta,
             g_gtcCalls.exchange(0), g_tgtCalls.exchange(0));
        if (g_knownExe)
            Logf("  sim ticks per frame: 0:%lu 1:%lu 2:%lu 3:%lu 4+:%lu | scheduler fires=%lu resyncs=%lu\n",
                 g_tpfHist[0], g_tpfHist[1], g_tpfHist[2], g_tpfHist[3], g_tpfHist[4],
                 g_tickFires.exchange(0), g_tickResyncs.exchange(0));
        LogInterpStats();
        LogWidescreenStats();
    }
    memset(g_tpfHist, 0, sizeof(g_tpfHist));
    g_minDelta = 1e9; g_maxDelta = 0.0; g_sumDelta = 0.0; g_intervalCount = 0;
}

static void LogAudioModule() {
    HMODULE ds = GetModuleHandleA("dsound.dll");
    if (!ds) { Logf("dsound.dll: not loaded yet\n"); return; }
    char path[MAX_PATH] = {};
    GetModuleFileNameA(ds, path, MAX_PATH);
    bool local = g_gameDir[0] && _strnicmp(path, g_gameDir, strlen(g_gameDir)) == 0;
    Logf("dsound.dll: %s (%s)\n", path, local ? "game folder - DSOAL/wrapper" : "system");
}

typedef HRESULT (STDMETHODCALLTYPE *Present_t)(
    IDirect3DDevice8*, CONST RECT*, CONST RECT*, HWND, CONST RGNDATA*);
static Present_t g_origPresent = nullptr;

static HRESULT STDMETHODCALLTYPE Hook_Present(
    IDirect3DDevice8* This, CONST RECT* pSourceRect, CONST RECT* pDestRect,
    HWND hDestWindowOverride, CONST RGNDATA* pDirtyRegion)
{
    RestoreCamOverrides();                      // frame drawn: raw cameras back for the game
    if (!g_firstPresentSeen.exchange(true)) {
        InitWaitTimer();
        LogAudioModule();
    }

    LONGLONG now = QpcNow();
    bool loading = LoadingNow(now);
    if (loading) g_loadPresents++;
    else EndLoadIfIdle(now);

    if (!g_inRestartAudio) EAXFixTick(now);

    if (!g_cfg.passthrough) {
        bool limit = g_cfg.enableLimiter && g_cfg.targetFPS > 0 &&
                     !(loading && !g_cfg.limiterDuringLoading);
        if (limit) {
            const LONGLONG period = g_qpcFreq.QuadPart / g_cfg.targetFPS;
            if (g_nextDeadline && now < g_nextDeadline) {
                WaitUntil(g_nextDeadline);
                now = QpcNow();
            }
            if (!g_nextDeadline || now - g_nextDeadline > period) {
                g_nextDeadline = now + period;
                // Missing the cap repeatedly: don't chase the frames with the
                // ticks, let them free-run until frames are on time again.
                if (g_lastReanchor && now - g_lastReanchor < g_qpcFreq.QuadPart) {
                    if (!g_gridUnstable) { g_gridUnstable = true; PublishGrid(0, 0); }
                } else if (!g_gridUnstable) {
                    PublishGrid(g_nextDeadline, period);   // new frame schedule: re-lock ticks
                }
                g_lastReanchor = now;
            } else {
                g_nextDeadline += period;
                if (g_gridUnstable && now - g_lastReanchor > 2 * g_qpcFreq.QuadPart) {
                    g_gridUnstable = false;                  // steady again: re-lock
                    PublishGrid(g_nextDeadline, period);
                }
            }
        } else {
            if (g_nextDeadline) PublishGrid(0, 0);   // limiter paused: free-running ticks
            g_nextDeadline = 0;   // resync cleanly when limiting resumes
        }
    }

    SampleTicksPerFrame(!loading);
    LogTimeScaleChanges();
    if (g_cfg.animDiag) {
        static LONGLONG diagStart = 0;
        if (!diagStart) diagStart = now;
        double el = QpcToMs(now - diagStart) / 1000.0;
        if (el >= 2.0) { AnimDiagFlush(el); diagStart = now; }
    }

    g_presentCount++;
    if (g_lastPresentQpc && !loading) {
        double deltaMs = QpcToMs(now - g_lastPresentQpc);
        if (deltaMs < g_minDelta) g_minDelta = deltaMs;
        if (deltaMs > g_maxDelta) g_maxDelta = deltaMs;
        g_sumDelta += deltaMs;
        g_intervalCount++;
    }
    g_lastPresentQpc = now;

    if (g_cfg.logEveryNFrames > 0 &&
        (g_presentCount % (unsigned long long)g_cfg.logEveryNFrames) == 0)
        LogStats();

    return g_origPresent(This, pSourceRect, pDestRect, hDestWindowOverride, pDirtyRegion);
}

// ---------------------------------------------------------------------------
// D3D8 hook chain: Direct3DCreate8 (IAT) -> IDirect3D8::CreateDevice (vt 15)
//                  -> IDirect3DDevice8::Present (vt 15)
// ---------------------------------------------------------------------------
typedef HRESULT (STDMETHODCALLTYPE *CreateDevice_t)(
    IDirect3D8*, UINT, D3DDEVTYPE, HWND, DWORD, D3DPRESENT_PARAMETERS*, IDirect3DDevice8**);
static CreateDevice_t g_origCreateDevice = nullptr;

static void** GetVTable(void* obj) { return *reinterpret_cast<void***>(obj); }

static void HookDevicePresent(IDirect3DDevice8* pDevice) {
    if (!pDevice || g_origPresent) return;
    void* old = nullptr;
    if (PatchPointer(&GetVTable(pDevice)[15], (void*)&Hook_Present, &old)) {
        g_origPresent = reinterpret_cast<Present_t>(old);
        Logf("Present hooked\n");
    } else {
        Logf("Present hook FAILED (VirtualProtect)\n");
    }
}

static HRESULT STDMETHODCALLTYPE Hook_CreateDevice(
    IDirect3D8* This, UINT Adapter, D3DDEVTYPE DeviceType, HWND hFocusWindow,
    DWORD BehaviorFlags, D3DPRESENT_PARAMETERS* pp, IDirect3DDevice8** ppDev)
{
    HRESULT hr = g_origCreateDevice(This, Adapter, DeviceType, hFocusWindow,
                                    BehaviorFlags, pp, ppDev);
    if (SUCCEEDED(hr) && ppDev && *ppDev) {
        if (pp)
            Logf("CreateDevice: %ux%u windowed=%d swapEffect=%d interval=%u refresh=%u\n",
                 pp->BackBufferWidth, pp->BackBufferHeight, pp->Windowed, (int)pp->SwapEffect,
                 pp->FullScreen_PresentationInterval, pp->FullScreen_RefreshRateInHz);
        HookDevicePresent(*ppDev);
    }
    return hr;
}

static void HookD3DCreateDevice(IDirect3D8* pD3D) {
    if (!pD3D || g_origCreateDevice) return;
    void* old = nullptr;
    if (PatchPointer(&GetVTable(pD3D)[15], (void*)&Hook_CreateDevice, &old))
        g_origCreateDevice = reinterpret_cast<CreateDevice_t>(old);
    else
        Logf("CreateDevice hook FAILED (VirtualProtect)\n");
}

typedef IDirect3D8* (WINAPI *Direct3DCreate8_t)(UINT);
static Direct3DCreate8_t g_origDirect3DCreate8 = nullptr;

static IDirect3D8* WINAPI Hook_Direct3DCreate8(UINT SDKVersion) {
    IDirect3D8* pD3D = g_origDirect3DCreate8(SDKVersion);
    if (pD3D) HookD3DCreateDevice(pD3D);
    return pD3D;
}

static bool PatchIAT_Direct3DCreate8() {
    bool patched = false;
    ForEachImport("d3d8.dll", [&](void** slot, const char* name) {
        if (patched || strcmp(name, "Direct3DCreate8") != 0) return;
        void* old = nullptr;
        if (PatchPointer(slot, (void*)&Hook_Direct3DCreate8, &old)) {
            g_origDirect3DCreate8 = reinterpret_cast<Direct3DCreate8_t>(old);
            patched = true;
        }
    });
    return patched;
}

// ---------------------------------------------------------------------------
// HD car reflections (v0.16)
//
// Patches from NFSHP2 HD Reflections by Arctic94 (fork of NFS HD Reflections by
// Aero_), https://github.com/Arctic94/NFSHDReflections, GPL-3.0 - so this file,
// as a derived work, is GPL-3.0 too. Changes over that mod:
//  - reflAllFacesEveryFrame: the cubemap update (0x53E7E0) renders face 2 on
//    odd calls and the other five on even ones ([mgr+0x254] parity) but
//    composites all six every frame, mixing two car positions - the seams
//    flicker at HD resolutions. All six now render every frame.
//  - every patched byte is checked first; if another reflections mod already
//    changed them, this section stays off.
// The probe position itself is blended by the interpolation (Hook_ReflUpdate).
// ---------------------------------------------------------------------------
namespace hp2 {
    struct ReflSig { DWORD addr; const char* bytes; size_t len; };
    const ReflSig kReflSigs[] = {
        { 0x53E143, "\x68\x80\x00\x00\x00\x68\x80\x00\x00\x00", 10 },  // composite target 128x128
        { 0x53E175, "\x68\x80\x00\x00\x00\x68\x80\x00\x00\x00", 10 },  // cube faces 128x128
        { 0x53E12A, "\x8B\x0D\x74\x54\x66\x00", 6 },                  // composite viewport [0x665474]
        { 0x53E169, "\xA1\x74\x54\x66\x00", 5 },                      // cube viewport [0x665474]
        { 0x665474, "\x00\x00\x00\x43", 4 },                          // 128.0f
        { 0x53EB4E, "\x68\x00\x00\x00\x3F\x68\x00\x00\x00\x3F", 10 },  // composite view scale 0.5
        { 0x53ED46, "\x68\x00\x00\x00\x3F\x68\x00\x00\x00\x3F", 10 },
        { 0x53E06D, "\x06\x00\x00\x00\xC7\x03\xFF\x00\x00\x00\xC7\x07\xFF\x00\x00\x00"
                    "\xC7\x46\x08\xFF\x00\x00\x00", 23 },               // reflection colour 255
        { 0x53E85F, "\x0F\x84\x88\x00\x00\x00", 6 },                  // face parity branch
        { 0x53E8E8, "\xE9\x45\x02\x00\x00", 5 },                      // jmp last face + composite
        { 0x53E8F6, "\x83\xB8\x78\x3D\x01\x00\x02\x0F\x85\x36\x02\x00\x00", 13 }, // detail == 2
        { 0x53EB32, "\x8B\xCE\xE8\xB7\x00\x00\x00", 7 },              // render face
    };
}
extern "C" void ReflFaceTwoThenRest();
extern "C" { DWORD g_reflFnRenderFace = 0x53EBF0, g_reflContRest = 0x53E8ED; }
__asm__(
    ".globl _ReflFaceTwoThenRest\n"
    "_ReflFaceTwoThenRest:\n"           // from jmp at 0x53E8E8; (view, 2) pushed, esi = manager
    "    movl %esi, %ecx\n"
    "    call *_g_reflFnRenderFace\n"    // render face 2 (thiscall, ret 8)
    "    jmp *_g_reflContRest\n");      // then the other five, then the composite

static float g_reflCubeRes = 128.f, g_reflEnvRes = 128.f, g_reflEnvScale = 0.5f;

static void InstallReflections() {
    if (!g_knownExe || !g_cfg.hdReflections) { Logf("HD reflections: off\n"); return; }
    for (const auto& sg : hp2::kReflSigs)
        if (IsBadReadPtr(reinterpret_cast<void*>(sg.addr), sg.len) ||
            memcmp(reinterpret_cast<void*>(sg.addr), sg.bytes, sg.len) != 0) {
            Logf("HD reflections: code at 0x%06lX already changed (NFSHP2HDReflections.asi or similar "
                 "loaded?) - left to that mod\n", (unsigned long)sg.addr);
            return;
        }
    int cube = g_cfg.reflCubemapRes, env = g_cfg.reflEnviromapRes;
    if (g_cfg.reflAutoRes) {
        int y = GetSystemMetrics(SM_CYSCREEN);
        cube = y >= 2160 ? 2048 : y >= 1440 ? 1024 : y >= 1080 ? 512 : 256;
        env = cube;
    }
    if (g_cfg.reflPowerOfTwo) {
        auto pot = [](int x) { if (x <= 0) return 128; int p = 1; while ((p << 1) <= x) p <<= 1; return p; };
        cube = pot(cube); env = pot(env);
    }
    if (cube < 32) cube = 32; if (cube > 4096) cube = 4096;
    if (env < 32) env = 32;   if (env > 4096) env = 4096;
    g_reflCubeRes = (float)cube; g_reflEnvRes = (float)env; g_reflEnvScale = (float)env / 256.0f;

    DWORD v; bool ok = true;
    v = env;  ok &= PatchBytes(0x53E144, &v, 4) && PatchBytes(0x53E149, &v, 4);
    v = cube; ok &= PatchBytes(0x53E176, &v, 4) && PatchBytes(0x53E17B, &v, 4);
    float* pe = &g_reflEnvRes; float* pc = &g_reflCubeRes;
    ok &= PatchBytes(0x53E12C, &pe, 4) && PatchBytes(0x53E16A, &pc, 4) && PatchBytes(0x665474, &g_reflCubeRes, 4);
    ok &= PatchBytes(0x53EB4F, &g_reflEnvScale, 4) && PatchBytes(0x53EB54, &g_reflEnvScale, 4) &&
          PatchBytes(0x53ED47, &g_reflEnvScale, 4) && PatchBytes(0x53ED4C, &g_reflEnvScale, 4);
    if (g_cfg.reflBrightness != 1.0f) {
        float b = 255.0f * g_cfg.reflBrightness;
        v = b < 0 ? 0 : b > 255.0f ? 255 : (DWORD)b;
        ok &= PatchBytes(0x53E073, &v, 4) && PatchBytes(0x53E079, &v, 4) && PatchBytes(0x53E080, &v, 4);
    }
    const BYTE nop6[6] = { 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 };
    if (g_cfg.reflFullDetail) ok &= PatchBytes(0x53E8FD, nop6, 6);
    if (g_cfg.reflAllFacesEveryFrame) {
        BYTE jmp[5] = { 0xE9 };
        DWORD rel = reinterpret_cast<DWORD>(&ReflFaceTwoThenRest) - (0x53E8E8 + 5);
        memcpy(jmp + 1, &rel, 4);
        ok &= PatchBytes(0x53E85F, nop6, 6) && PatchBytes(0x53E8E8, jmp, 5);
    }
    Logf("HD reflections: %s (cubemap %d, environment map %d, brightness %.2f, full detail %d, "
         "all faces every frame %d)\n", ok ? "on" : "patch FAILED", cube, env, g_cfg.reflBrightness,
         g_cfg.reflFullDetail, g_cfg.reflAllFacesEveryFrame);
}

// ---------------------------------------------------------------------------
// Widescreen (v0.17)
//
// Based on the findings of HP2WSFix by Lovro Pleše (xan1242) / hdbeefup,
// https://github.com/hdbeefup/hp2wsfix - MIT License:
//   Copyright (c) 2019 Lovro Pleše
//   Permission is hereby granted, free of charge, to any person obtaining a copy
//   of this software and associated documentation files (the "Software"), to deal
//   in the Software without restriction, including without limitation the rights
//   to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
//   copies of the Software, and to permit persons to whom the Software is
//   furnished to do so, subject to the following conditions: The above copyright
//   notice and this permission notice shall be included in all copies or
//   substantial portions of the Software. THE SOFTWARE IS PROVIDED "AS IS",
//   WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED.
//
// Reworked rather than ported. HP2WSFix widened the GUI's 800x600 layout space
// and then re-placed ~300 elements by name, so every screen or element it
// didn't list ended up off-center (backgrounds, parts of the HUD). Here:
//
//  - Menus and HUD: the GUI's layout space stays 800x600 and is drawn with one
//    uniform scale, centered (FE setup 0x595270). Nothing is distorted and every
//    screen is centered without per-element tables.
//  - The game's screen -> layout conversions (0x5954A0 rects, 0x595550 mouse)
//    computed screen*invScale - offset instead of (screen - offset)*invScale -
//    harmless while the offset was always 0, wrong once it isn't. Fixed.
//  - Full-screen background images are scaled to fill the screen, and HUD
//    elements placed on the left/right side of the HUD are moved to the screen
//    edges. Both are done while the GUI draws them (its recursive draw,
//    0x591350): the renderer converts every layout coordinate - images, text,
//    clip rects - through the same scale/offset globals, so changing those for
//    the duration of one widget's draw moves or scales that widget and its
//    children consistently. Layout data and the GUI's own clipping are untouched.
//  - 3D: the projection (0x54FD80) treats the FOV as horizontal and multiplies
//    the vertical scale by the aspect - at 16:9 that cut the top and bottom off
//    (Vert-). Projections made for 4:3 that fill the screen get the FOV widened
//    so the vertical view matches 4:3 (Hor+). One hook instead of HP2WSFix's
//    16 camera ini hooks; viewports that aren't full screen (mirror, menu car
//    previews) are left as they were.
//  - Resolution: the menus are hard-coded to 800x600 (mode pick, validation and
//    fallback); they can now run at the in-game resolution (no mode switch when
//    a race starts) or the desktop's. The options list limiter is removed.
//    FMVs are scaled to fit with their aspect kept, centered (they were scaled
//    to a fixed 800x600 pixel box).
//  - Not taken over from HP2WSFix: the reflection aspect patches (0x53EB84,
//    0x53ED7C - the reflection map is square, changing them warps it), the
//    GUI aspect constant (0x65D900, still right for a 4:3 layout space), the
//    save folder redirect and the memory pool sizes (not widescreen related).
// ---------------------------------------------------------------------------
namespace ws {
    struct GRect  { DWORD vt; int y, x, w, h; };      // GUI rect (vtable 0x65E010)
    struct GPoint { DWORD vt; int x, y; };            // GUI point (vtable 0x65E1F8)
    float& xscale = *reinterpret_cast<float*>(0x6C2DA8);
    float& yscale = *reinterpret_cast<float*>(0x6C2DAC);
    float& invx   = *reinterpret_cast<float*>(0x6C2DB0);
    float& invy   = *reinterpret_cast<float*>(0x6C2DB4);
    float& xpos   = *reinterpret_cast<float*>(0x84BC48);
    float& ypos   = *reinterpret_cast<float*>(0x84BC4C);
    int& virtY = *reinterpret_cast<int*>(0x84B808);
    int& virtX = *reinterpret_cast<int*>(0x84B80C);
    int& virtW = *reinterpret_cast<int*>(0x84B810);
    int& virtH = *reinterpret_cast<int*>(0x84B814);
    int& scrY  = *reinterpret_cast<int*>(0x84B81C);
    int& scrX  = *reinterpret_cast<int*>(0x84B820);
    int& scrW  = *reinterpret_cast<int*>(0x84B824);
    int& scrH  = *reinterpret_cast<int*>(0x84B828);
    const DWORD fnFESetup = 0x595270, fnRectToLayout = 0x5954A0, fnPointToLayout = 0x595550;
    const DWORD fnWidgetRender = 0x591350, fnReadBounds = 0x59B840, callReadBounds = 0x59410A;
    const DWORD fnProjection = 0x54FD80, callProjection = 0x40D680;
    const DWORD fnValidateModes = 0x5418C0, callValidate1 = 0x5419A2, callValidate2 = 0x5419C4;
    const DWORD fnFindMode = 0x5413B0;            // int cdecl(table, w, h, bpp)
    const DWORD movieHook = 0x4D7B97, movieCont = 0x4D7BD5;
    const DWORD pView = 0x722C08;                 // main 3D view (+0x14 vp width, +0x18 height)

    const hp2::Sig kSigs[] = {
        { fnFESetup,       "\x55\x8B\xEC\x51\x8B\x45\x08\x8B", 8 },
        { 0x5952B8,        "\xA3\x14\xB8\x84\x00", 5 },                 // mov [virtH],eax
        { 0x595304,        "\xD9\x1D\xA8\x2D\x6C\x00", 6 },             // fstp [xscale]
        { fnRectToLayout,  "\x55\x8B\xEC\x83\xEC\x10\xC7\x45", 8 },
        { 0x5954BD,        "\xD8\x0D\xB4\x2D\x6C\x00\xD8\x25\x4C\xBC\x84\x00", 12 }, // fmul invy / fsub ypos
        { 0x462FDF,        "\xE8\xBC\x24\x13\x00", 5 },                 // call RectToLayout (text size)
        { fnPointToLayout, "\x55\x8B\xEC\x83\xEC\x08\xC7\x45", 8 },
        { fnWidgetRender,  "\x55\x8B\xEC\x83\xEC\x08\x53\x8B\xD9", 9 },
        { 0x59138E,        "\xFF\x52\x48", 3 },                         // this->BeginPaint
        { callReadBounds,  "\xE8\x31\x77\x00\x00", 5 },                 // GUI.mBounds -> widget+0x60
        { 0x5940FF,        "\x8D\x47\x60", 3 },
        { callProjection,  "\xE8\xFB\x26\x14\x00", 5 },
        { 0x40D670,        "\xD9\x46\x34\xD8\x4D\x0C", 6 },             // aspect = cam+0x34 * arg
        { callValidate1,   "\xE8\x19\xFF\xFF\xFF", 5 },
        { callValidate2,   "\xE8\xF7\xFE\xFF\xFF", 5 },
        { fnFindMode,      "\x55\x8B\xEC\x8B\x45\x0C\x53\x83", 8 },
        { 0x541483,        "\x68\x58\x02\x00\x00\x68\x20\x03\x00\x00", 10 },
        { 0x54149F,        "\x68\x58\x02\x00\x00\x68\x20\x03\x00\x00", 10 },
        { 0x5414DA,        "\xC7\x46\x10\x20\x03\x00\x00\xC7\x46\x14\x58\x02\x00\x00", 14 },
        { 0x5415EE,        "\x68\x58\x02\x00\x00\x68\x20\x03\x00\x00", 10 },
        { 0x54160D,        "\xC7\x46\x10\x20\x03\x00\x00\xC7\x46\x14\x58\x02\x00\x00", 14 },
        { 0x541897,        "\x81\xFF\x20\x03\x00\x00", 6 },
        { 0x54189F,        "\x81\xFB\x58\x02\x00\x00", 6 },
        { 0x46F3A5,        "\x39\x1D\xAC\xEF\x83\x00\x74\x44", 8 },     // LimitResolution check
        { movieHook,       "\x0F\xBF\x40\x06\xDB\x45\x10\x89", 8 },
        { 0x4D7BA9,        "\x83\xC4\x1C", 3 },
        { movieCont,       "\x8B\xCF\xE8\xF4\x2F\x07\x00", 7 },
    };
}

static bool  g_wsOn = false;          // widescreen code installed
static float g_wsS = 1.0f;            // uniform layout -> screen scale
static float g_wsOffX = 0, g_wsOffY = 0;   // centering margins in pixels
static int   g_wsVX = 0, g_wsVY = 0, g_wsVW = 800, g_wsVH = 600;   // layout space
static int   g_wsSX = 0, g_wsSY = 0, g_wsSW = 800, g_wsSH = 600;   // screen rect
static int   g_wsLogLines = 0;
static unsigned long g_wsProjFixed = 0, g_wsShifted = 0, g_wsBgScaled = 0;

static bool WsActive() { return g_wsOn && (g_wsOffX > 0.5f || g_wsOffY > 0.5f); }

// FE setup (0x595270), cdecl(layoutRect, screenRect). Same globals as the
// original; with widescreen on the scale is uniform and the layout centered.
static void __cdecl Ws_FESetup(const ws::GRect* v, const ws::GRect* s) {
    if (!v->w || !v->h || !s->w || !s->h) return;
    ws::virtH = v->h; ws::virtW = v->w; ws::scrW = s->w; ws::scrH = s->h;
    ws::scrX = s->x;  ws::scrY = s->y;  ws::virtY = v->y; ws::virtX = v->x;
    float sx = (float)s->w / v->w, sy = (float)s->h / v->h;
    g_wsVX = v->x; g_wsVY = v->y; g_wsVW = v->w; g_wsVH = v->h;
    g_wsSX = s->x; g_wsSY = s->y; g_wsSW = s->w; g_wsSH = s->h;
    if (g_cfg.widescreen && g_cfg.wsCenterUI) {
        float u = sx < sy ? sx : sy;
        g_wsS = u;
        g_wsOffX = (s->w - v->w * u) * 0.5f;
        g_wsOffY = (s->h - v->h * u) * 0.5f;
        if (g_wsOffX < 0.5f) g_wsOffX = 0;
        if (g_wsOffY < 0.5f) g_wsOffY = 0;
        ws::xscale = ws::yscale = u;
        ws::xpos = s->x + g_wsOffX - v->x * u;
        ws::ypos = s->y + g_wsOffY - v->y * u;
    } else {
        g_wsS = sx; g_wsOffX = g_wsOffY = 0;
        ws::xscale = sx; ws::yscale = sy;
        ws::xpos = (float)(s->x - v->x);              // as the game did
        ws::ypos = (float)(s->y - v->y);
    }
    ws::invx = 1.0f / ws::xscale;
    ws::invy = 1.0f / ws::yscale;
    static int lastW = 0, lastH = 0;
    if (s->w != lastW || s->h != lastH) {
        lastW = s->w; lastH = s->h;
        Logf("widescreen: GUI %dx%d on %dx%d screen: scale %.3f, offset %.0f,%.0f%s\n",
             v->w, v->h, s->w, s->h, ws::xscale, ws::xpos, ws::ypos,
             WsActive() ? " (centered)" : "");
    }
}

// Screen -> layout conversions, done the right way round.
static ws::GRect* __cdecl Ws_RectToLayout(ws::GRect* out, const ws::GRect* in) {
    int bottom = lrintf((in->h + in->y - ws::ypos) * ws::invy);
    int right  = lrintf((in->w + in->x - ws::xpos) * ws::invx);
    int top    = lrintf((in->y - ws::ypos) * ws::invy);
    int left   = lrintf((in->x - ws::xpos) * ws::invx);
    out->vt = 0x65E010; out->y = top; out->x = left; out->w = right - left; out->h = bottom - top;
    return out;
}
// Text measuring (0x462C60) converts a size measured from 0,0 with the same
// function - that must stay a pure scale (no offset), as in the game.
static ws::GRect* __cdecl Ws_SizeToLayout(ws::GRect* out, const ws::GRect* in) {
    int bottom = lrintf((in->h + in->y) * ws::invy);
    int right  = lrintf((in->w + in->x) * ws::invx);
    int top    = lrintf(in->y * ws::invy);
    int left   = lrintf(in->x * ws::invx);
    out->vt = 0x65E010; out->y = top; out->x = left; out->w = right - left; out->h = bottom - top;
    return out;
}
static ws::GPoint* __cdecl Ws_PointToLayout(ws::GPoint* out, const ws::GPoint* in) {
    int y = lrintf((in->y - ws::ypos) * ws::invy);
    int x = lrintf((in->x - ws::xpos) * ws::invx);
    out->vt = 0x65E1F8; out->x = x; out->y = y;
    return out;
}

// --- widget classes, recorded when a widget reads its GUI.mBounds -------------
struct WsWidget { DWORD w, vt; BYTE flags; char name[23]; };
enum { kWsHudView = 1, kWsImage = 2, kWsActor = 4, kWsLogged = 8 };
static const int kWsTable = 8192;
static WsWidget g_wsWidgets[kWsTable];

static WsWidget* WsSlot(DWORD w, bool insert) {
    unsigned h = (w >> 3) * 2654435761u;
    for (int i = 0; i < 16; i++) {
        WsWidget& e = g_wsWidgets[(h + i) & (kWsTable - 1)];
        if (e.w == w) return &e;
        if (!e.w) return insert ? &e : nullptr;
    }
    return insert ? &g_wsWidgets[h & (kWsTable - 1)] : nullptr;   // full chain: reuse
}
static const WsWidget* WsFind(DWORD w) {
    if (!w) return nullptr;
    WsWidget* e = WsSlot(w, false);
    if (!e || e->vt != *reinterpret_cast<DWORD*>(w)) return nullptr;   // freed and reused
    return e;
}

typedef DWORD (__fastcall *ReadBounds_t)(void* reader, void* edx, const char* key, void* rect, void* def);
static DWORD __fastcall Ws_ReadBounds(void* reader, void*, const char* key, void* rect, void* def) {
    DWORD r = reinterpret_cast<ReadBounds_t>(ws::fnReadBounds)(reader, nullptr, key, rect, def);
    DWORD w = reinterpret_cast<DWORD>(rect) - 0x60;
    const char* name = nullptr;
    if (!SafeRead(reinterpret_cast<DWORD>(reader) + 8, reinterpret_cast<DWORD*>(&name)) || !name ||
        IsBadReadPtr(name, 1))
        return r;
    WsWidget* e = WsSlot(w, true);
    e->w = w; e->vt = *reinterpret_cast<DWORD*>(w); e->flags = 0;
    strncpy(e->name, name, sizeof(e->name) - 1); e->name[sizeof(e->name) - 1] = 0;
    const char* cls = strrchr(name, '.'); cls = cls ? cls + 1 : name;
    if (!strcmp(cls, "HudView")) {
        e->flags |= kWsHudView;
        // its parent (<HUD>.GWidget) holds more HUD parts: the minimap, the mirror
        WsWidget* pe = const_cast<WsWidget*>(WsFind(*reinterpret_cast<DWORD*>(w + 0x38)));
        if (pe) pe->flags |= kWsHudView;
    }
    static const char* const kImg[] = { "GStaticImage", "GSimpleImage", "GImageBox", "GAnimImage",
                                        "GImageBar", "HudStaticArt" };
    for (const char* c : kImg) if (!strcmp(cls, c)) e->flags |= kWsImage;
    if (strstr(cls, "Actor")) e->flags |= kWsActor;
    if ((e->flags & kWsHudView) && g_cfg.wsLog && g_wsLogLines < 900) {
        g_wsLogLines++;
        const ws::GRect* rc = static_cast<const ws::GRect*>(rect);
        Logf("widescreen: HUD root %s %d,%d %dx%d\n", name, rc->x, rc->y, rc->w, rc->h);
    }
    return r;
}

// Absolute layout rect of a widget, like the game's LocalToGlobal (0x5932D0).
static void WsGlobalRect(DWORD w, int& x, int& y, int& rw, int& rh) {
    rw = *reinterpret_cast<int*>(w + 0x6C); rh = *reinterpret_cast<int*>(w + 0x70);
    x = 0; y = 0;
    DWORD a = w;
    for (int guard = 0; a && guard < 64; guard++) {
        x += *reinterpret_cast<int*>(a + 0x68); y += *reinterpret_cast<int*>(a + 0x64);
        DWORD fl = *reinterpret_cast<DWORD*>(a + 0x3C);
        if (fl & 8) break;
        DWORD p = *reinterpret_cast<DWORD*>(a + 0x38);
        if (!p) break;
        if (!(fl & 0x40)) { x += *reinterpret_cast<int*>(p + 0x78); y += *reinterpret_cast<int*>(p + 0x7C); }
        a = p;
    }
}

enum WsMode { kWsNone, kWsLeft, kWsRight, kWsStretchX, kWsStretch, kWsFill };
static const char* const kWsModeName[] = { "none", "left edge", "right edge", "stretch width", "stretch", "fill" };

// widgets the GUI draws that were never seen loading (made by code): logged once
static DWORD g_wsSeenNoName[2][64]; static int g_wsSeenNoNameN[2] = {};
static bool WsOnce(const WsWidget* e, DWORD w, int which) {   // which: 0 = draw line, 1 = decision
    BYTE bit = which ? 16 : kWsLogged;
    if (e) { if (e->flags & bit) return false; const_cast<WsWidget*>(e)->flags |= bit; return true; }
    for (int i = 0; i < g_wsSeenNoNameN[which]; i++) if (g_wsSeenNoName[which][i] == w) return false;
    if (g_wsSeenNoNameN[which] < 64) g_wsSeenNoName[which][g_wsSeenNoNameN[which]++] = w;
    return true;
}

static WsMode WsClassify(DWORD w) {
    const WsWidget* e = WsFind(w);
    WsMode m = kWsNone;
    int x = 0, y = 0, rw = 0, rh = 0;
    if (g_cfg.wsLog >= 2 && g_wsLogLines < 900) {      // diagnostics: every widget drawn, once
        if (WsOnce(e, w, 0)) {
            WsGlobalRect(w, x, y, rw, rh);
            const WsWidget* p = WsFind(*reinterpret_cast<DWORD*>(w + 0x38));
            g_wsLogLines++;
            Logf("widescreen: draw %s (vt %06lX) at %d,%d %dx%d, parent %s, %d children\n",
                 e ? e->name : "(made by code)", *reinterpret_cast<DWORD*>(w) & 0xFFFFFF, x, y, rw, rh,
                 p ? p->name : "-", *reinterpret_cast<int*>(w + 0x10));
        }
    }
    // HUD: elements on the HUD's left/right third go to the screen edges. Only
    // the parent has to be known (the minimap is made by code, not loaded).
    if (g_cfg.wsHudToEdges && g_wsOffX > 0.5f) {
        DWORD pw = *reinterpret_cast<DWORD*>(w + 0x38);
        const WsWidget* p = WsFind(pw);
        if (p && (p->flags & kWsHudView) && !(e && (e->flags & kWsImage) && *reinterpret_cast<int*>(w + 0x6C) >=
                                                  *reinterpret_cast<int*>(pw + 0x6C) - 4)) {
            int px = *reinterpret_cast<int*>(w + 0x68), pwid = *reinterpret_cast<int*>(pw + 0x6C);
            int ew = *reinterpret_cast<int*>(w + 0x6C);
            if (pwid > 0 && ew < pwid * 6 / 10) {
                int cx2 = 2 * px + ew;                        // 2 * center
                if (cx2 * 3 < 2 * pwid) m = kWsLeft;
                else if (cx2 * 3 > 4 * pwid) m = kWsRight;
            }
            if (g_cfg.wsLog && g_wsLogLines < 900 && WsOnce(e, w, 1)) {
                g_wsLogLines++;
                Logf("widescreen: HUD %s at %d,%d %dx%d -> %s\n", e ? e->name : "(made by code)", px,
                     *reinterpret_cast<int*>(w + 0x64), ew, *reinterpret_cast<int*>(w + 0x70),
                     m == kWsLeft ? "left edge" : m == kWsRight ? "right edge" : "stays");
            }
            return m;
        }
    }
    if (!e) return kWsNone;
    // full-screen background images (leaves): fill the screen
    if ((e->flags & (kWsImage | kWsActor)) && g_cfg.wsBackgrounds && *reinterpret_cast<int*>(w + 0x10) == 0) {
        WsGlobalRect(w, x, y, rw, rh);
        bool fullW = x <= g_wsVX + 2 && x + rw >= g_wsVX + g_wsVW - 2;
        bool fullH = y <= g_wsVY + 2 && y + rh >= g_wsVY + g_wsVH - 2;
        if (fullW && fullH && g_wsOffX > 0.5f)
            m = (e->flags & kWsActor) ? kWsStretchX : g_cfg.wsBackgrounds == 2 ? kWsFill : kWsStretch;
        else if (fullW && fullH)
            m = (e->flags & kWsActor) ? kWsNone : g_cfg.wsBackgrounds == 2 ? kWsFill : kWsStretch;
        else if (fullW && rh < g_wsVH - 2 && g_wsOffX > 0.5f && (e->flags & kWsImage))
            m = kWsStretchX;                                  // bars / strips
    }
    if (m != kWsNone && g_cfg.wsLog && g_wsLogLines < 900 && WsOnce(e, w, 1)) {
        g_wsLogLines++;
        Logf("widescreen: %s %d,%d %dx%d -> %s\n", e->name, x, y, rw, rh, kWsModeName[m]);
    }
    return m;
}

typedef void (__fastcall *WidgetRender_t)(void* w, void* edx, void* renderer, DWORD flags);
static WidgetRender_t g_wsRenderTramp = nullptr;
static void __fastcall Ws_WidgetRender(void* w, void*, void* renderer, DWORD flags) {
    WsMode m = WsActive() ? WsClassify(reinterpret_cast<DWORD>(w)) : kWsNone;
    if (m == kWsNone) { g_wsRenderTramp(w, nullptr, renderer, flags); return; }
    float sx = ws::xscale, sy = ws::yscale, px = ws::xpos, py = ws::ypos;
    switch (m) {
    case kWsLeft:  ws::xpos -= g_wsOffX; g_wsShifted++; break;
    case kWsRight: ws::xpos += g_wsOffX; g_wsShifted++; break;
    case kWsStretchX:
        ws::xscale = (float)g_wsSW / g_wsVW; ws::xpos = g_wsSX - g_wsVX * ws::xscale; g_wsBgScaled++; break;
    case kWsStretch:
        ws::xscale = (float)g_wsSW / g_wsVW; ws::xpos = g_wsSX - g_wsVX * ws::xscale;
        ws::yscale = (float)g_wsSH / g_wsVH; ws::ypos = g_wsSY - g_wsVY * ws::yscale; g_wsBgScaled++; break;
    case kWsFill: {
        float kx = (float)g_wsSW / g_wsVW, ky = (float)g_wsSH / g_wsVH, k = kx > ky ? kx : ky;
        ws::xscale = ws::yscale = k;
        ws::xpos = g_wsSX + (g_wsSW - g_wsVW * k) * 0.5f - g_wsVX * k;
        ws::ypos = g_wsSY + (g_wsSH - g_wsVH * k) * 0.5f - g_wsVY * k;
        g_wsBgScaled++; break; }
    default: break;
    }
    g_wsRenderTramp(w, nullptr, renderer, flags);
    ws::xscale = sx; ws::yscale = sy; ws::xpos = px; ws::ypos = py;
}

// 3D projection (Hor+). For each view, the viewport the original game would
// have had is this one clipped to a 4:3 screen (centered 4:3 part of ours).
// If the shape differs (race view on 16:9, the showroom car whose widget is
// wider than the 800x600 layout), the FOV is widened so the vertical view and
// the proportions stay as in the original. Views inside that area (mirror,
// small previews) are unchanged.
typedef void (__fastcall *Projection_t)(void* view, void* edx, float fov, float aspect, float zn, float zf);
static int g_wsProjLogged = 0;
static void __fastcall Ws_Projection(void* view, void*, float fov, float aspect, float zn, float zf) {
    if (g_cfg.widescreen && g_cfg.wsFixFOV && fov > 1.0f && fov < 179.0f && aspect > 0.1f) {
        BYTE* v = reinterpret_cast<BYTE*>(view);
        int vx = *reinterpret_cast<int*>(v + 0x0C), vy = *reinterpret_cast<int*>(v + 0x10);
        int vw = *reinterpret_cast<int*>(v + 0x14), vh = *reinterpret_cast<int*>(v + 0x18);
        int sw = ws::scrW, sh = ws::scrH;
        if (vw > 0 && vh > 0 && sw > 0 && sh > 0) {
            float cw = (float)sw, ch = (float)sh;                   // centered 4:3 area
            if (cw * 3.0f > ch * 4.0f) cw = ch * 4.0f / 3.0f; else ch = cw * 3.0f / 4.0f;
            float cx0 = (sw - cw) * 0.5f, cy0 = (sh - ch) * 0.5f;
            float x0 = vx > cx0 ? (float)vx : cx0, x1 = vx + vw < cx0 + cw ? (float)(vx + vw) : cx0 + cw;
            float y0 = vy > cy0 ? (float)vy : cy0, y1 = vy + vh < cy0 + ch ? (float)(vy + vh) : cy0 + ch;
            float a1 = (float)vw / vh;
            if (x1 - x0 > 8.0f && y1 - y0 > 8.0f) {
                float a0 = (x1 - x0) / (y1 - y0), r = a1 / a0;
                bool fix = fabsf(r - 1.0f) > 0.01f;
                if (fix) {
                    const float d2r = 3.14159265f / 180.0f;
                    float f = 2.0f * atanf(tanf(fov * 0.5f * d2r) * r) / d2r;
                    if (f > 170.0f) f = 170.0f;
                    static DWORD seen[20];
                    DWORD key = (DWORD)vw << 16 | (DWORD)vh;
                    bool isNew = true;
                    for (int k = 0; k < g_wsProjLogged; k++) if (seen[k] == key) isNew = false;
                    if (g_cfg.wsLog && isNew && g_wsProjLogged < 20) {
                        seen[g_wsProjLogged++] = key;
                        Logf("widescreen: 3D view %dx%d at %d,%d (4:3 screen: %.0fx%.0f): fov %.1f -> %.1f, "
                             "aspect %.3f -> %.3f\n", vw, vh, vx, vy, x1 - x0, y1 - y0, fov, f, aspect, aspect * r);
                    }
                    fov = f; aspect *= r; g_wsProjFixed++;
                }
            }
        }
    }
    reinterpret_cast<Projection_t>(ws::fnProjection)(view, nullptr, fov, aspect, zn, zf);
}

// Menu display mode = in-game mode (or desktop): patch the 800x600 constants
// right before the game validates/picks its modes.
static DWORD g_wsMenuW = 0, g_wsMenuH = 0;
static void WsPatchMenuMode(DWORD w, DWORD h) {
    PatchBytes(0x541484, &h, 4); PatchBytes(0x541489, &w, 4);
    PatchBytes(0x5414A0, &h, 4); PatchBytes(0x5414A5, &w, 4);
    PatchBytes(0x5414DD, &w, 4); PatchBytes(0x5414E4, &h, 4);
    PatchBytes(0x5415EF, &h, 4); PatchBytes(0x5415F4, &w, 4);
    PatchBytes(0x541610, &w, 4); PatchBytes(0x541617, &h, 4);
    PatchBytes(0x541899, &w, 4); PatchBytes(0x5418A1, &h, 4);
}
typedef bool (__fastcall *ValidateModes_t)(void* caps, void* edx, void* table, DWORD flag);
typedef int (__cdecl *FindMode_t)(void* table, int w, int h, int bpp);
static bool __fastcall Ws_ValidateModes(void* caps, void*, void* table, DWORD flag) {
    if (g_cfg.widescreen && g_cfg.wsMenuResolution && table) {
        int w = 0, h = 0;
        if (g_cfg.wsMenuResolution == 1) {
            w = *reinterpret_cast<int*>(reinterpret_cast<BYTE*>(caps) + 0x20);
            h = *reinterpret_cast<int*>(reinterpret_cast<BYTE*>(caps) + 0x24);
        }
        if (w <= 0 || h <= 0) {
            DEVMODEA dm = {}; dm.dmSize = sizeof(dm);
            if (EnumDisplaySettingsA(nullptr, ENUM_CURRENT_SETTINGS, &dm)) { w = dm.dmPelsWidth; h = dm.dmPelsHeight; }
        }
        FindMode_t find = reinterpret_cast<FindMode_t>(ws::fnFindMode);
        if (w >= 640 && h >= 480 && (find(table, w, h, 32) != -1 || find(table, w, h, 16) != -1)) {
            if ((DWORD)w != g_wsMenuW || (DWORD)h != g_wsMenuH) {
                g_wsMenuW = w; g_wsMenuH = h;
                WsPatchMenuMode(w, h);
                Logf("widescreen: menus at %dx%d\n", w, h);
            }
        } else if (w > 0) {
            Logf("widescreen: %dx%d isn't a display mode the game lists - menus stay at 800x600\n", w, h);
        }
    }
    return reinterpret_cast<ValidateModes_t>(ws::fnValidateModes)(caps, nullptr, table, flag);
}

// FMV: scale to fit, aspect kept, centered (was: stretched to an 800x600 box).
extern "C" void __cdecl Ws_MovieLayout(void* sprite, int w, int h) {
    DWORD* vt = *reinterpret_cast<DWORD**>(sprite);
    typedef void (__fastcall *SetScale_t)(void*, void*, float, float);
    typedef void (__fastcall *SetPos_t)(void*, void*, float, float, float);
    float sx = 800.0f / (w > 0 ? w : 1), sy = 600.0f / (h > 0 ? h : 1), x = 0, y = 0;
    DWORD view = *reinterpret_cast<DWORD*>(ws::pView);
    if (g_cfg.widescreen && g_cfg.wsMovies && w > 0 && h > 0 && view) {
        int vw = *reinterpret_cast<int*>(view + 0x14), vh = *reinterpret_cast<int*>(view + 0x18);
        if (vw > 0 && vh > 0) {
            float s = (float)vw / w < (float)vh / h ? (float)vw / w : (float)vh / h;
            sx = sy = s; x = (vw - w * s) * 0.5f; y = (vh - h * s) * 0.5f;
        }
    }
    reinterpret_cast<SetScale_t>(vt[2])(sprite, nullptr, sx, sy);
    reinterpret_cast<SetPos_t>(vt[1])(sprite, nullptr, x, y, 0.0f);
}
extern "C" void Ws_MovieStub();
extern "C" { DWORD g_wsMovieCont = 0x4D7BD5; }
__asm__(
    ".globl _Ws_MovieStub\n"
    "_Ws_MovieStub:\n"
    "    movswl 6(%eax), %eax\n"          // movie height (width already in edx)
    "    addl $0x1c, %esp\n"              // the original's stack cleanup at 0x4D7BA9
    "    pushl %eax\n"
    "    pushl %edx\n"
    "    pushl %esi\n"                    // sprite
    "    call _Ws_MovieLayout\n"
    "    addl $12, %esp\n"
    "    jmp *_g_wsMovieCont\n");

static bool WsJmp(DWORD at, void* to) {
    BYTE j[5] = { 0xE9 };
    DWORD rel = reinterpret_cast<DWORD>(to) - (at + 5);
    memcpy(j + 1, &rel, 4);
    return PatchBytes(at, j, 5);
}

static void InstallWidescreen() {
    if (!g_knownExe || !g_cfg.widescreen) { Logf("widescreen: off\n"); return; }
    for (const auto& sg : ws::kSigs)
        if (IsBadReadPtr(reinterpret_cast<void*>(sg.addr), sg.len) ||
            memcmp(reinterpret_cast<void*>(sg.addr), sg.bytes, sg.len) != 0) {
            Logf("widescreen: code at 0x%06lX already changed (HP2WSFix.asi or similar loaded? remove it) "
                 "- widescreen left off\n", (unsigned long)sg.addr);
            return;
        }
    // trampoline for the widget draw (first 6 bytes: push ebp / mov ebp,esp / sub esp,8)
    BYTE* t = static_cast<BYTE*>(VirtualAlloc(nullptr, 16, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!t) { Logf("widescreen: out of memory\n"); return; }
    memcpy(t, reinterpret_cast<void*>(ws::fnWidgetRender), 6);
    t[6] = 0xE9;
    DWORD rel = (ws::fnWidgetRender + 6) - reinterpret_cast<DWORD>(t + 11);
    memcpy(t + 7, &rel, 4);
    g_wsRenderTramp = reinterpret_cast<WidgetRender_t>(t);

    bool ok = true;
    if (g_cfg.wsCenterUI) {
        ok &= WsJmp(ws::fnFESetup, (void*)&Ws_FESetup);
        ok &= WsJmp(ws::fnRectToLayout, (void*)&Ws_RectToLayout);
        ok &= PatchCallRel32(0x462FDF, (void*)&Ws_SizeToLayout);   // text measuring
        ok &= WsJmp(ws::fnPointToLayout, (void*)&Ws_PointToLayout);
        ok &= PatchCallRel32(ws::callReadBounds, (void*)&Ws_ReadBounds);
        ok &= WsJmp(ws::fnWidgetRender, (void*)&Ws_WidgetRender);
    }
    if (g_cfg.wsFixFOV) ok &= PatchCallRel32(ws::callProjection, (void*)&Ws_Projection);
    if (g_cfg.wsMenuResolution) {
        ok &= PatchCallRel32(ws::callValidate1, (void*)&Ws_ValidateModes);
        ok &= PatchCallRel32(ws::callValidate2, (void*)&Ws_ValidateModes);
    }
    if (g_cfg.wsUnlockResolutions) { BYTE jmp = 0xEB; ok &= PatchBytes(0x46F3AB, &jmp, 1); }
    if (g_cfg.wsMovies) {
        BYTE j[8] = { 0xE9, 0, 0, 0, 0, 0x90, 0x90, 0x90 };
        DWORD r = reinterpret_cast<DWORD>(&Ws_MovieStub) - (ws::movieHook + 5);
        memcpy(j + 1, &r, 4);
        ok &= PatchBytes(ws::movieHook, j, 8);
    }
    g_wsOn = ok;
    Logf("widescreen: %s (centered UI %d, Hor+ FOV %d, menus at game res %d, backgrounds %d, "
         "HUD to edges %d, movies %d, unlocked modes %d)\n", ok ? "installed" : "patch FAILED",
         g_cfg.wsCenterUI, g_cfg.wsFixFOV, g_cfg.wsMenuResolution, g_cfg.wsBackgrounds,
         g_cfg.wsHudToEdges, g_cfg.wsMovies, g_cfg.wsUnlockResolutions);
}

static void LogWidescreenStats() {
    if (!g_wsOn) return;
    if (g_wsProjFixed || g_wsShifted || g_wsBgScaled)
        Logf("  widescreen: Hor+ projections %lu | HUD draws moved to edges %lu | backgrounds scaled %lu\n",
             g_wsProjFixed, g_wsShifted, g_wsBgScaled);
    g_wsProjFixed = g_wsShifted = g_wsBgScaled = 0;
}

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
        InstallTickPatch();
        InstallSteeringPatch();
        InstallAnimClockPatch();
        InstallPropsPatch();
        InstallParticlesPatch();
        InstallCharacterAnimPatch();
        InstallInterpolation();
        InstallReflections();
        InstallWidescreen();

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
