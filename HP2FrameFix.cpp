// HP2FrameFix.asi  v0.7.2
// Frame pacing, game clock fix, EAX-on-startup fix and loading speed-up
// for Need for Speed: Hot Pursuit 2 (PC)
//
// Addresses below were found by disassembling NFSHP2.exe
// (SHA-1 63147101c2ca814e964f31c06730b1292093d7f4, PE timestamp 0x3DDDE96C).
// Every patch checks the original bytes first and disables itself on a
// different exe, so the ASI is still safe to load on other versions.
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
//  - Removed the Sleep hooks: NFSHP2.exe does not import Sleep/SleepEx at all,
//    so they never fired.
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

#define HP2FF_VERSION "0.7.2"

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

    struct Sig { DWORD addr; const char* bytes; int len; };
    const Sig kSigs[] = {
        { fnRestartAudio, "\x83\x3D\xE0\x0B\x7A\x00\xFC", 7 },
        { 0x5F2E36,       "\xC6\x05\xE1\xEC\x84\x00\x01", 7 },             // EAX flag set
        { 0x536270,       "\x55\x8B\xEC\x8B\x45\x08\x8B\x8C\x81\x0C\x91\x01\x00", 13 }, // GetOption
        { fnProgAdvance,  "\x8B\x11\x56\x8B\x71\x04\x46\x8B", 8 },
        { callLoadRender, "\xE8\x4D\xEC\x11\x00", 5 },
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
    }
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
            if (!g_nextDeadline || now - g_nextDeadline > period)
                g_nextDeadline = now + period;
            else
                g_nextDeadline += period;
        } else {
            g_nextDeadline = 0;   // resync cleanly when limiting resumes
        }
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
