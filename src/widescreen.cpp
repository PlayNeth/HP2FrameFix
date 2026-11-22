#include "common.h"
#include "widescreen.h"
#include "game.h"
#include "config.h"
#include "log.h"
#include "helpers.h"
#include "portable.h"
#include "animation.h"
#include "interp.h"
#include "reflections.h"

// ---------------------------------------------------------------------------
// Widescreen
//
// Based on the findings of HP2WSFix by Lovro Pleše (xan1242) / hdbeefup,
// https://github.com/hdbeefup/hp2wsfix - MIT License:
//   Copyright (c) 2019 Lovro Pleše
//   Permission is hereby granted, free of charge, to any person obtaining a copy
//   of this software and associated documentation files (the "Software"), to deal
//   in the Software without restriction, including without limitation the rights
//   to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
//   copies of the Software, and to permit persons to whom the Software is
//   furnished to do so, subject to the following conditions: The above copyright
//   notice and this permission notice shall be included in all copies or
//   substantial portions of the Software. THE SOFTWARE IS PROVIDED "AS IS",
//   WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED.
//
// Reworked rather than ported. HP2WSFix widened the GUI's 800x600 layout space
// and then re-placed ~300 elements by name, so every screen or element it
// didn't list ended up off-center (backgrounds, parts of the HUD). Here:
//
//  - Menus and HUD: the GUI's layout space stays 800x600 and is drawn with one
//    uniform scale, centered (FE setup 0x595270). Nothing is distorted and every
//    screen is centered without per-element tables.
//  - The game's screen -> layout conversions (0x5954A0 rects, 0x595550 mouse)
//    computed screen*invScale - offset instead of (screen - offset)*invScale -
//    harmless while the offset was always 0, wrong once it isn't. Fixed.
//  - Full-screen background images are scaled to fill the screen, and HUD
//    elements placed on the left/right side of the HUD are moved to the screen
//    edges. Both are done while the GUI draws them (its recursive draw,
//    0x591350): the renderer converts every layout coordinate - images, text,
//    clip rects - through the same scale/offset globals, so changing those for
//    the duration of one widget's draw moves or scales that widget and its
//    children consistently. Layout data and the GUI's own clipping are untouched.
//  - 3D: the projection (0x54FD80) treats the FOV as horizontal and multiplies
//    the vertical scale by the aspect - at 16:9 that cut the top and bottom off
//    (Vert-). Projections made for 4:3 that fill the screen get the FOV widened
//    so the vertical view matches 4:3 (Hor+). One hook instead of HP2WSFix's
//    16 camera ini hooks; viewports that aren't full screen (mirror, menu car
//    previews) are left as they were.
//  - Resolution: the menus are hard-coded to 800x600 (mode pick, validation and
//    fallback); they can now run at the in-game resolution (no mode switch when
//    a race starts) or the desktop's. The options list limiter is removed.
//    FMVs are scaled to fit with their aspect kept, centered (they were scaled
//    to a fixed 800x600 pixel box).
//  - Not taken over from HP2WSFix: the reflection aspect patches (0x53EB84,
//    0x53ED7C - the reflection map is square, changing them warps it), the
//    GUI aspect constant (0x65D900, still right for a 4:3 layout space), the
//    save folder redirect and the memory pool sizes (not widescreen related).
// ---------------------------------------------------------------------------
namespace ws {
    struct GRect  { DWORD vt; int y, x, w, h; };      // GUI rect (vtable 0x65E010)
    struct GPoint { DWORD vt; int x, y; };            // GUI point (vtable 0x65E1F8)
    float& xscale = *reinterpret_cast<float*>(0x6C2DA8);
    float& yscale = *reinterpret_cast<float*>(0x6C2DAC);
    float& invx   = *reinterpret_cast<float*>(0x6C2DB0);
    float& invy   = *reinterpret_cast<float*>(0x6C2DB4);
    float& xpos   = *reinterpret_cast<float*>(0x84BC48);
    float& ypos   = *reinterpret_cast<float*>(0x84BC4C);
    int& virtY = *reinterpret_cast<int*>(0x84B808);
    int& virtX = *reinterpret_cast<int*>(0x84B80C);
    int& virtW = *reinterpret_cast<int*>(0x84B810);
    int& virtH = *reinterpret_cast<int*>(0x84B814);
    int& scrY  = *reinterpret_cast<int*>(0x84B81C);
    int& scrX  = *reinterpret_cast<int*>(0x84B820);
    int& scrW  = *reinterpret_cast<int*>(0x84B824);
    int& scrH  = *reinterpret_cast<int*>(0x84B828);
    const DWORD fnFESetup = 0x595270, fnRectToLayout = 0x5954A0, fnPointToLayout = 0x595550;
    const DWORD fnWidgetRender = 0x591350, fnReadBounds = 0x59B840, callReadBounds = 0x59410A;
    const DWORD fnProjection = 0x54FD80, callProjection = 0x40D680;
    const DWORD fnValidateModes = 0x5418C0, callValidate1 = 0x5419A2, callValidate2 = 0x5419C4;
    const DWORD fnFindMode = 0x5413B0;            // int cdecl(table, w, h, bpp)
    const DWORD movieHook = 0x4D7B97, movieCont = 0x4D7BD5;
    const DWORD pView = 0x722C08;                 // main 3D view (+0x14 vp width, +0x18 height)

