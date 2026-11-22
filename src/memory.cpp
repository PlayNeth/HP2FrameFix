#include "common.h"
#include "memory.h"
#include "config.h"
#include "log.h"
#include "helpers.h"

// ---------------------------------------------------------------------------
// Memory pools (from HP2WSFix's [MEMORY] section)
//
// At startup (0x53BBC0, called from 0x537819) the game mallocs one general
// heap and then carves fixed-size class pools out of it (0x53B9E0, each via
// 0x53BB30). The sizes are push/mov immediates. Mods with high-poly cars or
// HD textures overflow the render/track/UI pools. Korean language uses bigger
// render/UI/general sizes in separate immediates.
// Differences from HP2WSFix: the Korean UI size is patched at 0x53BA36 (it
// wrote 0x53BA35, the opcode byte, which corrupts the instruction on Korean),
// and the general heap grows with the pools by default instead of having to
// be sized by hand (pools larger than the general heap fail to allocate).
// ---------------------------------------------------------------------------
namespace mem {
    struct Pool { const char* name; DWORD imm, immKor, out, def, defKor; float* cfg; };
    static Pool kPools[] = {
        { "render",    0x53BA84, 0x53BA7D, 0x83C1A4, 0x732000,  0xF32000,  &g_cfg.memRender },
        { "track",     0x53BA04, 0,        0x83C1A8, 0x2B00000, 0,         &g_cfg.memTrack },
        { "UI",        0x53BA27, 0x53BA36, 0x83C1C4, 0x400000,  0xA00000,  &g_cfg.memUI },
        { "cars",      0x53BA59, 0,        0x83C1B0, 0x700000,  0,         &g_cfg.memCars },
        { "audio",     0x53B9EE, 0,        0x83C1B4, 0xA00000,  0,         &g_cfg.memAudio },
        { "level",     0x53BA1A, 0,        0x83C1AC, 0x196000,  0,         &g_cfg.memLevel },
        { "character", 0x53BA9D, 0,        0x83C1BC, 0x17D000,  0,         &g_cfg.memCharacter },
        { "replay",    0x53BAB3, 0,        0x83C1C0, 0x64000,   0,         &g_cfg.memReplay },
        { "ini file",  0x53BADC, 0,        0x83C1C8, 0x10000,   0,         nullptr },
    };
    const DWORD immGeneral = 0x53BBC2, immGeneralKor = 0x53BBD1;
    const DWORD defGeneral = 0x5FB9000, defGeneralKor = 0x6DB9000;
    const DWORD callSetup = 0x537819, fnSetup = 0x53BBC0;
    const DWORD fnLanguage = 0x547C30;    // returns 6 for Korean
    const DWORD pHeapOwner = 0x8519B8;    // [owner]+0x58 = general heap
}
static bool g_memOn = false;

static DWORD MemBytes(float mb, DWORD def) {
    if (!(mb > 0)) return def;
    if (mb > 1024) mb = 1024;
    DWORD b = (DWORD)((double)mb * 1048576.0);
    return (b + 0xFFF) & ~0xFFFu;
}
static double MB(DWORD b) { return b / 1048576.0; }

static void __cdecl Hook_MemSetup() {
    reinterpret_cast<void(*)()>(mem::fnSetup)();
    bool kor = reinterpret_cast<int(*)()>(mem::fnLanguage)() == 6;
    DWORD owner = 0, heap = 0;
    SafeRead(mem::pHeapOwner, &owner);
    if (owner) SafeRead(owner + 0x58, &heap);
    DWORD gen = *reinterpret_cast<DWORD*>(kor ? mem::immGeneralKor : mem::immGeneral);
    Logf("memory pools: general heap %.1f MB %s\n", MB(gen), heap ? "OK" : "FAILED (malloc) - lower memGeneral");
    for (const auto& p : mem::kPools) {
        DWORD a = (kor && p.immKor) ? p.immKor : p.imm;
        DWORD size = *reinterpret_cast<DWORD*>(a), h = 0;
        SafeRead(p.out, &h);
        Logf("  %-9s %7.1f MB %s\n", p.name, MB(size),
             h ? "OK" : "FAILED - general heap too small (memGeneral=0 sizes it automatically)");
    }
}

