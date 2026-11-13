# Status

State of the rebuild after the fourth round (2026-11-13), which took the owner's six notes:
more picture shaders, Back on Android, a bigger default interface, two screens, touch
scrolling with weight, and settings chosen for the device. All six are in, were driven on
Windows and on the Retroid Pocket Duo, and the AppImage started for the first time on the
Legion Go S. Everything was driven through automation, adb and screenshots: nobody held a
device, dragged a real finger or listened.

## The owner's notes

| Note | State | What was run |
|---|---|---|
| More shaders, written here | 16 presets on a multi-pass chain (CRT, CRT bloom, arcade monitor, composite, S-Video, VHS, TV at night, dot matrix green, pocket grey, LCD grid, ghosting, backlight bleed, scanlines, smooth edges, sharp, pixel perfect), Auto per console, a choice per game / console / everywhere, live preview and sliders in the pause menu | Windows: Tetris opens in dot matrix by Auto; CRT bloom chosen on Gran Turismo 2 from the pause menu. Retroid (GLES 3): Tetris in dot matrix. The other presets were looked at on Windows by their author only; none of them on GLES or Linux |
| Android Back opens the pause menu | Fixed: Back from every source (key, gesture, the pad's Back key) reaches the app | Retroid, debug and release APK: `input keyevent 4` in Tetris paused with the menu; in the DS game too |
| Bigger interface on Android | The size comes from the screen: 125% on the Retroid Pocket Duo (it was 100%), up to 155% on a 5.5 inch screen | Retroid: setup, the folder browser, home, pause menu, details and both companion pages looked at. Two things were cut and are fixed: the selection border of a list's first row, and the details page, which ran off the top and bottom |
| Two screens | DS games put one screen on each display; one-screen games get quick settings on the second display, and browsing shows the focused game there | Retroid: Explorers of Skies with the top screen on the main display and the touch screen on the second; game details while browsing; quick settings during Tetris. Touch on the second display was run by the feature's author, not in this check. No 3DS game is in the test library |
| Scrolling with weight | Flings with friction, a rubber band at the ends, snapping shelves, no Qt | Windows: an injected fling on the library grid overshot the end and came back. Retroid: an `input swipe` (a real touch event) on the library grid ran on, overshot 835 to 870 and settled in about a second. No real finger |
| Settings chosen for the device | The device is recognised and its defaults sit below the user's settings; setup lists them; Settings > Device shows and overrides them | Retroid, after clearing the app's data: the first step is titled "Set up for Retroid Pocket Duo" and lists its choices (two screens, 125%, PSP 3x, 120 Hz, rewind on). Windows: Settings > Device shows the real PC (desktop, very high, 100%). Legion Go S: the log names it (handheld, mid) |

## Release check (last thing done)

`win` preset from an empty build directory at `3e202f5`: 11 of 11 test programs pass and the
seven parallel commits built together without a change. Two fixes came out of the device
check (`964602f`, `410665a`); after them the Windows and Linux tests ran again (11 of 11 on
both) and every package below was rebuilt.

- **Windows** (automation port 34712, portable `user` folder): Tetris by Auto in dot matrix,
  pause menu, exit; Gran Turismo 2 with CRT bloom chosen on the Picture page; a fling on the
  library grid; Settings > Device; the details page at 100% and with a simulated handheld.
  The app left no process behind. `dist\windows` (14.3 MB) is mirrored to
  `C:\Users\david\Desktop\EmuDEN`; the Desktop copy was not started.
- **Android** (Retroid Pocket Duo; the debug APK with automation on port 34722, then the
  release APK driven by adb touches): everything in the table above, from cleared app data.
  The Game Boy and DS cores were downloaded on Play. The release APK (debug key) is
  installed and force-stopped; it is `dist\android\EmuDEN.apk`. The fling, the first-run
  setup and the DS game were run on the debug APK of the commit before the two fixes; the
  final release APK was started, opened the details page and Tetris, and paused on Back.
- **Linux**: built in WSL Debian, 11 of 11 tests, AppImage 5.8 MB in `dist\linux`. Installed
  on the Legion Go S with `install-steamos.sh` and started once for 10 s over ssh: OpenGL
  4.6 on Mesa (radeonsi), the device recognised, closed on the signal. Nothing was looked
  at or played there.

## Games (Windows)

All 17 ROMs in the test library start, show a picture and advance frames; each ran about
10 s from the beginning, unpacked (second round, not repeated; everything from here to
"Platforms" is from the earlier rounds). "Picture" comes from
screenshot statistics (colour spread, two shots differing); only the PSP, Gran Turismo 2 and
Racing Lagoon pictures were looked at.

| Console | Game | Core | Frames in ~10 s | Picture |
|---|---|---|---|---|
| GB | Pokemon - Brown | mGBA | 621 | yes |
| GB | Tetris | mGBA | 603 | yes |
| GBA | Pokemon - Heart & Soul | mGBA | 605 | yes |
| GBA | Pokemon - Lazarus | mGBA | 604 | yes |
| GBA | Pokemon - Seaglass | mGBA | 605 | yes |
| GBC | Mario Golf | mGBA | 611 | yes |
| GBC | Mario Tennis | mGBA | 610 | yes |
| GBC | Pokemon - Ambrosia | mGBA | 612 | yes |
| GBC | Pokemon - Crystal Awakening | mGBA | 598 | yes |
| GBC | Pokemon - Edicion Cristal | mGBA | 606 | yes |
| GBC | Pokemon - Polished Crystal | mGBA | 605 | yes |
| GBC | Super Mario Bros. Deluxe | mGBA | 608 | yes |
| GBC | Wario Land 3 | mGBA | 609 | yes |
| NDS | Explorers of Skies | melonDS (FreeBIOS) | 606 | yes |
| PSP | PoPoLoCrois | PPSSPP | 606 | yes; title screen by about 20 s, black in between |
| PS1 | Gran Turismo 2 | PCSX-ReARMed (no BIOS) | 513 (50 fps game) | yes |
| PS1 | Racing Lagoon | PCSX-ReARMed (no BIOS) | 621 | black for the first ~12 s, then the intro plays |
| Switch 2 | Ocarina of Time | installed emulator (engine level 0) | - | in the engine's own window |

Pause, save state and exit to home were confirmed on one game per console in the first
round (Tetris, Mario Tennis, Pokemon - Lazarus, Explorers of Skies, Gran Turismo 2,
PoPoLoCrois).

Zipped ROMs play (from a scratch library, since the zips in the test library are hidden
behind their unpacked copies):

| Game (zip) | Starts | Save and load state | Fast-forward | Reset |
|---|---|---|---|---|
| Tetris (GB) | yes | yes, picture back at the saved point | about 3x | back at the copyright screen |
| Gran Turismo 2 | yes | yes, picture back at the saved point | yes, badge shown | back at the first logo |
| Racing Lagoon | yes | "Loaded slot 1" (screen still black then) | yes | not confirmed on the picture |

Pads, sound and rewind (third round, virtual pads and counters, port 30814):

| Game | Device underruns | Refills | Queue avg / min | Ratio range | 3 s WAV |
|---|---|---|---|---|---|
| Tetris | 0 | 0 | 84 / 72 ms | 1.003-1.012 | peak 0.64, one 190 ms silence (the game's own) |
| Gran Turismo 2 (50 fps on 60 Hz) | 0 | 0 | 65 / 54 ms | 0.999-1.003 | peak 0.68, no zero gap |
| PoPoLoCrois | 0 | 0 after the fix (4 before) | 105 / 44 ms | 0.991-1.007 | peak 0.44, no zero gap |

60 s each, counters reset after the first second; no overruns or dropped frames. Rewind was
confirmed on Tetris and Pokemon - Lazarus: the history shrinks at about 30 states/s and
screenshots show the picture going back.

## Platforms

| Platform | State |
|---|---|
| Windows | The release check above and the earlier tables. `tools/package.ps1` writes `dist\windows`; the same files are in `C:\Users\david\Desktop\EmuDEN` (a `user` folder there is left alone). |
| Linux | `linux` preset builds in WSL Debian (clang), 11 of 11 test programs pass. The AppImage is installed on the Legion Go S in `~/Games/EmuDEN` with a menu and desktop entry, and started there once (10 s, log read). An earlier AppImage played Tetris under WSLg. No game, pad or sound on the handheld yet. |
| Android | Retroid Pocket Duo (Android 16, Adreno 722): the release check above. From earlier rounds: the storage dialog and folder copy, PoPoLoCrois, Mario Golf, Pokemon - Lazarus and Gran Turismo 2, save and load state, the built-in pad in games, rotation and home/back. See `shells/android/README.md`. |

Plugin source (Denarmo/emuden-plugins, index sequence 6): every core except `fake08` has
`android-arm64` and `linux-x86_64` downloads (26 Android cores; N64 on Android is the
`mupen64plus_next_gles3` build), and `emuden.psp` carries PPSSPP's assets as a
`system-files` component that the app unpacks into the system folder with the core.

## What the fourth round added

- **Picture**: `gfx::EffectChain` (passes, render targets, previous-frame history) and the
  presets in `assets/shaders`; `player.picture` defaults to `auto`.
- **Scrolling**: one motion model per scrolling node (`ui/src/scroller.cpp`) for fingers,
  flings, the wheel and focus moves; automation has `drag`, `fling`, `touch`, `wheel`.
- **Device**: `pal::GetDeviceFacts()`, classification with a table of 16 known devices, a
  defaults layer in the settings store, `EMUDEN_FAKE_DEVICE=<id>` to simulate one.
- **Second screen**: `pal::GetSecondDisplay()` (a Presentation on Android, a second window
  on the desktop with `EMUDEN_SECOND_WINDOW=1`), the companion pages, `ClaimSecondScreen`
  for two-screen consoles, and layouts for one display (stacked, side by side, large and
  small, one screen with Swap).
- **Back on Android** from the key, the gesture and the pad's Back key.
- **From the release check**: a list's first and last row keep their whole selection
  border; the details page scrolls under its title when it does not fit; a rebuilt page
  shows its focused row at once.

## What the third round added

- **Pads**, driven through SDL virtual gamepads: hot-plug; Xbox, PlayStation and Nintendo
  families recognised (unknown pads sorted by name); confirm and back by family on the
  home screen; face buttons, Start and the stick as D-pad in Tetris; face buttons and R2 in
  Gran Turismo 2; guide and Start+Select open the pause menu; "Exit to home" by pad;
  Select+L1 rewinds and Select+R1 fast-forwards. Unplugging mid-game now pauses with a
  toast; a removed pad releases its held direction (by reading, not exercised); the PS1
  left stick is the D-pad; choosing a pad on the pause menu's Players page makes it
  player 1.
- **Sound**: counters and a WAV capture of the device's output. The queue target grows
  after a near-empty dip or a refill, and GPU cores start at 112 ms (about 48 ms more sound
  latency on PSP). The on-screen "gaps" number counts refills too.
- **Test endpoint** (`app/screens/player/test_endpoint.cpp`, automation port + 1): `vpad`,
  `player`, `audio reset`, `wav`; `tools/drive.py` routes them there.
- **Library**: a session log (last 5000) and a date added; "Continue playing" and
  "Recently added" on home, "Added" in details, "Last 7 days" in statistics. A session no
  longer counts the time of a game opened over it (driven: 11 s booked, not about 33).
  Covers are shown whole on a soft tint. An image beside a ROM named like the game without
  its tags is its cover (unit-tested only). "Install all" in setup and hidden games were
  driven once and needed no fix.
- **Linux**: a `user` folder beside the AppImage file makes it portable; `package.sh`
  validates the desktop entry; `tools/linux/install-steamos.sh [--portable] [--steam]`.
- **Android**: the storage dialog with a folder copy, a release build, and
  `tools/android/run.ps1 -Apk <file>` and `-Stop`.

The second round added the engine's clean stop, text entry, the Controllers page, glyphs
per pad family, system files of a core, and Back trapped on Android.

## Not run

- A real gamepad held in the hand, two real pads, and listening to sound on any platform.
- Rewind on consoles other than GB and GBA. Load state, fast-forward and Reset on NDS, PSP
  and GBA on Windows.
- The Switch 2 engine in full screen (the app passes `-f`; only read in the source), the
  clean stop on Linux, and the engine copy on the Desktop.
- The text entry, Controllers page and glyph sheet with a real pad or touch.
- The Desktop copy of this build: mirrored, not started.
- Linux on real hardware (GPU drivers, gamescope, the handheld), `install-steamos.sh` on
  real SteamOS and its `--steam` option, the PNG icon (`rsvg-convert` is not in WSL), any
  start of the final AppImage, and pads and sound on Linux.
- This round: a real finger on any scrolling page; the shaders other than dot matrix on
  GLES and all of them on Linux; a 3DS game; the AYN Thor and every device of the table
  other than the Retroid, the Legion Go S and this PC (their defaults are unit-tested
  only); the performance defaults themselves (nobody measured that PSP 3x holds its speed
  on the Retroid); the second window on Linux; two screens with the display rotated.
- Android: sticks inside
  a game (the left stick did not move the PoPoLoCrois title menu; no other game was
  tried), the Home/Mode button, touch in a game, the Android 10 permission path, a large
  folder copy or cancelling one, the first storage dialog in the final build (its garbled
  text was fixed but not seen again), the folder browser opening in the copied `Games`
  folder, and rewind.
- The ROM hack cover with a real image, the high contrast themes, per-game core and screen
  size end to end.
- The three agents' reports reached this file cut off at the end; their own "not run"
  lists may be longer than what is here.

## Known problems

- `fake08` (PICO-8) has no download on any platform: the buildbot has no build of it.
- Racing Lagoon stays black for its first ~12 s and PoPoLoCrois for ~15 s before the title.
- PPSSPP logs "Exception handler not implemented on this platform" at start; the game runs.
- PCSX-ReARMed logs `fopen ...\bios\PPSSPP failed: 13` once the PSP assets folder is in the
  system folder (it tries the folder as a BIOS file); the game runs.
- The pad and sound test commands are on the automation port + 1, not in the main
  endpoint, and the sound counters are in the `player` reply, not in `state`.
- Two apps started on the same automation port: the second one runs without automation
  and commands go to the first. Use a port of your own for every test.
- Switch 2 saves and settings stay in the installed emulator; per-game launch arguments
  are not possible yet. The engine runs in its own window, not inside the app.
- The engine's executable name is written in `engine/src/host/locate.cpp` and its test,
  which the naming rule forbids; it goes when the engine becomes a plugin.
- Android: a picked folder is copied, not read in place, so it takes its space twice; once
  a folder has been copied the app stops offering all files access. A game ended by force-stop is
  missing from "Continue playing". On the test page LB and RB are outlined while other
  pressed buttons are filled. Secrets are kept in memory only; no Switch 2 engine.
- The hint bar shows keyboard names (Enter, Esc) on a handheld until a pad button is
  pressed, and on the home screen it lies over the heading of the next shelf.
- The picture drawn beside the pause menu runs past the right edge for 4:3 games.
- ROM hacks without an image beside them still get the cover of the game they are based on
  (Pokemon - Lazarus shows Emerald's).
- Gran Turismo 2 shows the game's border colour left and right of the picture.
- A button remap is per console, not per pad or port.
- Not built: online (accounts, achievements, netplay, cloud saves), PC and Steam games,
  manuals, capture, accessibility tree, languages other than English, the Vulkan backend,
  SQLite store.

## Where things are

- Packaged Windows app: `dist\windows` (ignored by git); a copy is in
  `C:\Users\david\Desktop\EmuDEN`. It keeps its data in `%APPDATA%\EmuDEN` unless a `user`
  folder is created beside `emuden.exe`. With no `engine.switch2.path` set it finds the
  installed emulator in the Desktop folder by itself.
- Linux AppImage: `dist\linux\EmuDEN-x86_64.AppImage`. On SteamOS it goes to
  `~/Games/EmuDEN/` (`tools/linux/install-steamos.sh`, `shells/linux/README.md`); a `user`
  folder beside it makes it portable.
- Android APKs: `dist\android\EmuDEN.apk` (release, debug key) and `EmuDEN-debug.apk`.
- `docs/interfaces.md`: what each module offers the others.
- `tools/drive.py`: drives a running app (`EMUDEN_AUTOMATION=<port>`; virtual pads and
  sound counters on the next port).

## Next, from the build plan

1. Play a game on the Legion Go S with its pad (the AppImage is installed there).
2. A real pad and listening to sound on every platform.
3. Android: read a picked folder in place, sticks in games, a 3DS game on two screens.
4. Stage 3, full player: hardware-rendered cores on GLES, libretro shader presets, pacing,
   disc swap, capture.
5. Stage 4: manuals, appearance, languages.
6. Stage 6: the Switch 2 engine contract above level 0 (embedded window, saves and
   settings owned by EmuDEN), then the engine as a plugin.
7. Stages 5, 7 and 8 after that: online, PC and Steam, hardening.