    const hp2::Sig kSigs[] = {
        { fnFESetup,       "\x55\x8B\xEC\x51\x8B\x45\x08\x8B", 8 },
        { 0x5952B8,        "\xA3\x14\xB8\x84\x00", 5 },                 // mov [virtH],eax
        { 0x595304,        "\xD9\x1D\xA8\x2D\x6C\x00", 6 },             // fstp [xscale]
        { fnRectToLayout,  "\x55\x8B\xEC\x83\xEC\x10\xC7\x45", 8 },
        { 0x5954BD,        "\xD8\x0D\xB4\x2D\x6C\x00\xD8\x25\x4C\xBC\x84\x00", 12 }, // fmul invy / fsub ypos
        { 0x462FDF,        "\xE8\xBC\x24\x13\x00", 5 },                 // call RectToLayout (text size)
        { fnPointToLayout, "\x55\x8B\xEC\x83\xEC\x08\xC7\x45", 8 },
        { fnWidgetRender,  "\x55\x8B\xEC\x83\xEC\x08\x53\x8B\xD9", 9 },
        { 0x59138E,        "\xFF\x52\x48", 3 },                         // this->BeginPaint
        { callReadBounds,  "\xE8\x31\x77\x00\x00", 5 },                 // GUI.mBounds -> widget+0x60
        { 0x5940FF,        "\x8D\x47\x60", 3 },
        { callProjection,  "\xE8\xFB\x26\x14\x00", 5 },
        { 0x40D670,        "\xD9\x46\x34\xD8\x4D\x0C", 6 },             // aspect = cam+0x34 * arg
        { callValidate1,   "\xE8\x19\xFF\xFF\xFF", 5 },
        { callValidate2,   "\xE8\xF7\xFE\xFF\xFF", 5 },
        { fnFindMode,      "\x55\x8B\xEC\x8B\x45\x0C\x53\x83", 8 },
        { 0x541483,        "\x68\x58\x02\x00\x00\x68\x20\x03\x00\x00", 10 },
        { 0x54149F,        "\x68\x58\x02\x00\x00\x68\x20\x03\x00\x00", 10 },
        { 0x5414DA,        "\xC7\x46\x10\x20\x03\x00\x00\xC7\x46\x14\x58\x02\x00\x00", 14 },
        { 0x5415EE,        "\x68\x58\x02\x00\x00\x68\x20\x03\x00\x00", 10 },
        { 0x54160D,        "\xC7\x46\x10\x20\x03\x00\x00\xC7\x46\x14\x58\x02\x00\x00", 14 },
        { 0x541897,        "\x81\xFF\x20\x03\x00\x00", 6 },
        { 0x54189F,        "\x81\xFB\x58\x02\x00\x00", 6 },
        { 0x46F3A5,        "\x39\x1D\xAC\xEF\x83\x00\x74\x44", 8 },     // LimitResolution check
        { movieHook,       "\x0F\xBF\x40\x06\xDB\x45\x10\x89", 8 },
        { 0x4D7BA9,        "\x83\xC4\x1C", 3 },
        { movieCont,       "\x8B\xCF\xE8\xF4\x2F\x07\x00", 7 },
    };
}

static bool  g_wsOn = false;          // widescreen code installed
static float g_wsS = 1.0f;            // uniform layout -> screen scale
static float g_wsOffX = 0, g_wsOffY = 0;   // centering margins in pixels
static int   g_wsVX = 0, g_wsVY = 0, g_wsVW = 800, g_wsVH = 600;   // layout space
static int   g_wsSX = 0, g_wsSY = 0, g_wsSW = 800, g_wsSH = 600;   // screen rect
static int   g_wsLogLines = 0;
static unsigned long g_wsProjFixed = 0, g_wsShifted = 0, g_wsBgScaled = 0;

static bool WsActive() { return g_wsOn && (g_wsOffX > 0.5f || g_wsOffY > 0.5f); }

