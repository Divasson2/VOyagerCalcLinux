#!/bin/bash
# Cross-build and package VoyagerCalc for Windows from Linux.   usage: packaging/windows/build-windows.sh x64|arm64 [tag]
# Environment (see docs/BUILD-WINDOWS.md):
#   LLVM_MINGW    llvm-mingw root
#   QT_WIN        Qt 6 for Windows (llvm-mingw) matching the architecture
#   QT_HOST_PATH  Qt 6 for the Linux host (same version)
#   MAKENSIS      optional: makensis binary (NSIS) to also build the per-user installer;  NSISDIR optional: its share/nsis directory
#   OUT           output folder (default: ./dist-windows)
set -euo pipefail
ARCH=${1:-x64}; TAG=${2:-}
HERE=$(cd "$(dirname "$0")" && pwd); SRC=$(cd "$HERE/../.." && pwd)
: "${LLVM_MINGW:?set LLVM_MINGW}" "${QT_WIN:?set QT_WIN}" "${QT_HOST_PATH:?set QT_HOST_PATH}"
OUT=${OUT:-$PWD/dist-windows}; BUILD=${BUILD:-$PWD/build-win-$ARCH}
case $ARCH in
  x64)   TRIPLE=x86_64-w64-mingw32 ;;
  arm64) TRIPLE=aarch64-w64-mingw32 ;;
  *) echo "architecture must be x64 or arm64" >&2; exit 2 ;;
esac
export LLVM_MINGW QT_WIN QT_HOST_PATH
cmake -S "$SRC" -B "$BUILD" -G Ninja -DCMAKE_TOOLCHAIN_FILE="$HERE/cmake/toolchain-win-$ARCH.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD" -j"$(nproc)"
EXTRA=()
if [ -n "${MAKENSIS:-}" ]; then EXTRA+=(--makensis "$MAKENSIS"); [ -n "${NSISDIR:-}" ] && EXTRA+=(--nsisdir "$NSISDIR"); fi
python3 "$HERE/build-package.py" --arch "$ARCH" --build "$BUILD" --qt "$QT_WIN" --mingw-bin "$LLVM_MINGW/$TRIPLE/bin" \
  --objdump "$LLVM_MINGW/bin/llvm-objdump" --out "$OUT" ${TAG:+--tag "$TAG"} "${EXTRA[@]}"
