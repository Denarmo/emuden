#!/bin/sh
# Installs the AppImage for one user on SteamOS (or any desktop Linux). No root needed.
#   install-steamos.sh [--portable] [--steam] [EmuDEN-x86_64.AppImage]
# Copies the AppImage to ~/Games/EmuDEN, takes the icon out of it and adds a desktop entry to
# the application menu and the desktop. Running it again updates the copy.
#   --portable  keep the app's data in ~/Games/EmuDEN/user instead of ~/.local/share/EmuDEN
#   --steam     also add it to Steam as a non-Steam game (needs the desktop session)
set -eu

portable=0
steam=0
source=""
for arg in "$@"; do
    case "$arg" in
        --portable) portable=1 ;;
        --steam) steam=1 ;;
        -h|--help) sed -n '2,8p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
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

# The icon is inside the AppImage; unpacking it needs no FUSE.
icon="$data/icons/hicolor/scalable/apps/emuden.svg"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
if (cd "$work" && "$image" --appimage-extract emuden.svg >/dev/null 2>&1) &&
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

if [ "$steam" = 1 ]; then
    if command -v steamos-add-to-steam >/dev/null; then
        # Over ssh there is no session: these are the ones of the device's own desktop.
        DISPLAY="${DISPLAY:-:0}" XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}" \
            steamos-add-to-steam "$image" || echo "adding to Steam failed: add $image by hand" >&2
    else
        echo "steamos-add-to-steam is not here: add $image to Steam as a non-Steam game" >&2
    fi
fi

echo "installed: $image"
echo "menu entry: $entry"
if [ -d "$dest/user" ]; then
    echo "data: $dest/user (portable)"
else
    echo "data: $data/EmuDEN"
fi
