# Feature plugins and the ROM hack plugin

Design for a second plugin kind (`feature`) next to console plugins, the patching module it
uses, the "Plugins" section of the rail, and the first feature plugin: installing ROM hacks
that Hackdex lists. Sections 3 to 9 are the contract between three build agents (A: feature
host, B: patching and library, C: screens). Header texts in section 9 are normative.

## 1. Hackdex: what was found (2026-11-13)

Sources: the site's open repository `Hackdex-App/hackdex-website` (MIT, commit `8da2b78`), its
`docs/legal/TERMS.md` (1.1.0, effective 2026-10-03) and three requests to `www.hackdex.app`
with a plain client name. The live pages could not be read (see below). No sign-in.

| Question | Answer |
|---|---|
| Public API | None. Next.js 15 App Router on Vercel, data in Supabase read by server components and server actions. No documented JSON endpoint, no RSS. `sitemap.xml` lists `/hack/<slug>` |
| Reaching it without a browser | `robots.txt` and `sitemap.xml` answered **HTTP 429 with a Vercel Security Checkpoint** (a JavaScript challenge). A plain HTTP client gets nothing; passing the challenge would be bot-detection bypass and is not done |
| robots.txt (from `src/app/robots.ts`) | `Disallow: /_next/ /api/ /account/ /auth/ /roms/` for every agent |
| Terms, section 8 | Forbidden: to "use automated tools (including bots, scrapers, crawlers, scripts, or similar software) to access, scrape, crawl, or systematically download content from the Service without Hackdex's prior written permission"; bulk downloading; redistributing patch files without the creator's or Hackdex's authorisation |
| Login | Not needed to browse, patch or download ("Registration is required only for Creators") |
| A hack entry | slug, title, summary, description (Markdown), base ROM id, version and custom version name, language, completion status (Complete, Demo, Alpha, Beta), tags by category, box art, cover images (public URLs on `images.hackdex.app`), original author, creator profile, social links, downloads count, AI disclosure, credits |
| A patch entry | id, version, format **`bps` or `xdelta` only**, changelog, `breaks_saves`, published date. xdelta patches are VCDIFF made by xdelta3 with LZMA secondary compression available |
| Base ROMs | 39 known dumps in `src/data/baseRoms.ts`: id, name, platform (GB 5, GBC 6, GBA 13, NDS 15), region, CRC32 and SHA-1. All Pokemon |
| Patch download | `GET /api/patches/<id>/download` answers 302 to a URL on `patches.hackdex.app` signed for 5 minutes. Only when the creator set the hack's download permission to `Current` or `All` (`None` = the site's in-browser patcher only). The route sits under the disallowed `/api/` and logs clients without a browser name or referer. `/api/download/<slug>/<ext>` is a honeypot |
| Name | The licence reserves the name "Hackdex" for hackdex.app: not to be used for derivative projects without permission |

**Consequence.** An in-app catalogue that fetches Hackdex pages or patch files is automated
access that the terms forbid without written permission, that robots.txt disallows, and that
the checkpoint blocks. So the plugin ships in two stages:

