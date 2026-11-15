# Sync between devices

Saves, save states, settings and games move between the user's devices (Windows PC, Retroid,
Legion Go S) through a sync plugin: Google Drive, RomM or Syncthing. EmuDEN owns the sync
engine; a plugin only stores and fetches files. This is the contract between the host work
(sections 2 to 6) and the three plugins (section 7).

## 1. Rules

- **What syncs**, each a category the user turns on per provider: `saves` (battery saves),
  `states` (save states and their thumbnails, resume state included), `config` (settings,
  per-game settings and play time), `games` (an online library of the user's games: each goes
  up once, and downloads on demand), `bios` (off by default; only what the user put in
  EmuDEN's BIOS folder).
- **Never synced:** consoles of group `pc` (Steam and other PC launchers), their library
  entries and anything an engine app keeps itself. Google Drive never receives PC or Steam
  data in any form (owner's decision, 2026-11-12). Caches, logs, the plugin folders, signing
  material, credentials and the web view's data never sync.
- Nothing is ever deleted on a device because it is missing remotely; a remote delete is only
  sent for a file the user deleted in EmuDEN (a state slot cleared, for instance).
- Game folders the user added may be read only (the PC's `D:\ROMS`). Downloaded games go to a
  folder the user picks once per device (default `<data>/games`), which is added to the
  library.

## 2. Portable names

Remote files have the same name on every device, built from what does not depend on where a
game lives:

| Category | Remote path |
|---|---|
| saves | `saves/<system>/<rom stem>.<ext>` (`.srm`, and other save files a core writes) |
| states | `states/<system>/<rom stem>.slot<N>.state`, `.slot<N>.png`, `<rom stem>.resume.state` |
| config | `config/settings.json`, `config/settings-overrides.json`, `config/games/<system>/<rom stem>.json`, `config/playtime/<device id>.json` |
| games | `games/<system>/<rom file name>` (multi-file games: the index file and its members) |
| bios | `bios/<file name>` |

`<rom stem>` is the ROM's file name without its extension as it is on disk, matched case
insensitively. A local game is found by system plus stem; two games of one system with the
same stem (rare) sync under the first and the conflict is logged. Device-only settings
(display, paths, input devices, the sync settings themselves) are cut out of
`settings.json` before upload and kept when one is applied (a list in `sync/`).

## 3. Engine (`sync/`, C++)

- A **catalog** lists the local files of the enabled categories with portable name, size,
  modification time and SHA-256 (hashed only when size or time changed since the last run).
  Games are known by their size (`size:<bytes>`) and never hashed.
- A **journal** per provider (`<data>/sync/<provider id>/journal.json`) remembers, per
  portable name, the hash and remote revision of the last version both sides agreed on. It
  is written (atomically) after every transfer and at least every second otherwise, so a run
  cut short by the app closing keeps what it did and the next run makes no false conflicts.
- A **plan** compares catalog, journal and the provider's listing:
  local changed only → upload; remote changed only → download; both → conflict; new on one
  side → copy to the other. Deletes only as in section 1.
- **Order:** the play time files (small; they name the other devices), saves, then states,
  settings and BIOS files; a game that just ended goes first.
  Once these are done the run counts as a sync: the last sync time and the games' "Saves
  synced" marks are set then, not when the game uploads end.
- **Conflicts:** a file both sides changed is fetched and compared by content first: the
  same content is agreed on, and a remote copy that is still the one both last agreed on (its
  rev moved without a change) means only this device changed it. Only then is it a conflict:
  before a game starts the user chooses (section 3.3); otherwise the newer file by modification time wins; the loser is kept beside it as
  `<name>.conflict-<device>-<yyyymmdd-hhmmss>` (locally under `<data>/sync/conflicts/`) and
  the sync page lists it with "Use this one instead". A remote copy is named after the other
  device when only one other is known (from the play time files), else "another device".
  Settings files never make a conflict copy: changed on both devices, they are merged key by
  key against the last version both agreed on (kept under `<data>/sync/<provider>/base/`);
  a key changed here keeps this device's value, any other takes the remote one, and the merge
  goes up. Fractions are written with 9 significant digits, so a value read and written again
  does not make the file differ.
- Downloads land in a temporary file and replace the target atomically; a save or state of a
  game that is running is never replaced (it waits for the session to end).
- **When:** on app start, before a game starts and when it ends (that game's own sync,
  section 3.3, then the rest), on "Sync now", and every 15 minutes while the app is open
  (setting). Games only download
  when the user asks; uploads of games happen only when the user turns on "Upload my games"
  on that device.
- One provider at a time is the active one; the others can be set up but stay idle.
- Status for the UI: last sync time, files up and down, conflicts, errors, "N of M", the
  current file with its percent and size (uploads too, when the provider reports them), and
  the game uploads still to do.

### 3.1 Games: an online library

The `games` category is not a two-way sync. The remote is a library of the user's games that
every device can download from:

- A game goes up once, and only from a device with "Upload my games" on. It never goes up
  when the remote has its portable name (taken as the same game when the size matches, and
  the journal entry rebuilt from the listing), when the journal knows it (uploaded or
  downloaded before), however its file's time changes, and a game downloaded from the
  library never goes back.
- Nothing is deleted: a game deleted on a device stays in the library, and one gone from the
  library stays on the devices.
- Uploads are a pass of their own after the regular sync, one game at a time. The pass stops
  (it does not fail) when a game starts, when another run is asked for (a game ended, "Sync
  now", a download or "Use this one"), or when the app ends; the timer does not stop it. The
  next run syncs saves first and then goes on with the games. Google Drive resumes a cut
  upload where it stopped, also after a restart (section 7.1).
- "Your game library" (route `sync.games`) lists everything on the remote, per console, with
  "On this device" for what this device has.

### 3.2 Play time

Play time (the library's seconds, plays and last play) syncs in `config` without ever
counting a session twice:

- Each device has an id of its own (16 hex digits, `sync.device_id`, made once; not its
  name). It uploads only its own record, `config/playtime/<device id>.json`:
  `{"version": 1, "device": "<name>", "games": {"<system>/<rom stem>": {"seconds", "count",
  "last_played"}}}`, from what it played itself.
- The other devices' files are kept as they came under `<data>/sync/playtime/` and added up
  into the library, which keeps them apart from its own record (`library.json`,
  `other_seconds`, `other_count`, `other_last_played`). An entry shows its own time plus the
  others', and the latest of the last plays. A new file from a device replaces that device's
  part, so syncing again adds nothing.
- Games of `pc` consoles are never in these files.

### 3.3 A game's own sync

Before a game of the player starts (with `saves` or `states` on), its saves and states alone
are checked against the remote (`Engine::CheckGame`); the screen shows "Checking for newer
saves…" with Skip, and after 3 s, offline or skipped, the game starts as it is and the check
writes nothing more (`CheckGate`). Only the game's system folders are listed
(`list("saves/<system>/")`) where the provider does that quickly: the folder store, and
Google Drive from 1.0.4; else its categories.

- Changed only online since the last sync: downloaded, no question.
- Changed only here: the game starts; it goes up when the game ends.
- Changed on both sides (different content): "Which save do you want?" names the device the
  online copy came from (the other device that played the game last, by the play time files)
  and both times, with "Use the online save" and "Keep this device's save"; Back does not
  start the game. The other copy is kept as a conflict copy (section 3), and either way the
  journal takes the online copy as agreed on, so a kept local save simply goes up at the end.

When the game ends, its saves and states (and this device's play time file) go up at once on
a job of their own (`Engine::SyncGame`), then the regular run follows.

Neither waits for a run's game uploads: the journal and the save, state and settings files
have a lock of their own (`files_mutex_`), held by a run's regular part but not by its game
uploads. A run's regular part gives the lock to a waiting game between two files and leaves
out what the game's sync did meanwhile.

## 4. HTTP for plugins (platform and `den.http`)

`pal::HttpRequest` gains `method` (GET, POST, PUT, PATCH, DELETE, HEAD), `body` (in memory)
or `body_file` (streamed upload) with `body_offset` and `body_length` (a slice of the file),
`follow_redirects` and `loopback_any_certificate`; `HttpResponse` gains response `headers`
(names in lower case, of the last answer after redirects). All three backends (WinHTTP,
curl, Android) implement them.

`den.http.request{...}` (wait) returns `{status, body, headers}` for every answer the server
gave, 4xx and 5xx included; `nil, message` only when there was no answer (refused address,
no connection, a limit). Fields:

| Field | Meaning |
|---|---|
| `url` | required; see the hosts below |
| `method` | GET (default), POST, PUT, PATCH, DELETE, HEAD; any letter case |
| `headers` | a table of name and value; `Authorization` may be set (it may not for `den.http.get`); `User-Agent`, `Cookie`, `Referer`, `Host`, `Content-Length` are the host's |
| `body` | text (binary is fine), at most 8 MiB |
| `body_file`, `body_offset`, `body_length` | upload a file, or `body_length` bytes of it from `body_offset` (0 or absent: to its end); for resumable uploads |
| `multipart` | a list of at most 16 parts the host builds the body of: `{name, value}` or `{name, file, filename?, content_type?}`; any part may add `headers`; `body`/`body_file` are accepted for `value`/`file`. The host sets `Content-Type: multipart/<type>; boundary=...` |
| `multipart_type` | `form-data` (default; RomM), `related` (Google Drive's multipart upload, whose parts carry `content_type` and no `name`) or `mixed` |
| `to_file` | the answer's body goes to this file (only for a 2xx answer; `body` is then absent) |
| `on_progress` | a function called with the received fraction 0..1 (negative: size unknown) |

Give one of `body`, `body_file` and `multipart`. `body_file`, `to_file` and a part's `file`
are paths of the plugin's own folder, or the `local_path` the host passed to a provider
call (`"sync:<token>"`, valid during that call only). `to_file` into the plugin's folder
counts against its storage limit; a lent file has none. `den.http.get` stays as it is.

`den.files.read(local_path)` reads a lent file too, as it is (binary-safe), up to 8 MiB.
`den.files.size(local_path)` gives its size in bytes, of any size (the total a resumable
upload's `Content-Range` names); it works on the plugin's own files as well.

Hosts: `http.hosts` as before (https only), plus two permissions:
- `http.user_host`: the plugin may reach one https or http host the user typed on its setup
  page (`den.sync.set_server(url)` asks the user to confirm it; shown as "Connect to the
  server you enter"). Only the URL's scheme, host and port are kept (its path is not), and
  every path of that origin may be requested. A RomM server on the home network is plain
  http; the confirm page says so.
- `http.local`: `http://` and `https://` to `127.0.0.1:<port>` and `localhost:<port>`; the
  port is required. Over https any certificate is accepted there (Syncthing's
  self-signed one), never for another host. Shown as "Connect to apps on this device".

A sync provider may set `http.min_interval_ms` down to 100 (others: 500): a game's own sync
makes a few requests in a row and the user waits for them. Requests to the user's server and
to this device have no pause between them and their
redirects are not followed: a 3xx comes back as it is (`headers.location`). Requests to
`http.hosts` follow redirects and keep the per-host pause of `den.http.get`.

Answer headers: names in lower case; a header that came several times is one value joined
with `", "`.

## 5. Provider plugins (`den.sync`, permission `sync`)

A feature plugin with `sync` in `permissions` and `"sync": {"categories": [...]}` in its
`feature` object is a provider. `categories` is required with `sync`, may list only
`saves`, `states`, `config`, `games` and `bios`, and none twice; `feature.sync` without the
`sync` permission refuses the manifest. The install page shows "Keep these in step between
your devices: saves, save states, settings, games, BIOS files" for what it lists. The
plugin returns, beside the usual entry points:

```lua
plugin.provider = {
  -- Remote files under the provider's EmuDEN folder: { {path, size, modified, rev} }.
  -- prefix is one of the category folders ("saves/", ...); nil lists everything.
  -- modified is the file's own time as put() gave it (Unix seconds), not the upload time.
  list = function(prefix) end,
  -- Writes the remote file into local_path (a temporary file the host made).
  -- progress(done, total) may be called (or passed on as den.http.request's on_progress,
  -- whose one argument, a fraction, it also takes).
  get = function(path, local_path, progress) end,
  -- Uploads local_path as path; returns the new rev (text or number). rev is the one the
  -- journal knows (nil for a new file), for providers that can refuse a write over a newer
  -- remote version (nil, "conflict"). modified is the local file's time, Unix seconds:
  -- keep it and give it back in list().
  -- progress(done, total) may be called with the bytes sent so far. A long upload should look
  -- at den.sync.status().stopping between its steps and give up with (nil, "cancelled")
  -- when it is set: the engine then goes on with it in a later run.
  put = function(path, local_path, rev, modified, progress) end,
  delete = function(path) end,
  -- Optional: free space and who is signed in, for the sync page.
  status = function() end,
}
```

Paths are portable names (section 2). The host calls these from the sync engine's job, one
at a time per provider, without opening the plugin's page (the script is started for it and
kept 60 s after the last call), and treats `nil, message` or an error as a failure of that
file (retried on the next run). A `rev` the provider leaves empty is taken as
`<size>@<modified>`.

`den.sync`, present only with the `sync` permission:

| Call | Result |
|---|---|
| `den.sync.status()` | the table below |
| `den.sync.now()` | `true`, or `nil, message` (the plugin is not the active provider) |
| `den.sync.enabled_categories()` | the categories on for this provider on this device, `{"saves", ...}` |
| `den.sync.folder()` | the folder store's absolute path, or `nil` |
| `den.sync.use_folder(path?)` (wait) | the absolute path, or `nil, message`. Without `path` the app's folder picker opens. `"~/..."` is under the user's home on a desktop and the shared storage's root on Android; other paths must be absolute. A path the plugin names is confirmed by the user ("Keep the sync files here?"). Refused: a drive's root, EmuDEN's data, a game folder of the library |
| `den.sync.set_server(url)` (wait) | `true` once the user confirmed it, or `nil, message` (`"declined"`). Needs `http.user_host`. The page warns when the address is plain http. Confirming the same server again asks nothing |
| `den.sync.download_game(paths)` (wait) | `true`, or `nil, message`. `paths` (a list, or one text) are portable names under `games/`: the index file and its members. The host calls `provider.get` for each into `<games folder>/<system>/<file name>` and adds that folder to the library with its console |

`den.sync.status()` (fields the plugin does not know are to be skipped):

```lua
{ active = true,            -- this plugin is the active provider on this device
  running = false,          -- a run goes on
  last_sync = 1794700000,   -- end of the last run that reached the provider, 0 = never
  done = 3, total = 10,     -- files of the current (or last) run
  uploaded = 2, downloaded = 1,
  conflicts = 0,            -- conflict copies kept, not yet dealt with
  waiting = 0,              -- files of a running game, left for when it ends
  errors = { "saves/gba/Game.srm: ..." },  -- of the last run, at most 20
  error = "saves/gba/Game.srm: ...",       -- the first of them, absent when none
  current = { path = "saves/gba/Game.srm", fraction = 0.5 },  -- absent when idle
  folder = "/storage/emulated/0/Sync/EmuDEN" }                -- with the folder store
```

`den.json.encode` of an empty table gives `[]` (a table without a first element and without
other keys is an empty array); use `den.json.decode("{}")`'s value, or a table with a key,
for an object.

Credentials (tokens, API keys, RomM password) go to `den.secrets.get/set/remove(name)`:
like `den.storage` (any JSON value) but kept in the platform's credential store (Windows
Credential Manager, Android Keystore-encrypted file, a 0600 file on Linux) and never
synced. Always there, no permission needed. A name is up to 64 letters, digits, `.`, `_`
or `-`; a value at most 2 KiB as JSON; at most 32 per plugin. Removing a plugin with its
data removes its secrets.

A provider without a remote store of its own (Syncthing) uses the host's **folder store**:
`den.sync.use_folder(path)` (the user picks the folder; on Android a path under shared
storage) makes the host keep the remote side as plain files in that folder; `list/get/put`
are then not needed from the plugin. The folder store skips files and folders starting with
`.` (Syncthing's `.stfolder`, `.stversions`, temporary files) and `~syncthing~*`, and refuses
a write over a file that changed since the journal saw it (a conflict on the next run).

Host settings, all kept on the device: `sync.active` (provider id), `sync.categories.<id>`,
`sync.folder.<id>`, `sync.upload_games`, `sync.games_folder` (default `<data>/games`),
`sync.interval_minutes` (15; 0 = only at start, game end and Sync now).

For trying a provider: put it in `<data>/plugins-dev/<id>/` (plugin.json, main.lua), set
`"plugins.developer": true` in `<data>/settings.json` and start the app; it is listed under
Plugins as "Developer" and asks for its permissions before it first runs. `<data>` is the
`user` folder next to the program when it exists (portable), else `%APPDATA%\EmuDEN`, the
XDG data folder or the app's files on Android.

## 6. Screens

Settings > Sync: the active provider, the categories (per device), "Sync now", last sync,
conflicts, "Games" (the game library: download a game from the remote, per console, with
sizes) and "Upload my games" (off). A game's details page shows "Saves synced" and the conflict, if any.

As built (`app/screens/sync/`): the section's rows are "Sync with" (Off or a provider),
"Set up <provider>" (opens the plugin's page), "Sync now" (at the right the last sync, or
"Syncing N of M" and "Uploading game N of M" while it runs; below, live, the current file
with its percent and size, else the first error, the files waiting for a game, the games
still to upload or the last run's counts), "Conflicts" (route `sync.conflicts`: each kept copy,
Confirm is "Use this one instead", Option deletes the copy), one toggle per category the
provider offers, then with games on "Games" (route `sync.games`), "Upload my games" and
"Downloaded games go to", the folder store's "Sync folder" when there is one, and "Every 15
minutes". The engine is `sync/` (`den/sync/engine.h`); settings sync merges (remote values
win key by key, keys only this device has stay); settings met for the first time on two
devices are merged rather than a conflict.

## 7. The three plugins (official source, `Denarmo/emuden-plugins`)

### 7.1 `emuden.sync.gdrive`: Google Drive

- OAuth 2.0 **device flow** (`oauth2.googleapis.com/device/code`, `/token`), scope
  `https://www.googleapis.com/auth/drive.file` only: EmuDEN sees just the files it created.
  The page shows the code and `google.com/device`; the refresh token goes to `den.secrets`.
- The client id and secret are those of a "TVs and Limited Input devices" OAuth client the
  owner creates in Google Cloud; until then the page says sync is not set up.
- Files live in a visible `EmuDEN` folder in My Drive with one subfolder per category; `rev`
  is the Drive `version` (or `md5Checksum`); uploads are multipart under 5 MiB, resumable
  above, in 16 MiB chunks with progress after each. The resumable session is kept in the
  plugin's storage (`upload:<path>`, with the file's size and time), so an upload cut by the
  app closing, a game starting or the network goes on from the offset Google reports, in
  the same run or a later one; a session Google dropped (after a week) starts once more.
- `list("<category>/<system>/")` (1.0.4) searches that one folder, whose id is kept from the
  last full listing, instead of listing everything.
- Hosts: `oauth2.googleapis.com`, `www.googleapis.com`. Categories: all of section 1, and the
  host refuses any PC/Steam file to this provider regardless.

### 7.2 `emuden.sync.romm`: RomM

- The user enters the server address (`http.user_host`) and signs in with their RomM user
  (token from `/api/token`; refresh kept in `den.secrets`, the password never stored).
- `saves` and `states` map to RomM's saves and states of the matching ROM (platform slug plus
  file name); `games` lists the server's ROMs per platform and downloads them; `config` and
  `bios` are not offered (RomM has no place for them).
- Check the endpoints against the RomM version the server reports (`/api/heartbeat`) and the
  RomM source at that tag; refuse versions older than the one tested, with a message.

### 7.3 `emuden.sync.syncthing`: Syncthing

- Uses the folder store: the user picks a folder (default `Sync/EmuDEN` in shared storage or
  the user's home); Syncthing replicates it between devices.
- Optional setup through Syncthing's REST API on this device (`http.local`, API key the user
  pastes from Syncthing's settings, kept in `den.secrets`): shows the device id, adds the
  folder to Syncthing with id `emuden` and shares it with chosen devices, shows the folder's
  completion. Without Syncthing on the device the page says how to get it (Syncthing on
  Windows/Linux, Syncthing-Fork on Android) and still works as a plain folder.
- All categories.
