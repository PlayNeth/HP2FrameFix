// HP2FrameFix.asi  v0.13.1
// Frame pacing, game clock fix, EAX-on-startup fix and loading speed-up
// for Need for Speed: Hot Pursuit 2 (PC)
//
// Addresses below were found by disassembling NFSHP2.exe
// (SHA-1 63147101c2ca814e964f31c06730b1292093d7f4, PE timestamp 0x3DDDE96C).
// Every patch checks the original bytes first and disables itself on a
// different exe, so the ASI is still safe to load on other versions.
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
#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

#define HP2FF_VERSION "0.13.2"

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
};

static Config g_cfg;
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
            fclose(f);
        }
        return;
    }
    char line[256];
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
    }
    fclose(f);
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
        if (g_cfg.fixDriverShiftAnim) cs->looped = 0;
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
        cs->elapsed = 0;
        if (g_cfg.fixDriverShiftAnim) cs->looped = 0;   // nothing ended during this call
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
    cs->looped = looped ? 1 : 0;
    return looped;
}

static void InstallAnimClockPatch() {
    if (!g_knownExe || (!g_cfg.smoothSlowMotionAnims && !g_cfg.fixDriverShiftAnim && !g_cfg.animDiag)) return;
    InitializeCriticalSection(&g_animLock);
    BYTE jmp[5] = { 0xE9 };
    DWORD rel = reinterpret_cast<DWORD>(&Hook_AnimAdvance) - (hp2::fnAnimAdvance + 5);
    memcpy(jmp + 1, &rel, 4);
    bool ok = PatchBytes(hp2::fnAnimAdvance, jmp, sizeof(jmp));
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
