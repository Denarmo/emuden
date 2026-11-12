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

- **Consoles.** `src/systems_data.cpp` holds the `systems[]` of the official plugin manifests,
  so the library works with no plugin installed. The plugin host (or the app) hands the
  installed manifests' systems to `Library::SetSystems`; they replace or extend the table.
  The library does not include `plugins/`.
- **Folders.** A folder holds console folders (`gba`, `PSX`, `Game Boy Advance`, `SWITCH 2`:
  id, alias or name) and ROM files whose extension names one console. A folder named like a
  console, or added with a system, is that console's folder. No folder is added by default.
- **Ids.** `<system>:<hash of the path below the console's folder>`, so they survive renaming
  or moving the library folder. Switch 2: `switch2:<program id>`.
- **One entry per game.** A playlist stands for its discs, a cue sheet for its images, a zip
  is listed only when the game is not there unpacked, and regional versions of a game are one
  entry (`Entry::other_versions`; order in the setting `library.region_order`).
- **Covers.** An image next to the ROM (or in `media`, `covers`, `boxart`, `images`, or
  `<data>/library/covers/<system>/`), the icon inside a Switch 2 package, else the libretro
  thumbnail collection (cached under `<data>/library/artwork`; a miss is not asked again for
  two weeks), else a drawn placeholder (`Entry::placeholder_cover`).
- **Files.** `<data>/library/library.json` (folders, play records; versioned, see
  `den/store/document.h`) and `cache.json` (the last scan, shown at once on the next start).
- **Import.** On the first run the game folders of an earlier app's profile are taken over
  when `Config::import_profile` or the environment variable `EMUDEN_IMPORT_PROFILE` names it.

`den_library_tests "[.live]"` scans `D:/ROMS` (or `EMUDEN_TEST_ROMS`) and prints what it finds.
