#include "common.h"
#include "log.h"
#include "config.h"

// ---------------------------------------------------------------------------
// Logging (serialized across threads)
// ---------------------------------------------------------------------------
FILE* g_log = nullptr;
CRITICAL_SECTION g_logLock;
bool g_logLockInit = false;

void Logf(const char* fmt, ...) {
    if (!g_log) return;
    if (g_logLockInit) EnterCriticalSection(&g_logLock);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fflush(g_log);
    if (g_logLockInit) LeaveCriticalSection(&g_logLock);
}

void OpenLog() {
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
