// What the other parts use from helpers.cpp.
#pragma once
#include "common.h"

bool PatchPointer(void** slot, void* newValue, void** oldValue);
bool PatchBytes(DWORD at, const void* bytes, size_t len);
bool PatchCallRel32(DWORD at, void* target);
template <typename Fn>
static void ForEachImport(const char* dllName, Fn cb) {
    HMODULE hMod = GetModuleHandleA(nullptr);
    if (!hMod) return;
    auto base = reinterpret_cast<BYTE*>(hMod);
    auto dos = reinterpret_cast<PIMAGE_DOS_HEADER>(base);
    auto nt  = reinterpret_cast<PIMAGE_NT_HEADERS>(base + dos->e_lfanew);
    DWORD importRva = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    if (!importRva) return;
    auto imp = reinterpret_cast<PIMAGE_IMPORT_DESCRIPTOR>(base + importRva);
    for (; imp->Name; ++imp) {
        if (_stricmp(reinterpret_cast<const char*>(base + imp->Name), dllName) != 0) continue;
        auto thunk = reinterpret_cast<PIMAGE_THUNK_DATA>(base + imp->FirstThunk);
        auto orig  = reinterpret_cast<PIMAGE_THUNK_DATA>(base +
                     (imp->OriginalFirstThunk ? imp->OriginalFirstThunk : imp->FirstThunk));
        for (; thunk->u1.Function; ++thunk, ++orig) {
            if (IMAGE_SNAP_BY_ORDINAL(orig->u1.Ordinal)) continue;
            auto byName = reinterpret_cast<PIMAGE_IMPORT_BY_NAME>(base + orig->u1.AddressOfData);
            cb(reinterpret_cast<void**>(&thunk->u1.Function),
               reinterpret_cast<const char*>(byName->Name));
        }
    }
}
template <typename T>
static bool SafeRead(DWORD addr, T* out) {
    if (!addr || IsBadReadPtr(reinterpret_cast<void*>(addr), sizeof(T))) return false;
    *out = *reinterpret_cast<T*>(addr);
    return true;
}
extern bool g_knownExe;
bool CheckExeSignatures();
