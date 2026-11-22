#include "common.h"
#include "animation.h"
#include "game.h"
#include "config.h"
#include "log.h"
#include "helpers.h"
#include "portable.h"
#include "interp.h"
#include "reflections.h"

// ---------------------------------------------------------------------------
// Driver steering animation: 15 Hz -> 60 Hz
// ---------------------------------------------------------------------------
static float g_driverSteerStep = 0.025f;   // original 0.1 per 4 physics steps

void InstallSteeringPatch() {
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

static void AnimDiagFlushLocked(double seconds);
void AnimDiagFlush(double seconds) {
    if (!g_cfg.animDiag || !g_animDiagCount || seconds <= 0) return;
    EnterCriticalSection(&g_animLock);
    AnimDiagFlushLocked(seconds);
    LeaveCriticalSection(&g_animLock);
}
static void AnimDiagFlushLocked(double seconds) {
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
    if (counter < cs->lastStep) {                        // step counter reset (new race/phase):
        cs->lastStep = counter;                          // start over from here instead of
        cs->elapsed = 0;                                 // waiting for it to catch up
        if (g_cfg.smoothSlowMotionAnims) {
            EnterCriticalSection(&g_animLock);
            if (AnimClockEntry* e = AnimClockFor(cs)) { e->lastStep = counter; e->lastClock = GameStepClock(); }
            LeaveCriticalSection(&g_animLock);
        }
        return false;
    }

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
                cs->lastStep = counter;
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

    if (g_cfg.animDiag) {                                // shared with the flush at Present
        EnterCriticalSection(&g_animLock);
        AnimDiagRecord(cs, steps);
        LeaveCriticalSection(&g_animLock);
    }
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
typedef void (__fastcall *PlayAnim_t)(void* actor, void* edx, DWORD id, DWORD param);
static void __fastcall Hook_SelectorPlayAnim(void* actor, void*, DWORD id, DWORD param) {
    reinterpret_cast<AnimChannelSet*>(reinterpret_cast<BYTE*>(actor) + 0x34)->looped = 0;
    reinterpret_cast<PlayAnim_t>(hp2::fnPlayAnim)(actor, nullptr, id, param);
}
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

void InstallAnimClockPatch() {
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

void LogTimeScaleChanges() {
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

void InstallParticlesPatch() {
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

void InstallPropsPatch() {
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

void InstallCharacterAnimPatch() {
    if (!g_knownExe || (!g_cfg.smoothCharacterAnims && !g_propsSyncEveryStep)) return;
    bool ok = PatchCallRel32(hp2::callWorldStepMid, (void*)&Hook_WorldStepMid);
    Logf("even-step hook: %s (characters=%d, props=%d)\n", ok ? "installed" : "patch FAILED",
         g_cfg.smoothCharacterAnims, g_propsSyncEveryStep ? 1 : 0);
}
