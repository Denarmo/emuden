#!/usr/bin/env python3
"""Lists the English texts in the UI code that the string table translates.

Usage: python tools/extract_strings.py [--missing PACK.json]
Prints every string literal in app/, ui/ and player/ that looks like text a person reads
(starts with a capital letter, no dots-and-ids, not a log line). With --missing, only those
the pack does not translate. Used to keep a language pack complete.
"""
import json
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
DIRS = ["app", "ui", "player", "input", "plugins", "sync", "achievements", "library", "system", "update"]
LITERAL = re.compile(r'"((?:[^"\\\n]|\\.)*)"')
SKIP_LINE = re.compile(r'log::|#include|CHECK|REQUIRE|std::format\("[a-z]|Warn\(|Debug\(|ParseJson|R"')


def looks_like_text(text: str) -> bool:
    if len(text) < 2 or not text[0].isupper():
        return False
    if re.fullmatch(r"[A-Z0-9_]+", text):  # CONSTANT_NAME
        return False
    if re.search(r"[a-z]", text) is None:
        return False
    if "/" in text and " " not in text:
        return False
    if re.fullmatch(r"[A-Za-z0-9]+[.:][A-Za-z0-9_.:]+", text):
        return False
    return True


def unescape(text: str) -> str:
    """The text a C++ literal holds: \xE2\x80\xA6 style bytes are UTF-8, so they are read as that."""
    if "\\" not in text:
        return text
    raw = text.encode("latin-1", "backslashreplace").decode("unicode_escape")
    try:
        return raw.encode("latin-1").decode("utf-8")
    except (UnicodeEncodeError, UnicodeDecodeError):
        return raw


def collect() -> dict:
    found = {}
    for directory in DIRS:
        for path in (ROOT / directory).rglob("*.cpp"):
            rel = path.relative_to(ROOT).as_posix()
            if "/tests/" in rel or "/src/android/" in rel or "third_party" in rel:
                continue
            for number, line in enumerate(path.read_text(encoding="utf-8", errors="replace").splitlines(), 1):
                stripped = line.strip()
                if stripped.startswith("//") or SKIP_LINE.search(line):
                    continue
                for match in LITERAL.finditer(line):
                    text = unescape(match.group(1))
                    if looks_like_text(text):
                        found.setdefault(text, []).append(f"{rel}:{number}")
    return found


def main() -> None:
    found = collect()
    if len(sys.argv) > 2 and sys.argv[1] == "--missing":
        pack = json.loads(pathlib.Path(sys.argv[2]).read_text(encoding="utf-8"))
        found = {k: v for k, v in found.items() if k not in pack}
    for text in sorted(found):
        print(json.dumps({"text": text, "at": found[text][0]}, ensure_ascii=False))


if __name__ == "__main__":
    main()
