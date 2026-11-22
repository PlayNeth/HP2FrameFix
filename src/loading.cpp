#include "common.h"
#include "loading.h"
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
// Loading speed-up: throttle the loading-screen render inside Progress::Advance
// ---------------------------------------------------------------------------
typedef int (__cdecl *RenderScreen_t)(void* screen);
static RenderScreen_t g_origRenderScreen = reinterpret_cast<RenderScreen_t>(hp2::fnRenderScreen);

static LONGLONG g_loadLastStepQpc   = 0;
static LONGLONG g_loadStartQpc      = 0;
static LONGLONG g_loadLastRenderQpc = 0;
static bool     g_loadActive        = false;
unsigned long g_loadSteps = 0, g_loadRenders = 0, g_loadPresents = 0;
static double   g_loadRenderMs = 0.0;
static int      g_loadCount = 0;

bool LoadingNow(LONGLONG now) {
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

void EndLoadIfIdle(LONGLONG now) {
    if (!g_loadActive || QpcToMs(now - g_loadLastStepQpc) < 1000.0) return;
    g_loadActive = false;
    g_loadCount++;
    double total = QpcToMs(g_loadLastStepQpc - g_loadStartQpc);
    Logf("load #%d: %lu steps, %lu screen renders (%.0f ms), %lu presents, "
         "first->last step %.0f ms\n",
         g_loadCount, g_loadSteps, g_loadRenders, g_loadRenderMs, g_loadPresents, total);
}

void InstallLoadingPatch() {
    if (!g_knownExe) return;
    if (PatchCallRel32(hp2::callLoadRender, (void*)&Hook_LoadRender))
        Logf("loading: render call in Progress::Advance redirected (renderMs=%d, speedUp=%d)\n",
             g_cfg.loadScreenRenderMs, g_cfg.speedUpLoading);
    else
        Logf("loading: patch FAILED (VirtualProtect)\n");
}
