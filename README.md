# EmuDEN

A console-style game launcher: one C++20 binary on SDL3 with its own retained-mode UI kit,
for controller, keyboard and mouse, and touch. It plays ROMs through libretro cores inside its
own window and starts out-of-app engines for consoles that need one. Consoles are plugins
from the signed index of `Denarmo/emuden-plugins`.

The architecture, the stages and the coding rules are in [docs/build-plan.md](docs/build-plan.md).
The interfaces between modules, and how to drive the app from a script, are in
[docs/interfaces.md](docs/interfaces.md).

## Build

Windows (Visual Studio 2022 build tools, CMake, Ninja), from any shell:

```
powershell -ExecutionPolicy Bypass -File tools\build.ps1 -Preset win
powershell -ExecutionPolicy Bypass -File tools\build.ps1 -Preset win -Target den_ui_tests -BuildDir build\mine
powershell -ExecutionPolicy Bypass -File tools\build.ps1 -Preset win -Test
```

The result is `build/win/bin/emuden.exe`. Linux: `cmake --preset linux && cmake --build build/linux`.
Android: `cmake --preset android` with `ANDROID_NDK_HOME` set (the Gradle shell is not there yet).

Third-party sources (SDL3, FreeType, zlib, stb, Catch2) are fetched once into `.deps/` at
pinned versions and shared by every build directory.

## Layout

| Directory | Target | Contents |
|---|---|---|
| `base/` | `den_base` | results, log, files, UTF-8, JSON, hashes, zip and 7z, jobs |
| `platform/` | `den_platform` | `pal.h`: window, GL context, folders, HTTP, processes, secrets, Caps |
| `gfx/` | `den_gfx` | GL 3.3 / GLES 3 device, 2D canvas, text, images, texture cache |
| `input/` | `den_input` | pads, families, mapping by label, navigation events, glyph names |
| `ui/` | `den_ui` | nodes, layout, focus, widgets, pages, stage, themes |
| `store/` | `den_store` | settings |
| `library/` | `den_library` | the game library (facade + fake for now) |
| `plugins/` | `den_plugins` | consoles and cores from plugins (facade + fake for now) |
| `engine/` | `den_engine` | out-of-app engines (facade + fake for now) |
| `player/` | `den_player` | the libretro player (facade + fake for now) |
| `system/` | `den_system` | version and device information |
| `app/` | `den_app` (`emuden`) | main loop, registry, screens |

A directory never includes one listed below it. Each has its tests in `<dir>/tests`
(`den_<dir>_tests`).

## Data

The app keeps its data in a folder `user` next to the executable when that folder exists
(portable), otherwise in the OS app-data folder under `EmuDEN`.

## Licence

GPL-3.0-or-later, see [LICENSE](LICENSE). Google Sans (`assets/fonts`) is under the SIL Open
Font License, see `assets/fonts/OFL.txt`. `player/third_party/libretro/libretro.h` is MIT,
from upstream libretro.
