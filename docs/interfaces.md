# Interfaces

What each module offers the others. Headers are in `<module>/include/den/<module>/`; include
them as `"den/<module>/<file>.h"`. A directory never includes one listed after it in the
root `CMakeLists.txt` (base, platform, gfx, input, ui, store, library, plugins, online,
engine, player, system, app).

## Building one target

```
powershell -ExecutionPolicy Bypass -File tools\build.ps1 -Preset win -Target <target> -BuildDir <your dir>
```

Targets: `den_base`, `den_platform`, `den_gfx`, `den_input`, `den_ui`, `den_store`,
`den_library`, `den_plugins`, `den_engine`, `den_player`, `den_system`, `den_app` (the
executable `bin/emuden.exe`), and `den_<module>_tests` for every module that has files in
`<module>/tests`. Several targets: `-Target den_ui,den_ui_tests`. `-Test` runs ctest.

A module's `CMakeLists.txt` is one line, `den_module(<name> DEPS ... PRIVATE_DEPS ...)`.
Sources in `<module>/src` (recursively) and tests in `<module>/tests` are globbed: adding a
file needs no CMake edit. Third-party code comes from `cmake/deps.cmake` (`den_fetch`), whose
sources live once in `.deps/`.

## Foundation

| Header | What |
|---|---|
| `den/base/result.h` | `Status` (ok, error, notes), `Result<T>` |
| `den/base/log.h` | `log::Info("tag", "format {}", ...)`, also `Debug`, `Warn`, `Error` |
| `den/base/files.h` | UTF-8 paths, read/write files (atomic), `ListDirectory`, `ToLower`, `Trim` |
| `den/base/json.h` | `ParseJson`, `WriteJson`, `JsonValue` |
| `den/base/hash.h` | `Sha256`, `Md5`, `Crc32Update`, file variants |
| `den/base/zip.h` | `ListZip`, `ExtractZipEntry` |
| `den/base/jobs.h` | `Jobs::Run(name, fn(Job&), on_done)`: progress, message, cancel; `on_done` runs on the main thread |
| `den/platform/pal.h` | window and GL context, `DataDir`, `AssetsDir`, `HttpGet` (redirects, streaming to a file, progress, cancel), `StartProcess`, `GetSecrets`, `GetCaps`, `StartLineServer` |
| `den/gfx/gl.h` | the GL functions, loaded at run time (`gl::Viewport`, ...); add one by adding a line |
| `den/gfx/device.h` | textures, shader programs, `ReadPixels`, `SavePng` |
| `den/gfx/canvas.h` | `Canvas`: `FillRect`, `StrokeRect`, `DrawImage`, `DrawText`, clips, `Flush`, `RestoreState` |
| `den/gfx/text.h` | fonts (`FONT_REGULAR`, `FONT_BOLD`), `Measure`, `Elide` |
| `den/gfx/images.h` | `DecodeImage`, `LoadImageFile`, `TextureCache::Get(path)` (async decode) |
| `den/input/input.h` | `System::Poll` gives `Event`s (Nav, pointer, key, text); `PadState` by position for the player; `GlyphName`, `GlyphLabel` per pad family |
| `den/ui/node.h`, `widgets.h`, `page.h`, `theme.h` | the UI kit, below |
| `den/store/settings.h` | `Settings`: dotted keys in `settings.json` |
| `den/platform/child.h` | `StartChild`: a child that never outlives the app (job object, parent-death signal), working directory, `RequestClose`, `Kill` |
| `den/system/settings.h` | layered settings: `Key<T> K{"a.b", default, {meta}}` declared next to the feature registers itself; `system::Store().Get(K, Scope::Game(system, id))` resolves game, console, global (`settings.json`), default; overrides in `settings-overrides.json` (versioned); `Keys()`, `KeysIn(section)` for pages |
| `den/system/first_run.h` | `NoteStart` (called by the app), `ThisStart()` (`first_run`, `upgraded`), `SetupDone`, `MarkSetupDone` (`setup.done`) |

