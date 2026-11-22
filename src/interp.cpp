#include "common.h"
#include "interp.h"
#include "game.h"
#include "config.h"
#include "log.h"
#include "helpers.h"
#include "timing.h"
#include "portable.h"
#include "animation.h"
#include "reflections.h"

// ---------------------------------------------------------------------------
// Present hook
// ---------------------------------------------------------------------------
std::atomic<bool> g_firstPresentSeen{false};
LONGLONG g_lastPresentQpc = 0;
LONGLONG g_nextDeadline   = 0;
LONGLONG g_lastReanchor   = 0;
bool     g_gridUnstable   = false;
unsigned long long g_presentCount = 0, g_intervalCount = 0;
double g_minDelta = 1e9, g_maxDelta = 0.0, g_sumDelta = 0.0;

// ---------------------------------------------------------------------------
// Interpolation
//
// The world steps at 60 Hz (WorldStep 0x4D00A0, called from 0x4D0370) and the
// game draws whatever the last step left. Racing cars are re-posed at render
// time from their physics bodies (0x4CA860 -> CarModel::SetTransform 0x42B940
// with body pos +0x2A8 / 3x3 rotation +0x2B4, z flipped), and the view is built
// from the active camera object of each player view (0x40D620: eye +8,
// target +0x14, fov +0x20, up +0x40). The camera director reads the physics
// bodies, not the car models, so blended car models never feed back into the
// simulation.
//
// After every step we snapshot the bodies and cameras (time-stamped with the
// frame's display time); at render time each car's SetTransform and each view
// build get values blended between the two snapshots around "display time
// minus interpDelay steps". Anything that doesn't match the newest snapshot
// (replays, resets, cameras moved outside a step) is drawn as the game had it.
// ---------------------------------------------------------------------------

typedef void  (__cdecl    *WorldStep_t)(int step);
typedef void  (__fastcall *CarSetOwner_t)(void* car, void* edx, void* veh);
typedef void  (__fastcall *CarSetXform_t)(void* car, void* edx, void* view, const float* pos, const float* rot);
typedef DWORD (__fastcall *BuildView_t)(void* owner, void* edx, DWORD a, DWORD b, DWORD c);
typedef void  (__fastcall *CarDraw_t)(void* car, void* edx, void* view);

static const int kIpCars = 32, kIpCams = 4, kIpRing = 4;
struct IpCar { DWORD veh; float x[12]; };                 // body pos (3) + rotation (9), physics space
struct IpCam { DWORD owner, cam, activeId; float f[10]; }; // eye 3, target 3, fov, up 3
struct IpSnap { int idx; int nCars, nCams; IpCar cars[kIpCars]; IpCam cams[kIpCams]; };
static IpSnap g_ipRing[kIpRing];
static int    g_ipHead = 0, g_ipCount = 0, g_ipLastIdx = 0;
static LONGLONG g_ipLastStepQpc = 0;

struct IpVeh { DWORD veh, car; unsigned long long seen; };
static IpVeh  g_ipVehs[kIpCars]; int g_ipNVehs = 0;
static DWORD  g_ipOwners[kIpCams]; int g_ipNOwners = 0;
static unsigned long long g_ipOwnerSeen[kIpCams];
static DWORD  g_ipPendingCar = 0, g_ipPendingVeh = 0;
// A view built this frame with an un-blended camera (cut / switch / mismatch):
// draw the cars un-blended too, or they'd sit a step away from the camera.
static unsigned long long g_ipCutFrame = ~0ull;
// A frame whose camera can't be blended between its two steps (look back, a
// camera switch, a flip) gets the camera of the earlier one, moved along with
// the car it looks at to the frame's own time (CarryCam), and the cars blended
// as in any frame. Drawn as the game has things now instead,
// the frame was a step or more ahead of the blended ones around it, and the
// picture jumped forward and back at every cut. If no car is near the camera's
// target, the cars are drawn as that step had them too (g_ipSnap).
static unsigned long long g_ipSnapFrame = ~0ull;
static const IpSnap* g_ipSnap = nullptr;
static unsigned long long g_ipCamCutFrame = ~0ull;         // (the frame last counted as a cut, for the stats)
unsigned long g_ipCutFrames = 0;

// statistics
unsigned long g_ipFrames = 0, g_ipFramesBlended = 0, g_ipAhead = 0, g_ipBehind = 0,
                     g_ipCarsBlended = 0, g_ipCarsPass = 0, g_ipCarMismatch = 0, g_ipCarJump = 0,
                     g_ipCamsBlended = 0, g_ipCamCut = 0, g_ipCamMismatch = 0, g_ipResets = 0,
                     g_ipClockSnaps = 0, g_ipSteps = 0, g_ipDrawListB = 0, g_ipDrawSimCar = 0;