// FE setup (0x595270), cdecl(layoutRect, screenRect). Same globals as the
// original; with widescreen on the scale is uniform and the layout centered.
static void __cdecl Ws_FESetup(const ws::GRect* v, const ws::GRect* s) {
    if (!v->w || !v->h || !s->w || !s->h) return;
    ws::virtH = v->h; ws::virtW = v->w; ws::scrW = s->w; ws::scrH = s->h;
    ws::scrX = s->x;  ws::scrY = s->y;  ws::virtY = v->y; ws::virtX = v->x;
    float sx = (float)s->w / v->w, sy = (float)s->h / v->h;
    g_wsVX = v->x; g_wsVY = v->y; g_wsVW = v->w; g_wsVH = v->h;
    g_wsSX = s->x; g_wsSY = s->y; g_wsSW = s->w; g_wsSH = s->h;
    if (g_cfg.widescreen && g_cfg.wsCenterUI) {
        float u = sx < sy ? sx : sy;
        g_wsS = u;
        g_wsOffX = (s->w - v->w * u) * 0.5f;
        g_wsOffY = (s->h - v->h * u) * 0.5f;
        if (g_wsOffX < 0.5f) g_wsOffX = 0;
        if (g_wsOffY < 0.5f) g_wsOffY = 0;
        ws::xscale = ws::yscale = u;
        ws::xpos = s->x + g_wsOffX - v->x * u;
        ws::ypos = s->y + g_wsOffY - v->y * u;
    } else {
        g_wsS = sx; g_wsOffX = g_wsOffY = 0;
        ws::xscale = sx; ws::yscale = sy;
        ws::xpos = (float)(s->x - v->x);              // as the game did
        ws::ypos = (float)(s->y - v->y);
    }
    ws::invx = 1.0f / ws::xscale;
    ws::invy = 1.0f / ws::yscale;
    static int lastW = 0, lastH = 0;
    if (s->w != lastW || s->h != lastH) {
        lastW = s->w; lastH = s->h;
        Logf("widescreen: GUI %dx%d on %dx%d screen: scale %.3f, offset %.0f,%.0f%s\n",
             v->w, v->h, s->w, s->h, ws::xscale, ws::xpos, ws::ypos,
             WsActive() ? " (centered)" : "");
    }
}

// Screen -> layout conversions, done the right way round.
static ws::GRect* __cdecl Ws_RectToLayout(ws::GRect* out, const ws::GRect* in) {
    int bottom = lrintf((in->h + in->y - ws::ypos) * ws::invy);
    int right  = lrintf((in->w + in->x - ws::xpos) * ws::invx);
    int top    = lrintf((in->y - ws::ypos) * ws::invy);
    int left   = lrintf((in->x - ws::xpos) * ws::invx);
    out->vt = 0x65E010; out->y = top; out->x = left; out->w = right - left; out->h = bottom - top;
    return out;
}
// Text measuring (0x462C60) converts a size measured from 0,0 with the same
// function - that must stay a pure scale (no offset), as in the game.
static ws::GRect* __cdecl Ws_SizeToLayout(ws::GRect* out, const ws::GRect* in) {
    int bottom = lrintf((in->h + in->y) * ws::invy);
    int right  = lrintf((in->w + in->x) * ws::invx);
    int top    = lrintf(in->y * ws::invy);
    int left   = lrintf(in->x * ws::invx);
    out->vt = 0x65E010; out->y = top; out->x = left; out->w = right - left; out->h = bottom - top;
    return out;
}
static ws::GPoint* __cdecl Ws_PointToLayout(ws::GPoint* out, const ws::GPoint* in) {
    int y = lrintf((in->y - ws::ypos) * ws::invy);
    int x = lrintf((in->x - ws::xpos) * ws::invx);
    out->vt = 0x65E1F8; out->x = x; out->y = y;
    return out;
}

// --- widget classes, recorded when a widget reads its GUI.mBounds -------------
struct WsWidget { DWORD w, vt; BYTE flags; char name[23]; };
enum { kWsHudView = 1, kWsImage = 2, kWsActor = 4, kWsLogged = 8, kWsChyron = 32, kWsLetterbox = 64 };
static const int kWsTable = 8192;
static WsWidget g_wsWidgets[kWsTable];

static WsWidget* WsSlot(DWORD w, bool insert) {
    unsigned h = (w >> 3) * 2654435761u;
    for (int i = 0; i < 16; i++) {
        WsWidget& e = g_wsWidgets[(h + i) & (kWsTable - 1)];
        if (e.w == w) return &e;
        if (!e.w) return insert ? &e : nullptr;
    }
    return insert ? &g_wsWidgets[h & (kWsTable - 1)] : nullptr;   // full chain: reuse
}
static const WsWidget* WsFind(DWORD w) {
    if (!w) return nullptr;
    WsWidget* e = WsSlot(w, false);
    if (!e || e->vt != *reinterpret_cast<DWORD*>(w)) return nullptr;   // freed and reused
    return e;
}

