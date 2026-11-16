# Engines

A console's games run in a libretro core inside EmuDEN (`den::player`), or in an **engine**:
an emulator app outside it (`den::engine`). Engines that speak the
[Emulator Control Protocol](https://github.com/Denarmo/emulator-control-protocol) (ECP) are
driven from EmuDEN's own pause menu, quick settings and companion panel, like a core.

"Core" in EmuDEN means anything that runs a console. A plugin manifest lists an engine app in a
console's `cores` beside its libretro cores; it is chosen per console or per game the same way
(`Host::CoresFor`, `SelectCore`, the game's core setting).

## Switch 2: an engine the player brings

Switch 2 is not shipped: no release (Windows zip, AppImage, APK) holds its plugin or an engine,
and no list offers it. The player installs the signed package
`manual/emuden.switch2.emuplugin` of `Denarmo/emuden-plugins` with **Settings > Plugins >
Install a console from a file** (`Host::ProbePackage`, `InstallPackage`); EmuDEN then asks for
the engine program if `engine::Locate` does not find it. The chosen path
(`engine.switch2.path`) is device-only and the first place `Locate` looks, for any console.
Its console, content rules and folder names (`SWITCH 2`, `nx2`, ...) come from that manifest;
the library has no built-in Switch 2 entry, only its `switch2` scanner.

A device that had Switch 2 while it was built in keeps it: on the first start of a version
without it, the host notes the official console plugins its cached list named and it has no
manifest of, or only a copy the list had brought (`Host::CarryOvers`,
`<data>/plugins/carry-over.json`). The app keeps those with a copy on the device and those the
library has records of (`Library::SystemsWithRecords`), bringing them up to date from the signed
`manual/` package once the official list no longer offers them, and lets the others go. A new
install has no cached list and never gets it.

## Declaring an engine core

A component of type `engine-app`:

```json
{
  "id": "example-ps2",
  "type": "engine-app",
  "name": "Example PS2",
  "version": "1.4.0",
  "ecp": {
    "id": "com.example.ps2emu",
    "level": 2,
    "capabilities": ["pause", "menu", "states", "state_thumbnails", "settings", "fast_forward"],
    "needs": ["bios"]
  },
  "downloads": {
    "windows-x86_64": {
      "url": "https://example.org/ExamplePS2-1.4.0-win64.zip",
      "sha256": "<64 hex>", "size": 12345678, "archive": "zip", "entry": "ExamplePS2/"
    },
    "linux-x86_64": {
      "url": "https://example.org/ExamplePS2-1.4.0.AppImage",
      "sha256": "<64 hex>", "size": 23456789, "archive": "none"
    }
  },
  "executable": {
    "windows-x86_64": "ExamplePS2/ps2emu.exe",
    "linux-x86_64": "ExamplePS2.AppImage"
  },
  "android": {
    "package": "com.example.ps2emu",
    "cert_sha256": "AB:CD:...:EF",
    "min_version_code": 140,
    "apk": {
      "url": "https://example.org/ExamplePS2-1.4.0.apk",
      "sha256": "<64 hex>", "size": 34567890, "archive": "none"
    },
    "store": "https://play.google.com/store/apps/details?id=com.example.ps2emu"
  }
}
```

and the system's `"cores": ["example-ps2", "pcsx2"]`, best first.

- `ecp` is the engine's descriptor (spec section 2): `level` 1 or 2, the `capabilities` and
  `needs` it has. It is shown before the engine runs; at run time the engine's hello decides.
- **Desktop:** `downloads` as for a libretro core, and `executable` the program inside them per
  platform. A zip `entry` ending in `/` is a folder unpacked whole. EmuDEN unpacks it into
  `<data>/engines/<id>/`, records the executable's SHA-256 in `engines/engines.json` and checks
  it again before each start.
- **Android:** `package` and `cert_sha256` (the SHA-256 of the signing certificate, with or
  without colons) are required; then `apk` (a pinned plain file) or `store` (`https://` or
  `market://`), or both. EmuDEN downloads the APK and hands it to the system installer through
  a `PackageInstaller` session; the user confirms. An installed package counts, and is ever
  started, only when it is signed with the pinned certificate, is at least `min_version_code`
  and has the PLAY activity. Updates come through the installer or the store.
- The plugin may `need` `core.engine-app.1`; versions of EmuDEN without engine apps skip the
  component and offer the console's other cores.

The plugin schema is `emuden-plugins/schema/plugin.schema.json` (the only copy); the guide for
outside developers, with the namespace rule for engine apps of an added source, is
`emuden-plugins/docs/engines.md`. In the developer folder a download may name a local `file`
in place of `url`.

## How a session runs

1. The player screen opens a `player::Session` for the game as for a core; for an engine core it
   is the engine session (`player/src/engine_session.cpp`), behind the app's routed session.
2. `engine::Host::Start` checks the install (executable hash, or installed package and
   certificate) and starts it:
   - **Desktop:** `<executable> --ecp stdio` as a child in a Windows job object with
     kill-on-close, or a Linux process group with the parent-death signal. Its standard error is
     `<data>/logs/engines/<id>.log`.
   - **Android:** a loopback listener on a random port and a 32-byte token; an explicit intent to
     the engine's `io.github.ecp.action.PLAY` activity with the game as a `content://` URI
     (`GameProvider`, read-only, granted to that app only), the port, token, session id, the
     `MENU_INTENT` (a `PendingIntent` for EmuDEN's activity) and the launch display. The game
     goes on EmuDEN's own display; on a device with two (Retroid Pocket Duo) the companion panel
     stays on the other one.
3. A thread of the link does the opening exchange: auth (TCP), hello (within 10 s), EmuDEN's
   hello with the capabilities it uses, `get_schema`, then `launch` with the menu chord
   (`start`, `select`) and the setting values: the manifest's hints, the user's values for the
   engine, then for the game. An engine with `configure` gets it first (below).
4. While it runs, EmuDEN's window is hidden (desktop). When the engine sends `menu_request` it is
   raised (Android: the engine fires `MENU_INTENT`), the pause menu opens over the paused frame,
   and Resume brings the engine back (Android: the PLAY activity again with
   `FLAG_ACTIVITY_REORDER_TO_FRONT`).
5. Closing the game saves the resume state in the engine's slot 0, sends `stop`, and ends the
   engine if it has not exited 20 s later. Play time counts from `first_frame` to the end.

## Setting the engine up (`configure`)

An engine that lists `configure` (ECP section 6.6) is set up with what the player gave EmuDEN, so
the app opened on its own skips its first-run setup (`app/screens/engine/engine_setup.cpp`):

- `bios`: the files and folders the console's BIOS sets name for the core, under the BIOS folder
  (PS2: `pcsx2/bios/` for PSX2 ECP), when they are there.
- `game_folders`: the folders the library found the system's games in (a folder inside another
  is left out).
- `settings`: the manifest's hints and the player's values for the engine (not per game).

It goes in the session, after `get_schema` and before `launch`, and on Android also outside one:
whenever the plugins, the installed apps, the library or the BIOS imports change (and every
minute for option values), each installed engine app with `configure` in its manifest whose setup
differs from the last one sent gets the `io.github.ecp.action.CONFIGURE` broadcast. What was
sent is kept in `<data>/engines/configured.json`, on this device only. On Android the locations
are `content://` URIs of `SharedDocuments`, a read-only documents provider with no roots: each
shared path is granted to the engine's package with a persistable read grant (a prefix grant for
folders), and only paths shared that way, and what is inside them, can be opened.

## What the menus show

| EmuDEN | ECP |
|---|---|
| Save / Load state, slots 1-10 | `save_state` / `load_state` on slots 1-10; thumbnails from the results kept in `<data>/states/engines/<core>/<game>/` |
| Resume where you left off | slot 0, written on close and loaded after the first frame |
| Fast-forward | `set_speed` with `player.fast_forward_speed` as the factor |
| Reset | `reset` |
| Engine settings (the core options page) | the schema; values kept as `key=value` lists in `engine_options.<core>` and `engine_game_options.<system>.<game>`, sent again with every launch |
| Widescreen | `video.widescreen_patches`, when the schema has it |
| Game volume | `set_volume`, else `audio.volume` |
| FPS overlay | `perf` |

A row whose capability the engine lacks is hidden. Picture presets, screen size, button
remapping, players and rewind holds are EmuDEN's own drawing and input, so they are not shown
for an engine.

## Trying an engine without the app

`den_engine_try <engine> <game> [engine arguments]` (`player/tools/engine_try.cpp`, built on
request) runs an engine through the player session and reports each step: hello, first frame,
the menu request, pause, a state saved and loaded, the settings, resume and stop. With the ECP
test engine: `den_engine_try test-engine.exe game.ecptest --menu-after 1`.