No OS `#ifdef` outside `platform/`. What differs between devices is asked from
`pal::GetCaps()` (`gles`, `touch`, `windowed`, `child_processes`, `handheld`, `platform_key`).
`StartProcess` cannot set a working directory (SDL 3.2 has none): pass absolute paths.

## UI kit

Everything is in logical units: the window is always 720 units high, the width follows.

- `ui::Node`: a box that lays its children out along `axis` with `gap`, padding, fixed
  `width`/`height` or `AUTO`, and `grow`; `scrollable` makes it a list or shelf that scrolls
  to the focused child. `focusable` nodes take part in navigation; `on_activate` runs on
  Confirm or a tap.
- Widgets: `Label`, `ImageView`, `Tile` (cover + title), `Button`, `Row` built from a
  `RowSpec` (Action, Value, Toggle, Choice, Slider), `MakeList`, `MakeShelf`, `MakeHeading`.
- `ui::Page`: override `Build()` to fill `Root()`. Optional: `Update`, `OnNav`, `OnEvent`,
  `Hints`, `Immersive` (no hint bar) and `Draw` (a page that draws itself, like the player).
  `Rebuild()` builds again and keeps the focus by node id.
- `ui::Stage`: `Push(route, params)`, `Replace`, `Pop`, `PopToRoot`, `Toast(text)`,
  `Confirm(title, message, yes, on_yes)`.
- `ui::Theme`: Light (default) and Dark; widgets read `ctx.theme`. The accent exists only in
  `ui/src/selection.cpp`, as the border of the current selection.

## Adding a feature to the app

A feature is files under `app/screens/<feature>/` (globbed). Nothing shared is edited:

```cpp
#include "den/app/app.h"       // Svc(): window, canvas, textures, jobs, stage, settings,
#include "den/app/frame.h"     //        library, plugins, engine, player, theme
#include "den/app/registry.h"

class DetailsScreen final : public den::ui::Page {
    void Build() override {
        den::ui::Node* content = den::app::BuildPlainFrame(*this, "Title");
        ...
    }
};
DEN_REGISTER_SCREEN("details", DetailsScreen);            // stage.Push("details", {{"id", id}})
DEN_REGISTER_RAIL(20, "Library", "library");              // an entry in the left rail
DEN_REGISTER_SETTINGS(30, "Player", [] { return std::vector<den::ui::RowSpec>{...}; });
DEN_REGISTER_STARTUP([] { ... });                         // once, services ready
DEN_REGISTER_FRAME([] { ... });                           // every frame, main thread
```

Routes the home screen already opens when they exist: `details` (param `id`, a library
entry), `console` (param `system`). `setup` is shown first when registered and
`setup.done` is not set. `BuildRailFrame` gives a page the rail; `BuildPlainFrame` a title.

## Facades

Each is an abstract class with a factory; all four are real (no fakes are left).

| Facade | Header | Factory | Implementation |
|---|---|---|---|
| `den::library::Library` | `den/library/library.h` | `CreateLibrary(Config)` | `library/src` (see `library/README.md`) |
| `den::plugins::Host` | `den/plugins/host.h` | `CreateHost(Config)` | `plugins/src/real_host.h`, `host_install.cpp`; refreshed once per start by `app/screens/consoles/startup_refresh.cpp` (`plugins.refresh_on_start`) |
| `den::player::Session` | `den/player/session.h` | `CreateSession(Config)` | `player/src`; try it alone with `den_player_play` (`player/tools/play.cpp`) |
| `den::engine::Host` | `den/engine/host.h` | `CreateHost(Config)` | `engine/src/host/host.cpp` |

A runnable folder: `powershell -ExecutionPolicy Bypass -File tools\package.ps1` writes
`dist\windows` (executable, assets, licences); `-Build` builds first, `-Zip` adds an archive.