typedef DWORD (__fastcall *ReadBounds_t)(void* reader, void* edx, const char* key, void* rect, void* def);
static DWORD __fastcall Ws_ReadBounds(void* reader, void*, const char* key, void* rect, void* def) {
    DWORD r = reinterpret_cast<ReadBounds_t>(ws::fnReadBounds)(reader, nullptr, key, rect, def);
    DWORD w = reinterpret_cast<DWORD>(rect) - 0x60;
    const char* name = nullptr;
    if (!SafeRead(reinterpret_cast<DWORD>(reader) + 8, reinterpret_cast<DWORD*>(&name)) || !name ||
        IsBadReadPtr(name, 1))
        return r;
    WsWidget* e = WsSlot(w, true);
    e->w = w; e->vt = *reinterpret_cast<DWORD*>(w); e->flags = 0;
    strncpy(e->name, name, sizeof(e->name) - 1); e->name[sizeof(e->name) - 1] = 0;
    const char* cls = strrchr(name, '.'); cls = cls ? cls + 1 : name;
    if (!strcmp(cls, "ChyronCtl")) e->flags |= kWsChyron;           // EA Trax: left screen edge
    if (!strcmp(cls, "SimGUIInterface")) e->flags |= kWsHudView;   // race GUI root: EA Trax chyron
    if (!strcmp(cls, "HudView")) {
        e->flags |= kWsHudView;
        // its parent (<HUD>.GWidget) holds more HUD parts: the minimap, the mirror
        WsWidget* pe = const_cast<WsWidget*>(WsFind(*reinterpret_cast<DWORD*>(w + 0x38)));
        if (pe) pe->flags |= kWsHudView;
    }
    static const char* const kImg[] = { "GStaticImage", "GSimpleImage", "GImageBox", "GAnimImage",
                                        "GImageBar", "HudStaticArt" };
    for (const char* c : kImg) if (!strcmp(cls, c)) e->flags |= kWsImage;
    if (strstr(cls, "Actor")) e->flags |= kWsActor;
    // car select / results letterbox bars (the names HP2WSFix hides)
    static const char* const kBars[] = { "boxT.GImageBox", "tttt.GImageBox", "T.GImageBox",
                                         "boxB.GImageBox", "bbbb.GImageBox", "B.GImageBox" };
    for (const char* b : kBars) if (!strcmp(name, b)) e->flags |= kWsLetterbox;
    if ((e->flags & kWsHudView) && g_cfg.wsLog && g_wsLogLines < 900) {
        g_wsLogLines++;
        const ws::GRect* rc = static_cast<const ws::GRect*>(rect);
        Logf("widescreen: HUD root %s %d,%d %dx%d\n", name, rc->x, rc->y, rc->w, rc->h);
    }
    return r;
}

// Absolute layout rect of a widget, like the game's LocalToGlobal (0x5932D0).
static void WsGlobalRect(DWORD w, int& x, int& y, int& rw, int& rh) {
    rw = *reinterpret_cast<int*>(w + 0x6C); rh = *reinterpret_cast<int*>(w + 0x70);
    x = 0; y = 0;
    DWORD a = w;
    for (int guard = 0; a && guard < 64; guard++) {
        x += *reinterpret_cast<int*>(a + 0x68); y += *reinterpret_cast<int*>(a + 0x64);
        DWORD fl = *reinterpret_cast<DWORD*>(a + 0x3C);
        if (fl & 8) break;
        DWORD p = *reinterpret_cast<DWORD*>(a + 0x38);
        if (!p) break;
        if (!(fl & 0x40)) { x += *reinterpret_cast<int*>(p + 0x78); y += *reinterpret_cast<int*>(p + 0x7C); }
        a = p;
    }
}

enum WsMode { kWsNone, kWsLeft, kWsRight, kWsStretchX, kWsStretch, kWsFill };
static const char* const kWsModeName[] = { "none", "left edge", "right edge", "stretch width", "stretch", "fill" };

// widgets the GUI draws that were never seen loading (made by code): logged once
static DWORD g_wsSeenNoName[2][64]; static int g_wsSeenNoNameN[2] = {};
static bool WsOnce(const WsWidget* e, DWORD w, int which) {   // which: 0 = draw line, 1 = decision
    BYTE bit = which ? 16 : kWsLogged;
    if (e) { if (e->flags & bit) return false; const_cast<WsWidget*>(e)->flags |= bit; return true; }
    for (int i = 0; i < g_wsSeenNoNameN[which]; i++) if (g_wsSeenNoName[which][i] == w) return false;
    if (g_wsSeenNoNameN[which] >= 64) return false;      // table full: stop logging these
    g_wsSeenNoName[which][g_wsSeenNoNameN[which]++] = w;
    return true;
}

