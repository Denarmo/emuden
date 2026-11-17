# Interfaces

What each module offers the others. Headers are in `<module>/include/den/<module>/`; include
them as `"den/<module>/<file>.h"`. A directory never includes one listed after it in the
root `CMakeLists.txt` (base, platform, net, patching, gfx, input, ui, store, library,
plugins, engine, player, achievements, system, sync, app).

## Building one target

```
powershell -ExecutionPolicy Bypass -File tools\build.ps1 -Preset win -Target <target> -BuildDir <your dir>
```

Targets: `den_base`, `den_platform`, `den_net`, `den_patching`, `den_gfx`, `den_input`, `den_ui`,
`den_store`, `den_library`, `den_plugins`, `den_engine`, `den_player`, `den_achievements`,
`den_system`, `den_sync`, `den_app` (the executable `bin/emuden.exe`), and `den_<module>_tests`
for every module that has files in `<module>/tests`. Several targets: `-Target den_ui,den_ui_tests`. `-Test` runs ctest.

A module's `CMakeLists.txt` is one line, `den_module(<name> DEPS ... PRIVATE_DEPS ...)`.
Sources in `<module>/src` (recursively) and tests in `<module>/tests` are globbed: adding a
file needs no CMake edit. Third-party code comes from `cmake/deps.cmake` (`den_fetch`), whose
sources live once in `.deps/`.

`-DEMUDEN_DEV_TOOLS=OFF` leaves the developer tools out of the app: the automation endpoint
(`app/src/automation.cpp`), the player's and the browser's test endpoints (`test_endpoint.cpp`,
`web_endpoint.cpp`) and the sample plugin host (`fake_host.cpp`, `sample_pages.cpp`). It is on by
default; each tool still starts only when its environment variable is set.

## Foundation

| Header | What |
|---|---|
| `den/base/result.h` | `Status` (ok, error, notes), `Result<T>` |
| `den/base/log.h` | `log::Info("tag", "format {}", ...)`, also `Debug`, `Warn`, `Error` |
| `den/base/files.h` | UTF-8 paths, read/write files (atomic), `ListDirectory`, `ToLower`, `Trim` |
| `den/base/json.h` | `ParseJson`, `WriteJson`, `JsonValue` |
| `den/base/hash.h` | `Sha256`, `Md5`, `Crc32Update`, file variants |
| `den/base/zip.h` | `ListZip`, `ExtractZipEntry` |
| `den/base/archive.h` | zip or 7z by the file's first bytes: `ListArchive`, `CanExtractArchiveEntry`, `ArchiveEntryRefusal`, `ExtractArchiveEntry`, `ArchiveReader` (several files of one solid 7z block), `IsArchiveExtension`. 7z through the LZMA SDK's decoder (`cmake/deps.cmake`): copy, LZMA, LZMA2, BCJ, BCJ2, delta; no password, no PPMd. A solid block up to 256 MiB is unpacked in memory; a larger one of copy, LZMA or LZMA2 behind at most one filter (not BCJ2) is decoded in pieces straight to the file, of any size, holding its dictionary (at most 256 MiB); a larger BCJ2 block is unpacked in memory, at most 1 GiB. rar is not read |
| `den/base/jobs.h` | `Jobs::Run(name, fn(Job&), on_done)`: progress, message, cancel; `on_done` runs on the main thread |
| `den/platform/pal.h` | window and GL context, `DataDir`, `AssetsDir`, `HttpGet` (any method, bodies from memory or a file slice, answer headers, redirects, streaming to a file, progress, cancel), `StartProcess`, `GetSecrets` (the platform's credential store), `GetCaps`, `StartLineServer` |
| `den/sync/engine.h` | device sync (`docs/sync.md`): `CreateEngine`, the active provider, categories, triggers, status, conflicts, remote games; `app/include/den/app/sync_glue.h` `SyncSvc()` |
| `den/platform/apps.h` | `pal::apps`: the device's other apps where the platform can list them (Android): `Available`, `List` (id, label, icon PNG in the app cache; call it from a job), `Revision`, `Launch(id, Display::Default / Main / Second)`, `HasInputFocus`; for an app beside the game: `Raise(display)` (this app's own window over the other app) / `Lower(display, app)` (that app in front again), `ShowHandle` / `HideHandle`, `CatchBack(display, on)`, `TakeEvents()` (Handle, Back, Home) |
| `den/platform/lifecycle.h` | `pal::MainWindowHidden()` (Android: another app covers the main window, the loop goes on), `PrepareFrame()` at the start of every frame (keeps the GL context usable; false = no GL this frame), `EventPumpHeld()` for `den::input` |
| `den/platform/audio.h` | `pal::audio`: the system's media volume where an app may set it (Android): `HasSystemVolume`, `SystemVolumeSteps`, `SystemVolume`, `SetSystemVolume` |
| `den/platform/text_input.h` | `pal::GetTextInput()`: the platform's own keyboard for a text (Android): `Begin(request, TextDisplay::Main / Second)`, `SetText`, `End`, `Poll` (Shown, Hidden, Text, Done, Cancel), `CoveredShare`. `Available()` is false on the desktop |
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
| `den/system/first_run.h` | `NoteStart` (called by the app; `first_run`, `upgraded`), `SetupDone`, `MarkSetupDone` (`setup.done`) |

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
  A Slider row steps with left and right and follows a finger: a sideways drag that starts
  on its track, or a tap there, sets the value (`Node::WantsDrag` / `DragTo`, which any node
  can implement). A page that rebuilds on the value ends the drag, so do not.
