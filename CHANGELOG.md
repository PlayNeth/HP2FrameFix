# HP2FrameFix changelog

HP2FrameFix is an ASI plugin for **Need for Speed: Hot Pursuit 2** (`NFSHP2.exe`, PC). It adds:
- frame pacing and precise game timing;
- smoother animations, props and particles;
- interpolation for frame rates above 60;
- HD car reflections;
- widescreen;
- bigger memory pools for mods;
- several game bug fixes.

Every patch checks the original bytes first. On any other exe version, the mod leaves the game alone.

Credits:
- **Widescreen** is reworked from HP2WSFix by xan1242 / hdbeefup (MIT).
- **HD reflections** are derived from NFSHP2 HD Reflections by Arctic94, a fork of NFS HD Reflections by Aero_ (GPL-3.0).

---

## 0.18.9: cleanup

- **Weather mod cap removed:** NFSHP2 Weather Mod fixed its render-target check, so the cubemap cap and its module scan are gone. `reflWeatherModCap` no longer exists.
- **Unused option removed:** `eaxFixStableFrames` had been unused since 0.18.7.
- **Old INI files:** existing lines for either key are ignored.

## 0.18.8: code review fixes

- **Frozen animations:** character and driver animations no longer freeze after the game resets its step counter at a new race or phase. They used to wait until the counter caught up with its old value.
- **Timer fallback:** if the precise-tick thread can't start, the game's own timer is used. Before, the simulation stopped.
- **Timer resolution:** the scheduler's Sleep fallback gets 1 ms timer resolution from the first tick, not from the first frame.
- **Wrapper safety:** function-pointer patches keep execute rights, which is safer with d3d8 wrappers.
- **`wsMenuResolution=2`:** reads the desktop resolution, not the current fullscreen mode.
- **`wsLog=2`:** stops logging code-made widgets every frame once its table is full.
- **`animDiag`:** its counters are now thread-safe.

## 0.18.7: saved audio mode (EAX) applied properly

- **What was wrong:** the sound engine reads the audio mode only when it starts. That happens before your saved profile is applied. Loading the profile copied the mode but never restarted the engine, while changing it in the options menu does.
- **Fix:** when a loaded profile changes the mode of the running sound engine, the engine is restarted once, the same way the menu does it. EAX, mono and every other mode now stick from startup.
- **Replaces** the old "EAX selected but not active for 90 frames" watch.

## 0.18.6: NFSHP2 Weather Mod 0.9.x compatibility

- **Weather mod cap:** Weather Mod treated any render target 640+ wide as the race view, so HD cubemap faces broke its rain and fog. While a module with that check was loaded, the cubemap was capped at 639, or 512 with `reflPowerOfTwo` (`reflWeatherModCap`). Removed again in 0.18.9.
- **Reflections:** the shared 128.0 constant used by two reflection viewports is no longer overwritten. Both of its uses are redirected anyway.

## 0.18 – 0.18.5: memory pools, movies, letterbox

- **0.18, memory pools:** from HP2WSFix's `[MEMORY]` section.
  - The `memoryPools` and `mem*` keys give bigger fixed-size class pools, for mods with high-poly cars or HD textures. Car textures load into the track pool.
  - `memGeneral=0` sizes the general heap automatically: the game's 21 MB of spare room plus all pools.
  - The Korean UI size is patched at the correct address; HP2WSFix wrote the opcode byte instead.
  - The log reports every pool as OK or FAILED.
- **0.18.1, pools and display:**
  - Setting any `mem*` size turns the pools on. Before, they needed `memoryPools=1` and sizes were silently ignored.
  - FMVs are shown as a 4:3 picture, as the game intends; some, like the EA intro, are stored in another shape and were squeezed. `wsMovies=2` keeps the file's shape.
  - `wsNoLetterbox=1` removes the black bars at car select and race results (from HP2WSFix).
- **0.18.2, too-small `memGeneral`:** a `memGeneral` below what the pools need is raised to that minimum. `memGeneral=1` meant a 1 MB heap and a crash on start.
- **0.18.3, total size checked before patching:** if it doesn't fit, the game keeps its default sizes and the log says why. It used to crash on start.
- **0.18.4, memory limit raised:**
  - The total may be up to 1 GB, or just under 2 GB with the 4GB patch on the exe.
  - A test reservation must succeed, and at least 384 MB must stay free elsewhere.
