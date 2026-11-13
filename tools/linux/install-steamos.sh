#!/bin/sh
# Installs the AppImage for one user on SteamOS (or any desktop Linux). No root needed.
#   install-steamos.sh [--portable] [--steam] [EmuDEN-x86_64.AppImage]
# Copies the AppImage to ~/Games/EmuDEN, takes the icon out of it and adds a desktop entry to
# the application menu and the desktop. Running it again updates the copy.
#   --portable  keep the app's data in ~/Games/EmuDEN/user instead of ~/.local/share/EmuDEN
#   --steam     also add it to Steam as a non-Steam game (needs the desktop session)
# A Steam shortcut of this AppImage, new or already there, gets EmuDEN's cover, banner,
# logo and icon; Steam shows them after it restarts.
set -eu

portable=0
steam=0
source=""
for arg in "$@"; do
    case "$arg" in
        --portable) portable=1 ;;
        --steam) steam=1 ;;
        -h|--help) sed -n '2,10p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        -*) echo "unknown option: $arg" >&2; exit 2 ;;
        *) source="$arg" ;;
    esac
done

here="$(cd "$(dirname "$0")" && pwd)"
if [ -z "$source" ]; then
    for candidate in "$here/EmuDEN-x86_64.AppImage" "$here/../../dist/linux/EmuDEN-x86_64.AppImage" \
        "$PWD/EmuDEN-x86_64.AppImage"; do
        if [ -f "$candidate" ]; then source="$candidate"; break; fi
    done
fi
[ -n "$source" ] && [ -f "$source" ] || { echo "no AppImage: pass its path" >&2; exit 1; }

dest="$HOME/Games/EmuDEN"
image="$dest/EmuDEN-x86_64.AppImage"
data="${XDG_DATA_HOME:-$HOME/.local/share}"
mkdir -p "$dest" "$data/applications" "$data/icons/hicolor/scalable/apps"

# Copied beside the old file and renamed over it, so a running copy is not cut short.
if [ "$(readlink -f "$source")" != "$(readlink -f "$image")" ]; then
    cp "$source" "$image.new"
    chmod 755 "$image.new"
    mv -f "$image.new" "$image"
else
    chmod 755 "$image"
fi
[ "$portable" = 1 ] && mkdir -p "$dest/user"

# The icon and the Steam artwork are inside the AppImage; unpacking them needs no FUSE.
icon="$data/icons/hicolor/scalable/apps/emuden.svg"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
(cd "$work" && "$image" --appimage-extract 'usr/share/emuden/steam/*' >/dev/null 2>&1) || true
art="$work/squashfs-root/usr/share/emuden/steam"
if [ -f "$art/icon.png" ]; then
    # Steam draws no SVG icons: the menu entry, and so the Steam shortcut, use the PNG.
    mkdir -p "$data/icons/hicolor/256x256/apps"
    icon="$data/icons/hicolor/256x256/apps/emuden.png"
    cp "$art/icon.png" "$icon"
elif (cd "$work" && "$image" --appimage-extract emuden.svg >/dev/null 2>&1) &&
    [ -f "$work/squashfs-root/emuden.svg" ]; then
    cp "$work/squashfs-root/emuden.svg" "$icon"
elif [ -f "$here/../../shells/linux/emuden.svg" ]; then
    cp "$here/../../shells/linux/emuden.svg" "$icon"
else
    icon="applications-games"
    echo "the icon could not be taken out of the AppImage: using a stock one" >&2
fi

entry="$data/applications/emuden.desktop"
cat > "$entry" <<EOF
[Desktop Entry]
Type=Application
Name=EmuDEN
Comment=Console-style game launcher
Exec="$image"
TryExec=$image
Icon=$icon
Categories=Game;Emulator;
Terminal=false
StartupWMClass=emuden
EOF
chmod 755 "$entry"
if [ -d "$HOME/Desktop" ]; then
    cp "$entry" "$HOME/Desktop/EmuDEN.desktop"
    chmod 755 "$HOME/Desktop/EmuDEN.desktop"
fi
if command -v update-desktop-database >/dev/null; then
    update-desktop-database "$data/applications" >/dev/null 2>&1 || true
fi

# The appids of the Steam shortcuts that start this AppImage, one per line.
shortcut_ids() {
    for vdf in "$HOME"/.local/share/Steam/userdata/*/config/shortcuts.vdf; do
        [ -f "$vdf" ] || continue
        python3 - "$vdf" "$image" <<'PY' 2>/dev/null || true
import re, struct, sys
data = open(sys.argv[1], "rb").read()
ids = list(re.finditer(rb"\x02appid\x00(.{4})", data, re.I | re.S))
for n, found in enumerate(ids):
    end = ids[n + 1].start() if n + 1 < len(ids) else len(data)
    exe = re.search(rb"\x01exe\x00([^\x00]*)", data[found.end():end], re.I)
    if exe and sys.argv[2].encode() in exe.group(1):
        print(struct.unpack("<I", found.group(1))[0])
PY
    done
}

if [ "$steam" = 1 ] && [ -z "$(shortcut_ids)" ]; then
    if command -v steamos-add-to-steam >/dev/null; then
        # Added through the menu entry, the shortcut is named "EmuDEN" and has its icon. Over
        # ssh there is no session: these are the ones of the device's own desktop.
        DISPLAY="${DISPLAY:-:0}" XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}" \
            steamos-add-to-steam "$entry" || echo "adding to Steam failed: add $image by hand" >&2
        # Steam writes the new shortcut a moment later.
        tries=0
        while [ -z "$(shortcut_ids)" ] && [ "$tries" -lt 30 ]; do sleep 1; tries=$((tries + 1)); done
    else
        echo "steamos-add-to-steam is not here: add $image to Steam as a non-Steam game" >&2
    fi
fi

# Cover (p), wide capsule, banner (_hero), logo and icon, named by the shortcut's appid.
art_ids="$(shortcut_ids | sort -u)"
if [ -n "$art_ids" ] && [ -f "$art/cover.png" ]; then
    for config in "$HOME"/.local/share/Steam/userdata/*/config; do
        mkdir -p "$config/grid"
        for id in $art_ids; do
            cp "$art/cover.png" "$config/grid/${id}p.png"
            cp "$art/capsule.png" "$config/grid/${id}.png"
            cp "$art/hero.png" "$config/grid/${id}_hero.png"
            cp "$art/logo.png" "$config/grid/${id}_logo.png"
            cp "$art/icon.png" "$config/grid/${id}_icon.png"
        done
    done
    echo "Steam artwork set for shortcut $(echo $art_ids): restart Steam to see it"
fi

echo "installed: $image"
echo "menu entry: $entry"
if [ -d "$dest/user" ]; then
    echo "data: $dest/user (portable)"
else
    echo "data: $data/EmuDEN"
fi