void InstallMemoryPools() {
    bool anySet = g_cfg.memGeneral > 0 || g_cfg.memRender > 0 || g_cfg.memTrack > 0 || g_cfg.memUI > 0 ||
                  g_cfg.memCars > 0 || g_cfg.memAudio > 0 || g_cfg.memLevel > 0 || g_cfg.memCharacter > 0 ||
                  g_cfg.memReplay > 0;
    if (!g_cfg.memoryPools && anySet) {
        g_cfg.memoryPools = 1;
        Logf("memory pools: sizes set in the ini - turned on\n");
    }
    if (!g_cfg.memoryPools) { Logf("memory pools: off (game defaults)\n"); return; }
    if (!g_knownExe) { Logf("memory pools: unknown exe, left alone\n"); return; }
    BYTE* c = reinterpret_cast<BYTE*>(mem::callSetup);
    if (c[0] != 0xE8 || *reinterpret_cast<DWORD*>(c + 1) != mem::fnSetup - (mem::callSetup + 5)) {
        Logf("memory pools: startup code changed, left alone\n"); return;
    }
    // every immediate must still hold the game's value (HP2WSFix also patches these)
    auto same = [](DWORD a, DWORD v) { return *reinterpret_cast<DWORD*>(a) == v; };
    bool clean = same(mem::immGeneral, mem::defGeneral) && same(mem::immGeneralKor, mem::defGeneralKor);
    for (const auto& p : mem::kPools)
        clean = clean && same(p.imm, p.def) && (!p.immKor || same(p.immKor, p.defKor));
    if (!clean) {
        Logf("memory pools: sizes already changed (HP2WSFix.asi loaded? remove it) - left alone\n");
        return;
    }
    auto nt = reinterpret_cast<PIMAGE_NT_HEADERS>(0x400000 + reinterpret_cast<PIMAGE_DOS_HEADER>(0x400000)->e_lfanew);
    bool laa = (nt->FileHeader.Characteristics & IMAGE_FILE_LARGE_ADDRESS_AWARE) != 0;

    // sizes first (64-bit sums: huge values must not wrap around)
    unsigned long long sumDef = 0, sumDefKor = 0, sum = 0, sumKor = 0;
    DWORD val[16], valK[16];
    int n = 0;
    for (const auto& p : mem::kPools) {
        DWORD defK = p.immKor ? p.defKor : p.def;
        DWORD v  = p.cfg ? MemBytes(*p.cfg, p.def) : p.def;
        DWORD vK = (p.cfg && *p.cfg > 0) ? v : defK;
        sumDef += p.def; sumDefKor += defK; sum += v; sumKor += vK;
        val[n] = v; valK[n] = vK; n++;
    }
    // the general heap must hold every pool plus the game's own spare room
    unsigned long long gen    = mem::defGeneral    - sumDef    + sum;
    unsigned long long genKor = mem::defGeneralKor - sumDefKor + sumKor;
    if (g_cfg.memGeneral > 0) {
        unsigned long long want = MemBytes(g_cfg.memGeneral, mem::defGeneral);
        if (want < gen)
            Logf("memory pools: memGeneral=%g MB is too small for the pools (needs at least %.1f MB) "
                 "- using %.1f MB\n", g_cfg.memGeneral, gen / 1048576.0, gen / 1048576.0);
        if (want > gen) gen = want;
        if (want > genKor) genKor = want;
    }
    unsigned long long big = gen > genKor ? gen : genKor;

    // Can the game get that block? It is one malloc, so it needs one free
    // stretch of address space that big, and the rest of the game (Direct3D,
    // textures, the HD reflections) still needs room elsewhere. Limits: 1 GB
    // without the 4GB patch, just under 2 GB with it (the game's 2002 allocator
    // can't take sizes past 2 GB). Then a test reservation of the block, and
    // with it held, at least 384 MB must stay free elsewhere.
    const unsigned long long limit = laa ? 0x7F000000ull : 0x40000000ull;
    const char* why = nullptr;
    if (big > limit) why = laa ? "over the 2 GB limit" : "over the 1 GB limit without the 4GB patch";
    else {
        void* test = VirtualAlloc(nullptr, (SIZE_T)big, MEM_RESERVE, PAGE_NOACCESS);
        if (!test) why = "no free block of address space that large";
        else {
            unsigned long long freeRest = 0;
            MEMORY_BASIC_INFORMATION mbi;
            for (BYTE* q = nullptr; VirtualQuery(q, &mbi, sizeof(mbi)) == sizeof(mbi); ) {
                if (mbi.State == MEM_FREE) freeRest += mbi.RegionSize;
                BYTE* next = static_cast<BYTE*>(mbi.BaseAddress) + mbi.RegionSize;
                if (next <= q) break;
                q = next;
            }
            VirtualFree(test, 0, MEM_RELEASE);
            if (freeRest < 0x18000000ull) why = "too little memory would be left for the rest of the game";
        }
    }
    if (why) {
        Logf("memory pools: general heap of %.1f MB (pools %.1f MB) can't be allocated - %s. "
             "Game defaults kept; lower the sizes (large address aware exe: %s)\n",
             big / 1048576.0, sum / 1048576.0, why, laa ? "yes" : "no");
        return;
    }

    bool ok = true;
    n = 0;
    for (const auto& p : mem::kPools) {
        DWORD defK = p.immKor ? p.defKor : p.def, v = val[n], vK = valK[n]; n++;
        if (v != p.def) ok &= PatchBytes(p.imm, &v, 4);
        if (p.immKor && vK != defK) ok &= PatchBytes(p.immKor, &vK, 4);
    }
    DWORD g = (DWORD)gen, gK = (DWORD)genKor;
    if (g != mem::defGeneral)       ok &= PatchBytes(mem::immGeneral, &g, 4);
    if (gK != mem::defGeneralKor)   ok &= PatchBytes(mem::immGeneralKor, &gK, 4);
    ok &= PatchCallRel32(mem::callSetup, (void*)&Hook_MemSetup);
    g_memOn = ok;
    Logf("memory pools: %s (general %.1f MB, pools %.1f MB, large address aware exe: %s)\n",
         ok ? "installed" : "patch FAILED", gen / 1048576.0, sum / 1048576.0, laa ? "yes" : "no");
}