static WsMode WsClassify(DWORD w) {
    const WsWidget* e = WsFind(w);
    WsMode m = kWsNone;
    int x = 0, y = 0, rw = 0, rh = 0;
    if (g_cfg.wsLog >= 2 && g_wsLogLines < 900) {      // diagnostics: every widget drawn, once
        if (WsOnce(e, w, 0)) {
            WsGlobalRect(w, x, y, rw, rh);
            const WsWidget* p = WsFind(*reinterpret_cast<DWORD*>(w + 0x38));
            g_wsLogLines++;
            Logf("widescreen: draw %s (vt %06lX) at %d,%d %dx%d, parent %s, %d children\n",
                 e ? e->name : "(made by code)", *reinterpret_cast<DWORD*>(w) & 0xFFFFFF, x, y, rw, rh,
                 p ? p->name : "-", *reinterpret_cast<int*>(w + 0x10));
        }
    }
    // EA Trax chyron (menus and races): its edge pieces start left of the
    // layout and are cut at its edge - keep that cut on the screen edge.
    if (e && (e->flags & kWsChyron) && g_cfg.wsHudToEdges && g_wsOffX > 0.5f) return kWsLeft;
    // HUD: elements on the HUD's left/right third go to the screen edges. Only
    // the parent has to be known (the minimap is made by code, not loaded).
    if (g_cfg.wsHudToEdges && g_wsOffX > 0.5f) {
        DWORD pw = *reinterpret_cast<DWORD*>(w + 0x38);
        const WsWidget* p = WsFind(pw);
        if (p && (p->flags & kWsHudView) && !(e && (e->flags & kWsImage) && *reinterpret_cast<int*>(w + 0x6C) >=
                                                  *reinterpret_cast<int*>(pw + 0x6C) - 4)) {
            int px = *reinterpret_cast<int*>(w + 0x68), pwid = *reinterpret_cast<int*>(pw + 0x6C);
            int ew = *reinterpret_cast<int*>(w + 0x6C);
            int py = *reinterpret_cast<int*>(w + 0x64), ph = *reinterpret_cast<int*>(pw + 0x70);
            int eh = *reinterpret_cast<int*>(w + 0x70);
            if (pwid > 0 && ew < pwid * 6 / 10) {
                int cx2 = 2 * px + ew;                        // 2 * center
                if (cx2 * 3 < 2 * pwid) m = kWsLeft;
                else if (cx2 * 3 > 4 * pwid) m = kWsRight;
            } else if (pwid > 0 && ew < pwid - 8 && !(e && (e->flags & kWsImage))) {
                // wide text boxes (multiplayer chat / status lines): anchored by
                // the side they start or end at, if they hug that side
                if (px <= pwid / 20) m = kWsLeft;
                else if (px + ew >= pwid - pwid / 20) m = kWsRight;
            }
            // full-screen containers inside the HUD (multiplayer chat/status
            // layers) hold HUD parts too: their children get the same treatment
            if (m == kWsNone && e && !(e->flags & kWsImage) && pwid > 0 && ph > 0 &&
                px <= 4 && py <= 4 && ew >= pwid - 8 && eh >= ph - 8)
                const_cast<WsWidget*>(e)->flags |= kWsHudView;
            if (g_cfg.wsLog && g_wsLogLines < 900 && WsOnce(e, w, 1)) {
                g_wsLogLines++;
                Logf("widescreen: HUD %s at %d,%d %dx%d -> %s\n", e ? e->name : "(made by code)", px,
                     *reinterpret_cast<int*>(w + 0x64), ew, *reinterpret_cast<int*>(w + 0x70),
                     m == kWsLeft ? "left edge" : m == kWsRight ? "right edge" : "stays");
            }
            return m;
        }
    }
    if (!e) return kWsNone;
    // full-screen background images (leaves): fill the screen
    if ((e->flags & (kWsImage | kWsActor)) && g_cfg.wsBackgrounds && *reinterpret_cast<int*>(w + 0x10) == 0) {
        WsGlobalRect(w, x, y, rw, rh);
        bool fullW = x <= g_wsVX + 2 && x + rw >= g_wsVX + g_wsVW - 2;
        bool fullH = y <= g_wsVY + 2 && y + rh >= g_wsVY + g_wsVH - 2;
        if (fullW && fullH && g_wsOffX > 0.5f)
            m = (e->flags & kWsActor) ? kWsStretchX : g_cfg.wsBackgrounds == 2 ? kWsFill : kWsStretch;
        else if (fullW && fullH)
            m = (e->flags & kWsActor) ? kWsNone : g_cfg.wsBackgrounds == 2 ? kWsFill : kWsStretch;
        else if (fullW && rh < g_wsVH - 2 && g_wsOffX > 0.5f && (e->flags & kWsImage))
            m = kWsStretchX;                                  // bars / strips
    }
    if (m != kWsNone && g_cfg.wsLog && g_wsLogLines < 900 && WsOnce(e, w, 1)) {
        g_wsLogLines++;
        Logf("widescreen: %s %d,%d %dx%d -> %s\n", e->name, x, y, rw, rh, kWsModeName[m]);
    }
    return m;
}

typedef void (__fastcall *WidgetRender_t)(void* w, void* edx, void* renderer, DWORD flags);
static WidgetRender_t g_wsRenderTramp = nullptr;
static unsigned long g_wsBarsHidden = 0;
static void __fastcall Ws_WidgetRender(void* w, void*, void* renderer, DWORD flags) {
    if (g_cfg.wsNoLetterbox) {
        const WsWidget* e = WsFind(reinterpret_cast<DWORD>(w));
        if (e && (e->flags & kWsLetterbox)) { g_wsBarsHidden++; return; }
    }
    WsMode m = WsActive() ? WsClassify(reinterpret_cast<DWORD>(w)) : kWsNone;
    if (m == kWsNone) { g_wsRenderTramp(w, nullptr, renderer, flags); return; }
    float sx = ws::xscale, sy = ws::yscale, px = ws::xpos, py = ws::ypos;
    switch (m) {
    case kWsLeft:  ws::xpos -= g_wsOffX; g_wsShifted++; break;
    case kWsRight: ws::xpos += g_wsOffX; g_wsShifted++; break;
    case kWsStretchX:
        ws::xscale = (float)g_wsSW / g_wsVW; ws::xpos = g_wsSX - g_wsVX * ws::xscale; g_wsBgScaled++; break;
    case kWsStretch:
        ws::xscale = (float)g_wsSW / g_wsVW; ws::xpos = g_wsSX - g_wsVX * ws::xscale;
        ws::yscale = (float)g_wsSH / g_wsVH; ws::ypos = g_wsSY - g_wsVY * ws::yscale; g_wsBgScaled++; break;
    case kWsFill: {
        float kx = (float)g_wsSW / g_wsVW, ky = (float)g_wsSH / g_wsVH, k = kx > ky ? kx : ky;
        ws::xscale = ws::yscale = k;
        ws::xpos = g_wsSX + (g_wsSW - g_wsVW * k) * 0.5f - g_wsVX * k;
        ws::ypos = g_wsSY + (g_wsSH - g_wsVH * k) * 0.5f - g_wsVY * k;
        g_wsBgScaled++; break; }
    default: break;
    }
    g_wsRenderTramp(w, nullptr, renderer, flags);
    ws::xscale = sx; ws::yscale = sy; ws::xpos = px; ws::ypos = py;
}

