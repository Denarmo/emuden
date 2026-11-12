# Engine contract

How EmuDEN runs a game in an engine: a program outside the app that emulates one console
(tonight: Switch 2). The app owns the library, settings, play time, limits and menus; the
engine owns emulation and its own window. This file and `schema/` are MIT so any engine may
use them.

The contract has levels. An engine states the highest level it speaks; the app drives it at
that level or lower. The app side is `den::engine::Host` (`engine/`); a session is one
`Link` (`engine/src/protocol/link.h`), one class per level, so screens never see the level.

| Level | Meaning | State |
|---|---|---|
| 0 | Start the program for a game, wait for it to exit | Implemented |
| 1 | Level 0 plus one-shot commands and a session channel with lifecycle events | Specified |
| 2 | Level 1 plus pause, settings, menu hand-over, applets, add-ons, performance | Specified |

## Level 0 (as implemented)

Any program that takes a game on its command line and ends when the user is done.

**Finding the engine** for a system id (`switch2`), first match wins:

1. The setting `engine.<system>.path` in `settings.json`: an executable, or a folder that
   holds one. When it is set nothing else is tried, and a wrong path is reported as such.
2. A plugin-installed engine: `<data>/engines/<system>/`, then `<data>/emulators/<system>/`.
3. A copy of the previous emulator in its folder on the user's desktop.

In a folder the executable is found by the previous emulator's file name (the table in
`engine/src/host/locate.cpp`). Child processes must be
possible on the device (`pal::Caps::child_processes`); on Android the host reports that the
console is not available yet.

**Start.** `<executable> -f -g <game file>`; `-f` (full screen) is left out when the launch
asks for a window. The setting `engine.<system>.args` (a list) replaces the arguments;
`{content}` in it stands for the game file. The working directory is the executable's
folder and the environment is the app's. One session at a time.

**Process.**

- Windows: created suspended, put in a job object with kill-on-close, then resumed. When
  the app ends, however it ends, the engine and everything it started end with it.
- Linux: `fork`/`exec` as a child of the app in its own process group, with the
  parent-death signal set to `SIGKILL`.
- Android: not available.

**While it runs.** The app keeps its window for 1.5 s (so an engine that fails at once never
makes it blink), then hides it; it shows it again when the engine exits. The app ignores
pad input meanwhile. The engine draws everything, its own menus included.

**Stop.** The app asks politely: `WM_CLOSE` to the visible, unowned top-level windows of the
job's processes on Windows, `SIGTERM` to the process group on Linux. An engine that answers
with a "really close?" dialog gets Enter in that dialog (Windows; tried every 0.5 s for 6 s,
and only in the dialogs that came up first). If the engine has not exited 20 s after the
request it is ended (job terminated, `SIGKILL`), which may lose what it saves on exit. When
it could not be asked at all (no window yet), it is ended after 5 s. The same applies when
the app itself closes (1.5 s when the engine could not be asked).

**End.** The session is over when the process exits. The exit code is passed on as is; it
is not interpreted. Play time is the wall time from start to exit, booked by the app when
it is at least one second.

**Events the app's screens see:** `Started`, `Exited` (exit code, seconds), `Failed`
(user-facing message: no engine, wrong path, missing game file, start error, a game already
running).

**Limits of level 0.** The engine keeps its own settings, saves and firmware in its own data
folder. When the game is stopped inside the engine, the engine may stay open on its own
screen; the session lasts until its window is closed.

## Transport for levels 1 and 2

The engine is started by the app as at level 0 (same job object and process group), with
`--emuden-protocol` and nothing else on the command line.

- **Channel:** the engine's standard input (app to engine) and standard output (engine to
  app). Standard error is the engine's log; the app copies it to its own log. Android: the
  same messages over a bound service of the engine's APK (package name and certificate
  digest pinned in the signed plugin index).
- **Framing:** a 4-byte little-endian length, then that many bytes of UTF-8 JSON, one object
  per frame. A frame over 16 MiB is a protocol error.
- **Message:** `{"type": "<name>", "id": <number, optional>, ...}`. A request carries `id`;
  its answer is `{"type": "result", "id": <same>, "ok": true|false, "error": {...}}` with
  the request's result fields beside it. Messages without `id` are events. Unknown types and
  unknown fields are ignored, never an error.
- **Typed error:** `{"code": "<snake_case>", "message": "<for the user>", "detail": "<for
  the log>"}`. Codes: `content_missing`, `content_invalid`, `firmware_missing`,
  `keys_missing`, `gpu_unsupported`, `out_of_memory`, `crashed`, `protocol`, `internal`.
