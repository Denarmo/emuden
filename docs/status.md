# Status

State of the rebuild after the second round of the first night (2026-11-12). The Windows
app starts every game in the test library, PSP text shows, and a Switch 2 game stops
cleanly. Android played three consoles on the Retroid. Linux builds and passes its tests in
WSL; the AppImage was still never run on the Legion Go S.

## Release check (Windows, last thing done)

`win` preset built from an empty build directory with everything committed; 11 of 11 unit
test programs pass. Then one driven run (`tools/drive.py`) with a fresh portable `user`
folder holding only the game folder and the engine path; cores were downloaded from the
official plugin source on first Play.

| Game | Result |
|---|---|
| Tetris (Game Boy, mGBA) | core downloaded, title screen, 515 frames, exit to details |
| PoPoLoCrois (PSP, PPSSPP) | core and its assets downloaded, title screen with its menu text at 24 s, 1459 frames, no `ppge_atlas` warning |
| Ocarina of Time (Switch 2) | engine window for 27 s, "Stop the game" ended it in under 4 s with exit code 0, no engine process left |

The app left with exit code 0.

## Games (Windows)

All 17 ROMs in the test library start, show a picture and advance frames; each ran about
10 s from the beginning, unpacked. "Picture" comes from screenshot statistics (colour
spread, two shots differing); only the PSP, Gran Turismo 2 and Racing Lagoon pictures were
looked at.

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

## Platforms

| Platform | State |
|---|---|
| Windows | Everything above. `tools/package.ps1` writes `dist\windows` (13.7 MB); the same files are in `C:\Users\david\Desktop\EmuDEN`. |
| Linux | `linux` preset builds in WSL Debian (clang) and 11 of 11 test programs pass there. `tools/linux/package.sh` writes the AppImage (5.7 MB, copy in `dist\linux`). Game Boy played under WSLg in the first round; nothing was played on Linux in the second. Never run on the Legion Go S: its sshd refused the connection, so the AppImage was not copied. |
| Android | Debug APK (`io.github.denarmo.emuden.next`, arm64) installed and ran on the Retroid Pocket Duo (Android 16, Adreno 722): setup found 18 games on 7 consoles; Tetris (GB), Pokemon - Lazarus (GBA) and Gran Turismo 2 (PlayStation) showed their picture with a sound stream started; cores downloaded from the plugin source; touch taps, a Start press on the built-in pad, Back opening the pause menu, and Home and back again all worked. See `shells/android/README.md`. |

Plugin source (Denarmo/emuden-plugins, index sequence 6): every core except `fake08` has
`android-arm64` and `linux-x86_64` downloads (26 Android cores; N64 on Android is the
`mupen64plus_next_gles3` build), and `emuden.psp` carries PPSSPP's assets as a
`system-files` component that the app unpacks into the system folder with the core.

## What the second round added

- **Engine**: a clean stop. The app asks the engine window to close, answers its "are you
  sure" dialog, and waits up to 20 s before forcing; on POSIX `SIGTERM` to the process
  group, then `SIGKILL` after 20 s. Closing the app while a game runs uses the same stop.
- **Text entry**: `Stage::AskText` (pad, touch and a real keyboard). Collections can be
  created and renamed in a "Collections" settings section.
- **Controllers page**: pads by name with a Player 1..N choice (`input.players`), a button
  test view (`controller_test`) and a sheet of every symbol (`controller_glyphs`).
- **Glyphs**: button symbols per pad family drawn by the ui kit (`ui/src/glyphs.cpp`).
- **Plugins**: system files of a core; component types the app does not know are skipped.
- **Player**: the frame count is logged when a game is paused or left.
- **Android**: Back is trapped, automation over `adb forward` in debug builds,
  `tools/android/run.ps1`.

## Not run

- A real gamepad held in the hand, two pads, and listening to sound on any platform.
- Rewind anywhere. Load state, fast-forward and Reset on NDS, PSP and GBA, and on the
  unpacked games in this round.