double g_ipAlphaSum = 0;

// --- frame display time --------------------------------------------------
// With the limiter on, a frame drawn now reaches the screen at the next
// deadline. Without it, follow the frame start times with a light filter.
static LONGLONG g_ftLast = 0, g_ftInterval = 0, g_frameTime = 0;
static unsigned long long g_frameTimeFor = ~0ull;
static LONGLONG SmoothedNow() {
    LONGLONG now = QpcNow();
    if (!g_ftLast) { g_ftLast = now; return now; }
    LONGLONG d = now - g_ftLast;
    if (!g_ftInterval) g_ftInterval = d;
    LONGLONG pred = g_ftLast + g_ftInterval, err = now - pred, ft;
    if (g_ftInterval > 0 && err < g_ftInterval / 2 && err > -g_ftInterval / 2) {
        ft = pred + err / 8;
        g_ftInterval += (d - g_ftInterval) / 16;
    } else {
        ft = now;
        if (d > 0 && d < g_qpcFreq.QuadPart / 10) g_ftInterval = d;
    }
    g_ftLast = ft;
    return ft;
}
static LONGLONG FrameDisplayTime() {
    if (g_frameTimeFor != g_presentCount) {
        g_frameTimeFor = g_presentCount;
        LONGLONG sm = SmoothedNow();
        g_frameTime = g_nextDeadline ? g_nextDeadline : sm;
    }
    return g_frameTime;
}

// --- step clock -------------------------------------------------------------
// E(t) = continuous step index, following step arrivals gently; its rate is
// 60 steps per second times the game's time scale (slow motion).
static bool     g_ipClockValid = false;
static double   g_ipE0 = 0;   static LONGLONG g_ipT0 = 0;
static double   g_ipPeriod = 0;               // QPC ticks per step
static double StepPeriodNow() {
    float sc = 1.0f;
    SafeRead(hp2::pTimeScale, &sc);
    if (!(sc > 0.05f)) sc = 0.05f;
    if (sc > 4.0f) sc = 4.0f;
    return (double)g_qpcFreq.QuadPart / 60.0 / (double)sc;
}
static double ClockAt(LONGLONG t) { return g_ipE0 + (double)(t - g_ipT0) / g_ipPeriod; }
static void ClockOnStep(int idx, LONGLONG t) {
    double p = StepPeriodNow();
    if (!g_ipClockValid) {
        g_ipE0 = idx; g_ipT0 = t; g_ipPeriod = p; g_ipClockValid = true; return;
    }
    double e = ClockAt(t);
    g_ipE0 = e; g_ipT0 = t;                       // rebase (also when the rate changes)
    if (fabs(p - g_ipPeriod) > g_ipPeriod * 0.02) g_ipPeriod = p;
    double err = (double)idx - e;
    if (err > 3.0 || err < -3.0) { g_ipE0 = idx; g_ipClockSnaps++; }
    else g_ipE0 += err / 32.0;
}

// --- snapshots ----------------------------------------------------------------
static bool ReadBody(DWORD veh, float* x) {
    DWORD body = 0;
    if (!SafeRead(veh + 0x6E8, &body) || !body) return false;
    if (IsBadReadPtr(reinterpret_cast<void*>(body + 0x2A8), 48)) return false;
    memcpy(x, reinterpret_cast<void*>(body + 0x2A8), 48);
    return true;
}
static DWORD ActiveCamera(DWORD owner, DWORD* idOut) {
    DWORD slot = 0, id = 0, list = 0, n = 0;
    if (!SafeRead(owner + 0x164, &slot) || slot > 10) return 0;
    if (!SafeRead(owner + 0x138 + slot * 4, &id)) return 0;
    if (!SafeRead(owner + 4, &list) || !SafeRead(owner + 8, &n) || !list || n > 64) return 0;
    for (DWORD i = 0; i < n; i++) {
        DWORD cam = 0, cid = 0;
        if (!SafeRead(list + i * 4, &cam) || !cam || !SafeRead(cam + 0x68, &cid)) continue;
        if (cid == id) { if (idOut) *idOut = id; return cam; }
    }
    return 0;
}
static bool ReadCam(DWORD cam, float* f) {
    if (IsBadReadPtr(reinterpret_cast<void*>(cam + 8), 0x44)) return false;
    memcpy(f, reinterpret_cast<void*>(cam + 8), 7 * 4);          // eye, target, fov
    memcpy(f + 7, reinterpret_cast<void*>(cam + 0x40), 3 * 4);   // up
    return true;
}
static void WriteCam(DWORD cam, const float* f) {
    memcpy(reinterpret_cast<void*>(cam + 8), f, 7 * 4);
    memcpy(reinterpret_cast<void*>(cam + 0x40), f + 7, 3 * 4);
}

