// What the other parts use from stats.cpp.
#pragma once
#include "common.h"

void LogStats();
typedef HRESULT (STDMETHODCALLTYPE *Present_t)(
    IDirect3DDevice8*, CONST RECT*, CONST RECT*, HWND, CONST RGNDATA*);
extern Present_t g_origPresent;
HRESULT STDMETHODCALLTYPE Hook_Present(
    IDirect3DDevice8* This, CONST RECT* pSourceRect, CONST RECT* pDestRect,
    HWND hDestWindowOverride, CONST RGNDATA* pDirtyRegion);
