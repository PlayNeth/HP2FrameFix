#include "common.h"
#include "portable.h"
#include "game.h"
#include "config.h"
#include "log.h"
#include "helpers.h"
#include "animation.h"
#include "interp.h"
#include "reflections.h"

// ---------------------------------------------------------------------------
// Portable: saves next to the game, the game's registry keys in a file
// ---------------------------------------------------------------------------

static char g_saveDir[MAX_PATH] = {};    // full path, trailing backslash

static void MakeDirs(const char* path) {
    char tmp[MAX_PATH]; lstrcpynA(tmp, path, MAX_PATH);
    size_t n = strlen(tmp);
    for (size_t i = 3; i < n; i++)
        if (tmp[i] == '\\' || tmp[i] == '/') { char c = tmp[i]; tmp[i] = 0; CreateDirectoryA(tmp, nullptr); tmp[i] = c; }
    CreateDirectoryA(tmp, nullptr);
}
static bool DirHasFiles(const char* dir) {
    char pat[MAX_PATH]; snprintf(pat, sizeof(pat), "%s*", dir);
    WIN32_FIND_DATAA fd; HANDLE h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return false;
    bool any = false;
    do { if (strcmp(fd.cFileName, ".") && strcmp(fd.cFileName, "..")) any = true; } while (!any && FindNextFileA(h, &fd));
    FindClose(h);
    return any;
}
static int CopyTree(const char* src, const char* dst, int depth) {   // both with trailing backslash
    if (depth > 4) return 0;
    char pat[MAX_PATH]; snprintf(pat, sizeof(pat), "%s*", src);
    WIN32_FIND_DATAA fd; HANDLE h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    int n = 0;
    do {
        if (!strcmp(fd.cFileName, ".") || !strcmp(fd.cFileName, "..")) continue;
        char s2[MAX_PATH], d2[MAX_PATH];
        snprintf(s2, sizeof(s2), "%s%s", src, fd.cFileName); snprintf(d2, sizeof(d2), "%s%s", dst, fd.cFileName);
        if (strlen(s2) + 2 >= MAX_PATH || strlen(d2) + 2 >= MAX_PATH) continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            CreateDirectoryA(d2, nullptr);
            lstrcatA(s2, "\\"); lstrcatA(d2, "\\");
            n += CopyTree(s2, d2, depth + 1);
        } else if (CopyFileA(s2, d2, TRUE)) n++;
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return n;
}
typedef BOOL (WINAPI *SHGetSpecialFolderPathA_t)(HWND, LPSTR, int, BOOL);
static SHGetSpecialFolderPathA_t g_oGetFolder = nullptr;   // the exe's import, read before patching
// Runs on the game thread when the game first asks for its save folder (not in
// DllMain): create the folder and, if it is empty, copy the existing profiles
// over from Documents (copied, not moved).
static void PrepareSaveFolder() {
    static bool done = false;
    if (done) return;
    done = true;
    MakeDirs(g_saveDir);
    if (DirHasFiles(g_saveDir)) return;
    char docs[MAX_PATH] = {};
    if (!g_oGetFolder || !g_oGetFolder(nullptr, docs, 5 /* CSIDL_PERSONAL */, FALSE)) return;
    static const char* kNames[] = { "Need For Speed Hot Pursuit 2", "Need For Speed poursuite infernale 2" };
    for (const char* nm : kNames) {
        char old[MAX_PATH]; snprintf(old, sizeof(old), "%s\\EA Games\\%s\\", docs, nm);
        if (DirHasFiles(old)) {
            int c = CopyTree(old, g_saveDir, 0);
            Logf("save folder: copied %d file(s) from %s\n", c, old);
            return;
        }
    }
}
static BOOL WINAPI SaveFolderPath(HWND, LPSTR out, int, BOOL) {
    PrepareSaveFolder();
    lstrcpynA(out, g_saveDir, MAX_PATH);
    return TRUE;
}
void InstallSaveFolder() {
    if (!g_knownExe || !g_cfg.saveFolder[0]) { Logf("save folder: game default (Documents)\n"); return; }
    const char* f = g_cfg.saveFolder;
    bool absolute = (f[0] && f[1] == ':') || (f[0] == '\\' && f[1] == '\\');
    snprintf(g_saveDir, sizeof(g_saveDir), "%s%s", absolute ? "" : g_gameDir, f);
    for (char* p = g_saveDir; *p; p++) if (*p == '/') *p = '\\';
    size_t n = strlen(g_saveDir);
    if (n == 0 || n > MAX_PATH - 64) { Logf("save folder: path too long - game default kept\n"); g_saveDir[0] = 0; return; }
    if (g_saveDir[n - 1] != '\\') { g_saveDir[n] = '\\'; g_saveDir[n + 1] = 0; }
    const BYTE callOrig[6] = { 0xFF, 0x15, 0xD8, 0xD2, 0x65, 0x00 };
    const BYTE movOrig[6]  = { 0x8B, 0x15, 0x08, 0x3D, 0x6A, 0x00 };
    if (memcmp(reinterpret_cast<void*>(hp2::callDocsFolder), callOrig, 6) ||
        memcmp(reinterpret_cast<void*>(hp2::saveAppend), movOrig, 6)) {
        Logf("save folder: code changed by another mod - game default kept\n"); g_saveDir[0] = 0; return;
    }
    g_oGetFolder = *reinterpret_cast<SHGetSpecialFolderPathA_t*>(0x65D2D8);   // its IAT slot
    BYTE call[6] = { 0xE8, 0, 0, 0, 0, 0x90 };
    DWORD rel = reinterpret_cast<DWORD>(&SaveFolderPath) - (hp2::callDocsFolder + 5);
    memcpy(call + 1, &rel, 4);
    BYTE jmp[5] = { 0xE9, 0, 0, 0, 0 };
    rel = hp2::saveAppendDone - (hp2::saveAppend + 5);
    memcpy(jmp + 1, &rel, 4);
    bool ok = PatchBytes(hp2::saveAppend, jmp, 5) && PatchBytes(hp2::callDocsFolder, call, 6);
    Logf("save folder: %s\n", ok ? g_saveDir : "patch FAILED - game default kept");
}

