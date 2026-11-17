#!/bin/sh
# Builds the linux preset and runs the unit tests.
#   tools/linux/build.sh [build-dir] [target...]
# The default build directory is build/linux in the source tree. From WSL with the source on
# a Windows drive, pass a directory in the Linux file system: third-party sources then go
# beside it, not into the shared .deps.
# Debian packages: clang ninja-build cmake pkg-config libcurl4-openssl-dev libx11-dev
# libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxfixes-dev libxss-dev libxtst-dev
# libwayland-dev libxkbcommon-dev libdecor-0-dev libegl-dev libgles-dev libgl-dev libdrm-dev
# libgbm-dev libasound2-dev libpulse-dev libpipewire-0.3-dev libudev-dev libdbus-1-dev
# EMUDEN_VERSION=0.2.0 makes it a release build, which offers updates; it stays in the cache.
set -eu
root="$(cd "$(dirname "$0")/../.." && pwd)"
build="${1:-$root/build/linux}"
[ $# -gt 0 ] && shift
extra=""
case "$build" in
    "$root"/*) ;;
    *) extra="-DDEN_DEPS_DIR=$(dirname "$build")/deps" ;;
esac
[ -n "${EMUDEN_VERSION:-}" ] && extra="$extra -DEMUDEN_VERSION=$EMUDEN_VERSION"
mkdir -p "$build"
cmake -S "$root" --preset linux -B "$build" $extra
if [ $# -gt 0 ]; then
    cmake --build "$build" --target "$@"
else
    cmake --build "$build"
    ctest --test-dir "$build" --output-on-failure
fi
