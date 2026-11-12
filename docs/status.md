# Status

State of the rebuild after the first night (2026-11-12). The Windows app plays a game on
every console in the test library. Linux builds and runs in WSL only; Android builds but
was never started.

## Consoles (Windows, final play check)

One game each, started from the home screen with a fresh portable `user` folder and the
game folder added in first-run setup. Cores were downloaded from the official plugin source
on first Play. Driven through the automation endpoint (`tools/drive.py`); no real gamepad,
no touch, and sound was not listened to.

| Console | Game | Core or engine | Runs | Pause | Save state | Exit to home |
|---|---|---|---|---|---|---|
| Game Boy | Tetris | mGBA | yes | yes | yes | yes |
| Game Boy Color | Mario Tennis | mGBA | yes | yes | yes | yes |
| Game Boy Advance | Pokemon - Lazarus | mGBA | yes | yes | yes | yes |
| Nintendo DS | Explorers of Sky | melonDS (FreeBIOS) | yes | yes | yes | yes |
| PlayStation | Gran Turismo 2 | PCSX-ReARMed (no BIOS) | yes | yes | yes | yes |
| PSP | PoPoLoCrois | PPSSPP | yes | yes | yes | yes |
| Switch 2 | Ocarina of Time | installed emulator (engine level 0) | starts | - | - | "Stop the game" returns to home |

- Reopening a game resumes where it was left (seen on DS, PlayStation and PSP).
- The Switch 2 game ran 32 s in the engine's own window; after "Stop the game" no engine
  process was left. The engine is closed by force after 5 s (exit code 1).

## Platforms

| Platform | State |
|---|---|
| Windows | `win` preset builds from an empty build directory in about a minute; 11 of 11 unit test programs pass. `tools/package.ps1` writes `dist\windows` (13.6 MB). All consoles above play. |
| Linux | `linux` preset builds in WSL Debian (clang); tests pass there. An AppImage is written by `tools/linux/package.sh`. Game Boy played under WSLg. Never run on the Legion Go S (it was not reachable). |
| Android | `shells/android` builds a debug APK (arm64). Never installed or started. No core has an `android-arm64` download in the plugin source yet, so no core console can play there. |

## Not run

- A real gamepad, two pads, touch, and listening to sound.
- Load state in this pass (it was confirmed earlier on Game Boy and PlayStation), rewind,
  fast-forward, Reset, screen sizes other than Fit, core options and button remap during
  this pass.
- Zipped ROMs (the two PlayStation zips are hidden behind their unpacked copies), Racing
  Lagoon, and every game other than the one per console listed above.
- The Switch 2 engine in full screen, and the engine copy on the Desktop (only its
  detection was checked, it was not started).
- "Install all" in setup, hidden games, the statistics page with real play time, the high
  contrast themes, per-game core and screen size end to end.
- Linux on real hardware (GPU drivers, gamescope, the handheld), and anything on Android.

## Known problems

- PPSSPP's own assets (`ppge_atlas.zim`, fonts) are not installed, because the published
  manifest has no download for them. PSP games that use the system font or the built-in
  dialogs will miss text.
- Stopping a Switch 2 game ends the engine by force, so what it saves on exit may be lost.
- Switch 2 saves and settings stay in the installed emulator; per-game launch arguments
  are not possible yet.
- Button hints are lettered badges, not the pad families' real glyph art; only the
  keyboard look was seen on screen.
- Covers are cropped to fill their tiles. ROM hacks get the cover of the game they are
  based on or none that fits (Pokemon - Lazarus shows Emerald's).
- Gran Turismo 2 shows the game's border colour left and right of the picture.
- Collections are named "Collection N": there is no text entry to rename them.
- A button remap is per console, not per pad or port.
- The library keeps no session log or date added, so there is no "last 7 days".
- Play time of a first game includes a second one opened over it.
- An AppImage has no portable mode. Android keeps secrets in memory only.
- Not built at all tonight: online (accounts, achievements, netplay, cloud saves), PC and
  Steam games, manuals, capture, accessibility tree, languages other than English, the
  Vulkan backend, SQLite store.

## Where things are

- Packaged Windows app: `dist\windows` (ignored by git); a copy is in
  `C:\Users\david\Desktop\EmuDEN`. It keeps its data in `%APPDATA%\EmuDEN` unless a `user`
  folder is created beside `emuden.exe`. With no `engine.switch2.path` set it finds the
  installed emulator in the Desktop folder by itself.
- `docs/interfaces.md`: what each module offers the others.
- `tools/drive.py`: drives a running app (`EMUDEN_AUTOMATION=<port>`).

## Next, from the build plan

1. Plugin source: add `android-arm64` (and check `linux-x86_64`) core downloads and the
   PPSSPP assets to the manifests, then re-sign the index. This unblocks Android and PSP text.
2. Run on the devices: the AppImage on the Legion Go S, the APK on the Retroid, each with a
   real pad and touch. Fix what the first run shows (surface loss, permissions, glyphs).
3. Stage 2 leftovers: controller settings page, real glyph art per pad family, text entry.
4. Stage 3, full player: hardware-rendered cores on GLES, libretro shader presets, pacing,
   disc swap and zipped content, second screen on Android, capture.
5. Stage 4: library data (session log, date added, manuals), appearance, languages.
6. Stage 6: the Switch 2 engine contract above level 0 (clean stop, embedded window,
   saves and settings owned by EmuDEN), then the engine as a plugin.
7. Stages 5, 7 and 8 after that: online, PC and Steam, hardening.