// 3D projection (Hor+). For each view, the viewport the original game would
// have had is this one clipped to a 4:3 screen (centered 4:3 part of ours).
// If the shape differs (race view on 16:9, the showroom car whose widget is
// wider than the 800x600 layout), the FOV is widened so the vertical view and
// the proportions stay as in the original. Views inside that area (mirror,
// small previews) are unchanged.
typedef void (__fastcall *Projection_t)(void* view, void* edx, float fov, float aspect, float zn, float zf);
static int g_wsProjLogged = 0;
static void __fastcall Ws_Projection(void* view, void*, float fov, float aspect, float zn, float zf) {
    if (g_cfg.widescreen && g_cfg.wsFixFOV && fov > 1.0f && fov < 179.0f && aspect > 0.1f) {
        BYTE* v = reinterpret_cast<BYTE*>(view);
        int vx = *reinterpret_cast<int*>(v + 0x0C), vy = *reinterpret_cast<int*>(v + 0x10);
        int vw = *reinterpret_cast<int*>(v + 0x14), vh = *reinterpret_cast<int*>(v + 0x18);
        int sw = ws::scrW, sh = ws::scrH;
        if (vw > 0 && vh > 0 && sw > 0 && sh > 0) {
            float cw = (float)sw, ch = (float)sh;                   // centered 4:3 area
            if (cw * 3.0f > ch * 4.0f) cw = ch * 4.0f / 3.0f; else ch = cw * 3.0f / 4.0f;
            float cx0 = (sw - cw) * 0.5f, cy0 = (sh - ch) * 0.5f;
            float x0 = vx > cx0 ? (float)vx : cx0, x1 = vx + vw < cx0 + cw ? (float)(vx + vw) : cx0 + cw;
            float y0 = vy > cy0 ? (float)vy : cy0, y1 = vy + vh < cy0 + ch ? (float)(vy + vh) : cy0 + ch;
            float a1 = (float)vw / vh;
            // SetViewport (0x54F990) crops views that run past the screen and keeps
            // factors (view+0x11C/0x120) so the projection keeps the requested
            // shape - the showroom car is such a view: its shape is the layout's
            // and already right. Only views asking for exactly the screen (race
            // view) get Hor+.
            const float* fs = reinterpret_cast<const float*>(v + 0x11C);
            bool wholeScreen = fabsf(fs[0] - 1.0f) < 0.01f && fabsf(fs[1] - 1.0f) < 0.01f &&
                               vx <= 1 && vy <= 1 && vw >= sw - 2 && vh >= sh - 2;
            if (wholeScreen && x1 - x0 > 8.0f && y1 - y0 > 8.0f) {
                float a0 = (x1 - x0) / (y1 - y0), r = a1 / a0;
                bool fix = fabsf(r - 1.0f) > 0.01f;
                if (!fix && g_cfg.wsLog) {
                    static DWORD seenU[20]; static int nU = 0;
                    DWORD key = (DWORD)vw << 16 | (DWORD)vh; bool isNew = true;
                    for (int k = 0; k < nU; k++) if (seenU[k] == key) isNew = false;
                    if (isNew && nU < 20) {
                        seenU[nU++] = key;
                        Logf("widescreen: 3D view %dx%d at %d,%d unchanged: fov %.1f aspect %.3f | vp factors %.3f %.3f\n",
                             vw, vh, vx, vy, fov, aspect, fs[0], fs[1]);
                    }
                }
                if (fix) {
                    const float d2r = 3.14159265f / 180.0f;
                    float f = 2.0f * atanf(tanf(fov * 0.5f * d2r) * r) / d2r;
                    if (f > 170.0f) f = 170.0f;
                    static DWORD seen[20];
                    DWORD key = (DWORD)vw << 16 | (DWORD)vh;
                    bool isNew = true;
                    for (int k = 0; k < g_wsProjLogged; k++) if (seen[k] == key) isNew = false;
                    if (g_cfg.wsLog && isNew && g_wsProjLogged < 20) {
                        seen[g_wsProjLogged++] = key;
                        const float* fo = reinterpret_cast<const float*>(v + 0x154);
                        Logf("widescreen: 3D view %dx%d at %d,%d (4:3 screen: %.0fx%.0f): fov %.1f -> %.1f, "
                             "aspect %.3f -> %.3f | vp factors %.3f %.3f offs %.3f %.3f %.3f %.3f\n",
                             vw, vh, vx, vy, x1 - x0, y1 - y0, fov, f, aspect, aspect * r,
                             fs[0], fs[1], fo[0], fo[1], fo[2], fo[3]);
                    }
                    fov = f; aspect *= r; g_wsProjFixed++;
                }
            }
        }
    }
    reinterpret_cast<Projection_t>(ws::fnProjection)(view, nullptr, fov, aspect, zn, zf);
}

