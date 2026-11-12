# EmuDEN rebuild: build plan

Based on the inventory, the three proposals and the verdicts, plus a read of `docs/emuden-plugins.md` and the `src/emuden` licence headers. Nothing was built, run or changed.

## 1. Decision

- **Lean Native**: one C++20 binary on SDL3 with its own UI kit; two of three judges chose it.
- It is the only option where a feature, the quick menu and sign-in each exist once for all platforms, with no bridge or second toolkit.
- It has the smallest plausible start-up, memory and package size, and the player owns its swap chain.
- Grafted: stage 0 device spikes with kill criteria, in-process player by default, protocol levels, per-feature registration units, measured baselines.
- Assumption (the owner's sentence leaves it open): pure-logic `src/emuden` files written for EmuDEN are ported only after a per-file audit for fork snippets; everything else is new.

## 2. Repository

**Name:** `Denarmo/emuden`, namespace `den::`. No `yuzu`, `eden`, `lichii` or `eglantine` identifiers; CI greps for them.

```
base/      results, json, hashes, zip, log, jobs (progress, cancel)
platform/  pal.h interfaces + Caps; sdl/ win/ linux/ android/ null/
gfx/       device (GL 3.3 / GLES 3; Vulkan later), batcher, text, images
input/     pads, families, label mapping, glyphs, tuning, nav events
ui/        nodes, layout, focus, theme, widgets, semantic tree, automation
store/     SQLite schema, migrations, sync class per table
library/   scan, identity, names, versions, rules, artwork, metadata, PC stores
plugins/   manifest, index, sources, installer, ledger, settings schema, add-ons
online/    device-flow sign-in, Drive, RetroAchievements, RomM
engine/    protocol/, host/, fake/
player/    libretro host, session, rewind, shaders, pacing, netplay, menu model
system/    backup, storage, updates, diagnostics, child mode, power, upkeep
app/       main, router, screens/<feature>/, pages/ (rows as data)
shells/    android/ windows/ linux/
contracts/ engine protocol, manifest, index and add-on schemas (toolkit-free)
engine-sdk/ C library an engine links; includes the null engine
assets/ tests/ tools/ (relay, importer, signer, shader converter)
```

**Build:** CMake presets (`win`, `linux`, `android`) with Ninja and clang everywhere. vcpkg manifest mode with a pinned baseline; Gradle only wraps the Android preset.

| Dependency | Licence | Fetched by |
|---|---|---|
| SDL3 | zlib | vcpkg |
| SQLite | public domain | vcpkg |
| FreeType | FTL | vcpkg |
| HarfBuzz | MIT | vcpkg |
| SheenBidi | Apache-2.0 | FetchContent, pinned commit |
| libcurl (desktop only) | curl | vcpkg |
| zstd, LZ4 | BSD | vcpkg |
| zlib, libpng | zlib/libpng | vcpkg |
| libjpeg-turbo, libwebp | BSD | vcpkg |
| monocypher (Ed25519) | BSD-2/CC0 | vendored single file |
| rcheevos | MIT | FetchContent, pinned |
| PDFium | BSD-3 | prebuilt, pinned hash |
| SPIRV-Cross, glslang (build tools) | Apache-2.0/BSD | vcpkg |
| Catch2 | BSL-1.0 | vcpkg |
| `libretro.h` | MIT | from upstream libretro, not the fork |
| Google Sans | confirm OFL for the exact family | `assets/fonts` |

**Licence of new code:** GPL-3.0-or-later. The `src/emuden` files carry "Lichii Emulator Project, GPL-3.0-or-later" headers (checked on `plugins/manifest.h`). `contracts/` and `engine-sdk/` are MIT so any engine may link them.

**Coding rules:**
- A directory never includes one listed below it.
- No OS `#ifdef` outside `platform/`, `shells/` and gfx backends; CI enforces it.
- No blocking work on the main thread; all I/O runs as jobs.
- Files stay under about 600 lines, and each module carries its tests.
- `#00ffa7` exists only in the selection-border component; a lint fails any other use.
- Strings are keys in `assets/strings/en.json`; CI fails on missing keys.
- Screens, routes and schema rows register from their own files, collected by build globbing, so parallel work never edits a shared file.

**The single rule:** a feature is core logic plus a row or a kit-built screen under `app/`. It never touches `shells/`, and device differences are `Caps` queries. CI builds all three targets and runs the feature's automation script on each.

## 3. Shells

| Platform | Contains | Never contains |
|---|---|---|
| Windows | Packaging, DPAPI secrets, power broadcast, job objects, speech, update swap | Screens, settings, library or player logic |
| Linux/SteamOS | AppImage, Secret Service with file fallback, logind lock, SteamOS power helpers, gamescope properties, speech | The same, and no Game Mode-only screens |
| Android | One SDL activity, SAF folder grants, Keystore, platform HTTP, Presentation surface, engine intent/service client, speech | Kotlin screens, logic over JNI, required Vulkan, NFC, PiP |

The store-scan parsers stay in `library/`, compiled where they apply.

## 4. ROM player

- **Hosting:** in-process by default, one window, menu drawn over the game in the same swap chain. An `emuden-corehost` helper speaks the engine protocol for crash-prone GPU cores, headless probing and netplay tests. On Android it is a service in its own process inside the APK.
- **Threads:** the session thread owns the core, GL context, swap and pacing. The main thread publishes menu and overlay display lists. One typed command queue goes in and one atomic snapshot comes out. Start is asynchronous.
- **Frames:** uploaded in native format with the channel swap in the first shader pass. GPU cores render to an FBO in a shared context with a fence-synced three-texture swap. Capture uses PBO readback, so GPU cores get it too.
- **Shaders:** bundled presets are converted at build time from slang sources to GLSL 330 and ESSL 300, with no compatibility profile.
- **Pacing:** the four modes and their fallbacks stay, timed from measured swap completion. Waits use timers, with no yield spin on battery.
- **Audio:** a lock-free ring, an SDL callback and one drift controller.
- **Rewind:** the session thread only serialises into a preallocated slot. A worker does block delta plus LZ4, the interval adapts to a 5% frame budget, and state size is cached.
- **Menu:** 12 pages as data with one gating function (hardcore, netplay, GPU core, capability).
- **Per-core knowledge:** option keys, DS layout, texture-pack rules and the GPU flag move into manifests.
- **Netplay:** the lockstep and relay code are ported over one BSD/Winsock wrapper, with a default relay.

## 5. Switch 2 engine contract

**Protocol:** versioned, length-prefixed JSON with a capability hello. Level 0 is "start and wait for exit" (external emulators); the Switch 2 engine implements level 2.

- **One-shot commands:** `describe`, `schema`, `status`, `install-*`, `verify`, `precache-shaders`, `list-addons`, `save-info`, `export-saves`, `import-saves`.
- **App to engine:** `launch` (content, roots, resolved settings by key, add-on order, input plan with device identity and chord, locale, environment), `pause`, `resume`, `set_setting`, `screenshot`, `hud_level`, `notify`, `stop`.
- **Engine to app:** `loading`, `first_frame`, `running`, `paused`, `stopped` (typed error), performance samples, add-ons applied and skipped, `menu_request` with the paused frame, `applet_request` (text, error).
- **App-owned:** play time, power profiles, play limits and child mode, driven by session events. One fixed user.

**Quick menu, every platform:** the engine pauses and sends the frame, and the app comes forward and draws the menu over it. On resume the engine ignores held buttons. The HUD and toasts are engine-drawn through an SDK helper.

| Platform | Process and window |
|---|---|
| Windows | Child in a job object, pipes, own borderless window; the app hides at `first_frame` |
| Linux desktop | Same; a Wayland activation token travels in `launch` and `resume` |
| SteamOS | Child in the app's process tree; the app unmaps and remaps its window; the external-overlay property is optional |
| Android | Separate signed APK with a run activity and bound service; package name and certificate digest pinned in the signed index |

**Smallest change in the fork** (it stays in its own repository):

1. New target `emuden-nx2` built from the SDL frontend (`src/yuzu_cmd`), linking `engine-sdk`.
2. A protocol thread mapping messages to the existing calls (load, pause, stop, screenshot).
3. A schema export that walks the existing settings registry.
4. One-shot commands wrapping the loader, firmware check, content manager and patch manager.
5. Fixed user, no profile selector.
6. An SDK-drawn HUD and paused-frame export.
7. Android: the inherited frontend cut down to one activity and one service.

The Qt frontend there is left alone.

## 6. Plugin and add-on contracts

These carry over unchanged from `docs/emuden-plugins.md`, so `Denarmo/emuden-plugins` keeps working:

- `.emuplugin` zips with `plugin.json` (sections 2.1 to 2.9): ids, `systems[]`, `components[]`, platform keys such as `windows-x86_64`, settings schema, BIOS, `sync`, launch template.
- `index.json` with the detached Ed25519 `index.json.sig`, `addons.json`, `cores.lock.json`, release-asset naming, `trusted_component_sha256`, `revoked`, `blocked_sha256`.
- Source forms (`github:owner/repo@ref`), the two compiled-in official keys, trust-on-first-use for other sources, SHA-256 and size pinning, `integrity: "unpinned"` shown as nightly.
- `.emuaddon` packages and their `addon.json`.

Additive only, behind the format-evolution rules of section 1.3:
- `android-arm64` and `android-x86_64` targets.
- An out-of-process engine component (executable or APK with a certificate digest), replacing the section 3.4 in-process library.
- Optional per-core blocks for enhancements, DS layout and texture packs.

## 7. Data migration

`tools/import-profile` is also offered at first run. It copies and never edits the old profile.

- **Library and shelves:** folder lists, `shelves.ini`, `library.ini`, `playtime.ini`, `stats.ini` and wishlist are mapped from paths to content ids by hashing the files, with a preview of unmatched rows.
- **Settings:** `player.ini`, `power.ini`, `startup.ini` and `netplay.ini` go into the layered store with their sync class. Android SharedPreferences are exported by a last release of the old app.
- **Saves and states:** battery saves, slots with sidecars and resume states are copied to content-id paths. States keep their core tag and are offered only for the same core.
- **Cores and plugins:** installed plugins and the core ledger are re-verified by hash and adopted; failures are downloaded again.
- **Switch 2:** saves stay in the engine's data root, which the engine plugin adopts.

## 8. Stages

**Stage 0: skeleton and proofs (1-2 weeks).**
- Repository, presets, CI (root).
- Interface headers (`platform/`, `gfx/`, `ui/`, `contracts/`).
- Baselines of today's app on three devices (`docs/baseline`).
- gamescope window hand-over spike on the Legion Go S (`spikes/handover`).
- Android spike: surface loss, Presentation, touch scrolling, engine service Surface (`spikes/android`).
- Audit of `src/emuden` files for porting (`docs/port-audit`).

Done: Google Sans text in a window on three targets, with go/no-go numbers recorded.

**Stage 1: running app (3-4 weeks).**
- `gfx/`; `ui/`; `input/`.
- `store/` plus `library/` scan; `library/artwork`.
- `plugins/` minimal with a pinned Game Boy core.
- `player/` for software cores.
- `shells/`.
- `tests/` plus the automation endpoint.
- `app/screens/home` and `details`.

Done: one binary per platform from one tree shows the real library and plays a Game Boy game with sound and pad.

**Stage 2: consoles, settings, engine boundary (4-5 weeks).** Covers Settings, Controllers, Consoles, Setup, Window.
- `plugins/` sources, installer and 20 manifests.
- `app/pages` settings component.
- `ui/` keyboard, dialog, file browser.
- `platform/android` SAF and secrets.
- `engine/` plus the fake engine and `emuden-corehost`.
- `app/screens/setup`, `controllers`, `consoles`.

Done: any console installs from GitHub and plays, with no host dialog.

**Stage 3: full player (5-6 weeks).** Covers every Engine, Player, Pause menu, Overlays, Picture, Capture, Input, Speed, States and Gating area, plus the Android second screen. One agent per `player/` subfolder: `hw`, `shaders`, `rewind`, `pacing`, `menu`, `content`, `devices`, `screen`, `capture`.

Done: desktop player parity on three targets, within the pacing and rewind budgets.

**Stage 4: library and appearance (4-5 weeks, parallel with 3).** Covers Home, Search, Details, Library menu, Library data, Manuals, Appearance, Accessibility, Languages, Notifications, Gallery. One agent per `app/screens/<feature>` with its `library/` module.

Done: every inventory row in these areas has a passing automation script.

**Stage 5: online and system (4-5 weeks, parallel).** Covers Online, Netplay, System, Family, Power (sleep).
- `online/drive`, `online/ra`, `online/romm`.
- `player/netplay`.
- `system/backup`, `system/update`, `system/family`, `system/upkeep`.

Done: real-account checks pass on three devices.

**Stage 6: Switch 2 engine (6-10 weeks, after stage 2).** Covers Engine contract and Add-ons.
- Fork-repository items 1 to 7 from section 5.
- App: quick menu, loading screen, schema-driven console page, add-ons screens, engine packaging.

Done: a game starts, pauses into the app's menu and exits on all three.

**Stage 7: PC, Steam, handheld (3-4 weeks, parallel).** Covers PC and Steam, Power profiles, Startup, Remote play.
- `library/pc`.
- `system/tweaks`, `system/steam`, `system/remote`, `system/power`.

Done: parity on Windows and SteamOS.

**Stage 8: hardening (4-6 weeks).** AccessKit, Vulkan backend, video recording, CI budgets, x86_64 and release Android builds, importer sign-off. Legacy views, the More menu and the profile selector are dropped by decision.

## 9. Tooling to install on the build PC

- vcpkg with a pinned baseline and a binary cache.
- SDL3 Android Java sources (via vcpkg).
- Gradle 8.13 wrapper and JDK 21, for `shells/android` only.
- In WSL: clang, Ninja, `linuxdeploy`/`appimagetool`, Mesa llvmpipe for headless golden tests.
- Mesa llvmpipe for Windows, for the same tests.
- glslang and SPIRV-Cross (via vcpkg).
- Android emulator x86_64 image.
- Python 3 for the signer and importer scripts.

Qt 6.8.3 is not used.

## 10. Three biggest risks

1. **The UI kit is wrong or late.** Its API is frozen only after the stage 1 home screen ships. Kill criterion: if touch scrolling or text quality on the Retroid is unacceptable at the end of stage 1, switch `ui/` to Qt Quick. `base` through `player` stay toolkit-free, so only `ui/` and `app/` are redone.
2. **Window hand-over under gamescope and Android.** Proven in stage 0 with a dummy child. The player is in-process by default, so only engines depend on it; the fallback is engine-drawn menus through the SDK helper.
3. **Switch 2 engine extraction, never built for Android.** It starts right after stage 2 against the frozen contract and fake engine, PC first. The app ships without it, since "not installed" is a normal state.