// The blended camera stays written into the camera object for the whole frame
// render (not only during BuildView): CarSetXform (0x42B940) copies the camera
// eye into each car (+0xB60) for its sun specular, and the reflection update
// aims the sphere map at it. The raw camera goes back before the next physics
// step and after Present, so the game's own camera update never sees blended
// values. If the game wrote the camera itself meanwhile, its value is kept.
struct IpCamOverride { DWORD cam; float raw[10], blended[10]; bool active; };
static IpCamOverride g_ipCamOv[kIpCams];
static IpCamOverride* FindCamOverride(DWORD cam) {
    for (int i = 0; i < kIpCams; i++) if (g_ipCamOv[i].active && g_ipCamOv[i].cam == cam) return &g_ipCamOv[i];
    return nullptr;
}
static void RestoreCamOverride(IpCamOverride* o) {
    float now[10];
    if (ReadCam(o->cam, now) && !memcmp(now, o->blended, sizeof(now))) WriteCam(o->cam, o->raw);
    o->active = false;
}
void RestoreCamOverrides() {
    for (int i = 0; i < kIpCams; i++) if (g_ipCamOv[i].active) RestoreCamOverride(&g_ipCamOv[i]);
}
static void ApplyCamOverride(DWORD cam, const float* raw, const float* bl) {
    IpCamOverride* o = FindCamOverride(cam);
    if (!o) for (int i = 0; i < kIpCams && !o; i++) if (!g_ipCamOv[i].active) o = &g_ipCamOv[i];
    if (!o) { RestoreCamOverrides(); o = &g_ipCamOv[0]; }
    o->cam = cam; memcpy(o->raw, raw, sizeof(o->raw)); memcpy(o->blended, bl, sizeof(o->blended));
    o->active = true;
    WriteCam(cam, bl);
}

static void OnStep(int idx) {
    g_ipSteps++;
    LONGLONG t = FrameDisplayTime();
    if (g_ipCount && (idx <= g_ipLastIdx || idx - g_ipLastIdx > 16)) {
        g_ipCount = 0; g_ipClockValid = false; g_ipResets++;     // restart / jump
    }
    g_ipLastIdx = idx;
    g_ipLastStepQpc = QpcNow();
    g_ipHead = (g_ipHead + 1) % kIpRing;
    IpSnap& s = g_ipRing[g_ipHead];
    s.idx = idx; s.nCars = 0; s.nCams = 0;
    for (int i = 0; i < g_ipNVehs; i++)
        if (ReadBody(g_ipVehs[i].veh, s.cars[s.nCars].x)) s.cars[s.nCars++].veh = g_ipVehs[i].veh;
    for (int i = 0; i < g_ipNOwners; i++) {
        IpCam& c = s.cams[s.nCams];
        c.owner = g_ipOwners[i];
        c.cam = ActiveCamera(c.owner, &c.activeId);
        if (c.cam && ReadCam(c.cam, c.f)) s.nCams++;
    }
    if (g_ipCount < kIpRing) g_ipCount++;
    ClockOnStep(idx, t);
}

static void __cdecl Hook_WorldStep(int step) {
    RestoreCamOverrides();
    reinterpret_cast<WorldStep_t>(hp2::fnWorldStep)(step);
    if (!*reinterpret_cast<volatile BYTE*>(hp2::pReplayFlag)) OnStep(step + 1);
}

// --- per-frame blend position -------------------------------------------------
struct IpFrame { bool ok; const IpSnap* A; const IpSnap* B; float t; };
static IpFrame g_ipFrame; static unsigned long long g_ipFrameFor = ~0ull;
static const IpFrame& FrameBlend() {
    if (g_ipFrameFor == g_presentCount) return g_ipFrame;
    g_ipFrameFor = g_presentCount;
    IpFrame& f = g_ipFrame; f.ok = false;
    g_ipFrames++;
    if (!g_cfg.interpolate || g_ipCount < 2 || !g_ipClockValid) return f;
    if (QpcNow() - g_ipLastStepQpc > g_qpcFreq.QuadPart / 4) return f;   // paused / replay
    double r = ClockAt(FrameDisplayTime()) - g_cfg.interpDelay;
    const IpSnap* newest = &g_ipRing[g_ipHead];
    if (r >= (double)newest->idx) { g_ipAhead++; return f; }           // newest is the best we have
    const IpSnap *A = nullptr, *B = nullptr;
    for (int k = 0; k + 1 < g_ipCount; k++) {
        const IpSnap* nb = &g_ipRing[(g_ipHead - k + kIpRing) % kIpRing];
        const IpSnap* na = &g_ipRing[(g_ipHead - k - 1 + kIpRing) % kIpRing];
        if (r >= (double)na->idx) { A = na; B = nb; break; }
    }
    if (!A) {
        int k = g_ipCount - 2;
        B = &g_ipRing[(g_ipHead - k + kIpRing) % kIpRing];
        A = &g_ipRing[(g_ipHead - k - 1 + kIpRing) % kIpRing];
        r = A->idx; g_ipBehind++;
    }
    int span = B->idx - A->idx;
    if (span <= 0 || span > 8) return f;
    double t = (r - A->idx) / span;
    f.t = (float)(t < 0 ? 0 : t > 1 ? 1 : t);
    f.A = A; f.B = B; f.ok = true;
    g_ipFramesBlended++; g_ipAlphaSum += f.t;
    return f;
}

