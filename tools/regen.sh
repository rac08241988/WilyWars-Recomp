#!/usr/bin/env bash
# Regenerate the recompiled C by hand (normally the CMake build does this).
# Output goes to build-host/generated-manual/ and is never committed.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
[ -x "$ROOT/build-host/GenesisRecomp" ] || {
  cmake -S "$ROOT/segagenesisrecomp/recompiler" -B "$ROOT/build-host" -G Ninja -DCMAKE_BUILD_TYPE=Release
  cmake --build "$ROOT/build-host"; }
cd "$ROOT/game"
"$ROOT/build-host/GenesisRecomp" mmww.bin --game game.toml --output-dir "$ROOT/build-host/generated-manual"
