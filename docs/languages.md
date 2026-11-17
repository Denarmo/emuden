# Languages

English is built in. Every other language is a **language pack**: an interface plugin
(`languages.1`, format in `docs/language-packs.md` of emuden-plugins) that is downloaded like any
plugin, signed by its source. The first one is `emuden.lang-es` (Spanish).

## Where the language comes from

- Setting `ui.language`: `en` (or unset), or the code of a pack (`es`). Settings, Language
  (`app/screens/settings/language.cpp`) lists English and every pack of the plugin sources; a
  pack not yet installed is downloaded when chosen and applies once it is there. Removing the
  pack puts English back.
- First start: when `ui.language` was never set and the system's languages
  (`pal::PreferredLanguages()`, SDL's locale list; `EMUDEN_LOCALE=es-ES,en-US` stands in for it)
  match a pack of the plugin lists that is not installed, a question offers the download once
  (`ui.language_asked` remembers the answer, "Not now" included). `MatchLanguage` in
  `base/language.h` picks the pack: the exact code, else the same language.

## Texts

`Tr(key)` and `TrF(key, args...)` (`app/include/den/app/strings.h`) read the table
(`app/logic/string_table.h`): the pack's text, else the English one, else the key itself. A key is
either a dotted name from `assets/strings/en.json` or the English text itself, so
`TrF("Saved to slot {}", n)` needs no entry anywhere and a pack translates it by that text. Use a
whole sentence with `{}` fields, never a text joined from pieces: word order differs by language.
A pack text whose `{}` fields differ from the English text's is ignored.

The UI kit also translates what it draws (`ui/include/den/ui/translate.h`, set by the app): labels,
row labels, descriptions, choices and values, buttons, dialogs, toasts, hints and the text entry.
A literal English text given to those widgets is therefore translated without `Tr`. Dynamic text
(a game's title, a path) does not match any key and stays as it is.

`python tools/extract_strings.py --missing <pack>/strings.json` lists the English texts in the code
that a pack lacks.

## Games

- A core that asks the frontend's language (libretro `GET_LANGUAGE`) gets it from the app
  language (`RetroLanguage` in `base/language.h`).
- A core component of a console manifest can name its language option and a value per language:
  `"language": {"option": "melonds_language", "values": {"en": "English", "es": "Spanish", "*": "English"}}`.
  The host gives it the app's language before the manifest's `option_hints`, `forced_core_options`,
  the settings that set core options and the player's own options, so any of those wins: a core
  option the user set stays theirs.
