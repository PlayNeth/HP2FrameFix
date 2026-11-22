#include "common.h"
#include "stats.h"
#include "config.h"
#include "log.h"
#include "helpers.h"
#include "timing.h"
#include "loading.h"
#include "audio.h"
#include "ticks.h"
#include "animation.h"
#include "interp.h"
#include "widescreen.h"

// ---------------------------------------------------------------------------
// Stats
// ---------------------------------------------------------------------------
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

void LogStats() {
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
Present_t g_origPresent = nullptr;

HRESULT STDMETHODCALLTYPE Hook_Present(
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