- **Stage 1, allowed today (what the agents build):** the user browses hackdex.app in their
  own browser (the plugin has an "Open Hackdex" button), downloads the patch file there
  ("Download Patch" on the hack's Versions page, when its creator allows it) and the plugin
  installs it: it finds the patch file, reads the base ROM checksum out of it, finds that ROM
  in the user's library, patches a copy and adds the hack to the library. The plugin makes
  **no request to hackdex.app at all**. It never downloads a ROM.
- **Stage 2, only with Hackdex's written permission:** browse, search and one-press install
  inside the app, against whatever endpoint Hackdex agrees to (section 8.3). Until then the
  manifest declares no Hackdex host, so the sandbox itself refuses such requests.

The plugin is named "ROM Hacks" (`emuden.romhacks`), not "Hackdex"; its base ROM table comes
from their MIT file with its notice. Open point for the owner: ask Hackdex
(admin@hackdex.app, or an issue on their repository) for stage 2 and the use of the name.

## 2. Overview

A feature plugin is a package from the signed index (`plugin.json`, `main.lua`, other `.lua`,
`icon.png`, data files). `plugins/` runs it: one Lua 5.4 state per running plugin on its own
worker thread, with the host API `den.*`. `patching/` holds formats and ROM checksums and
depends only on base. `library/` finds base ROMs and registers hacks. `app/screens/plugins/`
is the rail entry, the list, the permission page and the renderer of the UI model.

Directory order in the root `CMakeLists.txt` becomes `base platform patching gfx input ui
store library plugins ...`; `den_library` gains `den_patching`, `den_plugins` gains
`den_library den_patching`. Lua: `den_fetch(lua https://github.com/lua/lua.git v5.4.8)`,
built as the static target `den_lua` from every `l*.c` except `lua.c`, `ltests.c`, `onelua.c`,
compiled as C with warnings off.

## 3. Manifest (`plugin.json`, kind `feature`)

The common fields (`id`, `name`, `version`, `api`, `needs`, `publisher`, `license`, `summary`)
are those of console plugins. `needs` must contain `kind.feature`; `HostCapabilities()` adds
`kind.feature` and `feature.lua.1`. The `feature` object:

| Field | Type | Required | Meaning |
|---|---|---|---|
| `entry` | string | yes | Script to run, a safe relative path ending in `.lua` |
| `icon` | string | no | PNG in the package, square, at most 512 px |
| `permissions` | string[] | yes | From the list below; anything else refuses the manifest |
| `http.hosts` | string[] | with `http` or `download` | Exact host names, https only, no wildcards, at most 8 |
| `http.min_interval_ms` | number | no | Pause between requests to one host; default and minimum 500 |
| `open_hosts` | string[] | with `open-url` | Hosts `den.open_url` may open in the browser |
| `file_types` | string[] | no | Extensions the plugin takes from "open with" and the import picker |
| `systems` | string[] | with `library.*`, `patch` | System ids the plugin may see; `["*"]` only from the official source |
| `limits.memory_mb` | number | no | 8 to 64, default 32 |
| `limits.storage_mb` | number | no | The plugin's folder quota, default 512, at most 4096 |

Permissions (the sentence is what the install page shows):

| Permission | Grants | Shown as |
|---|---|---|
| `http` | `den.http.get` to `http.hosts` | "Connect to: host, host" |
| `download` | `den.download` into the plugin's folder from `http.hosts` | "Download files from: host, host" |
| `open-url` | `den.open_url` to `open_hosts` | "Open pages of host in your browser" |
| `files.import` | `den.files.import` (the user picks a file; it is copied in), `plugin.file` | "Read files you choose" |
| `downloads.watch` | `den.files.inbox()` lists files of `file_types` in the device's Downloads folder | "See patch files in your Downloads folder" |
| `library.read` | `den.library.find`, `games` for `systems` | "See which games of these consoles you have" |
| `patch` | `den.patch.*`; creates hacks in the library | "Create patched copies of your games" |
| `library.write` | `den.library.update`, `remove` on hacks this plugin made | "Change the hacks it installed" |

Always there, without a permission: `den.json`, `den.storage`, `den.ui`, `den.log`,
`den.files` on the plugin's own folder. Never there: `os`, `io`, `package`, `debug`,
`require`, `dofile`, `loadfile`, `load` with binary chunks, `collectgarbage` with arguments.
Loaded libraries: base (without those), `string`, `table`, `math`, `utf8`, `coroutine`.

Example (stage 1 of the ROM hack plugin):

```json
{ "id": "emuden.romhacks", "name": "ROM Hacks", "version": "1.0.0", "kind": "feature",
  "api": 1, "needs": ["kind.feature", "feature.lua.1"], "publisher": "EmuDEN",
  "license": "GPL-3.0-or-later", "summary": "Install ROM hack patches on your own games.",
  "feature": { "entry": "main.lua", "icon": "icon.png",
    "permissions": ["open-url", "files.import", "downloads.watch", "library.read", "patch",
                    "library.write"],
    "open_hosts": ["www.hackdex.app"], "file_types": ["bps", "xdelta", "ips", "ups"],
    "systems": ["gb", "gbc", "gba", "nds"] } }
```

## 4. Lua host API (`den`, api 1)

Calls marked (wait) suspend the script's coroutine until the host has the answer; the
script reads like blocking code and the UI never waits. Failing calls return `nil, message`.
Paths are relative to the plugin's data folder; `..` and absolute paths are errors.

| Call | Result |
|---|---|
| `den.api`, `den.plugin.id`, `den.plugin.version`, `den.device.platform` | constants |
| `den.log(text)` | to the app log, tagged with the plugin id |
| `den.json.decode(text)` / `den.json.encode(value)` | value / text; arrays are tables 1..n, JSON null is `den.json.null` |
| `den.storage.get(key)` / `set(key, value)` / `remove(key)` / `keys()` | JSON values, 1 MiB in total, written atomically |
| `den.http.get{url, headers?, cache_seconds?}` (wait) | `{status, body, headers, cached}`; body at most 8 MiB |
| `den.download{url, name, sha256?, on_progress?}` (wait) | `"downloads/<name>"`; at most 256 MiB; checked against `sha256` when given |
| `den.open_url(url)` | opens the system browser |
| `den.files.import{title?, extensions?}` (wait) | `"inbox/<name>"` after the user picked a file, or nil |
| `den.files.inbox()` | `{ {name, size, modified, token} }` in Downloads (newest first, 50 at most) |
| `den.files.take(token)` | copies that Downloads file to `"inbox/<name>"`; the original stays |
| `den.files.list(dir)` / `remove(path)` / `exists(path)` | the plugin's own folder |
| `den.files.read(path)` | text, at most 4 MiB (package files: `"package/<path>"`) |
| `den.include(path)` | runs another script of the package, returns its value |
| `den.patch.inspect(path)` (wait) | `{format, source_size?, source_crc32?, target_size?, target_crc32?}` |
| `den.library.find{system?, crc32?, sha1?, md5?, size?}` (wait) | game or nil; hashes the candidates once and remembers them |
| `den.library.games{system?}` | `{ {id, name, system, size, cover, hack_of?} }` |
| `den.library.hacks()` | hacks this plugin made: `{id, name, base, version, source, installed}` |
| `den.patch.apply{patch, game, title, version?, description?, cover?, source?, keep_old?}` (wait) | `{game, path}`: the new library entry |
| `den.library.update(id, {title?, description?, cover?})` / `den.library.remove(id)` | own hacks only; remove deletes the patched file |
| `den.ui.set(page)` / `push(page)` / `pop()` | replaces or stacks the plugin's page (section 5) |
| `den.ui.patch(id, fields)` | changes one element by id (progress, text, enabled) without rebuilding |
| `den.ui.toast(text)`, `den.ui.confirm{title, message, yes}` (wait) | - / boolean |
| `den.app.open_game(id)` | opens the game's details page |
| `den.sleep(ms)` (wait), `den.now()` | - / Unix seconds |

`cover` is a path in the plugin's folder, an https URL of an allowed host, or nil (the base
game's cover with a "Hack" label). The HTTP client sends `User-Agent: EmuDEN/<app version>
(plugin <id>/<version>)`, no cookies, no referer, follows redirects only inside `http.hosts`,
keeps at most two requests in flight per plugin and the per-host pause, honours `ETag` and
`Retry-After`, stops for 15 minutes on 429 and caches bodies in `cache/http` for
`cache_seconds` (default 3600, images 7 days).

The script returns a table of entry points; each runs in a fresh coroutine:

```lua
local plugin = { actions = {} }
function plugin.open(args) den.ui.set(home_page()) end   -- opened from the Plugins section
function plugin.file(path) install_patch(path) end       -- a file of file_types was opened
function plugin.actions.import()                         -- an element with action = "import"
  local path = den.files.import{ title = "Choose a patch", extensions = {"bps", "xdelta"} }
  if path then install_patch(path) end
end
function install_patch(path)
  local info, err = den.patch.inspect(path)
  if not info then return den.ui.toast(err) end
  local base = info.source_crc32 and den.library.find{ crc32 = info.source_crc32 }
  if not base then return den.ui.push(missing_base_page(info)) end
  den.ui.patch("status", { label = "Patching " .. base.name, fraction = -1 })
  local hack, why = den.patch.apply{ patch = path, game = base.id, title = title_of(path) }
  den.ui.toast(hack and "Installed. It is in your library." or why)
end
return plugin
```

`plugin.actions.<name>(arg, value)` gets the element's `arg` and, for inputs, chips and
search, the current value. Optional: `plugin.close()`, `plugin.resume()` (app back in front).

Limits: a callback is stopped with an error after 200 million VM instructions (count hook)
or when the allocator passes `limits.memory_mb`; time spent waiting does not count. A
plugin whose callback failed shows the message with "Close" and "Try again"; three failures
in a row stop it until it is opened again. Scripts are loaded as text only.

## 5. UI model

A page is a table (C++: `PageModel`). The app's renderer draws it with the UI kit and owns
focus, pad, touch, keyboard, scrolling and the hint bar. The accent stays the selection
border; a model has no colours, sizes or positions.

```
page    = { title, subtitle?, search?, chips?, body = { element... }, busy? }
search  = { placeholder, value?, action }           -- action gets the text on submit
chips   = { action, multi?, items = { {id, label, selected?} } }
element = one of, by `type`:
  shelf    { title, items = {tile...}, more_action?, arg? }
  grid     { items = {tile...}, columns?, more_action? }     -- more_action: end reached
  tile     { title, subtitle?, image?, aspect?, badge?, action, arg }
  detail   { title, subtitle?, image?, aspect?, screenshots? = {image...},
             facts? = { {label, value} }, text?, buttons = {button...} }
  text     { text, style? }          -- style: body, heading, caption, warning
  image    { image, aspect?, height? }
  button   { label, action, arg?, enabled? }
  row      { label, value?, image?, action?, arg? }          -- a settings-like list row
  input    { id, label, value?, action, secret = false }     -- opens the on-screen keyboard
  progress { id, label?, fraction }  -- 0..1, negative = unknown
  empty    { title, text?, button? }
```

Every element may carry `id` (needed for `den.ui.patch` and kept focus). `image` is
`"plugin:<path>"` (package or data folder), `"game:<library id>"` (that game's cover) or an
https URL of `http.hosts`; the host fetches and caches it, the renderer shows a placeholder
meanwhile. Limits: 400 elements a page, 8 pages deep, strings cut at 4000 characters.
Unknown types and fields are ignored, so later API versions can add some.

## 6. Lifecycle, updates, removal, trust

| Step | What happens |
|---|---|
| Listed | Feature plugins come through the same signed index as consoles. The entry's `kind` is `feature`; it also carries `permissions`, `http_hosts`, `open_hosts` and `systems`, so the list shows them before any download |
| Installed | The package is fetched, its SHA-256 and size checked against the index, and unpacked to `<data>/plugins/<id>/<version>/`. A package holds at most 200 files and 8 MiB, only `.json .lua .png .jpg .txt .md`. The granted permissions go to `grant.json` |
| Started | When the user opens it or a file is routed to it: a Lua state is created, `entry` runs, then `plugin.open`. One state per plugin; it stays while its page is on the stage and 60 s after, then `plugin.close` runs, waits in flight are cancelled and the state is dropped |
| Updated | A newer version in the index is installed beside the old one and used from the next start; the data stays. An update asking for more permissions, hosts or systems than `grant.json` holds waits until the user accepts the difference on the consent page |
| Removed | The package folders go. The page asks whether to delete the plugin's data too. Hacks it installed stay in the library (they are the user's games) unless the user ticks "also remove the hacks it installed" |
| Revoked | A revocation or blocked hash in a source's index stops the plugin at the next refresh, as for consoles |

Data folder `<data>/plugin-data/<id>/`: `storage.json`, `grant.json`, `downloads/`, `inbox/`,
`cache/`. Nothing a plugin writes is ever executed or loaded as a script.

| Source | Installing | Limits |
|---|---|---|
| Bundled / official (`Denarmo/emuden-plugins`, the app's key) | One press; the permissions are on the plugin's page | `emuden.` ids, `systems: ["*"]` allowed |
| Third-party source (the user added `github:owner/repo`, its own key) | A permission page first: publisher, source, every permission sentence, hosts by name; default focus on "Cancel" | Id must start with the source owner's namespace; no `["*"]`; `downloads.watch` refused |
| Developer folder (`<data>/plugins-dev/<id>/`, setting `plugins.developer`) | Loaded unsigned, marked "Developer"; same permission page; `den.log` also shown in a console row | For writing plugins; "Reload" restarts the script |

No plugin of any source gets: other hosts than declared, the file system outside its
folder, other plugins' data, ROM contents, save files, settings, processes or native code.
`den.patch.apply` writes only into the hacks folder and reads only the game it names.

## 7. Patching and hacks in the library

### 7.1 Formats (`patching/`)

| Format | Magic | Checks | Notes |
|---|---|---|---|
| IPS | `PATCH` | none | RLE records, the truncation extension after `EOF`; a 24-bit offset equal to `EOF` handled |
| UPS | `UPS1` | source, target and patch CRC32 | Applies in either direction; only source to target is used |
| BPS | `BPS1` | source, target and patch CRC32 | The four actions; metadata skipped |
| APS (GBA) | `APS1` | source and target size, CRC16 per 64 KiB block | The GBA flavour only (not the N64 `APS10`) |
| xdelta3 / VCDIFF | `D6 C3 C4` | Adler-32 of each target window | Decoder of xdelta3 v3.1.0 (`den_fetch`, Apache-2.0) with secondary compressors DJW, FGK and LZMA (xz v5.8.1, `liblzma` only); source window up to the whole ROM |
| PPF 1, 2, 3 | `PPF10/20/30` | v2/v3: optional 1024-byte block check at `0x9320`; v3 undo data skipped | PlayStation disc images (`.bin`, `.iso`); 64-bit offsets in v3 |

A patch is applied to a temporary file beside the target, then renamed; a failed check deletes
it. Inputs are mapped or streamed: at most the patch plus 64 MiB in memory (NDS: 512 MiB ROMs).

### 7.2 Checksums and headers

`RomChecksums(file, system)` gives CRC32, MD5 and SHA-1 of every form a patch may have
been made against:

| System | Forms |
|---|---|
| `nes` | `raw`; `headerless` (16-byte iNES/NES 2.0 header removed, `NES\x1A`); `fds` likewise for the 16-byte fwNES header |
| `snes` | `raw`; `headerless` when `size % 1024 == 512` (copier header) |
| `n64` | `raw`; `z64` (big-endian, the reference order) made from `.v64` (`37 80 40 12`, bytes swapped in pairs) or `.n64` (`40 12 37 80`, words reversed) |
| `lynx` | `raw`; `headerless` (64-byte `LYNX` header) |
| others | `raw` |

A base ROM matches when any form's checksum equals the wanted one; the patch is then
applied to that form and the removed header is put back in front of the result (N64: the
result stays `z64`). Zipped ROMs are hashed and patched through `library::PrepareRom`.
Checksums are cached in `<data>/library/checksums.json` by path, size and modification
time.

### 7.3 Hacks

`den.patch.apply` -> `Library::InstallHack`: the output is
`<data>/library/hacks/<system>/<title><ext of the base>`, a folder the library always scans
for that system. Beside it `<file>.hack.json`: `{title, version, description, base_id,
base_sha1, patch_sha256, plugin, source, installed}` and the cover as `<file>.cover.png`.
The file name has no version, so installing a newer patch replaces the file and the entry
keeps its id, saves and play time; with `keep_old` (a patch that breaks saves) the old file
is first renamed `<title> (<old version>)` and stays a separate game. The entry gets
`Entry::hack_of` (the base's id), the sidecar's title and description, and its own cover.
The base ROM is only read. A plugin can change and remove only entries whose sidecar names it.

## 8. The ROM hack plugin (`emuden.romhacks`)

### 8.1 Stage 1 pages

| Page | Content |
|---|---|
| Home | `text` with the three steps; buttons "Open Hackdex" (`den.open_url "https://www.hackdex.app/discover"`) and "Choose a patch file" (`den.files.import`); shelf "Found in Downloads" (`den.files.inbox()`, a tile per patch file, refreshed on `plugin.resume`); grid "Installed hacks" (`den.library.hacks()`); row "Base games": the 39 known base ROMs from the package's `bases.json`, each "In your library" or "Missing" (`den.library.find{sha1}`) |
| Install | After a tile or the picker: `den.patch.inspect`. BPS carries the base CRC32, so the base is found without a question. xdelta has no source checksum: the plugin tries the library's games that are in `bases.json`, six at most, and keeps the one whose target windows verify; with none, a list to choose from. Then a `detail`: title (from the file name, an editable `input`), base game, format, "Install". A missing base gives a page naming the game, region and checksums: "Add your own copy of this game to the library". Nothing offers to download it |
| Hack | A tile of "Installed hacks": "Play" (`den.app.open_game`), "Update from a file", "Rename", "Remove" |

On Android "open with" and share intents for `file_types`, and on desktop a file path on the
command line, reach `FeatureHost::RouteFile`. The plugin never requests anything from
hackdex.app or its storage hosts, never downloads a ROM and bundles no patches.

### 8.2 Stage 2 (needs written permission first)

Adds `http`, `download` and the agreed hosts to the manifest, and a `catalog.lua` adapter the
pages are written against: `list{query, base, tags, sort, page}` -> `{items = {slug, title,
summary, cover, base, version, downloads}, next}`, `detail(slug)` -> the fields of section 1
plus `patches = {id, version, format, changelog, breaks_saves}`, `patch_url(id)`. Its rules:
lists cached 6 h, details 24 h, covers 7 days; one request at a time, 1 s apart; a patch is
fetched only on "Install"; no prefetch, background refresh or mirror; a detail page shows the
credits and a link to the Hackdex page; "no patch download" hacks offer only "Open on
Hackdex". Because of the checkpoint Hackdex must provide an endpoint or allow the client.

## 9. Interfaces between the agents

Each owner commits its headers first, exactly as below; until then the others build against
a copy in their own tests folder, never in the owner's path. Done means: A's fixture scripts
cannot reach `io`, an undeclared host or a path outside the folder, and a runaway loop and
a memory hog are stopped; B's formats round-trip on generated bytes (no ROM in the
repository) and NES and N64 forms match known vectors; C's renderer shows every element
type by pad and touch against a fake host. Together: a BPS and a copy of its base ROM from
`D:\ROMS` in a scratch library, "Choose a patch file", and the hack starts.

| Agent | Owns | Also edits (one line each) |
|---|---|---|
| A | `plugins/include/den/plugins/feature_*.h`, `plugins/src/feature/`, `plugins/tests/feature_*`, `assets/plugins/emuden.romhacks/` | `cmake/deps.cmake` (Lua), `plugins/CMakeLists.txt` (deps, bundled scripts), `platform`: `pal::OpenUrl`, `pal::DownloadsDir` |
| B | `patching/`, `library/include/den/library/hacks.h`, `library/src/hacks*.cpp`, `library/tests/hacks_*` | root `CMakeLists.txt` (`patching`), `cmake/deps.cmake` (xdelta3, xz), `library/CMakeLists.txt`, `Entry::hack_of` in `library.h`, the scan of the hacks folder |
| C | `app/screens/plugins/` | nothing shared |

`cmake/deps.cmake` is appended to by A and B: each adds its block at the end and rebases.

### 9.1 `patching/include/den/patching/patch.h`, `checksum.h` (B)

```cpp
namespace den::patching {
enum class Format { Unknown, Ips, Ups, Bps, ApsGba, Vcdiff, Ppf };
struct PatchInfo {
    Format format = Format::Unknown;
    std::string format_name;                 // "bps", "xdelta", ...
    std::optional<std::uint64_t> source_size, target_size;
    std::optional<std::uint32_t> source_crc32, target_crc32;
};
Result<PatchInfo> Inspect(const std::filesystem::path& patch);
struct ApplyOptions {
    bool verify_source = true;                         // refuse a base that fails the patch's check
    std::function<bool(float fraction)> progress;      // returning false cancels
};
/// Writes `output` (temporary file, then rename). `source` is never written.
Status Apply(const std::filesystem::path& patch, const std::filesystem::path& source,
             const std::filesystem::path& output, const ApplyOptions& options = {});
struct Checksums { std::uint32_t crc32 = 0; std::string md5, sha1; std::uint64_t size = 0; };
struct RomForm {
    std::string form;                // "raw", "headerless", "z64"
    Checksums sums;
    std::uint64_t header_bytes = 0;  // bytes removed in front
    bool reordered = false;          // N64 byte order was normalised
};
/// Every form of section 7.2 for the system; the first is always "raw".
Result<std::vector<RomForm>> RomChecksums(const std::filesystem::path& rom, std::string_view system);
/// Writes that form of the ROM to `out` (for patching), and the header back (for the result).
Status WriteForm(const std::filesystem::path& rom, std::string_view system, std::string_view form,
                 const std::filesystem::path& out);
Status RestoreHeader(const std::filesystem::path& original, const RomForm& form,
                     const std::filesystem::path& patched);
std::string Sha1Hex(const void* data, std::size_t size);
}
```

### 9.2 `library/include/den/library/hacks.h` (B)

```cpp
namespace den::library {
struct BaseQuery {
    std::string system;                    // empty = every system in `systems`
    std::vector<std::string> systems;
    std::optional<std::uint32_t> crc32;
    std::string sha1, md5;                 // lower-case hex, empty = not asked
    std::optional<std::uint64_t> size;     // narrows the candidates before hashing
};
struct BaseMatch { Entry entry; std::string form; };
struct HackRequest {
    std::string base_id;                   // library entry to patch
    std::filesystem::path patch;
    std::string title, version, description, plugin, source;
    std::filesystem::path cover;           // empty = the base's cover, labelled
    bool keep_old = false;
};
struct Hack { std::string id, base_id, title, version, plugin, source; std::int64_t installed = 0; };
class Hacks {
public:
    virtual ~Hacks() = default;
    /// Blocking (hashes candidates, cached): call from a job or a worker thread.
    virtual std::optional<BaseMatch> FindBase(const BaseQuery& query, const Job* cancel = nullptr) = 0;
    virtual Result<Entry> InstallHack(const HackRequest& request,
                                      std::function<bool(float)> progress = {}) = 0;
    virtual std::vector<Hack> List(const std::string& plugin = {}) const = 0;
    virtual Status Update(const std::string& id, const std::string& plugin,
                          const std::optional<std::string>& title,
                          const std::optional<std::string>& description,
                          const std::filesystem::path& cover) = 0;
    virtual Status Remove(const std::string& id, const std::string& plugin) = 0;
};
/// The hacks of a library; owned by it, valid as long as it is.
Hacks& HacksOf(Library& library);
}
```

### 9.3 `plugins/include/den/plugins/feature_ui.h` (A; shared with C)

```cpp
namespace den::plugins {
enum class ElementType { Shelf, Grid, Tile, Detail, Text, Image, Button, Row, Input, Progress, Empty };
struct Fact { std::string label, value; };
struct Element {
    ElementType type = ElementType::Text;
    std::string id, title, subtitle, text, label, value, style, badge;
    std::string image;                  // as the script gave it; resolve with FeatureHost::ImagePath
    std::string action, arg_json;       // arg as JSON text, passed back untouched
    float aspect = 0, height = 0, fraction = -1;
    int columns = 0;
    bool enabled = true, secret = false;
    std::string more_action;
    std::vector<Fact> facts;
    std::vector<std::string> screenshots;
    std::vector<Element> items;         // shelf, grid: tiles; detail: buttons; empty: its button
};
struct Chip { std::string id, label; bool selected = false; };
struct PageModel {
    std::string title, subtitle;
    bool has_search = false;
    std::string search_placeholder, search_value, search_action;
    std::string chips_action; bool chips_multi = false; std::vector<Chip> chips;
    std::vector<Element> body;
    bool busy = false;
};
struct UiEvent { std::string action, arg_json, value; };   // value: input text, chip ids, search text
enum class AskKind { PickFile, Confirm, Toast, OpenGame, OpenUrl, Pop };
struct Ask {                            // something only the screen can do
    std::uint64_t id = 0; AskKind kind = AskKind::Toast;
    std::string title, message, yes, value;      // value: game id or URL
    std::vector<std::string> extensions;
};
}
```

### 9.4 `plugins/include/den/plugins/feature_host.h` (A; used by C)

```cpp
namespace den::plugins {
struct PermissionLine { std::string permission, text; };
struct FeaturePlugin {
    std::string id, name, version, publisher, summary, source_name;
    std::filesystem::path icon;         // empty until installed or fetched
    enum class Trust { Official, ThirdParty, Developer } trust = Trust::Official;
    bool installed = false, update_available = false, running = false;
    bool needs_consent = false;         // install or update asks for permissions not yet granted
    std::vector<PermissionLine> permissions, new_permissions;
    std::uint64_t data_bytes = 0;
    std::string error;                  // last script failure, user-facing
};
class FeatureHost {
public:
    virtual ~FeatureHost() = default;
    virtual std::vector<FeaturePlugin> Plugins() const = 0;      // installed first, then available
    virtual std::uint64_t Revision() const = 0;                  // list changed
    virtual JobPtr Install(const std::string& id, bool consent) = 0;  // fails without consent when needed
    virtual JobPtr Remove(const std::string& id, bool delete_data, bool delete_hacks) = 0;
    virtual Status Open(const std::string& id) = 0;              // starts the script, runs plugin.open
    virtual void Close(const std::string& id) = 0;               // the page left the stage
    virtual void Reload(const std::string& id) = 0;              // developer plugins
    /// The plugin's page stack, top last; `revision` moves on every change.
    virtual std::vector<PageModel> Pages(const std::string& id, std::uint64_t& revision) const = 0;
    virtual void Send(const std::string& id, UiEvent event) = 0; // never blocks
    virtual void Back(const std::string& id) = 0;                // pops a pushed page
    virtual std::vector<Ask> TakeAsks(const std::string& id) = 0;
    virtual void Answer(std::uint64_t ask_id, bool accepted, const std::string& value = {}) = 0;
    /// A local file for an element's image, or empty while it loads (the revision moves when ready).
    virtual std::filesystem::path ImagePath(const std::string& id, const std::string& image) = 0;
    /// A file opened with the app: the plugin id that takes it, or empty.
    virtual std::string RouteFile(const std::filesystem::path& file) = 0;
    /// Main thread, every frame: applies what the workers produced.
    virtual void Pump() = 0;
};
struct FeatureConfig {
    std::filesystem::path data_dir;
    Jobs* jobs = nullptr; store::Settings* settings = nullptr;
    Host* plugins = nullptr; library::Library* library = nullptr;
    std::string app_version;
};
std::unique_ptr<FeatureHost> CreateFeatureHost(const FeatureConfig& config);
}
```

A's sources are in `plugins/src/feature/` (sandbox, one `api_*.cpp` per `den.` table, runner,
http, host); its tests use fixture scripts, a fake library and a local HTTP fake.

### 9.5 `app/screens/plugins/` (C)

| File | What |
|---|---|
| `plugins_glue.cpp` | `DEN_REGISTER_STARTUP` creates the host (`FeatureHostSvc()`), `DEN_REGISTER_FRAME` calls `Pump`, `DEN_REGISTER_RAIL(60, "Plugins", "plugins")`; routes a file given on the command line |
| `plugins_screen.cpp` | route `plugins`: shelf "Installed" (tiles with the icon; activate opens), list "Available" (name, publisher, summary, source), row "Add a source"; Option on a tile: update, remove |
| `plugin_consent.cpp` | route `plugin.consent` (`id`): publisher, source, trust, each `PermissionLine`, "Install" and "Cancel" (focus on Cancel for third-party) |
| `plugin_view.cpp` | route `plugin.view` (`id`): `Open` on build, `Close` when popped; rebuilds when the revision moves, keeping focus by element id; Back sends `Back` until one page is left, then pops; serves `TakeAsks` (file browser `app::Pick`, `Stage::Confirm`, `Toast`, push `details`) |
| `model_render.cpp/.h` | `BuildModel(ui::Node* into, const PageModel&, Sink)`: shelf -> `MakeShelf` of `Tile`s, grid -> rows of tiles sized from the window, detail -> cover left, facts and text right, buttons below; search and input open the on-screen keyboard; progress drawn as a bar in the theme's text colour; `empty` centred |

C's fake host (`fake_host.h`, used when `EMUDEN_FAKE_PLUGINS=1`) serves fixed models.
