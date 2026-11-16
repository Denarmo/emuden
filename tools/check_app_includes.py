#!/usr/bin/env python3
"""Fails when a feature folder of app/screens includes another one's files.

Each folder under app/screens is one feature. What features share lives in
app/include/den/app/ (included as "den/app/..."); a feature reaches another through those
headers and the registration hooks (den/app/registry.h, den/app/player_hooks.h), never
through "../<other feature>/...".

    python tools/check_app_includes.py
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SCREENS = ROOT / "app" / "screens"
INCLUDE = re.compile(r'^\s*#\s*include\s*"(\.\.[^"]*)"', re.M)


def main() -> int:
    found = []
    for path in sorted(SCREENS.rglob("*")):
        if path.suffix not in (".cpp", ".h"):
            continue
        text = path.read_bytes().decode("utf-8", errors="replace")
        for match in INCLUDE.finditer(text):
            line = text.count("\n", 0, match.start()) + 1
            found.append(f"{path.relative_to(ROOT).as_posix()}:{line}: includes {match.group(1)}")
    for item in found:
        print(item, file=sys.stderr)
    if found:
        print("a feature includes another feature's files: move what they share to app/include/den/app/",
              file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
