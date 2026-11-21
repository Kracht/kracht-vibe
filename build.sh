#!/bin/sh
# Build KRACHT with the VIBE FM SDK next door (../vibe-fm-sdk, or SDK=...) and Zig 0.13 (zig on the PATH, or ZIG=...). Output: ./build/
# The SDK takes the folder's name as the firmware's id: the sources are staged in build/stage/kracht, so the id is
# "kracht" (kracht.wasm, kracht.zip) whatever this folder is called.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
SDK="${SDK:-$HERE/../vibe-fm-sdk}"
Z="$HOME/.local/opt/zig-linux-x86_64-0.13.0/zig"; [ -x "$Z" ] || Z="$(command -v zig || true)"
export ZIG="${ZIG:-$Z}"
ST="$HERE/build/stage/kracht"
rm -rf "$ST"; mkdir -p "$ST"
cp "$HERE"/*.c "$HERE"/*.h "$HERE/firmware.json" "$ST/"
[ -f "$HERE/icon.png" ] && cp "$HERE/icon.png" "$ST/"
sh "$SDK/tools/build.sh" "$ST"
cp "$SDK/build/kracht.wasm" "$SDK/build/kracht.zip" "$HERE/build/"
[ "$1" = "check" ] && node "$SDK/tools/vibe-check.mjs" "$HERE/build/kracht.wasm" --manifest "$HERE/firmware.json"
exit 0
