# library

The games on this device. Screens use `den/library/library.h` only; the player also uses
`content.h`.

| Header | What |
|---|---|
| `library.h` | `Library`: folders, `Scan()` (a job: find games, then covers), entries, records, `SetSystems`, `ImportFolders` |
| `content.h` | `PrepareRom(entry, unpack_dir)` (unpacks a zipped ROM once), `DiscSetName`, `DiscSet`, `ReadRomHeaderTitle` |
| `scan.h` | `ScanFolders`, `RomId`, Switch 2 packages (`ReadSwitch2Package`: program id, title, icon) |
| `systems.h` | `SystemDef` (a manifest's `systems[]` entry), `ParseSystems`, `SystemTable` |
| `names.h` | `ParseGameName`, `CleanDisplayName`, `MatchKey`, `RegionTags` |
| `artwork.h` | box art matching, `Artwork::Resolve`, `WritePlaceholderCover` |
| `hacks.h` | `HacksOf(library)`: `FindBase` (a game by CRC-32, MD5 or SHA-1), `InstallHack`, `List`, `Update`, `Remove`; `OpenPatchFile` ("Patch a game") |
| `hack_covers.h` | `MatchHackName`, `HackSources`, `HackCoverOptions` |

- **Consoles.** `src/systems_data.cpp` holds the `systems[]` of the official plugin manifests,
  so the library works with no plugin installed. The plugin host (or the app) hands the
  installed manifests' systems to `Library::SetSystems`; they replace or extend the table.
  The library does not include `plugins/`. Switch 2 is not in the built-in table: it comes
  only from its plugin, installed by hand, and its scanner (`switch2`) runs only then.
- **Folders.** A folder holds console folders (`gba`, `PSX`, `Game Boy Advance`, `SWITCH 2`:
  id, alias or name) and ROM files whose extension names one console. A folder named like a
  console, or added with a system, is that console's folder. No folder is added by default.
- **Ids.** `<system>:<hash of the path below the console's folder>`, so they survive renaming
  or moving the library folder. Switch 2: `switch2:<program id>`.
- **One entry per game.** A playlist stands for its discs, a cue sheet for its images, a zip
  is listed only when the game is not there unpacked, and regional versions of a game are one
  entry (`Entry::other_versions`; order in the setting `library.region_order`).
- **Covers.** An image next to the ROM (or in `media`, `covers`, `boxart`, `images`, or
  `<data>/library/covers/<system>/`) named like the ROM file, or like the game without its
  tags (`Pokemon Lazarus.jpg` for `Pokemon - Lazarus (v2.1).gba`: a hack keeps its own picture), the icon inside a Switch 2 package, else the libretro
  thumbnail collection (cached under `<data>/library/artwork`; a miss is not asked again for
  two weeks), else a drawn placeholder (`Entry::placeholder_cover`). A game the collection
  does not know by name is a hack: its own art from the hack sources (`hack_covers.h`: fan
  collections on GitHub, libretro's hack entries, RetroAchievements and SteamGridDB with a
  key), else the box of the game it was made from with a HACK tag. `CoverChoices`,
  `SetCover` and `ResetCover` are behind "Find cover". See `docs/covers.md`.
- **Records.** `RecordPlay` adds a session to the log (`Sessions(since)`, the last 5000) and
  to the game's totals; time the log already holds for another game inside the session (a
  game opened over this one) is left out. `Entry::added` is when the library first saw the
  game: the file's date for the games of the first scan, the scan's time after that
  (`RecentlyAdded`).
- **Files.** `<data>/library/library.json` (folders, play records, session log; versioned, see
  `den/store/document.h`) and `cache.json` (the last scan, shown at once on the next start).
- **Hacks.** `InstallHack` patches a copy of a game (`patching/`) into `Hacks` in the console's
  folder, or into `<data>/library/hacks/<system>/Hacks` when the game is a loose file or the
  folder cannot be written. `<rom>.hack.json` beside it holds the title, version, base game,
  plugin and source; the entry has `hack_of` and appears without a scan. The file is named by
  the title, so a newer patch keeps the id, the saves and the records (`keep_old`: the new
  version becomes `<title> (<version>)` beside the old one). Checksums of the user's games
  are computed once and kept in `checksums.json` (path, size, date). A cue sheet's first image
  is the file that is patched; the result is that image alone.
- **Import.** On the first run the game folders of an earlier app's profile are taken over
  when `Config::import_profile` or the environment variable `EMUDEN_IMPORT_PROFILE` names it.

`den_library_tests "[.live]"` scans `D:/ROMS` (or `EMUDEN_TEST_ROMS`) and prints what it finds.
