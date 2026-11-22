#include "common.h"
#include "reflections.h"
#include "game.h"
#include "config.h"
#include "log.h"
#include "helpers.h"
#include "portable.h"
#include "animation.h"
#include "interp.h"

// ---------------------------------------------------------------------------
// HD car reflections
//
// Patches from NFSHP2 HD Reflections by Arctic94 (fork of NFS HD Reflections by
// Aero_), https://github.com/Arctic94/NFSHDReflections, GPL-3.0 - so this file,
// as a derived work, is GPL-3.0 too. Changes over that mod:
//  - reflAllFacesEveryFrame: the cubemap update (0x53E7E0) renders face 2 on
//    odd calls and the other five on even ones ([mgr+0x254] parity) but
//    composites all six every frame, mixing two car positions - the seams
//    flicker at HD resolutions. All six now render every frame.
//  - every patched byte is checked first; if another reflections mod already
//    changed them, this section stays off.
// The probe position itself is blended by the interpolation (Hook_ReflUpdate).
// ---------------------------------------------------------------------------

extern "C" void ReflFaceTwoThenRest();
extern "C" { DWORD g_reflFnRenderFace = 0x53EBF0, g_reflContRest = 0x53E8ED; }
__asm__(
    ".globl _ReflFaceTwoThenRest\n"
    "_ReflFaceTwoThenRest:\n"           // from jmp at 0x53E8E8; (view, 2) pushed, esi = manager
    "    movl %esi, %ecx\n"
    "    call *_g_reflFnRenderFace\n"    // render face 2 (thiscall, ret 8)
    "    jmp *_g_reflContRest\n");      // then the other five, then the composite

static float g_reflCubeRes = 128.f, g_reflEnvRes = 128.f, g_reflEnvScale = 0.5f;

void InstallReflections() {
    if (!g_knownExe || !g_cfg.hdReflections) { Logf("HD reflections: off\n"); return; }
    for (const auto& sg : hp2::kReflSigs)
        if (IsBadReadPtr(reinterpret_cast<void*>(sg.addr), sg.len) ||
            memcmp(reinterpret_cast<void*>(sg.addr), sg.bytes, sg.len) != 0) {
            Logf("HD reflections: code at 0x%06lX already changed (NFSHP2HDReflections.asi or similar "
                 "loaded?) - left to that mod\n", (unsigned long)sg.addr);
            return;
        }
    int cube = g_cfg.reflCubemapRes, env = g_cfg.reflEnviromapRes;
    if (g_cfg.reflAutoRes) {
        int y = GetSystemMetrics(SM_CYSCREEN);
        cube = y >= 2160 ? 2048 : y >= 1440 ? 1024 : y >= 1080 ? 512 : 256;
        env = cube;
    }
    if (g_cfg.reflPowerOfTwo) {
        auto pot = [](int x) { if (x <= 0) return 128; int p = 1; while ((p << 1) <= x) p <<= 1; return p; };
        cube = pot(cube); env = pot(env);
    }
    cube = cube < 32 ? 32 : cube > 4096 ? 4096 : cube;
    env  = env  < 32 ? 32 : env  > 4096 ? 4096 : env;
    g_reflCubeRes = (float)cube; g_reflEnvRes = (float)env; g_reflEnvScale = (float)env / 256.0f;

    DWORD v; bool ok = true;
    v = env;  ok &= PatchBytes(0x53E144, &v, 4) && PatchBytes(0x53E149, &v, 4);
    v = cube; ok &= PatchBytes(0x53E176, &v, 4) && PatchBytes(0x53E17B, &v, 4);
    float* pe = &g_reflEnvRes; float* pc = &g_reflCubeRes;
    // Only the two viewport reads are redirected; the 128.0f at 0x665474 that
    // they used is left alone (nothing else in the game reads it, so writing
    // it was never needed).
    ok &= PatchBytes(0x53E12C, &pe, 4) && PatchBytes(0x53E16A, &pc, 4);
    ok &= PatchBytes(0x53EB4F, &g_reflEnvScale, 4) && PatchBytes(0x53EB54, &g_reflEnvScale, 4) &&
          PatchBytes(0x53ED47, &g_reflEnvScale, 4) && PatchBytes(0x53ED4C, &g_reflEnvScale, 4);
    if (g_cfg.reflBrightness != 1.0f) {
        float b = 255.0f * g_cfg.reflBrightness;
        v = b < 0 ? 0 : b > 255.0f ? 255 : (DWORD)b;
        ok &= PatchBytes(0x53E073, &v, 4) && PatchBytes(0x53E079, &v, 4) && PatchBytes(0x53E080, &v, 4);
    }
    const BYTE nop6[6] = { 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 };
    if (g_cfg.reflFullDetail) ok &= PatchBytes(0x53E8FD, nop6, 6);
    if (g_cfg.reflAllFacesEveryFrame) {
        BYTE jmp[5] = { 0xE9 };
        DWORD rel = reinterpret_cast<DWORD>(&ReflFaceTwoThenRest) - (0x53E8E8 + 5);
        memcpy(jmp + 1, &rel, 4);
        ok &= PatchBytes(0x53E85F, nop6, 6) && PatchBytes(0x53E8E8, jmp, 5);
    }
    Logf("HD reflections: %s (cubemap %d, environment map %d, brightness %.2f, full detail %d, "
         "all faces every frame %d)\n", ok ? "on" : "patch FAILED", cube, env, g_cfg.reflBrightness,
         g_cfg.reflFullDetail, g_cfg.reflAllFacesEveryFrame);
}