// Registry: the game reads its install folder ("Install Dir"), the folder it
// was installed from ("Installed From", where it looks for the music), the CD
// key (ergc) and the GameSpy id ("Crypt"), and writes "Install Dir" and
// "Crypt". With portableRegistry those keys live in HP2FrameFix_registry.ini:
// the install folders default to the game folder, the CD key and GameSpy id
// are taken over from the real registry once if they exist there. Every other
// key (e.g. Direct3D's) still goes to the real registry.
typedef LONG (WINAPI *RegOpenKeyExA_t)(HKEY, LPCSTR, DWORD, REGSAM, PHKEY);
typedef LONG (WINAPI *RegOpenKeyA_t)(HKEY, LPCSTR, PHKEY);
typedef LONG (WINAPI *RegCreateKeyExA_t)(HKEY, LPCSTR, DWORD, LPSTR, DWORD, REGSAM, LPSECURITY_ATTRIBUTES, PHKEY, LPDWORD);
typedef LONG (WINAPI *RegQueryValueExA_t)(HKEY, LPCSTR, LPDWORD, LPDWORD, LPBYTE, LPDWORD);
typedef LONG (WINAPI *RegSetValueExA_t)(HKEY, LPCSTR, DWORD, DWORD, const BYTE*, DWORD);
typedef LONG (WINAPI *RegCloseKey_t)(HKEY);
static RegOpenKeyExA_t    g_oRegOpenKeyExA = nullptr;
static RegOpenKeyA_t      g_oRegOpenKeyA = nullptr;
static RegCreateKeyExA_t  g_oRegCreateKeyExA = nullptr;
static RegQueryValueExA_t g_oRegQueryValueExA = nullptr;
static RegSetValueExA_t   g_oRegSetValueExA = nullptr;
static RegCloseKey_t      g_oRegCloseKey = nullptr;
static char g_regIni[MAX_PATH] = {};

