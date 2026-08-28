#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
NDK="${ANDROID_NDK_HOME:-${ANDROID_NDK_ROOT:-}}"
if [[ -z "$NDK" || ! -f "$NDK/build/cmake/android.toolchain.cmake" ]]; then
  echo "Android NDK not found. Set ANDROID_NDK_HOME (recommended NDK r28c / 28.2.13676358)." >&2
  exit 1
fi
BUILD="$ROOT/build/android-arm64-v8a-Release"
DIST="$ROOT/dist/arm64-v8a"
PKG="$DIST/levi-item-physics"
rm -rf "$BUILD" "$DIST"
cmake -S "$ROOT" -B "$BUILD" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a \
  -DANDROID_PLATFORM=android-24 \
  -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD" --target levi_item_physics
mkdir -p "$PKG/config"
cp "$ROOT/manifest.json" "$PKG/manifest.json"
cp "$ROOT/config/config.json" "$PKG/config/config.json"
cp "$ROOT/config/config.schema.json" "$PKG/config/config.schema.json"
cp "$BUILD/out/arm64-v8a/liblevi_item_physics.so" "$PKG/liblevi_item_physics.so"
(
  cd "$PKG"
  zip -qr "$DIST/levi-item-physics.levipack" .
)
echo "Built: $DIST/levi-item-physics.levipack"
