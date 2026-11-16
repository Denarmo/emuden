#!/usr/bin/env python3
"""Copies the official console manifests into the app's bundled copies.

The app compiles in assets/plugins/<id>/plugin.json (plugins/CMakeLists.txt) so consoles are
there before anything was downloaded. Their source is emuden-plugins/plugins/<id>/plugin.json.

    python tools/sync_bundled_plugins.py [--plugins <emuden-plugins>] [--check] [--skip <id> ...] [id ...]

Only ids already bundled are copied (all of them unless ids are given). --check copies
nothing and exits 1 when a bundled manifest differs from its source. Line endings are not
compared: git may check this repository out with CRLF.
"""

import argparse
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BUNDLED = ROOT / "assets" / "plugins"


def text(path: Path) -> str:
    return path.read_bytes().decode("utf-8").replace("\r\n", "\n")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--plugins", type=Path, default=ROOT.parent / "emuden-plugins",
                        help="the emuden-plugins checkout (default: beside this repository)")
    parser.add_argument("--check", action="store_true", help="report drift, copy nothing")
    parser.add_argument("--skip", action="append", default=[], metavar="ID",
                        help="leave this id alone (repeatable)")
    parser.add_argument("ids", nargs="*", help="only these bundled ids")
    args = parser.parse_args()

    source_root = args.plugins / "plugins"
    if not source_root.is_dir():
        print(f"no plugins folder in {args.plugins}", file=sys.stderr)
        return 2
    bundled = sorted(p.name for p in BUNDLED.iterdir() if (p / "plugin.json").is_file())
    unknown = [i for i in args.ids if i not in bundled]
    if unknown:
        print(f"not bundled: {', '.join(unknown)}", file=sys.stderr)
        return 2
    wanted = [i for i in (args.ids or bundled) if i not in args.skip]

    drift, missing = [], []
    for plugin_id in wanted:
        source = source_root / plugin_id / "plugin.json"
        target = BUNDLED / plugin_id / "plugin.json"
        if not source.is_file():
            print(f"{plugin_id}: no plugin.json in {source_root}", file=sys.stderr)
            missing.append(plugin_id)
            continue
        if text(source) == text(target):
            continue
        drift.append(plugin_id)
        if not args.check:
            target.write_bytes(source.read_bytes())
            print(f"{plugin_id}: copied")
    if args.check and drift:
        print("bundled manifests differ from emuden-plugins: " + ", ".join(drift), file=sys.stderr)
        print("run: python tools/sync_bundled_plugins.py " + " ".join(drift), file=sys.stderr)
        return 1
    return 1 if missing else 0


if __name__ == "__main__":
    sys.exit(main())