- **Hello:** the engine speaks first:
  `{"type": "hello", "protocol": 1, "level": 2, "engine": "<id>", "version": "<semver>",
  "systems": ["switch2"], "capabilities": ["pause", "screenshot", "set_setting",
  "menu_frame", "applets", "addons", "perf", "hud"]}`. The app answers
  `{"type": "hello", "protocol": 1, "app": "<version>", "level": <level it will use>}`.
  `protocol` changes only for incompatible changes; everything else is negotiated by
  `level` and `capabilities`. No hello within 10 s: the app ends the engine and reports
  `protocol`.
- **End of channel:** when the app's end closes, the engine saves and exits by itself.

## Level 1

**One-shot commands.** The engine is started with `--emuden-oneshot <command>` and the
request as one JSON object on standard input; it prints one JSON result object on standard
output and exits (0 = `ok` true). No window, no GPU unless the command needs one.

| Command | Request | Result |
|---|---|---|
| `describe` | none | the hello fields, plus `data_root` |
| `schema` | none | the engine's settings as a plugin `settings` array (`schema/plugin.schema.json`) |
| `status` | none | `firmware` (version or null), `keys` (bool), `problems` (typed errors) |
| `install-firmware`, `install-keys` | `source` (path) | `ok`, typed error |
| `verify` | `content` | `ok`, `title_id`, `title`, `version`, typed error |
| `precache-shaders` | `content` | progress lines `{"type":"progress","done":n,"total":m}`, then the result |
| `list-addons` | `content` | `addons` (id, name, kind, enabled) |
| `save-info` | `content` | `saves` (path, bytes, modified) |
| `export-saves`, `import-saves` | `content`, `target` or `source` | `ok`, typed error |

**Session, app to engine:**

- `launch` (request): `content`, `system`, `game_id`, `roots` (`data`, `saves`, `cache`,
  `addons`), `fullscreen`, `locale`, `environment` (map). The result arrives when loading
  has begun, not when it has finished.
- `stop` (request): end the session, save, exit. The app ends the engine 5 s after it.

**Session, engine to app (events):**

- `loading`: `progress` (0-1, optional), `text` (optional).
- `first_frame`: the game's first frame is on screen. The app hides its window now (not
  after a fixed delay as at level 0).
- `running`
- `stopped`: `reason` (`user`, `app`, `error`), `error` (typed, when `reason` is `error`).
  The engine exits after sending it.

Play time is still the app's: from `first_frame` to `stopped`.

## Level 2

**Additions to `launch`:**

- `settings`: an object of resolved values by key (the app has already applied game,
  console and global layers; the engine applies them over its defaults and stores nothing).
- `addons`: add-on ids in load order.
- `input`: the input plan: per player, the device identity (name, vendor and product id,
  serial when known), the pad family, and `menu_chord` (the buttons that open the quick
  menu).
- `activation_token` (Linux desktop: the Wayland token for the engine's window).

**App to engine (requests):**

- `pause`, `resume` (`resume` carries `activation_token` on Wayland; after it the engine
  ignores buttons that are still held).
- `set_setting`: `key`, `value`; the result says `applied`: `now`, `next_launch` or `no`.
- `screenshot`: `path`; the result arrives when the file is written.
- `hud_level`: `level` 0-3 (off, frame rate, timing, full). The HUD is engine-drawn.
- `notify`: `text`, `seconds`; an engine-drawn toast.

**Engine to app (events):**

- `paused`: `reason` (`app`, `menu`, `applet`, `focus`).
- `menu_request`: the user pressed the menu chord. The engine pauses first and sends
  `frame`: `path` of the paused frame (PNG or raw RGBA with `width`, `height`, `stride`) in
  the session's `cache` root. The app comes forward and draws its menu over that frame;
  `resume` hands the screen back.
- `applet_request` (carries `id`; the app answers with a `result`): `kind` `text` (title,
  initial text, limits; result `text` or `cancelled`) or `error` (typed error to show;
  result when dismissed).
- `addons`: `applied` and `skipped` (id and reason) once loading has resolved them.
- `perf`: about once a second: `fps`, `frame_ms`, `speed`, `shaders_building`.

**Fixed rules:** one user (no profile selector), no engine-drawn menus other than the HUD
and toasts, no window before `launch`, settings are never written by the engine during a
session.

## Schemas

`schema/` holds the published formats, copied unchanged from the official plugin source
(`Denarmo/emuden-plugins`, `schema/`): `plugin.schema.json` (a plugin's `plugin.json`),
`index.schema.json` (the signed `index.json`), `addon.schema.json` and
`addons.schema.json` (add-on packages and their list).
