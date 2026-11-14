# Engines

A console's games run in a libretro core inside EmuDEN (`den::player`), or in an **engine**:
an emulator app outside it (`den::engine`). Engines that speak the
[Emulator Control Protocol](https://github.com/Denarmo/emulator-control-protocol) (ECP) are
driven from EmuDEN's own pause menu, quick settings and companion panel, like a core.

"Core" in EmuDEN means anything that runs a console. A plugin manifest lists an engine app in a
console's `cores` beside its libretro cores; it is chosen per console or per game the same way
(`Host::CoresFor`, `SelectCore`, the game's core setting).

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

The plugin schema (`emuden-plugins/schema/plugin.schema.json`) needs `engine-app` in the
component `type` enum and the `ecp`, `executable` and `android` objects above.

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
   engine, then for the game.
4. While it runs, EmuDEN's window is hidden (desktop). When the engine sends `menu_request` it is
   raised (Android: the engine fires `MENU_INTENT`), the pause menu opens over the paused frame,
   and Resume brings the engine back (Android: the PLAY activity again with
   `FLAG_ACTIVITY_REORDER_TO_FRONT`).
5. Closing the game saves the resume state in the engine's slot 0, sends `stop`, and ends the
   engine if it has not exited 20 s later. Play time counts from `first_frame` to the end.

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
