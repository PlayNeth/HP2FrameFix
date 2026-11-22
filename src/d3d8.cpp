#include "common.h"
#include "d3d8.h"
#include "log.h"
#include "helpers.h"
#include "stats.h"

// ---------------------------------------------------------------------------
// D3D8 hook chain: Direct3DCreate8 (IAT) -> IDirect3D8::CreateDevice (vt 15)
//                  -> IDirect3DDevice8::Present (vt 15)
// ---------------------------------------------------------------------------
typedef HRESULT (STDMETHODCALLTYPE *CreateDevice_t)(
    IDirect3D8*, UINT, D3DDEVTYPE, HWND, DWORD, D3DPRESENT_PARAMETERS*, IDirect3DDevice8**);
static CreateDevice_t g_origCreateDevice = nullptr;

static void** GetVTable(void* obj) { return *reinterpret_cast<void***>(obj); }

static void HookDevicePresent(IDirect3DDevice8* pDevice) {
    if (!pDevice || g_origPresent) return;
    void* old = nullptr;
    if (PatchPointer(&GetVTable(pDevice)[15], (void*)&Hook_Present, &old)) {
        g_origPresent = reinterpret_cast<Present_t>(old);
        Logf("Present hooked\n");
    } else {
        Logf("Present hook FAILED (VirtualProtect)\n");
    }
}

static HRESULT STDMETHODCALLTYPE Hook_CreateDevice(
    IDirect3D8* This, UINT Adapter, D3DDEVTYPE DeviceType, HWND hFocusWindow,
    DWORD BehaviorFlags, D3DPRESENT_PARAMETERS* pp, IDirect3DDevice8** ppDev)
{
    HRESULT hr = g_origCreateDevice(This, Adapter, DeviceType, hFocusWindow,
                                    BehaviorFlags, pp, ppDev);
    if (SUCCEEDED(hr) && ppDev && *ppDev) {
        if (pp)
            Logf("CreateDevice: %ux%u windowed=%d swapEffect=%d interval=%u refresh=%u\n",
                 pp->BackBufferWidth, pp->BackBufferHeight, pp->Windowed, (int)pp->SwapEffect,
                 pp->FullScreen_PresentationInterval, pp->FullScreen_RefreshRateInHz);
        HookDevicePresent(*ppDev);
    }
    return hr;
}

static void HookD3DCreateDevice(IDirect3D8* pD3D) {
    if (!pD3D || g_origCreateDevice) return;
    void* old = nullptr;
    if (PatchPointer(&GetVTable(pD3D)[15], (void*)&Hook_CreateDevice, &old))
        g_origCreateDevice = reinterpret_cast<CreateDevice_t>(old);
    else
        Logf("CreateDevice hook FAILED (VirtualProtect)\n");
}

typedef IDirect3D8* (WINAPI *Direct3DCreate8_t)(UINT);
static Direct3DCreate8_t g_origDirect3DCreate8 = nullptr;

static IDirect3D8* WINAPI Hook_Direct3DCreate8(UINT SDKVersion) {
    IDirect3D8* pD3D = g_origDirect3DCreate8(SDKVersion);
    if (pD3D) HookD3DCreateDevice(pD3D);
    return pD3D;
}

bool PatchIAT_Direct3DCreate8() {
    bool patched = false;
    ForEachImport("d3d8.dll", [&](void** slot, const char* name) {
        if (patched || strcmp(name, "Direct3DCreate8") != 0) return;
        void* old = nullptr;
        if (PatchPointer(slot, (void*)&Hook_Direct3DCreate8, &old)) {
            g_origDirect3DCreate8 = reinterpret_cast<Direct3DCreate8_t>(old);
            patched = true;
        }
    });
    return patched;
}