enum { kVKeyInstall = 1, kVKeyErgc = 2, kVKeyGameSpy = 3 };
static const DWORD kVKeyBase = 0x7E570000;          // fake HKEY values for the virtual keys
struct VKeyDef { HKEY root; const char* sub; int id; };
static const VKeyDef kVKeys[] = {
    { HKEY_LOCAL_MACHINE, "Software\\EA Games\\Need For Speed Hot Pursuit 2", kVKeyInstall },
    { HKEY_LOCAL_MACHINE, "Software\\EA Games\\Need For Speed Poursuite Infernale 2", kVKeyInstall },
    { HKEY_LOCAL_MACHINE, "Software\\Electronic Arts\\EA Games\\Need For Speed Hot Pursuit 2\\ergc", kVKeyErgc },
    { HKEY_LOCAL_MACHINE, "Software\\Electronic Arts\\EA Games\\Need For Speed poursuite infernale 2\\ergc", kVKeyErgc },
    { HKEY_CURRENT_USER,  "Software\\GameSpy\\GameSpy 3D\\Registration", kVKeyGameSpy },
};
static int VKeyFor(HKEY root, LPCSTR sub) {
    if (!sub) return 0;
    char k[256]; lstrcpynA(k, sub, sizeof(k));
    size_t n = strlen(k); while (n && k[n - 1] == '\\') k[--n] = 0;
    for (const auto& v : kVKeys) if (v.root == root && !_stricmp(k, v.sub)) return v.id;
    return 0;
}
static int VKeyId(HKEY h) {
    DWORD v = reinterpret_cast<DWORD>(h);
    return (v > kVKeyBase && v <= kVKeyBase + kVKeyGameSpy) ? (int)(v - kVKeyBase) : 0;
}
// [Registry] entry for a value of a virtual key, or nullptr if the key has no such value
static const char* VValueName(int id, LPCSTR name) {
    const char* n = name ? name : "";
    if (id == kVKeyInstall && !_stricmp(n, "Install Dir")) return "InstallDir";
    if (id == kVKeyInstall && !_stricmp(n, "Installed From")) return "InstalledFrom";
    if (id == kVKeyErgc && !n[0]) return "ergc";
    if (id == kVKeyGameSpy && !_stricmp(n, "Crypt")) return "GameSpyCrypt";
    return nullptr;
}
static void VLogOnce(const char* entry, const char* what) {
    static char seen[8][24]; static int n = 0;
    for (int i = 0; i < n; i++) if (!strcmp(seen[i], entry)) return;
    if (n < 8) lstrcpynA(seen[n++], entry, 24);
    Logf("registry: %s %s (HP2FrameFix_registry.ini)\n", entry, what);
}
static void ImportRealValue(HKEY root, const char* sub, const char* name, const char* entry);
// First use, on the game thread (not in DllMain): create the file and take over
// the CD key and GameSpy id from the real registry if they are there.
static void RegFirstUse() {
    static bool done = false;
    if (done) return;
    done = true;
    if (GetFileAttributesA(g_regIni) == INVALID_FILE_ATTRIBUTES) {
        WritePrivateProfileStringA("Registry", "InstallDir", "", g_regIni);
        WritePrivateProfileStringA("Registry", "InstalledFrom", "", g_regIni);
    }
    ImportRealValue(HKEY_LOCAL_MACHINE, "Software\\Electronic Arts\\EA Games\\Need For Speed Hot Pursuit 2\\ergc", "", "ergc");
    ImportRealValue(HKEY_LOCAL_MACHINE, "Software\\Electronic Arts\\EA Games\\Need For Speed poursuite infernale 2\\ergc", "", "ergc");
    ImportRealValue(HKEY_CURRENT_USER, "Software\\GameSpy\\GameSpy 3D\\Registration", "Crypt", "GameSpyCrypt");
}
static LONG VOpen(int id, PHKEY out) {
    if (!out) return ERROR_INVALID_PARAMETER;
    RegFirstUse();
    *out = reinterpret_cast<HKEY>(kVKeyBase + id);
    return ERROR_SUCCESS;
}
static LONG WINAPI HkRegOpenKeyExA(HKEY root, LPCSTR sub, DWORD o, REGSAM sam, PHKEY out) {
    if (int id = VKeyFor(root, sub)) return VOpen(id, out);
    return g_oRegOpenKeyExA(root, sub, o, sam, out);
}
static LONG WINAPI HkRegOpenKeyA(HKEY root, LPCSTR sub, PHKEY out) {
    if (int id = VKeyFor(root, sub)) return VOpen(id, out);
    return g_oRegOpenKeyA(root, sub, out);
}
static LONG WINAPI HkRegCreateKeyExA(HKEY root, LPCSTR sub, DWORD r, LPSTR cls, DWORD o, REGSAM sam,
                                     LPSECURITY_ATTRIBUTES sa, PHKEY out, LPDWORD disp) {
    if (int id = VKeyFor(root, sub)) {
        if (disp) *disp = REG_OPENED_EXISTING_KEY;
        return VOpen(id, out);
    }
    return g_oRegCreateKeyExA(root, sub, r, cls, o, sam, sa, out, disp);
}
static LONG WINAPI HkRegQueryValueExA(HKEY h, LPCSTR name, LPDWORD r, LPDWORD type, LPBYTE data, LPDWORD cb) {
    int id = VKeyId(h);
    if (!id) return g_oRegQueryValueExA(h, name, r, type, data, cb);
    const char* entry = VValueName(id, name);
    if (!entry) return ERROR_FILE_NOT_FOUND;
    char val[512] = {};
    GetPrivateProfileStringA("Registry", entry, "", val, sizeof(val), g_regIni);
    if (!val[0] && id == kVKeyInstall) lstrcpynA(val, g_gameDir, sizeof(val));   // default: the game folder
    if (!val[0]) { VLogOnce(entry, "not set"); return ERROR_FILE_NOT_FOUND; }
    VLogOnce(entry, "read");
    DWORD need = (DWORD)strlen(val) + 1;
    if (type) *type = REG_SZ;
    if (!cb) return data ? ERROR_INVALID_PARAMETER : ERROR_SUCCESS;
    if (!data) { *cb = need; return ERROR_SUCCESS; }
    if (*cb < need) { *cb = need; return ERROR_MORE_DATA; }
    memcpy(data, val, need); *cb = need;
    return ERROR_SUCCESS;
}
static LONG WINAPI HkRegSetValueExA(HKEY h, LPCSTR name, DWORD r, DWORD type, const BYTE* data, DWORD cb) {
    int id = VKeyId(h);
    if (!id) return g_oRegSetValueExA(h, name, r, type, data, cb);
    const char* entry = VValueName(id, name);
    if (!entry || id == kVKeyInstall) return ERROR_SUCCESS;   // install folders are not stored: the game folder is used
    if ((type != REG_SZ && type != REG_EXPAND_SZ) || !data) return ERROR_SUCCESS;
    char val[512] = {};
    memcpy(val, data, cb < sizeof(val) - 1 ? cb : sizeof(val) - 1);
    WritePrivateProfileStringA("Registry", entry, val, g_regIni);
    VLogOnce(entry, "saved");
    return ERROR_SUCCESS;
}
static LONG WINAPI HkRegCloseKey(HKEY h) {
    if (VKeyId(h)) return ERROR_SUCCESS;
    return g_oRegCloseKey(h);
}
// first run: take over the CD key and GameSpy id from the real registry
static void ImportRealValue(HKEY root, const char* sub, const char* name, const char* entry) {
    char cur[8] = {};
    if (GetPrivateProfileStringA("Registry", entry, "", cur, sizeof(cur), g_regIni)) return;
    HKEY k;
    if (g_oRegOpenKeyExA(root, sub, 0, KEY_READ, &k) != ERROR_SUCCESS) return;
    char val[512] = {}; DWORD cb = sizeof(val) - 1, type = 0;
    if (g_oRegQueryValueExA(k, name, nullptr, &type, reinterpret_cast<LPBYTE>(val), &cb) == ERROR_SUCCESS &&
        (type == REG_SZ || type == REG_EXPAND_SZ) && val[0]) {
        WritePrivateProfileStringA("Registry", entry, val, g_regIni);
        Logf("registry: %s taken over from the Windows registry\n", entry);
    }
    g_oRegCloseKey(k);
}
void InstallPortableRegistry() {
    if (!g_knownExe || !g_cfg.portableRegistry) { Logf("registry: Windows registry (portableRegistry=0)\n"); return; }
    snprintf(g_regIni, sizeof(g_regIni), "%sHP2FrameFix_registry.ini", g_asiDir[0] ? g_asiDir : g_gameDir);
    void** slots[6] = {};
    static const char* kNames[6] = { "RegOpenKeyExA", "RegOpenKeyA", "RegCreateKeyExA",
                                     "RegQueryValueExA", "RegSetValueExA", "RegCloseKey" };
    ForEachImport("ADVAPI32.dll", [&](void** slot, const char* nm) {
        for (int i = 0; i < 6; i++) if (!strcmp(nm, kNames[i])) slots[i] = slot;
    });
    for (int i = 0; i < 6; i++)
        if (!slots[i]) { Logf("registry: %s import not found - Windows registry used\n", kNames[i]); return; }
    g_oRegOpenKeyExA    = (RegOpenKeyExA_t)*slots[0];
    g_oRegOpenKeyA      = (RegOpenKeyA_t)*slots[1];
    g_oRegCreateKeyExA  = (RegCreateKeyExA_t)*slots[2];
    g_oRegQueryValueExA = (RegQueryValueExA_t)*slots[3];
    g_oRegSetValueExA   = (RegSetValueExA_t)*slots[4];
    g_oRegCloseKey      = (RegCloseKey_t)*slots[5];
    void* hooks[6] = { (void*)&HkRegOpenKeyExA, (void*)&HkRegOpenKeyA, (void*)&HkRegCreateKeyExA,
                       (void*)&HkRegQueryValueExA, (void*)&HkRegSetValueExA, (void*)&HkRegCloseKey };
    for (int i = 0; i < 6; i++) PatchPointer(slots[i], hooks[i], nullptr);
    Logf("registry: game keys in %s (install folders default to the game folder)\n", g_regIni);
}
