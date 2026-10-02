// HP2FrameFix.asi  v0.9
// Frame pacing, game clock fix, EAX-on-startup fix and loading speed-up
// for Need for Speed: Hot Pursuit 2 (PC)
//
// Addresses below were found by disassembling NFSHP2.exe
// (SHA-1 63147101c2ca814e964f31c06730b1292093d7f4, PE timestamp 0x3DDDE96C).
// Every patch checks the original bytes first and disables itself on a
// different exe, so the ASI is still safe to load on other versions.
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
#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

#define HP2FF_VERSION "0.9"

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
    Logf("preciseGameTicks=%d tickPhase=%.2f smoothDriverSteering=%d\n", g_cfg.preciseGameTicks,
         g_cfg.tickPhase, g_cfg.smoothDriverSteering);
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

        if (curGen != gen || base == 0) {
            gen = curGen;
            // Never fire two ticks closer than half a tick apart.
            LONGLONG earliest = lastFire ? lastFire + tickLen / 2 : now;
            if (earliest < now - tickLen) earliest = now - tickLen;
            if (gp > 0 && tpf > 0) {
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
                PublishGrid(g_nextDeadline, period);   // new frame schedule: re-lock ticks
            } else {
                g_nextDeadline += period;
            }
        } else {
            if (g_nextDeadline) PublishGrid(0, 0);   // limiter paused: free-running ticks
            g_nextDeadline = 0;   // resync cleanly when limiting resumes
        }
    }

    SampleTicksPerFrame(!loading);
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
