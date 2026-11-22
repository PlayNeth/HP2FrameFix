// What the other parts use from config.cpp.
#pragma once
#include "common.h"

struct Config {
    int   targetFPS            = 60;
    int   enableLimiter        = 1;
    int   passthrough          = 0;
    int   raiseTimerResolution = 1;
    int   hiResGameTimers      = 1;
    int   preciseGameTicks     = 1;
    int   smoothDriverSteering = 1;
    int   smoothSlowMotionAnims = 1;
    int   fixDriverShiftAnim   = 1;
    int   animDiag             = 0;
    int   smoothCharacterAnims = 1;
    int   smoothProps          = 1;
    int   smoothParticles      = 1;
    float tickPhase            = 0.75f; // where ticks sit inside each tick slot after a frame deadline
    int   fixEAXOnStartup      = 1;
    char  saveFolder[MAX_PATH] = "save";   // profiles/saves, relative to the game folder; empty = Documents
    int   portableRegistry     = 1;     // game registry keys read/written in HP2FrameFix_registry.ini
    int   speedUpLoading       = 1;
    int   loadScreenRenderMs   = 100;   // 0 = every step (original), -1 = never
    int   limiterDuringLoading = 0;
    int   logToFile            = 1;
    int   logEveryNFrames      = 600;
    // interpolation
    // HD car reflections (from NFSHP2 HD Reflections by Arctic94 / Aero_, GPL-3)
    int   hdReflections        = 1;
    int   reflAutoRes          = 1;     // 1080p 512, 1440p 1024, 2160p 2048
    int   reflCubemapRes       = 512;
    int   reflEnviromapRes     = 512;
    int   reflPowerOfTwo       = 0;
    float reflBrightness       = 1.0f;
    int   reflFullDetail       = 1;     // all 6 faces even on lower in-game reflection detail
    int   reflAllFacesEveryFrame = 1;   // no alternating halves (seam flicker)
    int   interpolate          = 1;     // blend cars between 60 Hz physics steps
    int   interpolateCamera    = 1;     // blend the camera too
    float interpDelay          = 1.0f;  // steps behind the newest step that frames are drawn
    float maxStepDistance      = 10.0f; // don't blend a jump bigger than this per step (resets, cuts)
    float maxStepAngle         = 90.0f;
    // widescreen
    int   widescreen           = 1;
    int   wsCenterUI           = 1;     // menus/HUD: uniform scale, centered
    int   wsFixFOV             = 1;     // Hor+ 3D
    int   wsMenuResolution     = 1;     // 1 = in-game resolution, 2 = desktop, 0 = 800x600
    int   wsBackgrounds        = 2;     // 0 = 4:3 with bars, 1 = stretch, 2 = fill (no distortion)
    int   wsHudToEdges         = 1;
    int   wsMovies             = 1;
    int   wsUnlockResolutions  = 1;
    int   wsLog                = 1;
    int   wsNoLetterbox        = 0;
    // memory pools (from HP2WSFix's [MEMORY] section)
    int   memoryPools          = 0;
    float memGeneral           = 0;     // MB, 0 = automatic (game's spare room + all pools)
    float memRender            = 0;     // MB, 0 = game default
    float memTrack             = 0;
    float memUI                = 0;
    float memCars              = 0;
    float memAudio             = 0;
    float memLevel             = 0;
    float memCharacter         = 0;
    float memReplay            = 0;
};
extern Config g_cfg;
extern char g_gameDir[MAX_PATH];
extern char g_asiDir[MAX_PATH];
void InitAsiDir();
void InitGameDir();
void LoadConfig();
