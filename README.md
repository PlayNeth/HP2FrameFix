# HP2FrameFix

Smooth frame pacing, high frame rates, HD reflections and widescreen for **Need for Speed: Hot Pursuit 2** on PC.

HP2FrameFix fixes the game's uneven timing so a steady 60 fps no longer judders, and draws frames between physics steps so motion stays smooth at 120 or 144 Hz and above. It also smooths the game's animations, adds HD car reflections and proper widescreen, and makes the game portable. It's a single file, `HP2FrameFix.asi`, and it doesn't touch the game's files on disk.

---

## Contents

- [Requirements](#requirements)
- [Installing](#installing)
- [Uninstalling](#uninstalling)
- [What it changes](#what-it-changes)
- [Settings](#settings)
- [Files the mod creates](#files-the-mod-creates)
- [Troubleshooting](#troubleshooting)
- [Building from source](#building-from-source)
- [Credits](#credits)
- [License](#license)

---

## Requirements

- **Need for Speed: Hot Pursuit 2** for PC, with `NFSHP2.exe` (SHA-1 `63147101c2ca814e964f31c06730b1292093d7f4`).
- An **ASI loader**, such as [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader).

It runs on Windows, and on Linux through Wine or Proton.

## Installing

1. Install the ASI loader in the game's folder, following its instructions for this game.
2. Copy **`HP2FrameFix.asi`** into the game's folder, or into the loader's `scripts` folder.
3. Start the game.

On the first start the mod creates `HP2FrameFix.ini`, with every setting explained, next to the `.asi`. To check it's working, look in `HP2FrameFix.log`: it starts with `=== HP2FrameFix v… started ===`.

Your profiles and saves are copied from `Documents\EA Games\Need For Speed Hot Pursuit 2` to a `save` folder in the game's folder. The originals stay where they were.

> **Don't install HP2WSFix or another HD reflections mod as well.** Their features are built in. If one of them has already changed the game's code, that part of HP2FrameFix stays off, and the log says so.

## Uninstalling

Delete `HP2FrameFix.asi`, and the ASI loader if nothing else uses it. You can also delete the `HP2FrameFix*` files the mod created.

Your saves stay in the `save` folder in the game's folder. Keep them, or copy them back to `Documents\EA Games\Need For Speed Hot Pursuit 2` if you return to the plain game. The mod never removed anything from the Windows registry, so the plain game finds its settings as it left them.

---

## What it changes

Almost everything can be switched off or adjusted in `HP2FrameFix.ini`.

### Frame rate and timing

- **Even frame pacing:** a deadline-based frame limiter, and a precise scheduler in place of the game's uneven 120 Hz simulation timer. With a frame cap that divides the tick rate (60, 120…), every frame gets the same number of simulation steps, so a steady 60 fps no longer judders.
- **Interpolation above 60 fps:** the game simulates at 60 Hz. Racing cars, the camera and the car reflections are drawn between physics steps, so motion is smooth at 120 Hz, 144 Hz and higher. Replays, resets and camera cuts are drawn exactly as the game has them.

### Smoother animation

- **Drivers, cops and other characters** animate every physics step instead of every other one.
- **The steering wheel** turns at 60 Hz instead of 15 Hz.
- **Slow motion:** characters keep animating every frame in the bust sequence.
- **Knocked props, particles and debris** move every physics step.
- **Gear shifts:** the driver's hand no longer stays on the wheel or repeats the shift forever, and quick shifts are animated properly.

### Graphics

- **HD car reflections:**
  - a higher reflection resolution, picked from your display's height;
  - all six cubemap faces drawn every frame, so the seams no longer flicker.
- **Widescreen:**
  - menus and HUD undistorted at any aspect ratio, with the HUD's side elements at the screen edges;
  - the 3D view is Hor+: the same vertical view, more at the sides;
  - menus run at the in-game resolution;
  - FMVs are shown 4:3;
  - every display mode is listed in the options.

### Sound

- **The saved audio mode works from the start:** EAX, mono or any other mode now applies when your profile loads. Before, it only took effect after you changed it in the options.

### Convenience and fixes

- **Portable:**
  - profiles and saves go to a `save` folder in the game's folder;
  - the game's registry settings (install folders, CD key, GameSpy id) live in `HP2FrameFix_registry.ini` instead of the Windows registry, so the game works wherever it's copied.
- **Faster loading:** the loading screen is redrawn less often.
- **Bigger memory pools** for mods with high-poly cars or HD textures.

---

## Settings

**`HP2FrameFix.ini`** is next to the `.asi`. Every setting has a comment saying what it does. The most useful ones:

| Setting | Default | What it does |
|---|---|---|
| `enableLimiter` | `1` | The frame limiter. |
| `targetFPS` | `60` | Frame cap. Set it to your refresh rate to get the benefit of interpolation. |
| `preciseGameTicks` | `1` | Runs the game's 120 Hz timer from a precise, frame-locked scheduler. |
| `interpolate` | `1` | Draws cars between physics steps. |
| `interpolateCamera` | `1` | Blends the camera too. |
| `interpDelay` | `1.0` | How many steps behind the newest one frames are drawn. |
| `smoothCharacterAnims` | `1` | Characters every physics step. |
| `smoothDriverSteering` | `1` | Steering wheel at 60 Hz. |
| `smoothSlowMotionAnims` | `1` | Characters every frame in slow motion. |
| `smoothProps` / `smoothParticles` | `1` | Knocked props and particles every physics step. |
| `fixDriverShiftAnim` | `1` | The gear-shift animation fix. |
| `fixEAXOnStartup` | `1` | Applies the saved audio mode when the profile loads. |
| `speedUpLoading` | `1` | Redraws the loading screen at most every `loadScreenRenderMs` (100). |
| `hdReflections` | `1` | HD car reflections; `reflAutoRes=1` picks 512, 1024 or 2048 from the display height. |
| `widescreen` | `1` | Master switch for the widescreen features (the `ws*` keys). |
| `wsMenuResolution` | `1` | `1` menus at the in-game resolution, `2` the desktop's, `0` 800×600. |
| `wsBackgrounds` | `2` | Full-screen backgrounds: `0` 4:3 with bars, `1` stretched, `2` filled without distortion. |
| `wsMovies` | `1` | FMVs: `1` 4:3, `2` the file's own shape, `0` stretched. |
| `wsNoLetterbox` | `0` | Removes the black bars at car select and race results. |
| `saveFolder` | `save` | Where profiles and saves go: a folder in the game's folder, or a full path. Empty keeps the game's own location. |
| `portableRegistry` | `1` | Keeps the game's registry settings in `HP2FrameFix_registry.ini`. |
| `memoryPools` / `mem*` | `0` | Bigger memory pools for mods, sizes in MB. Setting any size turns them on. |

[CHANGELOG.md](CHANGELOG.md) has the version history.

## Files the mod creates

| File | |
|---|---|
| `HP2FrameFix.ini` | Settings, next to the `.asi`. |
| `HP2FrameFix.log` | What the mod did; send it with any bug report. |
| `HP2FrameFix_registry.ini` | The game's registry settings, with `portableRegistry=1`. |
| `save\` | Your profiles and saves, in the game's folder. On the first start, existing ones are copied there from Documents. |

## Troubleshooting

- **Nothing changed, and there's no `HP2FrameFix.log`:** the ASI loader isn't loading. Check its instructions for this game, and that `HP2FrameFix.asi` is in the game's folder or the loader's `scripts` folder.
- **The log says a part is off because the code differs:** either your `NFSHP2.exe` isn't the version the mod was made for, or another mod (HP2WSFix, an HD reflections mod) changed that code first. Remove the other mod. The parts that match keep working.
- **Motion isn't smooth above 60 fps:** set `targetFPS` to your refresh rate. Keep `preciseGameTicks=1` and `interpolate=1`.
- **Occasional hitches:** raise `interpDelay` a little, to `1.5` for example.
- **A modded car or HD texture pack crashes the game:** set `memoryPools=1`. The `mem*` keys set each pool's size if the defaults aren't enough.
- **Something feels off:** most features can be switched off in the INI one at a time. Please report bugs with your `HP2FrameFix.log`.

## Building from source

The source is a single file, `HP2FrameFix.cpp`, built with MinGW-w64, 32-bit:

```
i686-w64-mingw32-g++ -std=c++17 -O2 -shared -s -static-libgcc -static-libstdc++ \
  -o HP2FrameFix.asi HP2FrameFix.cpp -lwinmm -luser32
```

## Credits

- **Widescreen and memory pools** are reworked from [HP2WSFix](https://github.com/hdbeefup/hp2wsfix) by Lovro Pleše (xan1242) / hdbeefup, MIT License. Its notice is kept in the source.
- **HD reflections** are derived from [NFSHP2 HD Reflections](https://github.com/Arctic94/NFSHDReflections) by Arctic94, a fork of NFS HD Reflections by Aero_, GPL-3.0.
- Need for Speed: Hot Pursuit 2 is © Electronic Arts. This mod contains no game files.

## License

GPL-3.0. See [LICENSE](LICENSE).