- **0.18.5, widescreen HUD in multiplayer:** full-screen children of the HUD containers also anchor their side elements to the screen edges. This is meant for the multiplayer chat lines and the "race start synchronizing" text. It hasn't been tested in multiplayer.

## 0.17 – 0.17.11: widescreen

- **0.17, widescreen** (`widescreen` and `ws*` keys), reworked from HP2WSFix:
  - menus and HUD are centred and undistorted at any aspect ratio;
  - full-screen backgrounds are filled;
  - HUD side elements sit at the screen edges;
  - Hor+ 3D field of view;
  - menus run at the in-game resolution;
  - FMVs keep their aspect ratio;
  - all display modes are listed.
- **0.17.1, minimap:** it is made by code, so it was never seen loading. It now goes to the left edge like the rest of the HUD. `wsLog=2` logs every widget drawn.
- **0.17.2, minimap container:** the minimap sits in the HUD root's parent container. That container's side elements go to the edges too.
- **0.17.3, centred text:** text was off to the right. Text measuring no longer subtracts the centring offset.
- **0.17.4, FMV last column:** the last pixel column of FMVs, green after filtering, was cropped.
- **0.17.5, crop removed:** 0.17.4's crop is gone, because its viewport stayed set for the race.
- **0.17.6, showroom cars:** they were stretched sideways. Big 3D views of any shape get Hor+.
- **0.17.7, Hor+ reworked:** each 3D view keeps the look it had on a 4:3 screen, whatever aspect it asks for.
- **0.17.8:** extra logging of 3D view crop factors (diagnostic).
- **0.17.9, Hor+ scope:** only views that ask for exactly the whole screen (the race view) get Hor+. Views cropped by the screen, like the showroom car, already keep their shape, and correcting them squished the car.
- **0.17.10, EA Trax chyron in races:** goes to the left edge with the rest of the HUD.
- **0.17.11:**
  - **Camera interpolation from the 5th race on:** it had stopped working, because each race creates a new camera owner and the list of 4 never dropped old ones. Old owners are now dropped, least recently used first.
  - **EA Trax chyron in the menus:** at the left edge too.

## 0.16 – 0.16.3: HD car reflections

- **0.16, HD reflections built in** (`hdReflections` and `refl*` keys): higher cubemap and environment-map resolution, chosen by display height. Also fixes the cubemap's alternating halves, which caused seam flicker.
- **0.16.1, steady glints:** sun glints and reflections on the car no longer wobble with interpolation. The cubemap update now sees the blended camera.
- **0.16.2, player car blending:** 0.16.1 also swapped the body rotation during the cubemap update, which left the player car unblended. That part is removed; the camera part is kept.
- **0.16.3, camera kept all frame:** the blended camera stays in the camera object for the whole frame. Each car copies the camera eye for its sun specular, which was computed from the raw step camera. The raw camera is restored before the next physics step and at Present.

## 0.15 – 0.15.6: interpolation

0.15 is built on 0.13.2. 0.14 was an abandoned FOV experiment.


- **0.15, interpolation** (`interpolate`, `interpolateCamera`, `interpDelay`, `maxStepDistance`, `maxStepAngle`):
  - Racing cars and the camera are drawn between the game's 60 Hz physics steps, so motion is smooth at any frame rate above 60.
  - After every physics step the cars' bodies and each view's camera are recorded.
  - At render time they are blended to "display time minus `interpDelay` steps".
  - Replays, resets and cameras changed outside a step are drawn exactly as the game had them.
- **0.15.1, camera cuts:** a frame whose camera can't be blended (look back, camera switch, jump) draws the cars unblended too. They were a step away from the camera for a frame, which flickered.
- **0.15.2, flipping cameras:** a camera whose direction turns more than 45° in one step, rolls, or whose FOV jumps counts as a cut. Look back on cameras that flip was blended through the car.
- **0.15.3, endless shift animation fixed:** the driver sometimes repeated the gear-shift animation forever. The animation's "looped" flag is now a latch: it is set when a clip wraps and cleared only when a new clip starts.
- **0.15.4, quick shifts:** gear changes during a shift are folded into it, and quick back-and-forth shifts are not animated. Replaced in 0.15.5.
- **0.15.5, shift pacing:** every gear change is animated again. A change during a shift extends it: the hand stays at the shifter and returns about 0.5 s after the last change.
- **0.15.6, reflections follow the car:** the car reflection probe gets the car's blended position. It used the raw 60 Hz position, so reflections stepped against the smoothly drawn car.

