# Status

What EmuDEN is today (2026-11-16). The check records of each round, with what was run on
which device, are in `docs/history.md`; add new ones there, newest first.

## What is there

- **Consoles as plugins.** Every console is an official plugin from `Denarmo/emuden-plugins`
  (signed index), with copies bundled in `assets/plugins` so consoles show before anything
  is downloaded. Only cores that need no BIOS are offered. A core is a pinned libretro core,
  an engine app spoken to over the Emulator Control Protocol (`docs/engines.md`), or the
  installed Switch 2 emulator (engine level 0, its own window). Other developers' sources can
  be added (Settings > Plugin sources), limited to their own namespace.
- **Player.** Software, OpenGL and Vulkan cores (`docs/vulkan.md`); save states, rewind,
  fast-forward, picture presets on a multi-pass chain, per-game and per-console settings,
  button remapping, black frame insertion at whole multiples of the game's rate, two screens (DS layouts, quick settings and the pause menu on the second
  display, an Android app beside the game).
- **Feature plugins** (Lua 5.4 in a sandbox, `docs/feature-plugins.md`): the in-app browser
  (`docs/in-app-browser.md`), four ROM hack site plugins, RetroAchievements, interface
  plugins for the home screen, tuning advice, and sync providers.
- **Sync between devices** (`docs/sync.md`): saves, states, settings and games through Google
  Drive, RomM or Syncthing plugins. PC and Steam consoles never sync.
- **Play together** (`net/`): two devices on one network, by core link (TGB Dual, gpSP,
  melonDS DS) or input-delay lockstep.
- **Library.** Scan by console folder, zip and 7z, downloads filed under clean names, ROM
  hacks patched from BPS, IPS, UPS, xdelta and other formats, covers (`docs/covers.md`), the library
  tidy-up.
- **Platforms.** Windows, Linux (AppImage, SteamOS in Game Mode: `docs/steamos.md`) and
  Android (Retroid Pocket Duo is the test device; `shells/android/README.md`).

## Known problems

- Switch 2 saves and settings stay in the installed emulator; per-game launch arguments are
  not possible and the engine runs in its own window. Its executable names are written in
  `engine/src/host/locate.cpp` and its test, which the naming rule forbids; they go when the
  engine becomes a plugin.
- Android: a picked folder is copied (`FolderImport.java`), not read in place, so it takes
  its space twice. No Switch 2 engine.
- ROM hack covers: the default fan collection (`tttrash/pokehackcovers`) has no licence and
  covers Pokemon hacks only; name matching has no typo tolerance and WebP pictures are
  skipped.
- A button remap is per console, not per pad or port.
- Two apps started on the same automation port: the second runs without automation and
  commands go to the first. Use a port of your own for every test.
- rar is not read; 7z with a password, PPMd or a solid block over 1 GiB is refused with a
  sentence. An archive with several patches gives its first.
- Linux without WebKitGTK 4.1 (SteamOS) has no page inside the app: the browser plugins use
  the fallback page and the Downloads folder.
- Not built: manuals, video capture, an accessibility tree, languages other than English
  (`assets/strings/en.json` only), a Vulkan backend for the interface, an SQLite store.

Last seen in the 2026-11-13 rounds and not checked since (see `docs/history.md`): Racing
Lagoon black for ~12 s and PoPoLoCrois for ~15 s before their titles; PPSSPP's "Exception
handler not implemented" and PCSX-ReARMed's `fopen ...\bios\PPSSPP` log lines; a long
console badge wider than its tile; Gran Turismo 2's border colour beside the picture; a game
closed with the automation `quit` not moving to the front of "Continue playing".

## Where things are

- `docs/interfaces.md`: what each module offers the others, the build targets, hidden
  settings. `docs/build-plan.md`: the plan the rebuild followed.
- Packaged Windows app: `dist\windows` (ignored by git); a copy is in
  `C:\Users\david\Desktop\EmuDEN`. It keeps its data in `%APPDATA%\EmuDEN` unless a `user`
  folder is created beside `emuden.exe`.
- Linux AppImage: `dist\linux\EmuDEN-x86_64.AppImage`. On SteamOS it goes to
  `~/Games/EmuDEN/` (`tools/linux/install-steamos.sh`, `shells/linux/README.md`); a `user`
  folder beside it makes it portable.
- Android APKs: `dist\android\EmuDEN.apk` (release, debug key) and `EmuDEN-debug.apk`.
- `tools/drive.py`: drives a running app (`EMUDEN_AUTOMATION=<port>`); virtual pads and
  sound counters are on the next port, the browser's test endpoint on the port + 3. Built
  only with `EMUDEN_DEV_TOOLS` (on by default).
- `tools/sync_bundled_plugins.py`: copies the official console manifests into
  `assets/plugins`; the test `den_bundled_plugins_in_sync` fails when they differ.

## Next

1. A real pad and listening to sound on every platform; a game played on the Legion Go S with
   its pad.
2. Android: read a picked folder in place, sticks in games, a 3DS game on two screens.
3. Phase 2 of the 2026-11-16 health check: console knowledge out of the C++ into manifests,
   `[[nodiscard]]` results, one path key and safe-filename helper, files over 600 lines
   split, CI for both repositories.
4. The Switch 2 engine above level 0 (embedded window, saves and settings owned by EmuDEN),
   then the engine as a plugin.
5. Stage 4 and 8 leftovers of the build plan: manuals, languages, accessibility, video
   capture.
