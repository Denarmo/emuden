#!/bin/sh
# Copies this machine's WebKitGTK 4.1 and what it needs into an AppDir, for systems without
# one (SteamOS). The app finds it at run time (platform/src/linux/webkit_bundle.cpp).
#   tools/linux/bundle-webkit.sh <AppDir>
# Debian packages: libwebkit2gtk-4.1-0 glib-networking patchelf
# Left to the system: glibc, the C++ runtime, the graphics stack (GL, EGL, GBM, DRM, Mesa),
# X11, XCB, Wayland, xkbcommon, ALSA, udev, systemd, D-Bus, fontconfig and FreeType: every
# desktop has them, and they must match its drivers and its configuration.
set -eu
app="$1"
lib="$(dirname "$(readlink -f "$(ldconfig -p | sed -n 's/.*libwebkit2gtk-4\.1\.so\.0 (libc6,x86-64) => //p' | head -n1)")")"
[ -f "$lib/libwebkit2gtk-4.1.so.0" ] || { echo "WebKitGTK 4.1 is not installed" >&2; exit 1; }
libexec="$lib/webkit2gtk-4.1"
gio="$lib/gio/modules/libgiognutls.so"
for f in "$libexec/WebKitWebProcess" "$libexec/WebKitNetworkProcess" \
         "$libexec/injected-bundle/libwebkit2gtkinjectedbundle.so" "$gio"; do
    [ -f "$f" ] || { echo "missing $f (apt install libwebkit2gtk-4.1-0 glib-networking)" >&2; exit 1; }
done
command -v patchelf >/dev/null || { echo "patchelf is missing (apt install patchelf)" >&2; exit 1; }

out="$app/usr/lib/webkit"
rm -rf "$out"
mkdir -p "$out/injected-bundle" "$out/gio"
# The folder WebKit was built to start its helpers from; the app points that string at $out.
printf '%s\n' "$libexec" > "$out/libexec-dir"
strings -n 8 "$lib/libwebkit2gtk-4.1.so.0" | grep -qx "$libexec" ||
    { echo "libwebkit2gtk-4.1 does not name $libexec" >&2; exit 1; }

system='^(ld-linux.*|libc|libm|libdl|libpthread|librt|libresolv|libutil|libstdc\+\+|libgcc_s|libatomic|libGL|libGLX|libGLdispatch|libEGL|libGLESv2|libOpenGL|libgbm|libdrm|libX[A-Za-z0-9-]*|libxcb[A-Za-z0-9-]*|libxkbcommon[a-z0-9-]*|libwayland-[a-z]*|libasound|libudev|libsystemd|libdbus-1|libfontconfig|libfreetype|libexpat|libz)\.so'
roots="$lib/libwebkit2gtk-4.1.so.0 $libexec/WebKitWebProcess $libexec/WebKitNetworkProcess
$libexec/injected-bundle/libwebkit2gtkinjectedbundle.so $gio"
# shellcheck disable=SC2086
if ldd $roots | grep -q 'not found'; then
    echo "WebKitGTK's libraries are not all installed:" >&2; ldd $roots | grep 'not found' >&2; exit 1
fi
# shellcheck disable=SC2086
ldd $roots | awk '/=> \//{print $1, $3}' | sort -u | while read -r name path; do
    if ! echo "$name" | grep -qE "$system"; then
        cp -L "$path" "$out/$name"
    fi
done
cp "$libexec/WebKitWebProcess" "$libexec/WebKitNetworkProcess" "$out/"
cp "$libexec/injected-bundle/libwebkit2gtkinjectedbundle.so" "$out/injected-bundle/"
cp "$gio" "$out/gio/"

chmod 644 "$out"/*.so* "$out"/injected-bundle/*.so "$out"/gio/*.so
chmod 755 "$out/WebKitWebProcess" "$out/WebKitNetworkProcess"
# RPATH, not RUNPATH: it also holds for what each library loads in turn.
for f in "$out"/*.so* "$out/WebKitWebProcess" "$out/WebKitNetworkProcess"; do
    patchelf --force-rpath --set-rpath '$ORIGIN' "$f"
done
for f in "$out"/injected-bundle/*.so "$out"/gio/*.so; do
    patchelf --force-rpath --set-rpath '$ORIGIN/..' "$f"
done

# Their licences, one per Debian package.
lic="$app/usr/share/licenses/webkit"
mkdir -p "$lic"
for f in "$out"/*.so* "$out"/injected-bundle/*.so "$out"/gio/*.so; do
    dpkg -S "*/$(basename "$f")" 2>/dev/null | cut -d: -f1
done | sort -u | while read -r pkg; do
    if [ -f "/usr/share/doc/$pkg/copyright" ]; then
        cp "/usr/share/doc/$pkg/copyright" "$lic/$pkg.txt"
    else
        echo "licence not found: $pkg" >&2
    fi
done
echo "web engine: $(du -sh "$out" | cut -f1) in $out ($(ls "$out" | grep -c '\.so') libraries)"
