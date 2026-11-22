#include "common.h"
#include "timing.h"
#include "config.h"
#include "log.h"
#include "helpers.h"

// ---------------------------------------------------------------------------
// Timing
// ---------------------------------------------------------------------------
LARGE_INTEGER g_qpcFreq = {};
HANDLE g_waitTimer = nullptr;
bool   g_timerResRaised = false;

double QpcToMs(LONGLONG delta) {
    if (g_qpcFreq.QuadPart == 0) return 0.0;
    return (double)delta * 1000.0 / (double)g_qpcFreq.QuadPart;
}
LONGLONG QpcNow() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }

// Game clock hooks: GetTickCount / timeGetTime backed by QPC
typedef DWORD (WINAPI *GetTickCount_t)(void);
typedef DWORD (WINAPI *timeGetTime_t)(void);
static GetTickCount_t g_origGetTickCount = nullptr;
static timeGetTime_t  g_origTimeGetTime  = nullptr;
std::atomic<unsigned long> g_gtcCalls{0}, g_tgtCalls{0};
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

void PatchIAT_Timers() {
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

void InitWaitTimer() {
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

void WaitUntil(LONGLONG deadline) {
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