// --- blending math -------------------------------------------------------------
struct IpQuat { float w, x, y, z; };
static IpQuat MatToQuat(const float* m) {   // row-major 3x3
    IpQuat q; float tr = m[0] + m[4] + m[8];
    if (tr > 0) { float s = sqrtf(tr + 1.0f) * 2; q.w = 0.25f * s; q.x = (m[7] - m[5]) / s;
                  q.y = (m[2] - m[6]) / s; q.z = (m[3] - m[1]) / s; }
    else if (m[0] > m[4] && m[0] > m[8]) { float s = sqrtf(1.0f + m[0] - m[4] - m[8]) * 2;
                  q.w = (m[7] - m[5]) / s; q.x = 0.25f * s; q.y = (m[1] + m[3]) / s; q.z = (m[2] + m[6]) / s; }
    else if (m[4] > m[8]) { float s = sqrtf(1.0f + m[4] - m[0] - m[8]) * 2;
                  q.w = (m[2] - m[6]) / s; q.x = (m[1] + m[3]) / s; q.y = 0.25f * s; q.z = (m[5] + m[7]) / s; }
    else { float s = sqrtf(1.0f + m[8] - m[0] - m[4]) * 2;
                  q.w = (m[3] - m[1]) / s; q.x = (m[2] + m[6]) / s; q.y = (m[5] + m[7]) / s; q.z = 0.25f * s; }
    return q;
}
static void QuatToMat(const IpQuat& q, float* m) {
    float xx = q.x*q.x, yy = q.y*q.y, zz = q.z*q.z, xy = q.x*q.y, xz = q.x*q.z, yz = q.y*q.z,
          wx = q.w*q.x, wy = q.w*q.y, wz = q.w*q.z;
    m[0] = 1 - 2*(yy+zz); m[1] = 2*(xy-wz);     m[2] = 2*(xz+wy);
    m[3] = 2*(xy+wz);     m[4] = 1 - 2*(xx+zz); m[5] = 2*(yz-wx);
    m[6] = 2*(xz-wy);     m[7] = 2*(yz+wx);     m[8] = 1 - 2*(xx+yy);
}
static bool IsRotation(const float* m) {
    for (int r = 0; r < 3; r++) {
        float l = m[r*3]*m[r*3] + m[r*3+1]*m[r*3+1] + m[r*3+2]*m[r*3+2];
        if (fabsf(l - 1.0f) > 0.02f) return false;
    }
    float det = m[0]*(m[4]*m[8]-m[5]*m[7]) - m[1]*(m[3]*m[8]-m[5]*m[6]) + m[2]*(m[3]*m[7]-m[4]*m[6]);
    return fabsf(det - 1.0f) < 0.05f;
}
// a, b: pos(3) + rot(9). Returns false if the pair shouldn't be blended.
static bool BlendXform(const float* a, const float* b, float t, float* out) {
    float dx = b[0]-a[0], dy = b[1]-a[1], dz = b[2]-a[2];
    if (dx*dx + dy*dy + dz*dz > g_cfg.maxStepDistance * g_cfg.maxStepDistance) return false;
    for (int i = 0; i < 3; i++) out[i] = a[i] + (b[i] - a[i]) * t;
    const float *ma = a + 3, *mb = b + 3; float* mo = out + 3;
    if (!memcmp(ma, mb, 36)) { memcpy(mo, mb, 36); return true; }
    if (!IsRotation(ma) || !IsRotation(mb)) {
        for (int i = 0; i < 9; i++) mo[i] = ma[i] + (mb[i] - ma[i]) * t;
        return true;
    }
    IpQuat qa = MatToQuat(ma), qb = MatToQuat(mb);
    float d = qa.w*qb.w + qa.x*qb.x + qa.y*qb.y + qa.z*qb.z;
    if (d < 0) { qb.w = -qb.w; qb.x = -qb.x; qb.y = -qb.y; qb.z = -qb.z; d = -d; }
    if (d > 1) d = 1;
    if (2.0f * acosf(d) * 57.29578f > g_cfg.maxStepAngle) return false;
    IpQuat q;
    if (d > 0.9995f) {
        q.w = qa.w + (qb.w-qa.w)*t; q.x = qa.x + (qb.x-qa.x)*t;
        q.y = qa.y + (qb.y-qa.y)*t; q.z = qa.z + (qb.z-qa.z)*t;
    } else {
        float th = acosf(d), s = sinf(th), wa = sinf((1-t)*th)/s, wb = sinf(t*th)/s;
        q.w = qa.w*wa + qb.w*wb; q.x = qa.x*wa + qb.x*wb; q.y = qa.y*wa + qb.y*wb; q.z = qa.z*wa + qb.z*wb;
    }
    float l = sqrtf(q.w*q.w + q.x*q.x + q.y*q.y + q.z*q.z);
    if (l < 1e-6f) return false;
    q.w /= l; q.x /= l; q.y /= l; q.z /= l;
    QuatToMat(q, mo);
    return true;
}
// physics space -> what 0x4CA860 hands to SetTransform (z flipped)
static void ToRenderSpace(const float* x, float* pos, float* rot) {
    pos[0] = x[0]; pos[1] = x[1]; pos[2] = -x[2];
    const float* m = x + 3;
    rot[0] = m[0];  rot[1] = m[1];  rot[2] = -m[2];
    rot[3] = m[3];  rot[4] = m[4];  rot[5] = -m[5];
    rot[6] = -m[6]; rot[7] = -m[7]; rot[8] = m[8];
}
static bool NearlyEqual(const float* a, const float* b, int n) {
    for (int i = 0; i < n; i++) {
        float d = fabsf(a[i] - b[i]), s = fabsf(a[i]) + fabsf(b[i]);
        if (d > 1e-4f * (1.0f + s)) return false;
    }
    return true;
}
static const IpCar* FindCar(const IpSnap* s, DWORD veh) {
    for (int i = 0; i < s->nCars; i++) if (s->cars[i].veh == veh) return &s->cars[i];
    return nullptr;
}
static const IpCam* FindCam(const IpSnap* s, DWORD owner) {
    for (int i = 0; i < s->nCams; i++) if (s->cams[i].owner == owner) return &s->cams[i];
    return nullptr;
}