- **Library**: folders (add, remove), `Scan()` as a job, `Entries()` snapshots (id, clean
  name, system id, path, cover image path, favourite, last played, play time and count),
  `Systems()`, `RecentlyPlayed`, `Favourites`, `SetFavourite`, `RecordPlay`, and `Revision()`
  which grows on every change so screens know when to rebuild. Switch 2 games and ROMs are
  entries alike; `system` is the plugin manifests' system id.
- **Plugins**: `Consoles()` (id, names, extensions, `Runner::Core` or `Runner::Engine`,
  installed), `CoreFor(system)` (core file, system folder, option hints, missing BIOS),
  `Install(system)` / `Remove` / `Refresh` as jobs, `Revision()`.
- **Player**: `Open(OpenRequest)` (core, content, system folder, game id, option hints), then
  per frame `SetPad(port, input::PadState)`, `Advance(seconds)`, `Draw(canvas, area)`; `Pause`,
  `Resume`, `SaveState`/`LoadState`/`HasState` (slots 0-9), `SetFastForward`, `SetRewind`,
  `Reset`, `Options`/`SetOption`, `GetSnapshot()`. Main thread. `Draw` may `canvas.Flush()`,
  use GL from `den/gfx/gl.h`, then `canvas.RestoreState()`. `libretro.h` is in
  `player/third_party/libretro` (on the include path of `den_player`). Also:
  `OpenRequest::resume` (continue from the state `Close` wrote), `StateThumbnail(slot)`,
  `SetPointer(x, y, pressed)` (touch screens), `SetAspectOverride`, `FlushSaves()`. Files:
  `<data>/saves/<system>/<game id>.srm`, `<data>/states/<system>/<game id>.slotN.state` and
  `.png`, `.resume.state`. Zipped ROMs are unpacked to `<data>/cache/content` while they
  run. Settings: `player.resume`, `player.rewind`, `player.fast_forward_speed`,
  `player.integer_scale`, `player.smooth`, `player.pad_by_label`, `player.volume`,
  `core_options.<core>.<key>`. GPU cores draw in their own GL context that shares objects
  with the app's.
- **Engine** (level 0, real; see `contracts/engine-contract.md`): `Available(system)`,
  `UnavailableReason`, `Find(system)` (the `Install` it would use), `Start(LaunchRequest)`,
  `Running`, `Current`, `Stop` (polite, forced after 5 s), `PollEvents()` giving `Started`,
  `Exited` (exit code, seconds) and `Failed` (message). Settings `engine.<system>.path` and
  `.args` (`engine::PathKey`, `ArgsKey`). `app/screens/engine/engine_glue.cpp` pumps the
  host every frame, hides the window while a game runs, covers the app with the
  `engine.running` page (pad input ignored) and books the play time with
  `Library::RecordPlay`: screens must not book engine sessions again.

## Automation endpoint

Off unless the app is started with `EMUDEN_AUTOMATION=<port>`. It listens on
`127.0.0.1:<port>`; a client connects, sends one line and gets one line back.

| Line | Reply |
|---|---|
| `state` | JSON: `route`, `depth`, `focus` (`id`, `label`), `dialog`, `toasts`, `family`, `fps`, `theme` |
| `key <name>` | `ok`; names: up down left right confirm back option extra menu view l r home |
| `go <route> [k=v ...]` | `ok`; pushes a route with parameters |
| `tap <x> <y>` | `ok`; a tap at a point in logical units |
| `pad <button...> [ms]` | `ok`; holds game buttons on port 1 (south east west north up down left right l r start select), 150 ms by default |
| `screenshot <png path>` | `ok <w>x<h>` once the next frame is saved |
| `quit` | `ok`; the app closes |

`tools/drive.py` wraps it (default port 5577; it waits for the app to listen):

```
mkdir build\win\bin\user                       # portable data, so tests leave app-data alone
set EMUDEN_AUTOMATION=5577 && start build\win\bin\emuden.exe --windowed
python tools/drive.py state
python tools/drive.py key down + key right + key confirm + screenshot C:/tmp/shot.png
python tools/drive.py quit
```

Use your own port when several agents test at once (`--port N`). Give nodes an `id` so
`state` can tell what is focused. Always end with `quit`.
