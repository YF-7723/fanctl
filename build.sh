#!/bin/bash
# Build FanCtl without touching /usr/bin/clang (which refuses to run until the
# Xcode licence is accepted).  We use Xcode's internal toolchain directly.
set -euo pipefail
cd "$(dirname "$0")"

DEV=$(xcode-select -p 2>/dev/null || true)
[ -d "$DEV" ] || DEV=/Applications/Xcode.app/Contents/Developer
TC="$DEV/Toolchains/XcodeDefault.xctoolchain/usr/bin"
SDK="$DEV/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk"
MAKE="$DEV/usr/bin/make"
QT="${QT_PREFIX:-/opt/homebrew/opt/qt6}"

for f in "$TC/clang++" "$SDK" "$MAKE"; do
  [ -e "$f" ] || { echo "build: missing $f" >&2; exit 1; }
done
[ -d "$QT" ] || { echo "build: Qt6 not found at $QT (set QT_PREFIX)" >&2; exit 1; }

export PATH="$TC:$PATH"
cmake -S . -B build -G "Unix Makefiles" \
  -DCMAKE_MAKE_PROGRAM="$MAKE" \
  -DCMAKE_C_COMPILER="$TC/clang" \
  -DCMAKE_CXX_COMPILER="$TC/clang++" \
  -DCMAKE_OSX_SYSROOT="$SDK" \
  -DCMAKE_PREFIX_PATH="$QT" \
  "$@"
cmake --build build -j"$(sysctl -n hw.ncpu)"
echo
echo "built:"
ls -l build/fanctl build/fanctld 2>/dev/null || true
ls -d build/FanCtl.app 2>/dev/null || true