// --- render-time hooks -------------------------------------------------------------
static void RegisterVehicle(DWORD veh, DWORD car) {
    for (int i = 0; i < g_ipNVehs; i++)
        if (g_ipVehs[i].veh == veh) { g_ipVehs[i].car = car; g_ipVehs[i].seen = g_presentCount; return; }
    // drop vehicles not drawn for 10 s worth of frames, then add
    int w = 0;
    for (int i = 0; i < g_ipNVehs; i++)
        if (g_presentCount - g_ipVehs[i].seen < 2000) g_ipVehs[w++] = g_ipVehs[i];
    g_ipNVehs = w;
    if (g_ipNVehs < kIpCars) g_ipVehs[g_ipNVehs++] = { veh, car, g_presentCount };
}

static void __fastcall Hook_CarSetOwner(void* car, void*, void* veh) {
    g_ipPendingCar = reinterpret_cast<DWORD>(car);
    g_ipPendingVeh = reinterpret_cast<DWORD>(veh);
    if (g_cfg.interpolate) RegisterVehicle(g_ipPendingVeh, g_ipPendingCar);
    reinterpret_cast<CarSetOwner_t>(hp2::fnCarSetOwner)(car, nullptr, veh);
}

static void __fastcall Hook_CarSetXform(void* car, void*, void* view, const float* pos, const float* rot) {
    if (g_cfg.interpolate && g_ipCutFrame == g_presentCount) { g_ipCarsPass++; }
    else if (g_cfg.interpolate && reinterpret_cast<DWORD>(car) == g_ipPendingCar && g_ipPendingVeh) {
        const IpFrame& f = FrameBlend();
        const bool snap = g_ipSnapFrame == g_presentCount;
        const IpCar* a = f.ok ? FindCar(snap ? g_ipSnap : f.A, g_ipPendingVeh) : nullptr;
        const IpCar* b = f.ok ? FindCar(snap ? g_ipSnap : f.B, g_ipPendingVeh) : nullptr;
        const IpCar* n = f.ok ? FindCar(&g_ipRing[g_ipHead], g_ipPendingVeh) : nullptr;
        if (a && b && n) {
            float np[3], nr[9];
            ToRenderSpace(n->x, np, nr);
            // only if the newest snapshot is still what the game is drawing
            if (NearlyEqual(np, pos, 3) && NearlyEqual(nr, rot, 9)) {
                float x[12];
                if (BlendXform(a->x, b->x, f.t, x)) {
                    float p[3], m[9];
                    ToRenderSpace(x, p, m);
                    g_ipCarsBlended++;
                    reinterpret_cast<CarSetXform_t>(hp2::fnCarSetXform)(car, nullptr, view, p, m);
                    return;
                }
                g_ipCarJump++;
            } else g_ipCarMismatch++;
        }
        g_ipCarsPass++;
    }
    reinterpret_cast<CarSetXform_t>(hp2::fnCarSetXform)(car, nullptr, view, pos, rot);
}