- The Switch 2 engine in full screen (the app passes `-f`; only read in the source), the
  clean stop on Linux, and the engine copy on the Desktop.
- The text entry, Controllers page and glyph sheet with a real pad or touch; they were
  driven through the automation endpoint once each.
- Linux on real hardware (GPU drivers, gamescope, the handheld), and any game on Linux
  with tonight's build.
- Android: the storage permission screen (access was granted with `appops`),
  `tools/android/run.ps1` itself, save and load state, rewind, the second screen, sticks
  and the other buttons, NDS, PSP and GBC games, the Android cores other than mGBA and
  PCSX-ReARMed, and the PSP assets download.
- The Linux core downloads added to the plugin source, and the Desktop copy from the first
  round reading the new PSP manifest (its parser was read, not run).
- "Install all" in setup, hidden games, the statistics page with real play time, the high
  contrast themes, per-game core and screen size end to end.

## Known problems

- `fake08` (PICO-8) has no download on any platform: the buildbot has no build of it.
- Racing Lagoon stays black for its first ~12 s and PoPoLoCrois for ~15 s before the title.
- PPSSPP logs "Exception handler not implemented on this platform" at start; the game runs.
- Two apps started on the same automation port: the second one runs without automation
  and commands go to the first. Use a port of your own for every test.
- Switch 2 saves and settings stay in the installed emulator; per-game launch arguments
  are not possible yet. The engine runs in its own window, not inside the app.
- The engine's executable name is written in `engine/src/host/locate.cpp` and its test,
  which the naming rule forbids; it goes when the engine becomes a plugin.
- Android: no folder grants (SAF), BIOS files cannot be put into the app's private folder
  with a file manager, secrets are kept in memory only, no Switch 2 engine.
- Covers are cropped to fill their tiles. ROM hacks get the cover of the game they are
  based on or none that fits (Pokemon - Lazarus shows Emerald's).
- Gran Turismo 2 shows the game's border colour left and right of the picture.
- A button remap is per console, not per pad or port.
- The library keeps no session log or date added, so there is no "last 7 days".
- Play time of a first game includes a second one opened over it.
- An AppImage has no portable mode.
- Not built: online (accounts, achievements, netplay, cloud saves), PC and Steam games,
  manuals, capture, accessibility tree, languages other than English, the Vulkan backend,
  SQLite store.

## Where things are

- Packaged Windows app: `dist\windows` (ignored by git); a copy is in
  `C:\Users\david\Desktop\EmuDEN`. It keeps its data in `%APPDATA%\EmuDEN` unless a `user`
  folder is created beside `emuden.exe`. With no `engine.switch2.path` set it finds the
  installed emulator in the Desktop folder by itself.
- Linux AppImage: `dist\linux\EmuDEN-x86_64.AppImage`. On SteamOS it goes to
  `~/Games/EmuDEN/` (`shells/linux/README.md`).
- Android APK: `build\android\gradle\app\outputs\apk\debug\app-debug.apk`.
- `docs/interfaces.md`: what each module offers the others.
- `tools/drive.py`: drives a running app (`EMUDEN_AUTOMATION=<port>`).

## Next, from the build plan

1. Run the AppImage on the Legion Go S with a real pad, and finish the Android pass (PSP
   with its assets, NDS, states, the storage permission screen, a folder grant).
2. A real pad and listening to sound on every platform; rewind.
3. Stage 3, full player: hardware-rendered cores on GLES, libretro shader presets, pacing,
   disc swap, second screen on Android, capture.
4. Stage 4: library data (session log, date added, manuals), appearance, languages.
5. Stage 6: the Switch 2 engine contract above level 0 (embedded window, saves and
   settings owned by EmuDEN), then the engine as a plugin.
6. Stages 5, 7 and 8 after that: online, PC and Steam, hardening.