## 0.13.1 – 0.13.2

- **0.13.1, knocked props flickering:** fixed. The extra prop sync on even steps now also files props back into the grid cells the renderer draws from.
- **0.13.2, game speeding up when the frame cap can't be reached:** fixed, for example a 480 cap running at about 270 fps.
  - Re-locking to the frame schedule never fires a tick earlier than one full tick after the last.
  - Free-running ticks keep their cadence.
  - If the limiter keeps missing its deadlines, ticks run free until frames are on time again.

## 0.11 – 0.13: smoother world

- **0.11, `smoothCharacterAnims`:** the game ran character actors only on odd physics steps, so every character animated at 30 Hz. The drivers, cops, the dog and the busted driver now also update on even steps. Animation speed is unchanged.
- **0.12, `smoothParticles`:** the particle and debris pool (smoke, sparks, crash debris) moved at 30 Hz. It now moves every physics step at the same speed. In 0.12 this setting was mistakenly called `smoothProps`.
- **0.13, `smoothProps`:** this setting now covers the real knockable props, which move every step. Their transforms are synced and their simplified integrator steps every physics step (dt 1/60). Collisions keep their original rate.

## 0.9 – 0.10: animation clock

- **0.9, `smoothDriverSteering`:** the steering wheel pose updated every 4th physics step (15 Hz). It now updates every step with a quarter of the step size, so the wheel turns at the same speed, at 60 Hz.
- **0.10, `smoothSlowMotionAnims`:**
  - Slow-motion cameras, like the bust sequence, scale game time down to 0.25x, so animations moved only 15 times a second.
  - Animations now advance by fractional steps taken from race time, so characters animate every frame at any game speed.
- **0.10, `fixDriverShiftAnim`:** a stale "looped" flag, often left by the steering clip, cancelled freshly started shift animations, so the hand never left the wheel. The flag is cleared whenever an update advances no time.
- **0.10, game speed logging:** game speed changes are logged.

## 0.8: precise game ticks

- **The problem:** the game simulates on a 120 Hz tick thread driven by a chain of one-shot multimedia timers. Windows delivers those late and unevenly, so some frames got 1 or 3 ticks instead of 2, a visible judder at a steady 60 fps.
- **`preciseGameTicks`:** a high-resolution scheduler replaces that timer.
- **`tickPhase`:** with a frame cap that divides the tick rate (60, 30, 120…), ticks are phase-locked to the frame schedule, so every frame gets the same number of ticks.
- **Stats:** they include a ticks-per-frame histogram; `2:600` means every frame got 2 ticks.

## 0.7.1 – 0.7.2

- **0.7.1, INI and log location:** `HP2FrameFix.ini` and `HP2FrameFix.log` live next to the `.asi`, for example in a `scripts\` folder, instead of next to the game exe.
- **0.7.2, `skipRapidPresents` removed:** dropping presents that came soon after the previous one threw away real frames and caused stutter.

## 0.7: EAX and loading

- **EAX fix (`fixEAXOnStartup`):** the game only turned EAX on when the audio system restarted, and nothing restarted it after the saved profile was applied. The mod restarted it once when EAX was selected but inactive. Reworked in 0.18.7.
- **Loading speed-up (`speedUpLoading`, `loadScreenRenderMs`):**
  - Every load step rendered and presented a full loading-screen frame, so load time was bound to frame time.
  - The screen is now drawn at most every `loadScreenRenderMs`, and the limiter is bypassed while loading.
  - Per-load stats are logged.

## 0.1 – 0.6: frame pacing

These first versions weren't recorded in detail.
- **Frame limiter:** a deadline-based limiter (`enableLimiter`, `targetFPS`) using a high-resolution waitable timer.
- **Timer resolution:** `timeBeginPeriod(1)` is set (`raiseTimerResolution`).
- **`hiResGameTimers`:** answers the game's `GetTickCount` and `timeGetTime` from the high-resolution performance counter.
- **Logging:** a serialized log with frame statistics.
