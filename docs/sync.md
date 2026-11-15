# Sync between devices

Saves, save states, settings and games move between the user's devices (Windows PC, Retroid,
Legion Go S) through a sync plugin: Google Drive, RomM or Syncthing. EmuDEN owns the sync
engine; a plugin only stores and fetches files. This is the contract between the host work
(sections 2 to 6) and the three plugins (section 7).

## 1. Rules

- **What syncs**, each a category the user turns on per provider: `saves` (battery saves),
  `states` (save states and their thumbnails, resume state included), `config` (settings and
  per-game settings), `games` (ROM files, download on demand), `bios` (off by default; only
  what the user put in EmuDEN's BIOS folder).
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
| config | `config/settings.json`, `config/settings-overrides.json`, `config/games/<system>/<rom stem>.json` |
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
- A **journal** per provider (`<data>/sync/<provider id>/journal.json`) remembers, per
  portable name, the hash and remote revision of the last version both sides agreed on.
- A **plan** compares catalog, journal and the provider's listing:
  local changed only → upload; remote changed only → download; both → conflict; new on one
  side → copy to the other. Deletes only as in section 1.
- **Conflicts:** the newer file by modification time wins; the loser is kept beside it as
  `<name>.conflict-<device>-<yyyymmdd-hhmmss>` (locally under `<data>/sync/conflicts/`) and
  the sync page lists it with "Use this one instead".
- Downloads land in a temporary file and replace the target atomically; a save or state of a
  game that is running is never replaced (it waits for the session to end).
- **When:** on app start, when a game session ends (that game's saves and states first), on
  "Sync now", and every 15 minutes while the app is open (setting). Games only download
  when the user asks; uploads of games happen only when the user turns on "Upload my games"
  on that device.
- One provider at a time is the active one; the others can be set up but stay idle.
- Status for the UI: last sync time, files up and down, conflicts, errors, the current file
  with progress.

## 4. HTTP for plugins (platform and `den.http`)

`pal::HttpRequest` gains `method` (GET, POST, PUT, PATCH, DELETE, HEAD), `body` (in memory)
or `body_file` (streamed upload), and `HttpResponse` gains response `headers`. All three
backends (WinHTTP, curl, Android) implement them.

`den.http.request{url, method?, headers?, body?, body_file?, to_file?, on_progress?}` (wait)
returns `{status, body?, headers}`; `body_file` and `to_file` are paths the host gave the
plugin for this sync (section 5) or the plugin's own folder. `den.http.get` stays as it is.

Hosts: `http.hosts` as before, plus two permissions:
- `http.user_host`: the plugin may reach one https or http host the user typed on its setup
  page (`den.sync.set_server(url)` asks the user to confirm it; shown as "Connect to the
  server you enter"). A RomM server on the home network is plain http; the confirm page says
  so.
- `http.local`: `http://127.0.0.1:<port>` and `http://localhost:<port>` (Syncthing's REST
  API). Shown as "Connect to apps on this device".

## 5. Provider plugins (`den.sync`, permission `sync`)

A feature plugin with `sync` in `permissions` and `"sync": {"categories": [...]}` in its
`feature` object is a provider. It returns, beside the usual entry points:

```lua
plugin.provider = {
  -- Remote files under the provider's EmuDEN folder: { {path, size, modified, rev} }.
  -- prefix is one of the category folders ("saves/", ...); nil lists everything.
  list = function(prefix) end,
  -- Writes the remote file into local_path (a temporary file the host made).
  get = function(path, local_path) end,
  -- Uploads local_path as path; returns the new rev. rev is the one the journal knows,
  -- for providers that can refuse a write over a newer remote version (nil, "conflict").
  put = function(path, local_path, rev) end,
  delete = function(path) end,
  -- Optional: free space and who is signed in, for the sync page.
  status = function() end,
}
```

Paths are portable names (section 2). The host calls these from the sync engine's job, one
at a time per provider, and treats `nil, message` as a failure of that file (retried on the
next run). `den.sync.status()`, `den.sync.now()` and `den.sync.enabled_categories()` let the
plugin's page show and start syncing; `den.sync.set_server(url)` is section 4.

Credentials (tokens, API keys, RomM password) go to `den.secrets.get/set/remove`: like
`den.storage` but kept in the platform's credential store (Windows Credential Manager,
Android Keystore-encrypted file, libsecret or a 0600 file on Linux) and never synced.

A provider without a remote store of its own (Syncthing) uses the host's **folder store**:
`den.sync.use_folder(path)` (the user picks the folder; on Android a path under shared
storage) makes the host keep the remote side as plain files in that folder; `list/get/put`
are then not needed from the plugin.

## 6. Screens

Settings > Sync: the active provider, the categories (per device), "Sync now", last sync,
conflicts, "Games" (download a game from the remote, per console, with sizes) and "Upload my
games" (off). A game's details page shows "Saves synced" and the conflict, if any.

## 7. The three plugins (official source, `Denarmo/emuden-plugins`)

### 7.1 `emuden.sync.gdrive`: Google Drive

- OAuth 2.0 **device flow** (`oauth2.googleapis.com/device/code`, `/token`), scope
  `https://www.googleapis.com/auth/drive.file` only: EmuDEN sees just the files it created.
  The page shows the code and `google.com/device`; the refresh token goes to `den.secrets`.
- The client id and secret are those of a "TVs and Limited Input devices" OAuth client the
  owner creates in Google Cloud; until then the page says sync is not set up.
- Files live in a visible `EmuDEN` folder in My Drive with one subfolder per category; `rev`
  is the Drive `version` (or `md5Checksum`); uploads are multipart under 5 MiB, resumable
  above.
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
