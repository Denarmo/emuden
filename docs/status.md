# Status

State of the rebuild after the fifth round (2026-11-13): the small problems left by the
fourth round, the dot matrix and LCD pictures on high resolution screens, and covers for ROM
hacks. The fourth round took the owner's six notes (table below). Everything was driven
through automation, adb and screenshots: nobody held a device, dragged a real finger or
listened.

## Fifth round

| Item | State | What was run |
|---|---|---|
| Hint bar shows the pad from the start | Fixed: a handheld starts with its pad's glyphs, a desktop with the keyboard until a pad is used | Retroid, release APK: A / X / Y in the hint bar of home and the library at the first frame. Windows: keyboard names, as it has no pad |
| Hint bar over a shelf heading | Fixed: a shelf scrolls into view with its heading, and a heading the list edge would cut is not drawn | Windows at 125% in a 1280x720 window: home and two steps down, no heading under the bar |
| Pause menu picture past the right edge | Fixed: on the Picture pages the game is fitted beside the menu; the main pause menu lies over the dimmed game by design | Windows at 125%: Gran Turismo 2 (4:3) inside the window in the pause menu, on the Picture page and while running |
| Row labels, held LB | "Auto: CRT" style labels, a value takes the width it needs; LB is filled when held | Windows: the labels seen in the pause menu; the test page was seen by the fix's author only |
| Dot matrix, Pocket, LCD on high resolution screens | Rewritten: each output pixel takes its share of each dot in linear light, gaps never thinner than a pixel, whole multiples chosen automatically when they cost under 10% of the height | Tetris in the app in a real 3840x2160 window (15x, 60 fps) and at 1280x720 (5x): even grid, same brightness, crops looked at. Retroid (GLES 3, 1920x1080): dot matrix and LCD grid at 7x, crops looked at |
| Covers for ROM hacks | Own art from a fan collection on GitHub and libretro's hack entries, optional RetroAchievements and SteamGridDB keys, else the original's box with a HACK chip drawn on its top right corner; "Find cover" on the details page. `docs/covers.md` says which sites are not used and why (Hackdex, GBAtemp, Whack a Hack and others forbid or block automated reading) | Windows and Retroid libraries: Brown, Lazarus, Heart & Soul, Seaglass and Polished Crystal show their own covers; Ambrosia and Crystal Awakening show Crystal's box tagged HACK |

`emuden --windowed --size 3840x2160` opens a window of that size (new, for such checks).

## Second screen: quick settings and the pause menu (after the fifth round)

Checked on the Retroid Pocket Duo with the release APK at `75fa362`, through `adb` only
(`input` taps, swipes and Back, `screencap -d` of both displays, every picture looked at),
after the same on Windows with `EMUDEN_SECOND_WINDOW=1`.

| Item | State | What was run |
|---|---|---|
| Quick settings: Swap screens, Screen size, Core options, Buttons | On the bottom screen beside the old rows; a rebuilt page keeps every scrolling row where it was (`ui::Page`, by node id) | Retroid, Tetris: the four are there; the Picture row was swiped along and "Arcade monitor" tapped, the row stayed and the picture changed; Core options and Buttons opened on the bottom screen with the game undimmed on top, a fling scrolled Core options (Tetris) and Buttons (Explorers of Skies) |
| Swap screens | The game goes to the second display, the quick settings to the main one; kept in `player.swap_displays` | Retroid: Tetris on the bottom screen, quick settings on top, taps and a fling worked there, Back opened the menu on the top screen; swapped back. Windows: still swapped after a restart |
| Pause menu on the second display | With two displays the menu opens on the one with the quick settings, the game stays undimmed; `player.overlay_on_second_screen` in Settings > Player turns it off | Retroid: Back in Tetris; Explorers of Skies with "Top on main" and "Top on second": the menu on the bottom screen, the other screen undimmed, the game's screen back after closing, a fling scrolled the menu, "Exit to home" tapped there |

Two fixes came out of the check: a two-screen game set to "Top on second" still got the menu
over the dimmed main display (it now always takes the second display), and the selected
"Fit" button was cut to "..." in the quick settings on the main display at 125%.

Not run: a real pad or finger, guide and Start+Select with two displays, touch on the game
while it is on the second display, a 3DS game, "One display" for a two-screen game, the
"Pause menu on the second screen" switch on Android, any of it on Linux. Known gaps: a
question ("Reset the game?") is drawn on the main display while the menu is on the second
one; while swapped the game on the second display is drawn one frame behind and, on the
1280x960 screen, "Fit" gives Game Boy games 6.67x, so the dot matrix grid is not a whole
multiple there; nothing tests the kept scroll position.

