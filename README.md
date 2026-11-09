# glOSports-gtavc — GTA Vice City, native, on the GA36-MB

Grand Theft Auto: Vice City through Rockstar's Android build (War Drum
Studios, 2012): a loader (`loader/`) maps the game's own `libGTAVC.so`
(armeabi-v7a),
resolves its imports to goodluckOS's glibc, ARM's Mali GLES 2.0 driver (from
the folder's `glos/`, see `goodluckOS-mali-driver`), OpenAL Soft and SDL's pad,
answers its Java calls through a fake JNI, and runs it. The loader core is
and the game's fixes are carried over from `glOSports-gta3` (the same War
Drum layer), checked against this library. Separate from `glOSports-gtavc-pc`,
which runs the PC data on reVC. The console stays stock goodluckOS.

Status: runs on the GA36-MB: menus at 55 fps, new game,
saving and loading work, Xbox button names in the help texts.

## Game files

From your own install of the Android game (package `com.rockstargames.gtavc`), into
`/home/player/ports/gtavc/game/`:

| From | Version | What goes into `game/` |
|---|---|---|
| `main.11.com.rockstargames.gtavc.obb` | 11 | its contents (it is a zip), first |
| the `.apk` | version 1.0.7 (`libGTAVC.so` md5 `0d10ceaf…`) | the contents of `assets/` over the OBB's (its `TEXT/*.gxt`, `data/main.scm` are newer), and `lib/armeabi-v7a/libGTAVC.so` |

The OBB is in `/sdcard/Android/obb/com.rockstargames.gtavc/` on the phone. Files
named `…~HEAD` or `…~<hash>` in some unpacked APKs are leftovers of the tool
that unpacked it and aren't needed. The game opens everything loose from
`game/`: the loader answers no archive name, so the game's own ZIP layer is
never used, and its fallback to the APK (a Java helper) is switched off.

## The game folder on the console (`/home/player/ports/gtavc/`)

| | |
|---|---|
| `gtavc` | the loader (DT_RPATH `$ORIGIN/lib`) |
| `lib/` | OpenAL Soft 1.19 and its dependencies (sndio, bsd, md) |
| `alsoft.conf` | OpenAL reads it from the game folder by itself (ALSA) |
| `game/` | the game files above; saves land here too |
| `glos/` | the ARM driver set from `goodluckOS-mali-driver` |
| `start.sh` | pad mapping and settings, then `exec ./glos/glos-gpu run mali ./gtavc` |