// A camera that flips or swings in one step (look back on cameras that turn the
// same camera object around, quick pans) must not be blended: halfway through a
// 180-degree flip the view sits inside the car.
static bool CameraJumped(const float* a, const float* b) {
    float dx = b[0]-a[0], dy = b[1]-a[1], dz = b[2]-a[2];
    if (dx*dx + dy*dy + dz*dz > g_cfg.maxStepDistance * g_cfg.maxStepDistance) return true;
    float da[3] = { a[3]-a[0], a[4]-a[1], a[5]-a[2] }, db[3] = { b[3]-b[0], b[4]-b[1], b[5]-b[2] };
    float la = sqrtf(da[0]*da[0] + da[1]*da[1] + da[2]*da[2]);
    float lb = sqrtf(db[0]*db[0] + db[1]*db[1] + db[2]*db[2]);
    if (la < 1e-4f || lb < 1e-4f) return true;
    float c = (da[0]*db[0] + da[1]*db[1] + da[2]*db[2]) / (la * lb);
    if (c < 0.7071f) return true;                                  // view turned > 45 deg
    float ua = sqrtf(a[7]*a[7] + a[8]*a[8] + a[9]*a[9]), ub = sqrtf(b[7]*b[7] + b[8]*b[8] + b[9]*b[9]);
    if (ua > 1e-4f && ub > 1e-4f &&
        (a[7]*b[7] + a[8]*b[8] + a[9]*b[9]) / (ua * ub) < 0.7071f) return true;   // rolled > 45 deg
    if (fabsf(b[6] - a[6]) > 0.2f * (fabsf(a[6]) + 1e-3f)) return true;          // fov jumped > 20 %
    return false;
}

// The blended camera used for this frame's view, per owner, so the reflection
// update (which reads the camera eye itself) can see the same camera.
struct IpViewCam { DWORD owner, cam; unsigned long long frame; float f[10]; };
static IpViewCam g_ipViewCams[kIpCams];
static void RememberViewCam(DWORD owner, DWORD cam, const float* f) {
    int slot = 0;
    for (int i = 0; i < kIpCams; i++) {
        if (g_ipViewCams[i].owner == owner) { slot = i; break; }
        if (g_ipViewCams[i].frame < g_ipViewCams[slot].frame) slot = i;
    }
    IpViewCam& v = g_ipViewCams[slot];
    v.owner = owner; v.cam = cam; v.frame = g_presentCount; memcpy(v.f, f, sizeof(v.f));
}
static const IpViewCam* FindViewCam(DWORD owner) {
    for (int i = 0; i < kIpCams; i++)
        if (g_ipViewCams[i].owner == owner && g_ipViewCams[i].frame == g_presentCount && g_ipViewCams[i].cam)
            return &g_ipViewCams[i];
    return nullptr;
}

static void MarkCutFrame() {
    if (g_ipCutFrame != g_presentCount) { g_ipCutFrame = g_presentCount; g_ipCutFrames++; }
}
static void MarkSnapFrame(const IpSnap* s) {      // (the frame's first view to ask decides for all of them)
    if (g_ipSnapFrame != g_presentCount) { g_ipSnapFrame = g_presentCount; g_ipSnap = s; }
}
// The camera `cam` of step s, at the frame's time: moved as far as the car
// nearest its target moves from that step to the frame (at most half a step).
// The camera's space is the cars' or its mirror in z: whichever puts a car
// nearer the target is taken.
static bool CarryCam(const IpFrame& f, const IpSnap* s, const float* cam, float* out) {
    const IpCar* best = nullptr; float bestD = 30.0f * 30.0f, sign = 1.0f;
    for (int i = 0; i < s->nCars; i++)
        for (int k = 0; k < 2; k++) {
            const float z = k ? -s->cars[i].x[2] : s->cars[i].x[2];
            const float dx = s->cars[i].x[0] - cam[3], dy = s->cars[i].x[1] - cam[4], dz = z - cam[5];
            const float d = dx*dx + dy*dy + dz*dz;
            if (d < bestD) { bestD = d; best = &s->cars[i]; sign = k ? -1.0f : 1.0f; }
        }
    if (!best) return false;
    const IpCar* a = FindCar(f.A, best->veh);
    const IpCar* b = FindCar(f.B, best->veh);
    if (!a || !b) return false;
    float d[3];
    for (int i = 0; i < 3; i++) d[i] = a->x[i] + (b->x[i] - a->x[i]) * f.t - best->x[i];
    d[2] *= sign;
    if (d[0]*d[0] + d[1]*d[1] + d[2]*d[2] > g_cfg.maxStepDistance * g_cfg.maxStepDistance) return false;
    memcpy(out, cam, 10 * sizeof(float));
    for (int i = 0; i < 3; i++) { out[i] += d[i]; out[3 + i] += d[i]; }
    return true;
}

