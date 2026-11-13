# Covers for ROM hacks

A ROM hack used to get the box of the game it was made from (Pokemon - Lazarus showed
Emerald's) or a placeholder. Now a game the console's box art collection does not know by
name is treated as a hack and goes through a chain of sources.

## Research (2026-11-13)

robots.txt of every site was fetched on that day; terms were read where a source looked
usable. "Checked" says what was actually looked at.

| Source | Machine-readable access | Terms / robots.txt on automated access | Images | Finding a hack by name | Used |
|---|---|---|---|---|---|
| libretro-thumbnails (thumbnails.libretro.com) | Directory listing per console, one file per game; no robots.txt | A public mirror made for frontends | Box art | A few hacks are listed ("Pokemon - Brown Version", "Pokemon - Glazed Version (Hack)", "... [Hack by X]"); of the test set only Brown | **Yes, no account** (the listing the app already caches) |
| Fan cover collections on GitHub (default `tttrash/pokehackcovers`) | GitHub's tree API (one request for the file list) and raw files | GitHub allows API use within its rate limits (60 requests an hour without an account). The repository has **no licence**: the app only downloads a picture to the user's device for display and redistributes nothing, as with libretro-thumbnails | Fan-made box art, 150 files in `gb/`, `gbc/`, `gba/` | File names ("Pokemon - Lazarus.png", "(alt)" variants) | **Yes, no account**; the list of repositories is a setting, so it can be replaced or emptied |
| RetroAchievements | Web API (`API_GetGameList`, `API_GetGame`), needs the user's web API key | robots.txt allows `/`; the API documentation asks to cache the game list aggressively | Box art (`ImageBoxArt`), title and in-game shots; only hacks with an achievement set ("~Hack~ ...") | Title in the console's game list | **Yes, with a key**. Not run against the live API: no key here |
| SteamGridDB | API v2 (`search/autocomplete`, `grids/game`), Bearer key from an account | robots.txt allows `/`; the API is the offered way in. The documentation page did not render in the fetch, so the endpoints are from the API's published v2 shape | Fan-made grids (600x900 and others) | Name search | **Yes, with a key**. Not run against the live API: no key here |
| Hackdex (hackdex.app) | None public; a Next.js site on Supabase (the site's code is on GitHub) | Terms (docs/legal/TERMS.md in its repository) forbid "automated tools (including bots, scrapers, crawlers, scripts...)" without written permission; robots.txt disallows `/api/`; the site answered this check with a bot checkpoint (HTTP 429) | Cover per hack, screenshots | Site search | **No**: forbidden by its terms. Asking Hackdex for permission would make it the best source |
| Whack a Hack (whackahack.com) | None found; a forum | robots.txt carries only content-signal terms, no path rules. Not checked further | Thread attachments, no box art as such | Forum search | **No**: no stable pages to read without crawling threads |
| GBAtemp | None; a forum | robots.txt disallows `/search/` and the download files | Thread attachments, screenshots | Only through the disallowed search | **No**: robots.txt |
| PokeCommunity (ROM hacks studio) | None; a forum | robots.txt disallows `/search/`, `/attachments/`, `/posts/` | Thread banners and screenshots (attachments) | Only through the disallowed search | **No**: robots.txt |
| romhacking.net | None | robots.txt disallows every `?page=` URL, which is how its database is searched | Title screen and in-game screenshots, no box art | `?page=hacks&title=...` (disallowed) | **No**: robots.txt, and no box art |
| RomHack Plaza | None found | robots.txt disallows `/database?`, its search | Screenshots (not checked further) | Database search (disallowed) | **No**: robots.txt |
| romhack.ing | None found | robots.txt disallows everything but the front page | - | - | **No**: robots.txt |
| LaunchBox games database | `Metadata.zip`, 110 MB (checked: `Content-Length: 109609772`), with a "ROM Hack" release type; images on its image host | robots.txt carries only content-signal terms. Terms of use not read | Box art, fan art, screenshots | In the downloaded metadata | **No, for now**: a 110 MB download (several hundred MB unpacked) on a handheld for a few covers. A candidate once the app has a metadata store |
| ScreenScraper | API v2, needs developer credentials of the software and a user account | No robots.txt (404). API rules not read | Box art, screenshots; hacks are flagged | Name or checksum | **No**: the app would have to ship developer credentials; nothing was registered |
| Pokemon ROM hack lists on GitHub | A search for repositories found the fan cover collection above; PokeROM Codex is a document, PkHax a site | - | - | - | Only the collection above |
| Patch files (IPS, UPS, BPS) | - | - | None: the formats carry no picture (BPS has a free-text metadata field, usually empty) | - | **No**: nothing to read |

## What was built

A game is a hack when `MatchThumbnail` finds no box under its name in the console's listing
(a "(Hack)" tag is not needed; a hack the listing does name, or an official release such as
"Pokemon - Edicion Cristal (Spain)", keeps the ordinary path). Checksums are not compared:
the app has no ROM database.

The chain (`Artwork::Resolve`, `library/src/artwork.cpp`):

1. An image beside the ROM or in the library's covers folder (the scanner, as before). A
   cover picked with "Find cover" is stored there, so it wins every later scan.
2. Sources without an account: the fan cover collections, then the hack entries of the
   libretro listing.
3. Sources with a key, only when one is entered and step 2 was not sure: RetroAchievements,
   then SteamGridDB.
4. The box of the game it was made from (`MatchBaseGame`: the ROM header or a table of
   known hacks), with a dark "HACK" tag drawn into its top left corner
   (`WriteHackMarkedCover`), so it is not taken for the original.
5. The drawn placeholder (a colour per console, the initials); tiles print the game's name
   over it. This step is unchanged.

Details:

- **Name matching** (`MatchHackName`, `library/src/hack_match.cpp`): case, accents,
  punctuation, tags in brackets, "~Hack~" and words like "Version" are ignored, so
  "Pokemon - Heart & Soul" is "Pokémon Heart and Soul (v1.2)". A title with more words than
  the file name is taken automatically only when the ROM's header names them ("Pokemon -
  Emerald Seaglass" for `Pokemon - Seaglass.gba`, header `POKEMON EMER`); a title with fewer
  words never is. Everything else is only offered in "Find cover". There is no tolerance
  for typos.
- **Sources** (`HackSources`, `library/src/hack_sources.cpp`): one request for a
  collection's file list (kept 7 days), one for a RetroAchievements console list (kept 30
  days), then one request per picture. Requests to one host are 700 ms apart, the client
  names itself `emuden-covers`, nothing is crawled. A game no source has is not asked about
  again for 7 days (`.miss`), a missing original for 14; a failed connection is retried at
  the next scan.
- **Cache**: `<data>/library/artwork/hacks/<system>/<name>.own.png|jpg` (the hack's own
  art), `.raw.png` and `.base.png` (the original and its tagged copy), `hacks/index` (the
  listings), `hacks/candidates` (pictures shown in "Find cover").
- **Threads**: all of it runs in the library's scan job and its cover threads, or in the
  job the "Find cover" page starts; nothing on the UI thread.
- **Settings** (Settings > Covers, `app/screens/details/find_cover.cpp`):
  `library.hack_covers.online` (on by default; off: steps 2 and 3 are skipped, covers
  already downloaded stay), `library.hack_covers.retroachievements_key`,
  `library.hack_covers.steamgriddb_key`, and `library.hack_covers.collections` (a list of
  GitHub `owner/repo`, no row yet). The keys are in `settings.json` in plain text.
- **Find cover** (a button on the details page of games run by a core, route `find_cover`):
  shows up to 8 candidates and the tagged original, "Choose an image file" and "Choose
  automatically". `Library::CoverChoices`, `SetCover`, `ResetCover`.
- Every scan now asks for the cover of each game without a local image again (answered
  from the cache), so a hack that had the original's box gets its own art once a source
  has it.

## The test library (D:\ROMS, live, 2026-11-13)

| Game | Cover | From |
|---|---|---|
| Pokemon - Brown (GB) | its own | fan collection ("Pokemon - Brown"); libretro has "Pokemon - Brown Version" too |
| Pokemon - Lazarus (GBA) | its own | fan collection |
| Pokemon - Heart & Soul (GBA) | its own | fan collection ("Pokemon - Heart and Soul") |
| Pokemon - Seaglass (GBA) | its own | fan collection ("Pokemon - Emerald Seaglass", sure through the header) |
| Pokemon - Polished Crystal (GBC) | its own | fan collection |
| Pokemon - Ambrosia (GBC) | Crystal's box, tagged HACK | no keyless source has it; RetroAchievements and SteamGridDB not asked (no key) |
| Pokemon - Crystal Awakening (GBC) | Crystal's box, tagged HACK | no keyless source has it; "Find cover" offers other Crystal hacks, none is taken automatically |
| Pokemon - Edicion Cristal (GBC) | the Spanish Crystal box | not a hack: libretro lists "Pokemon - Edicion Cristal (Spain)" |

## Not done

- RetroAchievements and SteamGridDB against the real services (unit-tested against the
  documented answers only).
- WebP pictures of the collections (the app's decoder has no WebP), a settings row for the
  list of collections, the source's name under a candidate in "Find cover".
- Checksum-based recognition, LaunchBox metadata, permission from Hackdex.