- `ui::Page`: override `Build()` to fill `Root()`. Optional: `Update`, `OnNav`, `OnEvent`,
  `Hints`, `Immersive` (no hint bar) and `Draw` (a page that draws itself, like the player).
  `Rebuild()` builds again and keeps the focus by node id.
- `ui::Stage`: `Push(route, params)`, `Replace`, `Pop`, `PopToRoot`, `Toast(text)`,
  `Confirm(title, message, yes, on_yes)`, `AskText(request, on_done)` (the text entry;
  `TextRequest::on_change` follows the typing).
- `Node::on_hold` runs when a finger rests on the node for half a second; the release is
  then not a tap. The pad's button for the same thing is the page's business (`OnNav`).
- Text entry and keyboards (`den/ui/text_entry.h`): `Stage::SetKeyboard(KeyboardSetup)` says
  where the keys of the next entries come from: the platform's keyboard (`pal::TextInput`),
  whether the user prefers it, the display it opens on, and whether that is another display
  than the stage's. With it the entry shows the line alone (plus the app's keys when the
  request was made with a pad and the keyboard is on the other display); on the same
  display the panel sits above the keyboard (`CoveredShare`). A keyboard that does not
  report `Shown` within a second, or is put away, gives way to the app's keys; when
  `keys_apart` is set they are wanted on the other display (`Stage::Entry()->KeysApart()`,
  `DrawKeys`, `KeysPointer`), which `app/screens/companion/second_screen.cpp` does. The
  app's choice is made every frame in `app/screens/settings/keyboard.cpp` (`ui.keyboard`:
  `system` or `emuden`).
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
DEN_REGISTER_OVERLAY([](den::ui::Context& ctx) { ... });  // drawn over the screens
DEN_REGISTER_EVENT_FILTER([](const den::input::Event& e) { return false; }); // true eats it
```

A feature never includes another feature's files (`../<feature>/...`; ctest's
`den_app_feature_includes` runs `tools/check_app_includes.py`). What features share is in
`app/include/den/app/` (`games.h`, `strings.h`, `launch.h`, `boot.h`, `game_prefs.h`, ...),
included as `den/app/...`. The running game is `CurrentPlayer()` (`den/app/player.h`), and a
feature adds to it with `DEN_REGISTER_PLAYER_HOOKS` (`den/app/player_hooks.h`): the core it
chooses, a gate before the game starts (sync's save check), its part of the open request
(achievements' hardcore, Together's connection), what happens when the game is left,
pause-menu rows, a status line and an overlay over the picture.

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
  `SetPointer(x, y, pressed)` (touch screens), `SetAspectOverride`, `FlushSaves()`,
  `SaveResumeState()` (the state `Close` writes, now, and the game goes on). Files:
  `<data>/saves/<system>/<game id>.srm`, `<data>/states/<system>/<game id>.slotN.state` and
  `.png`, `.resume.state`. ROMs in a zip or a 7z are unpacked to `<data>/cache/content`
  while they run. Settings: `player.resume`, `player.rewind`, `player.fast_forward_speed`,
  `player.integer_scale`, `player.smooth`, `player.pad_by_label`, `player.volume`,
  `core_options.<core>.<key>`. GPU cores draw in their own GL context that shares objects
  with the app's. Core options have a scope: `SetOption(key, value, OptionScope::Game)` keeps
  the value for this game only (`game_options.<system>.<game>.<key>`, wins over the
  console's), `ClearGameOptions()` drops them; `CoreOption::for_game` tells which is shown.
  Buttons: `den/player/buttons.h` (`LoadButtons`, `SaveButton`, `ResetButtons`; settings
  `player.buttons.<system>.<game button>` = a pad button by position), then
  `Session::ReloadButtons()`. Picture presets: `PicturePresets()` (id, name, group, sliders),
  read from `assets/shaders/presets.json`; each is a chain of GLSL passes of that folder run
  by `gfx::EffectChain` (`den/gfx/effect_chain.h`: render targets, a previous-frame history,
  `p_<id>` parameters; one source for GL 3.3 and GLES 3). `Session::SetPicture(id)` takes a
  preset id or `"auto"` (`AutoPicture(system)`: the console's default), `PictureParams`,
  `SetPictureParam` and `ResetPictureParams` its sliders (`player.shader.<preset>.<param>`).
  The screen keeps the choice in the layered key `player.picture` (game, console, global;
  `app/include/den/app/picture_choice.h`). The presentation path in `player/src/session_video.cpp`
  is `PreparePicture` (the core's frame as a `PictureSource`), then `DrawRegion` per
  `PictureRegion` (a part of the picture, its target, and a slot that keeps the chain's
  targets apart): a two-screen layout calls it once per screen. The snapshot also has
  `audio_queued_ms`, `audio_underruns` and `picture`. While playing, Select + R1 holds
  fast-forward and Select + L1 rewind on every pad; button glyphs per pad family are drawn
  by `app/include/den/app/glyphs.h` (`DrawGlyph`, `HintLine`).
- **Engine** (level 0 and the Emulator Control Protocol, levels 1 and 2; see `docs/engines.md`): `Available(system)`,
  `UnavailableReason`, `Find(system)` (the `Install` it would use), `Start(LaunchRequest)`,
  `Running`, `Current`, `Stop` (polite, forced after 5 s), `PollEvents()` giving `Started`,
  `Exited` (exit code, seconds) and `Failed` (message). Settings `engine.<system>.path` and
  `.args` (`engine::PathKey`, `ArgsKey`). `app/screens/engine/engine_glue.cpp` pumps the
  host every frame, hides the window while a game runs, covers the app with the
  `engine.running` page (pad input ignored) and books the play time with
  `Library::RecordPlay`: screens must not book engine sessions again.

- **Achievements** (`den/achievements/runtime.h`, `games.h`): `achievements::Runtime` wraps
  rcheevos' `rc_client` with server calls on helper threads, answered on the main thread in
  `Pump()`; events come out of `TakeEvents()`. `HashFile`, `ResolveHash`, `FetchProgress` work
  without a running game. The player side is in `Session`: `MemoryMap()`, `MemoryRegion(id)`,
  `SetFrameHook` (`FrameEvent::Frame`, `Reset`, `StateLoaded`, `MemoryMoved`),
  `SetHardcore`, `OpenRequest::hardcore`, `ContentPath()`, `SaveScreenshot(png)`. App glue:
  `app/screens/achievements/`; the plugin services: `den/plugins/feature_achievements.h`.

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

The player's test endpoint (`app/screens/player/test_endpoint.cpp`) listens on the next
port, `<port> + 1`; `tools/drive.py` sends these lines there by itself:

| Line | Reply |
|---|---|
| `vpad attach <xbox\|playstation\|nintendo>` | `ok <id>`; a virtual gamepad. Also `press`, `down`, `up`, `stick`, `trigger`, `detach` (see `tools/drive.py`) |
| `player` | JSON: the session (state, core, frames, fps, rewind), its sound counters (`audio`) and the pads |
| `audio reset` | `ok`; the sound counters start again |
| `wav <file> [seconds]` | records the device's output; answers with its level and the gaps in it |

Use your own port when several agents test at once (`--port N`). Give nodes an `id` so
`state` can tell what is focused. Always end with `quit`.

## Second screen

- `den/platform/second_display.h`: `pal::GetSecondDisplay()` is the other screen of the
  device, drawn with the app's own GL context (the same textures, shaders and canvas):
  `Present()`, `SetWanted(bool)`, `Ready()`, `PixelSize`, `BeginFrame()` / `EndFrame()` around
  the drawing, `TakePointer` (presses in its pixels; they never reach `input::System`).
  Android: an activity of its own on the other display (`shells/android`, `PanelActivity`),
  which stays while the main window is hidden; a Presentation where the display takes none.
  Desktop: a second window, there when the app is started with `EMUDEN_SECOND_WINDOW=1` or
  the setting is "on".
- `app/include/den/app/second_screen.h` drives it every frame, before the main window's
  frame, with a `ui::Stage` of its own (`CompanionStage()`). Its pages are ordinary screens
  whose route starts with `companion.`: `companion.browse` (the game the main screen has in
  focus: cover, records, Play, favourite, Details, Game settings) and `companion.play` (quick
  settings while a game runs: pause, fast-forward, rewind, exit, save and load slots with
  their pictures, the picture preset, the volume). Touch only; the pad stays with the main
  screen. Setting `ui.companion.enabled`: `auto` (on when `Present()`), `on`, `off`.
- For a console with two screens: `ClaimSecondScreen({draw, pointer})` gives the screen to
  the player instead of the companion pages until `ReleaseSecondScreen()`. `draw(ctx, area)`
  runs every frame with the canvas begun for the second screen; `pointer(event)` gets its
  presses in pixels (`SecondScreenScale()` converts). `SecondScreenReady()` and
  `SecondScreenPixelSize` say what is there; a claim fails when the device has no second
  screen.
- `SetSecondScreenOverlay({draw, pointer})` draws in place of whatever has the screen (the
  pages or a claim) until `ClearSecondScreenOverlay()`; `DrawCompanionStage(ctx)` ticks and
  draws the companion pages on another canvas. The player screen
  (`app/screens/player/player_displays.cpp`) uses both: with two displays an open pause menu
  takes the display the quick settings are on and the game stays whole and undimmed on its
  own (`player.overlay_on_second_screen`, on by default; for a two-screen game the menu takes
  the second display, over whichever screen is there), and "Swap screens" puts a one-screen game on the
  second display and the quick settings in the main window (`player.swap_displays`). The
  pad always goes to the game, or to the menu while it is open; touch works on whichever
  display shows the menu or the quick settings.
- Quick settings reach the game through `CurrentPlayer()` (`den/app/player.h`):
  `OpenPage("options" | "buttons")` opens that page of the pause menu by itself (Back
  closes it), `SwapDisplays()`, `SetScreenSize(index)`, `Exit()`.
- `ui::Page::Pointer(event, scale, time)` gives a page presses from a display its stage
  does not see. `Rebuild()` keeps every scrolling node that has an id where it was;
  `Rebuild(false)` is for other content under the same ids.
- `player::Session::SetVolume(0..1)` changes the running game's volume at once.
- Consoles with two screens (`den/player/screens.h`): `Session::Screens()` is two
  `GameScreen`s (part of the picture, own pixels, `touch`) while the core's picture is the
  stacked shape `ScreensOf(system, w, h)` knows (NDS 2:3, 3DS 5:6), else empty and the
  picture is one screen. `DrawScreen(canvas, screen, target)` draws one through the picture
  preset on either display, `SetScreenPointer(screen, u, v, pressed)` is its touch.
  `LayoutScreens` / `FitScreen` are the geometry of one display (stacked, side by side,
  large and small, one screen; fit or whole multiples). `app/screens/player/dual_screen.h`
  (`DualScreens`) uses them: it claims the second display while such a game runs and
  `player.dual_screen.mode` is not `single`, puts one screen on each display, and falls back
  to the layouts of one display (`player.dual_screen.layout`, `.swap`) when there is no
  second one. The pause menu has "Swap screens" and "Screens". The player's test endpoint
  answers `screens` with the mode and where each screen is drawn.
- Test endpoint on the automation port + 2: `state`, `tap <x> <y>` (the second screen is 600
  units high), `drag <x1> <y1> <x2> <y2>`, `screenshot <png>`.

## Apps

- `den/system/app_shelf.h`: `system::AppShelf`, the list as it is shown: by name,
  favourites first, a search by words, the apps opened last. Pure data, unit-tested.
- `app/include/den/app/apps_catalog.h`: `AppsAvailable()`, `Apps()` (the shelf, filled from
  `pal::apps::List()` on a job and again when `pal::apps::Revision()` moves),
  `AppsRevision()`, `ToggleAppFavourite`, `OpenApp(id, beside_game, stage)` and
  `PadWithOtherApp()`. Settings `apps.favourites`, `apps.recent`. `EMUDEN_FAKE_APPS=1`
  gives the desktop a made-up list for driving the screens (opening only shows a toast).
- `app/include/den/app/app_grid.h`: `AddAppGrid(parent, options)`, the grid both pages use.
- Routes: `apps` (in the rail only when `AppsAvailable()`; `from=game` is the pause menu's
  "Apps" row on a device with one display) and `companion.apps` on the companion stage (the
  quick settings' "Apps" button). Confirm or a tap opens, Option or a held finger pins,
  Extra searches.
- `OpenApp` with `beside_game` goes through `app/include/den/app/beside.h`. Two displays: the
  app opens on the display the quick settings are on (the second one, or the main one when
  the game is swapped to the second) and the game goes on on the other; one display: the
  game is paused in its menu and the app takes the display.
- `den/system/app_beside.h`: `system::AppBeside`, the state of all that (which display has
  the game, the quick settings, the app; `Stage::None / AppFront / SettingsOver`). Every
  transition (`Launch`, `PullSettings`, `BackToApp`, `CloseApp`, `Back`, `Home`,
  `SetSurfaces`, `SetGame`, `Tick`) returns `BesideAction`s (Launch, Raise, Lower,
  ShowHandle, HideHandle, OpenMenu, SaveGame, HoldGame, ReleaseGame), so it is tested with
  a fake device (`system/tests/app_beside_tests.cpp`). `beside.cpp` feeds it every frame
  (`pal::MainWindowHidden()`, `SecondDisplay::Ready()`, `PlayerScreen::Swapped()`,
  `pal::apps::TakeEvents()`) and does what it asks. `Beside()` is the state for pages:
  the quick settings show "Back to <app>" and "Close <app>" first while `SettingsOver`,
  `WantsTab()` makes the player draw a tab for the quick settings on the game's display,
  `PlayerScreen::Hold(bool)` pauses a game whose display is covered without a menu.
- `app/include/den/app/volume_rows.h`: `VolumeRows(prefix, other_app)`, the "Game" slider
  (`player.volume`, the game's share) and "Other app" (`pal::audio`, the system volume),
  each with the screen it belongs to (`ScreenName`); in the quick settings (the second one
  while an app is beside the game) and in the pause menu.
- The quick settings keep a "Recent apps" row (the last three) and `companion.idle` is the
  second screen's page while the main one shows the Apps section.

## Launcher additions

- Routes: `setup` (four steps; "Run setup again" in Settings), `library_options`,
  `hidden_games`, `statistics`, `game_settings` (param `id`), `collect` (param `id`).
  `library` also takes `collection=<name>`.
- `app/include/den/app/collections.h`: the user's collections (`library.collections`,
  `library.collection_games.<name>`), the library order (`library.sort`) and layout
  (`library.view`: `grid` or `list`). `TouchGames()` (games.h) moves `GamesRevision()`.
- `app/include/den/app/game_prefs.h`: what one game or console overrides in the layered
  settings. `CoreForGame(entry)` (the game's own core, `player.core`, with its own core
  options as option hints), `ScreenSizeFor(entry)` (`player.screen_size`),
  `EngineFullscreenFor` (`engine.screen`: app, fullscreen, window). Core options of a game
  or console are `core_option.<core>.<key>` in its layer; the list of a core's options is
  noted in `<data>/cache/core_options/<core>.json` whenever a game runs. Whoever opens a
  session uses `CoreForGame`, not `plugins->CoreFor`.
- Themes: Light, Dark, Green, Magenta, Amber, High contrast, High contrast dark
  (`ui/src/theme.cpp`).

## In-app browser

`docs/in-app-browser.md`. `den/platform/webview.h` is `pal::WebView`: a page shown with the
platform's web engine in a native view over part of the window (WebView2 on Windows,
android.webkit.WebView on Android, WebKitGTK on Linux when the system has it, a fake for
tests). Without an engine the same route opens the site in the user's browser and offers the
wanted files that then appear in the Downloads folder (`den/platform/downloads.h`:
`pal::OpenInBrowser`, `pal::DownloadFolders`, `pal::DownloadWatch`). The route `web` (`app/include/den/app/web_browser.h`:
`web::Open(OpenRequest)`, `web::RegisterOpener`) lets the user browse the hosts an opener
allows and hands the opener the files the user downloads there (`<data>/inbox/web`; patch and
archive formats, never a ROM image). Its test endpoint is on the automation port + 3.

## Hidden settings

Keys the app reads that no page shows; set them in `settings.json` by hand.

| Key | Default | What |
|---|---|---|
| `player.integer_scale` | `false` | The game picture only in whole multiples of the core's size, whatever the preset (`player/src/session.cpp`, `app/screens/player/player_displays.cpp`) |
| `plugins.refresh_on_start` | `true` | Fetch the signed plugin list once per start (`app/screens/consoles/startup_refresh.cpp`); `false` keeps the bundled manifests and the last list |
| `plugins.downloads_folder` | the platform's Downloads folder | Where feature plugins look for files the user's browser saved (`plugins/src/feature/feature_catalog.cpp`) |