static DWORD __fastcall Hook_BuildView(void* ownerP, void*, DWORD a, DWORD b, DWORD c) {
    BuildView_t orig = reinterpret_cast<BuildView_t>(hp2::fnBuildView);
    DWORD owner = reinterpret_cast<DWORD>(ownerP);
    if (!g_cfg.interpolate || !g_cfg.interpolateCamera) return orig(ownerP, nullptr, a, b, c);
    bool known = false;
    for (int i = 0; i < g_ipNOwners; i++)
        if (g_ipOwners[i] == owner) { known = true; g_ipOwnerSeen[i] = g_presentCount; }
    if (!known) {                 // each race makes a new camera owner: replace the stalest
        int slot = g_ipNOwners < kIpCams ? g_ipNOwners++ : 0;
        if (slot == 0 && g_ipNOwners == kIpCams)
            for (int i = 1; i < kIpCams; i++) if (g_ipOwnerSeen[i] < g_ipOwnerSeen[slot]) slot = i;
        g_ipOwners[slot] = owner; g_ipOwnerSeen[slot] = g_presentCount;
    }
    const IpFrame& f = FrameBlend();
    if (f.ok) {
        const IpCam* ca = FindCam(f.A, owner);
        const IpCam* cb = FindCam(f.B, owner);
        const IpCam* cn = FindCam(&g_ipRing[g_ipHead], owner);
        DWORD id = 0, cam = ActiveCamera(owner, &id);
        float cur[10];
        bool have = false;
        if (cam) {
            if (IpCamOverride* o = FindCamOverride(cam)) {
                float now[10];
                if (ReadCam(cam, now) && !memcmp(now, o->blended, sizeof(now))) {
                    memcpy(cur, o->raw, sizeof(cur)); have = true;    // still ours: raw is the real state
                } else o->active = false;                             // the game moved it meanwhile
            }
            if (!have) have = ReadCam(cam, cur);
        }
        if (ca && cb && cn && cam && have) {
            if (cn->cam != cam || cn->activeId != id) {
                g_ipCamCut++; MarkCutFrame();                   // camera switched since the last step
            } else if (!NearlyEqual(cur, cn->f, 10)) {
                g_ipCamMismatch++; MarkCutFrame();              // moved outside a step
            } else {
                // The steps' cameras are written into the camera drawn now, also when
                // another camera object was the active one at those steps.
                float bl[10];
                const float* use = bl;
                auto cutBetween = [](const IpCam* x, const IpCam* y) {
                    return x->cam != y->cam || x->activeId != y->activeId || CameraJumped(x->f, y->f);
                };
                if (!cutBetween(ca, cb)) {
                    // The step right after a cut isn't shown: the game puts the camera
                    // at a first pose there and at its place a step later (back from
                    // look back, the chase camera starts close and is at its distance
                    // the next step), which showed as the car jumping for a frame. The
                    // later pose is drawn for that whole step, moved back with the car.
                    const IpSnap* before = nullptr;
                    for (int k = 0; k < g_ipCount; k++) {
                        const IpSnap* r = &g_ipRing[(g_ipHead - k + kIpRing) % kIpRing];
                        if (r->idx == f.A->idx - 1) { before = r; break; }
                    }
                    const IpCam* cp = before ? FindCam(before, owner) : nullptr;
                    if (!(cp && cutBetween(cp, ca) && CarryCam(f, f.B, cb->f, bl)))
                        for (int i = 0; i < 10; i++) bl[i] = ca->f[i] + (cb->f[i] - ca->f[i]) * f.t;
                    g_ipCamsBlended++;
                } else {                                        // a cut between the two steps: the earlier one's camera
                    // (no car to go by, or another view decided already: the cars as that step had them)
                    if (g_ipSnapFrame == g_presentCount || !CarryCam(f, f.A, ca->f, bl)) {
                        MarkSnapFrame(f.A);
                        const IpCam* cs = FindCam(g_ipSnap, owner);
                        use = cs ? cs->f : nullptr;
                        if (!cs) MarkCutFrame();                // (not expected: the cars as the game has them, too)
                    }
                    g_ipCamCut++;
                    if (g_ipCamCutFrame != g_presentCount) { g_ipCamCutFrame = g_presentCount; g_ipCutFrames++; }
                }
                if (use) {
                    ApplyCamOverride(cam, cur, use);            // stays until the next step / Present
                    RememberViewCam(owner, cam, use);
                    return orig(ownerP, nullptr, a, b, c);
                }
            }
        }
    }
    if (DWORD cam0 = ActiveCamera(owner, nullptr))                  // drawn unblended: raw camera
        if (IpCamOverride* o = FindCamOverride(cam0)) RestoreCamOverride(o);
    return orig(ownerP, nullptr, a, b, c);
}
typedef void (__fastcall *ReflUpdate_t)(void* mgr, void* edx, void* view, const float* pos);
unsigned long g_ipReflBlended = 0, g_ipReflPass = 0;
unsigned long g_ipReflCamBlended = 0;
static void __fastcall Hook_ReflUpdate(void* mgr, void*, void* view, const float* pos) {
    ReflUpdate_t orig = reinterpret_cast<ReflUpdate_t>(hp2::fnReflUpdate);
    if (g_cfg.interpolate && pos && g_ipCutFrame != g_presentCount) {
        const IpFrame& f = FrameBlend();
        if (f.ok) {
            const IpSnap* n = &g_ipRing[g_ipHead];
            for (int i = 0; i < n->nCars; i++) {
                float np[3], nr[9];
                ToRenderSpace(n->cars[i].x, np, nr);
                if (!NearlyEqual(np, pos, 3)) continue;
                const bool snap = g_ipSnapFrame == g_presentCount;
                const IpCar* a = FindCar(snap ? g_ipSnap : f.A, n->cars[i].veh);
                const IpCar* b = FindCar(snap ? g_ipSnap : f.B, n->cars[i].veh);
                if (!a || !b) break;
                float x[12];
                if (!BlendXform(a->x, b->x, f.t, x)) break;
                float bp[3], br[9];
                ToRenderSpace(x, bp, br);
                g_ipReflBlended++;
                // The update orients the sphere map toward the camera eye, read
                // through the view (0x40CFD0). (Don't touch the body: the game
                // caches the car's draw transform from it during this call.)
                // (The blended camera stays in place for the whole frame.)
                const IpViewCam* vc = FindViewCam(reinterpret_cast<DWORD>(view));
                if (vc && FindCamOverride(vc->cam)) g_ipReflCamBlended++;
                orig(mgr, nullptr, view, bp);
                return;
            }
        }
    }
    g_ipReflPass++;
    orig(mgr, nullptr, view, pos);
}

