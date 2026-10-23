# HP2FrameFix

An ASI plugin for **Need for Speed: Hot Pursuit 2** (PC, `NFSHP2.exe`) that fixes frame pacing and the game's timing, smooths its animation, interpolates frames above 60 fps, and adds HD car reflections and widescreen.

## What it does

- **Frame pacing and precise timing.** A deadline-based frame limiter, and a high-resolution scheduler in place of the game's uneven 120 Hz simulation timer. With a frame cap that divides the tick rate (60, 120…), every frame gets the same number of simulation ticks, so a steady 60 fps no longer judders.
- **Interpolation above 60 fps.** The game simulates at 60 Hz. Racing cars, the camera and the car reflections are drawn between physics steps, so motion is smooth at 120, 144 Hz and higher. Replays, resets and camera cuts are drawn exactly as the game has them.
- **Smoother world:**
  - drivers, cops and other characters animate every physics step instead of every other one;
  - the steering wheel turns at 60 Hz instead of 15 Hz;
  - characters keep animating every frame in slow motion (the bust sequence);
  - knocked props, particles and debris move every physics step.
- **Gear-shift animation fixes.** The driver's hand no longer stays on the wheel or repeats the shift forever, and quick shifts are animated properly.
- **HD car reflections:**
  - higher reflection resolution, picked from the display height;
  - the cubemap's alternating halves fixed, so the seams no longer flicker.
- **Widescreen:**
  - menus and HUD are undistorted at any aspect ratio;
  - HUD side elements sit at the screen edges;
  - the 3D view is Hor+;
  - menus run at the in-game resolution;
  - FMVs are shown 4:3;
  - every display mode is listed.
- **Memory pools.** Bigger fixed-size pools for mods with high-poly cars or HD textures.
- **Audio mode on startup.** The saved audio mode (EAX, for example) is applied when the profile loads. Before, it only took effect after you changed it in the options.
- **Faster loading.** The loading screen is redrawn less often.

## Requirements

- `NFSHP2.exe` (SHA-1 `63147101c2ca814e964f31c06730b1292093d7f4`).
- An ASI loader.

Every patch checks the original bytes first. On any other exe version, the parts that don't match stay off, and the log says which.

Don't load HP2WSFix or another HD reflections mod alongside it. Their features are built in. If one of them has already changed the code, that section of HP2FrameFix stays off.

## Installation

1. Install an ASI loader for the game.
2. Put `HP2FrameFix.asi` in the game folder (or the loader's `scripts` folder).
3. Start the game. `HP2FrameFix.ini` and `HP2FrameFix.log` are created next to the `.asi`.

## Settings

All settings are in `HP2FrameFix.ini`. The most useful ones:

| Key | Default | Meaning |
|---|---|---|
| `enableLimiter` | `1` | Frame limiter. |
| `targetFPS` | `60` | Frame cap. Set it to your refresh rate to get the benefit of interpolation. |
| `preciseGameTicks` | `1` | Run the game's 120 Hz simulation timer from a precise, frame-locked scheduler. |
| `interpolate` | `1` | Draw cars between physics steps. |
| `interpolateCamera` | `1` | Blend the camera too. |
| `interpDelay` | `1.0` | Steps behind the newest step that frames are drawn. |
| `smoothCharacterAnims` | `1` | Update characters every physics step. |
| `smoothDriverSteering` | `1` | Steering wheel at 60 Hz. |
| `smoothSlowMotionAnims` | `1` | Animate characters every frame in slow motion. |
| `smoothProps` / `smoothParticles` | `1` | Knocked props and particles every physics step. |
| `fixDriverShiftAnim` | `1` | Gear-shift animation fix. |
| `fixEAXOnStartup` | `1` | Apply the saved audio mode when the profile loads. |
| `speedUpLoading` | `1` | Redraw the loading screen at most every `loadScreenRenderMs` (100). |
| `hdReflections` | `1` | HD car reflections; `reflAutoRes=1` picks 512, 1024 or 2048 from the display height. |
| `widescreen` | `1` | Master switch for the widescreen features (`ws*` keys). |
| `wsMenuResolution` | `1` | `1` = menus at the in-game resolution, `2` = desktop, `0` = 800x600. |
| `wsBackgrounds` | `2` | Full-screen backgrounds: `0` = 4:3 with bars, `1` = stretch, `2` = fill without distortion. |
| `wsMovies` | `1` | FMVs: `1` = 4:3, `2` = the file's own shape, `0` = stretched. |
| `wsNoLetterbox` | `0` | Remove the black bars at car select and race results. |
| `memoryPools` / `mem*` | `0` | Bigger memory pools for mods, sizes in MB (setting any size turns them on). |

The INI itself has a comment for every key. Version history is in [CHANGELOG.md](CHANGELOG.md).

## Building

A single source file, built with MinGW-w64 (i686):

```
i686-w64-mingw32-g++ -std=c++17 -O2 -shared -s -static-libgcc -static-libstdc++ \
  -o HP2FrameFix.asi HP2FrameFix.cpp -lwinmm -luser32
```

## Credits

- **Widescreen and memory pools** are reworked from [HP2WSFix](https://github.com/hdbeefup/hp2wsfix) by Lovro Pleše (xan1242) / hdbeefup, MIT License. Its notice is kept in the source.
- **HD reflections** are derived from [NFSHP2 HD Reflections](https://github.com/Arctic94/NFSHDReflections) by Arctic94, a fork of NFS HD Reflections by Aero_, GPL-3.0.

## License

GPL-3.0. See [LICENSE](LICENSE).