Launcher entry: `gtavc.puppy` ("Grand Theft Auto: Vice City", with the reVC port's icon). Log: `/dev/shm/gtavc.log`.

## Settings (`start.sh`)

| Variable | Default | |
|---|---|---|
| `GAME_STICK_SCALE`, `GAME_STICK_DEADZONE` | 1.5, 0.12 | stick shaping (radial dead zone after scaling) |
| `GAME_FPS_COUNTER` | 0 (1 in `start.sh`) | frames per second, top right |
| `GAME_AUDIO_RATE` | 44100 | OpenAL mixing rate (the game asks for 22050) |

## What the loader changes in the game (`loader/game.c`)

- **Files:** the game is told there is no archive, so it opens everything
  loose from `game/`; its fallback to the APK (a Java helper) is off.
- **Saves:** a save is written to `GTA3sf<n>.b.tmp` and renamed through Java;
  the loader does the rename.
- **Pad:** the `WarGamepad_*` calls answer from SDL's pad, reported as an
  Xbox 360 pad.
- **Button prompts:** help texts fill `~k~~ACTION~` from the action's PC
  joystick binding (the mobile build runs the pad through the PC joystick
  layer); the text for it (`FEC_JBO`, "Joy %d") is missing from the mobile
  `.gxt`, so the prompts were empty. They now name the GA36-MB's button
  (A, B, X, Y, LB, RB, LT, RT, BACK, ...); Vice City's `.gxt` has the
  entry, which would print "Joy n". The bindings are at `ControlsManager`
  +3580 per action here (GTA III: +3340).
- **Touch icons in help texts:** the mobile texts write buttons as touch
  tokens (`Tap ::FIRE:: to fire`), drawn as touch-button icons. Each touch
  button's pad button is in the game's default layout
  (`ButtonDefaultsTablet`), so after the text loads (`CText::Load`,
  `CText::LoadMissionText`) the tokens are replaced with those names: FIRE
  and ATTACK B, SPRINT, ACCEL and BUY A, BRAKE X, LOCKON and HANDBRAKE RB,
  CAR, DOOR and EXIT Y, HORN R3, SUBMISSIONS L3, PHONE LB, weapon LEFT/RIGHT
  the D-pad. TANKLEFT/TANKRIGHT (analog aim), SAVE (the save-spot icon) and
  SWAP stay icons.
- **HUD:** the HUD is laid out like the touch controls: the radar is drawn in
  the touchscreen's radar button, the clock, money, health, weapon and
  wanted level in its summary button, the help box in its tut button. The
  loader places them every frame: the radar with SA's `Adjustable.cfg`
  centre and radius (76, 370, 44 in 640×448), the stats block at SA's
  measured inset, the help box top left at the same insets. The zone and
  vehicle names, subtitles, pager and mission timers are drawn at fixed
  positions in `CHud::Draw` and stay where the game puts them. Vice City reads no HUD
  layout file of its own.
- **Camera and lock-on:** the right stick turns the free camera (the
  game's own, made for touch swipes). Invert Look did nothing: CPad's
  `LookAroundUpDown` turns the stick by the setting and `CCamera::CamControl`
  turned it again by the same setting, so the two cancelled and the view
  moved like a dragged touch picture. CamControl's second turn is gone, so
  off pushes the view the way the stick goes and on inverts. While
  locked, `ShiftTargetLeft/RightJustDown` (LT / RT) also answer a
  right-stick flick past half; the game picks the next target on that side
  and keeps the lock when there is none. RB's lock-on
  (`FindWeaponLockOnTarget`) and target switching follow GTA SA's rules
  (`EvaluateTarget`, `EvaluateNeighbouringTarget`, `FindTargetPriority`,
  `LOSBlockedBetweenPeds`): each candidate is scored off the player's
  facing and, when in front of the camera, off the camera's heading, the
  better counting; the score is 1 − 0.785 × the angle off over the fourth
  root of the distance, times 0.8 for the game's aiming priorities
  (`IsThisPedAnAimingPriority`) and 0.1 for the rest, and counts only in
  line of sight (buildings and objects). The next target is the nearest in
  angle from the camera on the asked side, within 50 degrees. The game's
  own pre-check (in front of the player, 40 m), which SA doesn't have, is
  answered yes.
- **Startup:** the legal screens and "tap to start" count as shown
  (`shownLegalScreen`), so the game opens on the main menu after its videos.
- **Frame Limiter:** `MainGameTick` asks `OS_ScreenSetRefresh` for 30 Hz
  with the limiter on (60 on two phone chip types), 0 with it off and 60 in
  the menu; on Android that function is empty (`bx lr`), so the setting did
  nothing. Its four calls go to the loader: with the limiter on each swap
  waits for its slot on a 1/30 s grid (`egl_frame_limit`), otherwise the
  flips set the pace. Measured: 30 fps in gameplay with it on, 55 in the menu.
- **Off:** the app-rating popup and vibration feedback (cloud saves are already off in this build).

## Videos

The logos and the opening titles (`game/movies/Logo.mpg.m4v`,
`Gtatitles.mpg.m4v`) play at startup as they come in the OBB (H.264 640x480,
AAC), no conversion: 25 fps, no frame dropped. A or Start skips. On Android a
Java MediaPlayer shows them while `MainGameTick` ticks and polls
`OS_MovieIsPlaying`; here `OS_MoviePlay` and `AND_IsMoviePlaying` /
`AND_StopMovie` go to `loader/movie.c` (as in `glOSports-gta3`). The game
thread doesn't hold the screen here: `GraphicsThread` does, and while the
render queue is empty it sleeps through `OS_ThreadSleep`, a jump to `usleep`
(4 bytes, too short for a hook). The loader answers the game's `usleep`
import (`w_usleep` in `loader/imports.c`): while a video plays, the thread
holding the game's context shows the newest picture first. Without
`game/movies/` the game goes straight to the menu.

## Build

```
./build.sh      # loader/ -> build/gtavc (Docker, Debian bookworm armhf)
./package.sh    # -> out/ports/gtavc/ and out/.local/share/applications/gtavc.puppy
```

`build.sh` runs `tools/build-ffmpeg.sh` first when `build/ffmpeg/` is
missing: a static FFmpeg 6.1.2 (MP4 demuxer, H.264 and AAC decoders, NEON),
from ffmpeg.org's `ffmpeg-6.1.2.tar.xz` (sha256 `3b624649…`) unpacked in
`upstream/ffmpeg-6.1.2`.

Test tools are shared: `../goodluckOS-tools/testing-sync.sh gtavc` puts them
on the console (`glos/testing/`), `testing-clean.sh gtavc` takes them off,
`shot.sh gtavc file.png` takes a screenshot (see `goodluckOS-tools`).

## Licence

The port's own code is MIT (`LICENSE`); `package.sh` puts a copy into the game folder's
`licenses/`. Third-party code it carries keeps its own licence (below).

## Third-party components

Code carried over from the Vita loaders it names keeps their MIT notices;
those, FFmpeg (LGPL-2.1, linked statically for the videos), OpenAL Soft and
the other bundled libraries, and ARM's Mali driver are listed with their
licences in `licenses/`, which `package.sh` copies into the game folder. The
game itself is not included.