static void __fastcall Hook_DrawListB(void* car, void*, void* view) {
    g_ipDrawListB++;
    reinterpret_cast<CarDraw_t>(hp2::fnCarDraw)(car, nullptr, view);
}
static void __fastcall Hook_DrawSimCar(void* car, void*, void* view) {
    g_ipDrawSimCar++;
    reinterpret_cast<CarDraw_t>(hp2::fnCarDraw)(car, nullptr, view);
}

void InstallInterpolation() {
    if (!g_knownExe || !g_cfg.interpolate) { Logf("interpolation: off\n"); return; }
    bool ok = PatchCallRel32(hp2::callWorldStep,   (void*)&Hook_WorldStep) &&
              PatchCallRel32(hp2::callCarSetOwner, (void*)&Hook_CarSetOwner) &&
              PatchCallRel32(hp2::callCarSetXform, (void*)&Hook_CarSetXform) &&
              PatchCallRel32(hp2::callBuildView1,  (void*)&Hook_BuildView) &&
              PatchCallRel32(hp2::callBuildView2,  (void*)&Hook_BuildView) &&
              PatchCallRel32(hp2::callDrawListB,   (void*)&Hook_DrawListB) &&
              PatchCallRel32(hp2::callDrawSimCar,  (void*)&Hook_DrawSimCar) &&
              PatchCallRel32(hp2::callReflUpdate,  (void*)&Hook_ReflUpdate);
    Logf("interpolation: %s (camera=%d, delay=%.2f steps)\n", ok ? "installed" : "patch FAILED",
         g_cfg.interpolateCamera, g_cfg.interpDelay);
}
