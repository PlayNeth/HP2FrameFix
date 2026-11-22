#include "common.h"
#include "helpers.h"
#include "game.h"
#include "log.h"
#include "portable.h"
#include "animation.h"
#include "interp.h"
#include "reflections.h"

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
bool PatchPointer(void** slot, void* newValue, void** oldValue) {
    DWORD oldProt;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProt)) return false;
    if (oldValue) *oldValue = *slot;
    *slot = newValue;
    DWORD dummy;
    VirtualProtect(slot, sizeof(void*), oldProt, &dummy);
    return true;
}

bool PatchBytes(DWORD at, const void* bytes, size_t len) {
    void* p = reinterpret_cast<void*>(at);
    DWORD oldProt;
    if (!VirtualProtect(p, len, PAGE_EXECUTE_READWRITE, &oldProt)) return false;
    memcpy(p, bytes, len);
    DWORD dummy;
    VirtualProtect(p, len, oldProt, &dummy);
    FlushInstructionCache(GetCurrentProcess(), p, len);
    return true;
}

bool PatchCallRel32(DWORD at, void* target) {
    BYTE* p = reinterpret_cast<BYTE*>(at);
    DWORD oldProt;
    if (!VirtualProtect(p, 5, PAGE_EXECUTE_READWRITE, &oldProt)) return false;
    p[0] = 0xE8;
    *reinterpret_cast<DWORD*>(p + 1) = (DWORD)target - (at + 5);
    DWORD dummy;
    VirtualProtect(p, 5, oldProt, &dummy);
    FlushInstructionCache(GetCurrentProcess(), p, 5);
    return true;
}

bool g_knownExe = false;

bool CheckExeSignatures() {
    if (GetModuleHandleA(nullptr) != reinterpret_cast<HMODULE>(0x400000)) {
        Logf("exe check: unexpected image base, game-specific fixes disabled\n");
        return false;
    }
    for (const auto& s : hp2::kSigs) {
        if (IsBadReadPtr(reinterpret_cast<void*>(s.addr), s.len) ||
            memcmp(reinterpret_cast<void*>(s.addr), s.bytes, s.len) != 0) {
            Logf("exe check: bytes at 0x%06lX differ - not the analysed NFSHP2.exe, "
                 "game-specific fixes disabled\n", (unsigned long)s.addr);
            return false;
        }
    }
    Logf("exe check: NFSHP2.exe matches, game-specific fixes available\n");
    return true;
}
