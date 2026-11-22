#include "common.h"
#include "config.h"

// ---------------------------------------------------------------------------
// Config
// ---------------------------------------------------------------------------

Config g_cfg;
static bool   g_cfgLoaded = false;
char   g_gameDir[MAX_PATH] = {};

char   g_asiDir[MAX_PATH] = {};

// Folder containing this .asi, found from an address inside the module so it
// works no matter where the ASI loader picked it up from.
void InitAsiDir() {
    if (g_asiDir[0]) return;
    HMODULE self = nullptr;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(&InitAsiDir), &self) &&
        GetModuleFileNameA(self, g_asiDir, MAX_PATH)) {
        char* slash = strrchr(g_asiDir, '\\');
        if (slash) { *(slash + 1) = 0; return; }
    }
    g_asiDir[0] = 0;   // fall back to the current directory
}

void InitGameDir() {
    if (g_gameDir[0]) return;
    if (!GetModuleFileNameA(nullptr, g_gameDir, MAX_PATH)) { g_gameDir[0] = 0; return; }
    char* slash = strrchr(g_gameDir, '\\');
    if (slash) *(slash + 1) = 0; else g_gameDir[0] = 0;
}

static const char kInterpBlock[] =
    "; --- interpolation: draw cars and camera between the game's 60 Hz physics steps,\n"
    ";     so motion is smooth at any frame rate above 60 ---\n"
    "interpolate=1\n"
    "interpolateCamera=1\n"
    "; how many 1/60 s steps behind the newest step frames are drawn (1.0 = least delay)\n"
    "interpDelay=1.0\n"
    "; don't blend jumps bigger than this per step (resets, teleports, camera cuts)\n"
    "maxStepDistance=10\n"
    "maxStepAngle=90\n";

static const char kReflBlock[] =
    "; --- HD car reflections (from NFSHP2 HD Reflections by Arctic94 / Aero_) ---\n"
    "; sharper car reflections: the reflection cubemap and environment map are 128x128 in the game\n"
    "hdReflections=1\n"
    "; pick the resolution from the display height (1080p = 512, 1440p = 1024, 2160p = 2048)\n"
    "reflAutoRes=1\n"
    "; used when reflAutoRes=0 (128 = game default, 256, 512, 1024, 2048)\n"
    "reflCubemapRes=512\n"
    "reflEnviromapRes=512\n"
    "; round the resolutions down to a power of two (old GPUs)\n"
    "reflPowerOfTwo=0\n"
    "; reflection intensity 0.0 - 1.0 (1.0 = game default)\n"
    "reflBrightness=1.0\n"
    "; render the full dynamic reflection even when the in-game reflection detail is lower\n"
    "reflFullDetail=1\n"
    "; render all 6 cubemap faces every frame (the game alternates halves -> seam flicker at HD)\n"
    "reflAllFacesEveryFrame=1\n";

static const char kWsBlock[] =
    "; --- widescreen (reworked from HP2WSFix by xan1242 / hdbeefup) ---\n"
    "; pick the resolution in the game's Options as usual; everything adapts to its aspect ratio\n"
    "widescreen=1\n"
    "; menus and HUD: draw the 800x600 layout with one scale, centered (no stretching)\n"
    "wsCenterUI=1\n"
    "; 3D view: keep the 4:3 vertical view and show more at the sides (Hor+)\n"
    "wsFixFOV=1\n"
    "; menu resolution: 1 = same as in-game, 2 = desktop, 0 = game default 800x600\n"
    "wsMenuResolution=1\n"
    "; full-screen backgrounds: 0 = 4:3 with side bars, 1 = stretch, 2 = fill without distortion\n"
    "wsBackgrounds=2\n"
    "; in-race HUD: move the elements on the left/right side to the screen edges\n"
    "wsHudToEdges=1\n"
    "; FMVs: 1 = 4:3 picture centered (as the game shows them), 2 = keep the video file's shape,\n"
    ";        0 = stretch over the screen (game default)\n"
    "wsMovies=1\n"
    "; car select / race results: remove the black bars at the top and bottom\n"
    "wsNoLetterbox=0\n"
    "; list every display mode in the options (the game hides some)\n"
    "wsUnlockResolutions=1\n"
    "; log what the widescreen code adjusts (2 = also every widget drawn, for diagnostics)\n"
    "wsLog=1\n";