When the check began somebody was playing Explorers of Skies on the device. The checker
waited for three minutes without input and pressed Back to leave through the menu; the app
was swiped away from the recents list at that moment, so that session ended without its
resume state (the game continued from an earlier one afterwards). A debug build from
another branch was installed in between and replaced by this release build again. The
device is left with the release APK, force-stopped, "Top on second" and Tetris on dot matrix
as they were.

## Feature plugins and ROM Hacks (branch `feat/hackdex`, 2026-11-13)

Design and contract: `docs/feature-plugins.md`. Not merged into main.

- **Plugins section** on the rail lists installed and available feature plugins; a feature
  plugin is a Lua 5.4 package from the signed index, run in a sandbox.
- **ROM Hacks** (`emuden.romhacks` 1.0.0) is published in `Denarmo/emuden-plugins` as index 7.
  Its source is there (`plugins/emuden.romhacks`), not in this repository; the copy under
  `plugins/tests/fixtures` is an older sample the tests run.
- **What Hackdex allows:** nothing automated. Its terms (1.1.0, section 8) forbid apps and
  scripts from reading the site or downloading from it without written permission, and the
  site answers a plain client with a bot checkpoint. So the plugin makes no request at all:
  no browse, search, filters, covers or descriptions from Hackdex. The user downloads the
  patch in their browser; the plugin finds it in the Downloads folder (or a chosen file),
  reads the base checksum, finds that game in the library, patches a copy and registers the
  hack. Name, version, description, cover picture and the hack's Hackdex address are typed or
  chosen by the user. Updates are a newer patch file, matched by file name. An in-app
  catalogue (the app itself fetching Hackdex) needs Hackdex's written permission first
  (admin@hackdex.app). (This was version 1.0.0; 2.0 below lets the user browse the site in
  the app's browser page instead.)
- **Run on Windows** (automation port 27413, fresh portable `user` folder, scratch library
  with copies of two ROMs): the plugin was installed from the live source with one press,
  found a patch in the Downloads folder (`plugins.downloads_folder` pointed at a scratch
  folder), showed "You have the base game", installed it, the hack appeared in the library
  with the base game's cover and ran for about 20 s (1244 frames, mGBA). No clean base ROM of
  Hackdex's 39 is in `D:\ROMS`, so the patch was a generated BPS on a copy of Tetris, not a
  real hack. Also looked at: Installed, a hack's page, Base games, a base's page, About.
- **Not run:** xdelta, UPS and IPS through the plugin (unit tests only); the cover picker,
  rename, remove, "Update from a file" and "Look for newer patches"; the missing-base page;
  a patch opened with the app; Linux and Android; the real Downloads folder
  (`pal::DownloadsDir`, SDL's user folder; empty on Android, where the file picker is used).
- **In-app browser for plugins:** `den.web.open` (permission `browse`) shows a declared site
  in the browser page and hands the plugin each patch file the user downloads there; ROM
  images, files that are no patch and archives holding a game are refused by the host.
- **Four ROM Hacks plugins, one per site** (index 9 of `Denarmo/emuden-plugins`, one shared
  Lua library; `docs/feature-plugins.md` section 8): hackdex.app (`emuden.romhacks` 2.0.1,
  replacing the Downloads-folder version), whackahack.com, pokeharbor.com and
  romhacking.net (1.0.1 each). Browse tab ("Browse <site>", "Choose a patch file"),
  Installed (play, update from the site or a file, rename, remove), About.
  - New in the app for them: a link that leaves the page's hosts asks "This link goes to
    <host>. Open it?" (allowed for that host until the page is closed); the refusal reads
    "EmuDEN only installs patch files; this is a full game file"; 7z and rar get their own
    sentence; `den.ui.depth()`; a developer-folder plugin may show `http://127.0.0.1`; a
    hack whose own cover lookup finds nothing keeps the box of its base game (before, it
    fell to a drawn placeholder after the next scan).
  - **Run on Windows** (automation port 58999, fresh portable `user` folder, scratch library
    of copies of Tetris, Pokemon Brown and Wario Land 3): the four plugins were installed
    from the live index with one press each (their page lists the hosts) and later updated
    to 1.0.1/2.0.1 from it. Each plugin's browser page was opened once and the window
    captured: all four start pages rendered in WebView2 (hackdex.app past its checkpoint,
    whackahack.com's forum, pokeharbor.com, romhacking.net with news dated 2026-11-12).
    Nothing was clicked or downloaded on those sites.
  - The whole path was run on a test site on `127.0.0.1` with a developer-folder copy of the
    same Lua: a BPS (base found by checksum, "You have the base game"), a zip holding an IPS
    and an xdelta with LZMA sections (the game chosen from a list; chosen automatically the
    second time) each landed as a hack beside its base game's folder (`Hacks`); a `.gba`, a
    zip holding a game and a `.7z` were refused with their messages; a BPS for a game not
    in the library gave "Base game missing" with its checksum; a link to another host
    asked first and, after "Open", a patch downloaded there installed; "Update from the
    site" replaced a hack with a newer patch; Remove; the Tetris hack ran in mGBA (1517
    frames, 59.7 fps). The fallback page (`EMUDEN_WEB_FALLBACK=1`) opened through the
    plugin and showed the Downloads folder it watches.
  - **Not run:** a download and install from any of the four real sites (**for the owner to
    try by hand**: on hackdex.app "Download Patch" on a hack's Versions page; on
    whackahack.com a thread's attachment, with the owner's own sign-in if the forum asks,
    and a link to a file host through the leaving question; on pokeharbor.com that a
    pre-patched download is refused; on romhacking.net a zip download, and one from
    archive.org); Rename (the automation cannot type); "Update from a file"; a file handed
    over by the fallback page; UPS, APS and PPF through the plugin; pad and touch; Linux
    (SteamOS has no engine: fallback) and Android.
  - **Limits:** 7z and rar are not read (most of romhacking.net's files: unpack by hand and
    use "Choose a patch file"); a zip with several patches gives its first; pokeharbor.com
    offers pre-patched games, so its plugin is mostly a browser that refuses them; MEGA
    downloads are made by the page itself (a blob), which Android hands over without the
    file's name; `library/src/library.cpp` was already over 600 lines after the merge and
    gained three.
- **xdelta:** patches packed with LZMA, DJW or FGK apply (unit tests with patches xdelta3
  3.1.0 wrote; no real hack was tried).
- **Fixed:** `den.open_url` opens the user's browser (`pal::OpenUrl`) and only after a
  press; the selected tab chip keeps its mark; the consent page of a developer-folder plugin
  says "Allow"; the web inbox drops files older than a day at start; the WebView2 and XZ
  Embedded licences are packaged.
  Builds older than this branch download the plugin's package on every list refresh, refuse
  it ("not a console plugin") and keep the rest of the list.

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

`win` preset from an empty build directory at `e82045d`: 11 of 11 test programs pass and the
three parallel commits built together without a change. Two small commits came out of the
check (`--size`, and the HACK tag moved out from under the tiles' badges); after them the
Windows and Linux tests ran again (11 of 11 on both) and every package below was rebuilt.

All three were rebuilt at `75fa362` after the second-screen check (11 of 11 tests on Windows
and Linux again) and put in the same places; the Legion Go S start was repeated (10 s, log
read), the Windows build of that commit was not started.

- **Windows** (automation port 28467, portable `user` folder): the fifth-round table above.
  The app left no process behind. `dist\windows` (14.5 MB) is mirrored to
  `C:\Users\david\Desktop\EmuDEN`; the Desktop copy was not started.
- **Android** (Retroid Pocket Duo, app data kept): the release APK (debug key) of `75fa362`
  is installed and force-stopped after the second-screen check above; it is
  `dist\android\EmuDEN.apk`. `EmuDEN-debug.apk` there is still the fifth round's.
- **Linux**: built in WSL Debian, 11 of 11 tests, AppImage 5.9 MB in `dist\linux`. Installed
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
- Fifth round: a touch-only device (no hint bar there: unit-tested only); Screen size
  "Pixel perfect" in a room smaller than the frame; the controller test page; the Settings >
  Covers rows, "Choose an image file" and "Choose automatically"; RetroAchievements and
  SteamGridDB with a real key (SteamGridDB's endpoints were written from memory); the CRT,
  composite, S-Video and VHS presets after their rewrite beyond one look at CRT on Gran
  Turismo 2; every preset on Linux; a two-screen game in the pause menu.
- Fourth round: a real finger on any scrolling page; the shaders other than dot matrix on
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
- The high contrast themes, per-game core and screen
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
  missing from "Continue playing". Secrets are kept in memory only; no Switch 2 engine.
- ROM hack covers: a game counts as a hack when libretro has no box under its name (no
  checksum database); the default fan collection (`tttrash/pokehackcovers`) has no licence
  and covers Pokemon hacks only; name matching has no typo tolerance and WebP pictures are
  skipped; the new texts are not in `assets/strings/en.json`. The HACK tag hides the bottom
  left corner of the box.
- In the library grid a long console badge ("Game Boy Advance") is wider than its tile.
- A game closed with the automation `quit` while it runs does not move to the front of
  "Continue playing".
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
