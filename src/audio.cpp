#include "common.h"
#include "audio.h"
#include "game.h"
#include "config.h"
#include "log.h"
#include "helpers.h"
#include "loading.h"
#include "portable.h"
#include "animation.h"
#include "interp.h"
#include "reflections.h"

// ---------------------------------------------------------------------------
// EAX-on-startup fix
// ---------------------------------------------------------------------------
// Root cause: the sound engine reads the audio mode (options+0x938) only when it
// starts (0x514A30). At startup it starts with the default mode; the saved
// profile is applied later by Profile::Apply (0x536BA0), which copies the mode
// into the options but never restarts the engine. The options menu does restart
// it (0x446360) when the mode changes, which is why switching by hand works.
// Fix: the three calls of Profile::Apply go through a wrapper; if the profile
// changed the mode of an engine that is already running, the engine is
// restarted once, on the next front-end frame (same call the menu makes).
// ---------------------------------------------------------------------------
static int  g_applyOldMode = -1;
static bool g_audioPending = false;
bool g_inRestartAudio = false;

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
static const char* AudioModeName(int m) {
    return m == 1 ? "stereo" : m == 2 ? "mono" : m == 3 ? "EAX" : m == 0 ? "mode 0" : "unknown mode";
}
extern "C" void __cdecl AudioPreApply() {
    int m, a, r;
    g_applyOldMode = ReadAudioState(&m, &a, &r) ? m : -1;
}
extern "C" void __cdecl AudioPostApply(DWORD result) {
    int m, a, r;
    if (!(result & 0xFF) || !ReadAudioState(&m, &a, &r)) return;
    if (m != g_applyOldMode && r) {
        Logf("audio: profile sets %s (engine running in %s) - restarting the sound engine\n",
             AudioModeName(m), AudioModeName(g_applyOldMode));
        g_audioPending = true;
    } else {
        Logf("audio: profile applied, mode %s%s\n", AudioModeName(m),
             r ? "" : " (sound engine not started yet - it will start in this mode)");
    }
}
extern "C" void ProfileApplyStub();
__asm__(
    ".globl _ProfileApplyStub\n"
    "_ProfileApplyStub:\n"               // replaces call Profile::Apply (thiscall, 1 arg)
    "    pushl %ecx\n"
    "    call _AudioPreApply\n"
    "    popl %ecx\n"
    "    pushl 4(%esp)\n"
    "    movl $0x536BA0, %eax\n"
    "    call *%eax\n"                    // ret 4 pops the copied arg
    "    pushl %eax\n"
    "    pushl %eax\n"
    "    call _AudioPostApply\n"
    "    addl $4, %esp\n"
    "    popl %eax\n"
    "    ret $4\n");

void InstallAudioModeFix() {
    if (!g_knownExe || !g_cfg.fixEAXOnStartup) return;
    static const DWORD kCalls[] = { 0x447B99, 0x45845B, 0x4586E3 };
    int n = 0;
    for (DWORD at : kCalls) {
        BYTE* p = (BYTE*)at;
        if (p[0] == 0xE8 && at + 5 + *(int*)(p + 1) == 0x536BA0 && PatchCallRel32(at, (void*)&ProfileApplyStub)) n++;
    }
    Logf("audio: saved audio mode applied on profile load (%d/3 calls hooked)\n", n);
}

void EAXFixTick(LONGLONG now) {
    if (!g_audioPending) return;
    int mode, active, ready;
    if (!ReadAudioState(&mode, &active, &ready) || !ready || LoadingNow(now)) return;
    g_audioPending = false;
    g_inRestartAudio = true;
    reinterpret_cast<void (*)()>(hp2::fnRestartAudio)();
    g_inRestartAudio = false;
    if (ReadAudioState(&mode, &active, &ready))
        Logf("audio: engine now in %s%s\n", AudioModeName(mode),
             mode == hp2::kAudioModeEAX ? (active ? " - EAX active" : " - EAX not accepted by the driver (is DSOAL installed?)") : "");
}