static const char kMemBlock[] =
    "; --- memory pools (from HP2WSFix): bigger fixed-size memory pools for mods ---\n"
    "; only needed for mods with high-poly cars or HD textures (missing textures, crashes on load)\n"
    "; (turned on automatically when any size below is set)\n"
    "memoryPools=0\n"
    "; sizes in MB, 0 = game default (render 7.2, track 43, UI 4, cars 7, audio 10,\n"
    "; level 1.6, character 1.5, replay 0.4)\n"
    "; HD mod example: memRender=256  memTrack=128  memUI=128  (car textures load into the track pool)\n"
    "; the total must fit in one block: up to 1 GB, or just under 2 GB with the 4GB patch on the exe\n"
    "; (too much and the game keeps its defaults - the log says why)\n"
    "memRender=0\n"
    "memTrack=0\n"
    "memUI=0\n"
    "memCars=0\n"
    "memAudio=0\n"
    "memLevel=0\n"
    "memCharacter=0\n"
    "memReplay=0\n"
    "; all pools above are carved out of the general pool: 0 = sized automatically\n"
    "; (the game's own spare room plus every pool), or set an exact size in MB\n"
    "memGeneral=0\n";

void LoadConfig() {
    if (g_cfgLoaded) return;
    g_cfgLoaded = true;
    InitGameDir();
    InitAsiDir();
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%sHP2FrameFix.ini", g_asiDir);
    FILE* f = fopen(path, "r");
    if (!f) {
        f = fopen(path, "w");
        if (f) {
            fputs(
                "; HP2FrameFix.ini (v" HP2FF_VERSION ")\n"
                "; --- frame limiter ---\n"
                "enableLimiter=1\n"
                "targetFPS=60\n"
                "passthrough=0\n"
                "; --- game clock (fix for duplicated frames on Windows) ---\n"
                "raiseTimerResolution=1\n"
                "hiResGameTimers=1\n"
                "; replace the game's 120 Hz sim timer with a precise, frame-locked one\n"
                "preciseGameTicks=1\n"
                "tickPhase=0.75\n"
                "; update the driver's steering animation every physics step (60 Hz) instead of 15 Hz\n"
                "smoothDriverSteering=1\n"
                "; animate characters every frame even in slow motion (bust sequence)\n"
                "smoothSlowMotionAnims=1\n"
                "; stop the gear-shift animation from being cancelled by a stale flag\n"
                "fixDriverShiftAnim=1\n"
                "; update characters (drivers, cops, dog) every physics step instead of every other\n"
                "smoothCharacterAnims=1\n"
                "; move knocked props (signs, meters, cones) every physics step instead of every other\n"
                "smoothProps=1\n"
                "; move particles/debris (smoke, sparks) every physics step\n"
                "smoothParticles=1\n"
                "; log per-character animation update rates (diagnostics)\n"
                "animDiag=0\n"
                "; --- audio: apply the saved audio mode (e.g. EAX) when the profile loads ---\n"
                "fixEAXOnStartup=1\n"
                "; --- portable ---\n"
                "; save folder for profiles and saves, relative to the game folder (or a full path);\n"
                ";   empty = the game's default (Documents\\EA Games\\Need For Speed Hot Pursuit 2)\n"
                "saveFolder=save\n"
                "; the game's registry keys (install folder, CD key, GameSpy id) come from\n"
                ";   HP2FrameFix_registry.ini instead of the Windows registry\n"
                "portableRegistry=1\n"
                "; --- loading ---\n"
                "speedUpLoading=1\n"
                "; draw the loading screen at most every N ms (0 = every step, -1 = never)\n"
                "loadScreenRenderMs=100\n"
                "limiterDuringLoading=0\n"
                "; --- logging ---\n"
                "logToFile=1\n"
                "logEveryNFrames=600\n", f);
            fputs(kInterpBlock, f);
            fputs(kReflBlock, f);
            fputs(kWsBlock, f);
            fputs(kMemBlock, f);
            fclose(f);
        }
        return;
    }
    char line[MAX_PATH + 64];
    bool sawInterp = false, sawRefl = false, sawWs = false, sawMem = false, sawWsNoLb = false, sawPortable = false;
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == ';' || line[0] == '#' || line[0] == '\r' || line[0] == '\n') continue;
        char key[64] = {}, val[64] = {};
        if (sscanf(line, " %63[^= \t] = %63s", key, val) < 1) continue;
        if (!_stricmp(key, "saveFolder")) {              // raw value: may be empty or contain spaces
            const char* v = strchr(line, '=');
            v = v ? v + 1 : "";
            while (*v == ' ' || *v == '\t') v++;
            lstrcpynA(g_cfg.saveFolder, v, MAX_PATH);
            if (char* c = strchr(g_cfg.saveFolder, ';')) *c = 0;   // trailing comment
            char* e = g_cfg.saveFolder + strlen(g_cfg.saveFolder);
            while (e > g_cfg.saveFolder && (e[-1] == '\r' || e[-1] == '\n' || e[-1] == ' ' || e[-1] == '\t')) *--e = 0;
            sawPortable = true;
            continue;
        }
        if (!val[0]) continue;
        if      (!_stricmp(key, "targetFPS"))            g_cfg.targetFPS = atoi(val);
        else if (!_stricmp(key, "enableLimiter"))        g_cfg.enableLimiter = atoi(val);
        else if (!_stricmp(key, "passthrough"))          g_cfg.passthrough = atoi(val);
        else if (!_stricmp(key, "raiseTimerResolution")) g_cfg.raiseTimerResolution = atoi(val);
        else if (!_stricmp(key, "hiResGameTimers"))      g_cfg.hiResGameTimers = atoi(val);
        else if (!_stricmp(key, "preciseGameTicks"))     g_cfg.preciseGameTicks = atoi(val);
        else if (!_stricmp(key, "smoothDriverSteering")) g_cfg.smoothDriverSteering = atoi(val);
        else if (!_stricmp(key, "smoothSlowMotionAnims")) g_cfg.smoothSlowMotionAnims = atoi(val);
        else if (!_stricmp(key, "fixDriverShiftAnim"))   g_cfg.fixDriverShiftAnim = atoi(val);
        else if (!_stricmp(key, "animDiag"))             g_cfg.animDiag = atoi(val);
        else if (!_stricmp(key, "smoothCharacterAnims")) g_cfg.smoothCharacterAnims = atoi(val);
        else if (!_stricmp(key, "smoothProps"))          g_cfg.smoothProps = atoi(val);
        else if (!_stricmp(key, "smoothParticles"))      g_cfg.smoothParticles = atoi(val);
        else if (!_stricmp(key, "tickPhase"))            g_cfg.tickPhase = (float)atof(val);
        else if (!_stricmp(key, "fixEAXOnStartup"))      g_cfg.fixEAXOnStartup = atoi(val);
        else if (!_stricmp(key, "portableRegistry"))     { g_cfg.portableRegistry = atoi(val); sawPortable = true; }
        else if (!_stricmp(key, "speedUpLoading"))       g_cfg.speedUpLoading = atoi(val);
        else if (!_stricmp(key, "loadScreenRenderMs"))   g_cfg.loadScreenRenderMs = atoi(val);
        else if (!_stricmp(key, "limiterDuringLoading")) g_cfg.limiterDuringLoading = atoi(val);
        else if (!_stricmp(key, "logToFile"))            g_cfg.logToFile = atoi(val);
        else if (!_stricmp(key, "logEveryNFrames"))      g_cfg.logEveryNFrames = atoi(val);
        else if (!_stricmp(key, "hdReflections"))        { g_cfg.hdReflections = atoi(val); sawRefl = true; }
        else if (!_stricmp(key, "reflAutoRes"))          g_cfg.reflAutoRes = atoi(val);
        else if (!_stricmp(key, "reflCubemapRes"))       g_cfg.reflCubemapRes = atoi(val);
        else if (!_stricmp(key, "reflEnviromapRes"))     g_cfg.reflEnviromapRes = atoi(val);
        else if (!_stricmp(key, "reflPowerOfTwo"))       g_cfg.reflPowerOfTwo = atoi(val);
        else if (!_stricmp(key, "reflBrightness"))       g_cfg.reflBrightness = (float)atof(val);
        else if (!_stricmp(key, "reflFullDetail"))       g_cfg.reflFullDetail = atoi(val);
        else if (!_stricmp(key, "reflAllFacesEveryFrame")) g_cfg.reflAllFacesEveryFrame = atoi(val);
        else if (!_stricmp(key, "interpolate"))          { g_cfg.interpolate = atoi(val); sawInterp = true; }
        else if (!_stricmp(key, "interpolateCamera"))    g_cfg.interpolateCamera = atoi(val);
        else if (!_stricmp(key, "interpDelay"))          g_cfg.interpDelay = (float)atof(val);
        else if (!_stricmp(key, "maxStepDistance"))      g_cfg.maxStepDistance = (float)atof(val);
        else if (!_stricmp(key, "maxStepAngle"))         g_cfg.maxStepAngle = (float)atof(val);
        else if (!_stricmp(key, "widescreen"))           { g_cfg.widescreen = atoi(val); sawWs = true; }
        else if (!_stricmp(key, "wsCenterUI"))           g_cfg.wsCenterUI = atoi(val);
        else if (!_stricmp(key, "wsFixFOV"))             g_cfg.wsFixFOV = atoi(val);
        else if (!_stricmp(key, "wsMenuResolution"))     g_cfg.wsMenuResolution = atoi(val);
        else if (!_stricmp(key, "wsBackgrounds"))        g_cfg.wsBackgrounds = atoi(val);
        else if (!_stricmp(key, "wsHudToEdges"))         g_cfg.wsHudToEdges = atoi(val);
        else if (!_stricmp(key, "wsMovies"))             g_cfg.wsMovies = atoi(val);
        else if (!_stricmp(key, "wsUnlockResolutions"))  g_cfg.wsUnlockResolutions = atoi(val);
        else if (!_stricmp(key, "wsLog"))                g_cfg.wsLog = atoi(val);
        else if (!_stricmp(key, "wsNoLetterbox"))        { g_cfg.wsNoLetterbox = atoi(val); sawWsNoLb = true; }
        else if (!_stricmp(key, "memoryPools"))          { g_cfg.memoryPools = atoi(val); sawMem = true; }
        else if (!_stricmp(key, "memGeneral"))           g_cfg.memGeneral = (float)atof(val);
        else if (!_stricmp(key, "memRender"))            g_cfg.memRender = (float)atof(val);
        else if (!_stricmp(key, "memTrack"))             g_cfg.memTrack = (float)atof(val);
        else if (!_stricmp(key, "memUI"))                g_cfg.memUI = (float)atof(val);
        else if (!_stricmp(key, "memCars"))              g_cfg.memCars = (float)atof(val);
        else if (!_stricmp(key, "memAudio"))             g_cfg.memAudio = (float)atof(val);
        else if (!_stricmp(key, "memLevel"))             g_cfg.memLevel = (float)atof(val);
        else if (!_stricmp(key, "memCharacter"))         g_cfg.memCharacter = (float)atof(val);
        else if (!_stricmp(key, "memReplay"))            g_cfg.memReplay = (float)atof(val);
    }
    fclose(f);
    if (!sawPortable) {                   // ini from an older version: add the portable keys
        FILE* a = fopen(path, "a");
        if (a) {
            fputs("\n; --- portable ---\n"
                  "; save folder for profiles and saves, relative to the game folder (or a full path);\n"
                  ";   empty = the game's default (Documents\\EA Games\\Need For Speed Hot Pursuit 2)\n"
                  "saveFolder=save\n"
                  "; the game's registry keys (install folder, CD key, GameSpy id) come from\n"
                  ";   HP2FrameFix_registry.ini instead of the Windows registry\n"
                  "portableRegistry=1\n", a);
            fclose(a);
        }
    }
    if (!sawInterp) {                     // ini from an older version: add the new keys
        FILE* a = fopen(path, "a");
        if (a) { fputs("\n", a); fputs(kInterpBlock, a); fclose(a); }
    }
    if (!sawRefl) {
        FILE* a = fopen(path, "a");
        if (a) { fputs("\n", a); fputs(kReflBlock, a); fclose(a); }
    }
    if (!sawWs) {
        FILE* a = fopen(path, "a");
        if (a) { fputs("\n", a); fputs(kWsBlock, a); fclose(a); }
    }
    if (sawWs && !sawWsNoLb) {
        FILE* a = fopen(path, "a");
        if (a) {
            fputs("\n; car select / race results: remove the black bars at the top and bottom\n"
                  "wsNoLetterbox=0\n", a);
            fclose(a);
        }
    }
    if (!sawMem) {
        FILE* a = fopen(path, "a");
        if (a) { fputs("\n", a); fputs(kMemBlock, a); fclose(a); }
    }
}
