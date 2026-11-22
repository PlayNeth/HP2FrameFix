#include "common.h"
#include "ticks.h"
#include "game.h"
#include "config.h"
#include "log.h"
#include "helpers.h"
#include "timing.h"
#include "portable.h"
#include "animation.h"
#include "interp.h"
#include "reflections.h"

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
std::atomic<unsigned long> g_tickFires{0}, g_tickResyncs{0};

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

void PublishGrid(LONGLONG anchor, LONGLONG period) {
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
    if (!timer && !g_timerResRaised) {                   // Sleep fallback needs 1 ms resolution
        timeBeginPeriod(1);                              // from the first tick, not the first frame
        g_timerResRaised = true;
    }
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
            if (!g_tickThread) {                         // no scheduler: keep the game's own timer
                g_ticksActive = false;
                Logf("game ticks: scheduler thread FAILED - using the game's timer\n");
                return g_origTimeSetEvent ? g_origTimeSetEvent(delay, res, cb, user, flags)
                                          : ::timeSetEvent(delay, res, cb, user, flags);
            }
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

void InstallTickPatch() {
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
unsigned long g_tpfHist[5] = {};

void SampleTicksPerFrame(bool countIt) {
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
