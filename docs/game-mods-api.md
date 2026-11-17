# `den.mods`: plugins that set up a mod for a PC game

Design, 2026-11-17. For the Helix SR and LSFG plugins; nothing here is specific to them.

A feature plugin cannot write a file outside its own folder. Helix SR replaces a DLL in a
game's folder; LSFG (lsfg-vk) is a Vulkan layer in the user's home folder. `den.mods` is the
app doing those two things for a plugin, after the user agreed to a list of every file, with
the originals kept and an undo that the app owns.

- Capability `feature.mods.1`; permission `game.files`, official plugins only for now.
- Steam games only (`den.steam.status().apps`, not shortcuts). The game's folder is its
  `installdir` from `appmanifest_<appid>.acf` in whichever library folder holds it.
- Every call may wait. The changing calls need a press on the plugin's page first, like
  `den.steam.set`, and then show the app's own question.

## Calls

| Call | Takes | Gives |
|---|---|---|
| `den.mods.find{appid, names}` | up to 16 file names; one `*` allowed in a name; compared without case | `{ {path, size, sha256} }`: files of the game's folder with those names, paths relative with `/`, at most 64, folders at most 8 deep, no symlinks followed. `sha256` only for files up to 512 MiB |
| `den.mods.unpack{file, entries}` | an archive under `downloads/` (zip, or tar.xz); up to 32 entry paths | `{ [entry] = "cache/mods/<archive stem>/<entry>" }`; an entry at most 512 MiB; refused paths (`..`, absolute) are errors |
| `den.mods.apply{appid, name, files}` *(user)* | `name`: what the question calls the mod; `files = { {from, to} }`, `from` under `cache/` or `downloads/`, `to` relative in the game's folder | `true`, or `nil, "cancelled"`. The app lists each file as new or replaced, then copies. An earlier apply by this plugin to the game is undone first |
| `den.mods.state(appid)` | | `nil`, or `{name, time, files = { {to, intact} }}`; `intact` is false when the file is no longer the one the app wrote (a game update) |
| `den.mods.remove(appid)` *(user)* | | `true`. Files the app added are deleted; replaced files get their original back, but only where the file is still the one the app wrote |
| `den.mods.layer{name, layers}` *(user)* | Linux only. `layers = { {manifest, library} }`: Vulkan implicit layer manifests (JSON) and their libraries, under `cache/` | `true`. Libraries go to the app's data folder; each manifest is written to `$XDG_DATA_HOME/vulkan/implicit_layer.d/emuden-<plugin>-<file>` with `library_path` set to the copy |
| `den.mods.layer_state(name)` | | `nil`, or `{time, manifests = {..}, intact}` |
| `den.mods.remove_layer(name)` *(user)* | | `true` |

## Rules the app keeps

- `to` is checked after resolving: inside the game's folder, no `..`, no symlink on the way,
  at most 64 files and 1 GiB per apply.
- A manifest must be a Vulkan layer manifest with a `disable_environment`: a layer that could
  not be switched off per game is refused. Nothing but `library_path` is changed in it.
- The record of an apply is `<data>/mods/<plugin id>/<appid>/record.json`: for each file its
  path, the SHA-256 the app wrote, and the original's SHA-256 (or none). Originals are in
  `originals/` beside it. It is written before the first copy, so an apply cut short can be
  undone.
- Removing a plugin offers to remove what it applied; the records stay until then.
- Nothing here is synced, and nothing is ever written under a game folder but the listed files.
- `den.download` allows 512 MiB (not 256) for a plugin with `game.files`.
- A plugin never runs a program. A mod whose setup must run its own installer is out of scope.

## The two plugins

**`emuden.tune.helixsr`** (Windows, Linux). Helix SR is closed freeware that may only be passed
on unmodified, so the plugin downloads the official release, pinned by size and SHA-256
(upstream has replaced assets under one tag). First version: the FSR drop-in only. A game that
ships `amd_fidelityfx_upscaler_dx12.dll` gets it replaced by the release's prebuilt HLX FSR 4
library. Games with anti-cheat files (EasyAntiCheat, BattlEye, and the like) are refused. The
DLSS route needs upstream's own setup program and NVIDIA's library: not in this version.

**`emuden.tune.lsfg`** (Linux). lsfg-vk 2 (CC BY-NC-ND: passed on unmodified, never bundled)
is downloaded pinned, installed with `den.mods.layer`, and switched on per game through
`den.steam.set` with `LSFGVK_ENV=1 LSFGVK_MULTIPLIER=<n> ...` in front of `%command%`. The
user must own Lossless Scaling (app 993090) on its `lsfg-vk` beta branch; the plugin checks
for `lsfg-vk.dll` with `den.mods.find` and says what to do when it is missing. On Windows
Lossless Scaling is its own program; the plugin only says so.