// Menu display mode = in-game mode (or desktop): patch the 800x600 constants
// right before the game validates/picks its modes.
static DWORD g_wsMenuW = 0, g_wsMenuH = 0;
static void WsPatchMenuMode(DWORD w, DWORD h) {
    PatchBytes(0x541484, &h, 4); PatchBytes(0x541489, &w, 4);
    PatchBytes(0x5414A0, &h, 4); PatchBytes(0x5414A5, &w, 4);
    PatchBytes(0x5414DD, &w, 4); PatchBytes(0x5414E4, &h, 4);
    PatchBytes(0x5415EF, &h, 4); PatchBytes(0x5415F4, &w, 4);
    PatchBytes(0x541610, &w, 4); PatchBytes(0x541617, &h, 4);
    PatchBytes(0x541899, &w, 4); PatchBytes(0x5418A1, &h, 4);
}
typedef bool (__fastcall *ValidateModes_t)(void* caps, void* edx, void* table, DWORD flag);
typedef int (__cdecl *FindMode_t)(void* table, int w, int h, int bpp);
static bool __fastcall Ws_ValidateModes(void* caps, void*, void* table, DWORD flag) {
    if (g_cfg.widescreen && g_cfg.wsMenuResolution && table) {
        int w = 0, h = 0;
        if (g_cfg.wsMenuResolution == 1) {
            w = *reinterpret_cast<int*>(reinterpret_cast<BYTE*>(caps) + 0x20);
            h = *reinterpret_cast<int*>(reinterpret_cast<BYTE*>(caps) + 0x24);
        }
        if (w <= 0 || h <= 0) {
            DEVMODEA dm = {}; dm.dmSize = sizeof(dm);
            // registry = the desktop mode even while the game is fullscreen in another mode
            if (EnumDisplaySettingsA(nullptr, ENUM_REGISTRY_SETTINGS, &dm) ||
                EnumDisplaySettingsA(nullptr, ENUM_CURRENT_SETTINGS, &dm)) { w = dm.dmPelsWidth; h = dm.dmPelsHeight; }
        }
        FindMode_t find = reinterpret_cast<FindMode_t>(ws::fnFindMode);
        if (w >= 640 && h >= 480 && (find(table, w, h, 32) != -1 || find(table, w, h, 16) != -1)) {
            if ((DWORD)w != g_wsMenuW || (DWORD)h != g_wsMenuH) {
                g_wsMenuW = w; g_wsMenuH = h;
                WsPatchMenuMode(w, h);
                Logf("widescreen: menus at %dx%d\n", w, h);
            }
        } else if (w > 0) {
            Logf("widescreen: %dx%d isn't a display mode the game lists - menus stay at 800x600\n", w, h);
        }
    }
    return reinterpret_cast<ValidateModes_t>(ws::fnValidateModes)(caps, nullptr, table, flag);
}

// FMV: scale to fit, aspect kept, centered (was: stretched to an 800x600 box).
extern "C" void __cdecl Ws_MovieLayout(void* sprite, int w, int h) {
    DWORD* vt = *reinterpret_cast<DWORD**>(sprite);
    typedef void (__fastcall *SetScale_t)(void*, void*, float, float);
    typedef void (__fastcall *SetPos_t)(void*, void*, float, float, float);
    float sx = 800.0f / (w > 0 ? w : 1), sy = 600.0f / (h > 0 ? h : 1), x = 0, y = 0;
    DWORD view = *reinterpret_cast<DWORD*>(ws::pView);
    if (g_cfg.widescreen && g_cfg.wsMovies && w > 0 && h > 0 && view) {
        int vw = *reinterpret_cast<int*>(view + 0x14), vh = *reinterpret_cast<int*>(view + 0x18);
        if (vw > 0 && vh > 0) {
            // The game always stretches movies over its 4:3 screen, and some are
            // stored in a different shape (EA intro) - so by default show them
            // as a 4:3 picture; wsMovies=2 keeps the file's own shape.
            float bw = (float)w, bh = (float)h;
            if (g_cfg.wsMovies != 2) { bw = 4.0f; bh = 3.0f; }
            float s = (float)vw / bw < (float)vh / bh ? (float)vw / bw : (float)vh / bh;
            float dw = bw * s, dh = bh * s;
            sx = dw / w; sy = dh / h; x = (vw - dw) * 0.5f; y = (vh - dh) * 0.5f;
        }
    }
    reinterpret_cast<SetScale_t>(vt[2])(sprite, nullptr, sx, sy);
    reinterpret_cast<SetPos_t>(vt[1])(sprite, nullptr, x, y, 0.0f);
}
extern "C" void Ws_MovieStub();
extern "C" { DWORD g_wsMovieCont = 0x4D7BD5; }
__asm__(
    ".globl _Ws_MovieStub\n"
    "_Ws_MovieStub:\n"
    "    movswl 6(%eax), %eax\n"          // movie height (width already in edx)
    "    addl $0x1c, %esp\n"              // the original's stack cleanup at 0x4D7BA9
    "    pushl %eax\n"
    "    pushl %edx\n"
    "    pushl %esi\n"                    // sprite
    "    call _Ws_MovieLayout\n"
    "    addl $12, %esp\n"
    "    jmp *_g_wsMovieCont\n");

