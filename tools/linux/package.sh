#!/bin/sh
# Wraps a finished linux build into an AppImage.
#   tools/linux/package.sh [build-dir] [out-dir]
# Needs mksquashfs (squashfs-tools) and curl. The AppImage runtime is the pinned release of
# github.com/AppImage/type2-runtime, checked by SHA-256 and kept beside the output.
set -eu
root="$(cd "$(dirname "$0")/../.." && pwd)"
build="${1:-$root/build/linux}"
out="${2:-$build/package}"
exe="$build/bin/emuden"
[ -x "$exe" ] || { echo "no $exe: run tools/linux/build.sh first" >&2; exit 1; }
command -v mksquashfs >/dev/null || { echo "mksquashfs is missing (apt install squashfs-tools)" >&2; exit 1; }

runtime_tag=20251108
runtime_sha=2fca8b443c92510f1483a883f60061ad09b46b978b2631c807cd873a47ec260d
runtime="$out/runtime-x86_64-$runtime_tag"
mkdir -p "$out"
if [ ! -f "$runtime" ]; then
    curl -fsSL -o "$runtime.part" \
        "https://github.com/AppImage/type2-runtime/releases/download/$runtime_tag/runtime-x86_64"
    mv "$runtime.part" "$runtime"
fi
echo "$runtime_sha  $runtime" | sha256sum -c - >/dev/null || {
    echo "the AppImage runtime does not match its pinned hash: $runtime" >&2; exit 1; }

app="$out/EmuDEN.AppDir"
rm -rf "$app"
mkdir -p "$app/usr/bin" "$app/usr/share/applications" "$app/usr/share/licenses/emuden" \
    "$app/usr/share/icons/hicolor/scalable/apps"
cp "$exe" "$app/usr/bin/emuden"
strip --strip-debug "$app/usr/bin/emuden"
# Assets come from the source tree, so a stale copy in the build folder never ships.
cp -r "$root/assets" "$app/usr/bin/assets"
shell="$root/shells/linux"
cp "$shell/AppRun" "$app/AppRun"
chmod 755 "$app/AppRun" "$app/usr/bin/emuden"
cp "$shell/emuden.desktop" "$app/emuden.desktop"
cp "$shell/emuden.desktop" "$app/usr/share/applications/emuden.desktop"
cp "$shell/emuden.svg" "$app/emuden.svg"
cp "$shell/emuden.svg" "$app/usr/share/icons/hicolor/scalable/apps/emuden.svg"
ln -s emuden.svg "$app/.DirIcon"

deps="$(sed -n 's/^DEN_DEPS_DIR:PATH=//p' "$build/CMakeCache.txt")"
lic="$app/usr/share/licenses/emuden"
cp "$root/LICENSE" "$lic/EmuDEN.txt"
copy_licence() {
    if [ -f "$1" ]; then cp "$1" "$lic/$2"; else echo "licence not found: $1" >&2; fi
}
copy_licence "$deps/sdl3-src/LICENSE.txt" SDL3.txt
copy_licence "$deps/zlib-src/LICENSE" zlib.txt
copy_licence "$deps/freetype-src/LICENSE.TXT" FreeType.txt
copy_licence "$deps/freetype-src/docs/FTL.TXT" FreeType-FTL.txt
copy_licence "$deps/stb-src/LICENSE" stb.txt
copy_licence "$root/plugins/src/third_party/monocypher/LICENCE.md" Monocypher.md
copy_licence "$root/assets/fonts/OFL.txt" Google-Sans-OFL.txt

image="$out/EmuDEN-x86_64.AppImage"
rm -f "$out/emuden.squashfs" "$image.part"
mksquashfs "$app" "$out/emuden.squashfs" -root-owned -noappend -comp zstd -Xcompression-level 19 -quiet -no-progress
cat "$runtime" "$out/emuden.squashfs" > "$image.part"
rm "$out/emuden.squashfs"
chmod 755 "$image.part"
mv "$image.part" "$image"
ls -l "$image"
