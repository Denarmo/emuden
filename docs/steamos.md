# SteamOS (Legion Go S)

What the Linux AppImage does on SteamOS, checked feature by feature against Windows and the
Retroid (Android) on 2026-11-15. Device: Legion Go S, SteamOS 3.9 (glibc 2.43, Mesa 26.1,
radeonsi, OpenGL 4.6), panel 1920x1200 at 120 Hz, Game Mode. Everything was run in Game Mode,
started from the EmuDEN Steam shortcut, and driven through the automation endpoint over ssh;
nobody pressed the device's buttons.

## Installing

`tools/linux/build.sh` and `tools/linux/package.sh` in WSL Debian, then on the device
`tools/linux/install-steamos.sh --steam <AppImage>`: `~/Games/EmuDEN/EmuDEN-x86_64.AppImage`, a menu
and desktop entry, one Steam shortcut "EmuDEN" with its cover, banner, logo and icon (Steam shows
the artwork after it restarts). No root is needed for any of it.

Testing in Game Mode: a process started over ssh is not shown (gamescope shows only what Steam
started). Start the shortcut through Steam instead, with `EMUDEN_AUTOMATION=<port> %command%` as
its launch options for the test, and clear them afterwards.

## Checklist

| Feature | Linux | What was run |
|---|---|---|
| Start in Game Mode, full screen, handheld layout | Works | Steam shortcut; 1920x1200, 120 fps in menus, "Legion Go S, Handheld, known device" |
| Controller | Works | The pad shows as "Lenovo Legion Go S Controller" (Xbox layout, Xbox glyphs); no second copy from Steam Input. Game buttons and sticks through the virtual pad of the test endpoint |
| Exit from Steam (Exit game) | Works | The app closes on Steam's terminate; the running game's save and resume state are written |
| Library scan with plugin consoles | Fixed | Console folders under `roms/` (`~/Games/roms/gb`, EmuDeck's `Emulation/roms/<console>`) were not found |
| Covers, LaunchBox first | Works, fixed one | LaunchBox boxes for every test game, the Lazarus hack included; `0012 - Asphalt - Urban GT (U)(Wario).nds` got none until scene release numbers were dropped from names |
| Core install from the signed index | Works | mGBA, melonDS, PCSX ReARMed, Play! from the Consoles page |
| Game Boy, GBC, GBA (mGBA) | Works | Tetris, Wario Land 3, Mario Kart Super Circuit: 59.9 fps |
| DS (melonDS) | Fixed | glibc 2.41+ refused the core (it asks for an executable stack); `AppRun` sets `glibc.rtld.execstack=2`. Asphalt Urban GT 59.8 fps. Of the 38 Linux cores in the index only melonDS has the flag, and every library the cores link is on SteamOS |
| PlayStation (PCSX ReARMed) | Works | Gran Turismo 2, 49.9 fps (PAL) |
| PlayStation 2 (Play!, no BIOS) | Works | Colin McRae Rally 3 to the title screen, 49.9 fps (PAL), one audio underrun at boot. The title's background draws in patches of different brightness; not compared with Windows |
| Save states, load | Works | Slot 1 with its picture, loaded back |
| Rewind | Works | Right stick held left steps back through the states |
| Shaders | Works | Dot matrix by Auto on Game Boy, CRT chosen in the pause menu |
| Sound | Works | Speaker sink; 3 s recorded: level 0.72 peak, no gaps; no underruns while playing |
| Home interfaces | Works | Shelf (3D boxes and cartridges, LaunchBox faces) and Showcase installed from the index and used as Home |
| ROM hack plugins | Works | romhacking.net installed; its page is the fallback page (SteamOS has no WebKitGTK). "Open in browser" opens the site in Steam's own browser over the app. A BPS put in `~/Downloads` was offered, matched to Tetris by CRC and installed as a hack in the library |
| Feature plugin install | Fixed | A new install read a pointer into a destroyed catalogue (undefined on every platform); on Linux the unit tests' installs answered "The plugin list does not offer this plugin" |
| Sync (Settings > Sync) | Works, fixed one | Syncthing plugin: "Use Sync/EmuDEN" pointed at `~/Downloads/Sync/EmuDEN` (the Downloads folder had a trailing separator); now `~/Sync/EmuDEN`. "Sync now" wrote saves, states and settings there as plain files. File times came back a second early on Linux (rounding) |
| Settings | Works | Device page: Legion Go S, tier Medium, 1920x1200 8.0 in 120 Hz, Ryzen Z2 Go |
| In-app browser | Fallback by design | No WebKitGTK on SteamOS: the fallback page and the Downloads folder, as above |
| Linux build | Fixed | `http_curl.cpp` had raw line breaks inside character literals and did not compile |
| Second screen | Not applicable | One screen |
| Steam integration | Works as designed | EmuDEN is a Steam shortcut with its artwork, takes Steam Input's pad, and Steam's browser opens its links. No Steam shelf inside EmuDEN (Android has one through DroidDeck): in Game Mode Steam itself is the launcher, and starting a Steam game from inside a running Steam app was not tried (no Steam game was started) |

## Not run

- The device's own buttons, a real finger, sleep and wake during a game.
- A download in Steam's browser (romhacking.net shows a Cloudflare check); that its downloads
  land in `~/Downloads` is not confirmed.
- Syncthing itself (not installed on the device), Google Drive and RomM.
- Switch 2 games (the engine is not set up on the device).