static bool WsJmp(DWORD at, void* to) {
    BYTE j[5] = { 0xE9 };
    DWORD rel = reinterpret_cast<DWORD>(to) - (at + 5);
    memcpy(j + 1, &rel, 4);
    return PatchBytes(at, j, 5);
}

void InstallWidescreen() {
    if (!g_knownExe || !g_cfg.widescreen) { Logf("widescreen: off\n"); return; }
    for (const auto& sg : ws::kSigs)
        if (IsBadReadPtr(reinterpret_cast<void*>(sg.addr), sg.len) ||
            memcmp(reinterpret_cast<void*>(sg.addr), sg.bytes, sg.len) != 0) {
            Logf("widescreen: code at 0x%06lX already changed (HP2WSFix.asi or similar loaded? remove it) "
                 "- widescreen left off\n", (unsigned long)sg.addr);
            return;
        }
    // trampoline for the widget draw (first 6 bytes: push ebp / mov ebp,esp / sub esp,8)
    BYTE* t = static_cast<BYTE*>(VirtualAlloc(nullptr, 16, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!t) { Logf("widescreen: out of memory\n"); return; }
    memcpy(t, reinterpret_cast<void*>(ws::fnWidgetRender), 6);
    t[6] = 0xE9;
    DWORD rel = (ws::fnWidgetRender + 6) - reinterpret_cast<DWORD>(t + 11);
    memcpy(t + 7, &rel, 4);
    g_wsRenderTramp = reinterpret_cast<WidgetRender_t>(t);

    bool ok = true;
    if (g_cfg.wsCenterUI) {
        ok &= WsJmp(ws::fnFESetup, (void*)&Ws_FESetup);
        ok &= WsJmp(ws::fnRectToLayout, (void*)&Ws_RectToLayout);
        ok &= PatchCallRel32(0x462FDF, (void*)&Ws_SizeToLayout);   // text measuring
        ok &= WsJmp(ws::fnPointToLayout, (void*)&Ws_PointToLayout);
        ok &= PatchCallRel32(ws::callReadBounds, (void*)&Ws_ReadBounds);
        ok &= WsJmp(ws::fnWidgetRender, (void*)&Ws_WidgetRender);
    }
    if (g_cfg.wsFixFOV) ok &= PatchCallRel32(ws::callProjection, (void*)&Ws_Projection);
    if (g_cfg.wsMenuResolution) {
        ok &= PatchCallRel32(ws::callValidate1, (void*)&Ws_ValidateModes);
        ok &= PatchCallRel32(ws::callValidate2, (void*)&Ws_ValidateModes);
    }
    if (g_cfg.wsUnlockResolutions) { BYTE jmp = 0xEB; ok &= PatchBytes(0x46F3AB, &jmp, 1); }
    if (g_cfg.wsMovies) {
        BYTE j[8] = { 0xE9, 0, 0, 0, 0, 0x90, 0x90, 0x90 };
        DWORD r = reinterpret_cast<DWORD>(&Ws_MovieStub) - (ws::movieHook + 5);
        memcpy(j + 1, &r, 4);
        ok &= PatchBytes(ws::movieHook, j, 8);
    }
    g_wsOn = ok;
    Logf("widescreen: %s (centered UI %d, Hor+ FOV %d, menus at game res %d, backgrounds %d, "
         "HUD to edges %d, movies %d, unlocked modes %d)\n", ok ? "installed" : "patch FAILED",
         g_cfg.wsCenterUI, g_cfg.wsFixFOV, g_cfg.wsMenuResolution, g_cfg.wsBackgrounds,
         g_cfg.wsHudToEdges, g_cfg.wsMovies, g_cfg.wsUnlockResolutions);
}

void LogWidescreenStats() {
    if (!g_wsOn) return;
    if (g_wsProjFixed || g_wsShifted || g_wsBgScaled || g_wsBarsHidden)
        Logf("  widescreen: Hor+ projections %lu | HUD draws moved to edges %lu | backgrounds scaled %lu"
             " | letterbox bars hidden %lu\n", g_wsProjFixed, g_wsShifted, g_wsBgScaled, g_wsBarsHidden);
    g_wsProjFixed = g_wsShifted = g_wsBgScaled = g_wsBarsHidden = 0;
}
