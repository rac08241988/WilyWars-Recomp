#!/usr/bin/env bash
# Mega Man: The Wily Wars Genesis recomp — source setup.
# 1. Checks out the pinned framework (segagenesisrecomp) and launcher (recomp-ui).
# 2. Optionally fetches the libretro shader sets (see fetch_shaders.sh).
# 3. Optionally verifies YOUR OWN ROM against rom_identity.txt (SHA-256) and
#    copies it to game/mmww.bin, where the build generates C from it.
# No ROM, generated C, or ROM-derived data ships with this repository.
# Usage: scripts/setup.sh [--rom PATH] [--no-shaders]
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ROM=""; SHADERS=1
while [ $# -gt 0 ]; do
  case "$1" in
    --rom) ROM="$2"; shift 2 ;;
    --no-shaders) SHADERS=0; shift ;;
    -h|--help) sed -n '2,8p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "setup.sh: unknown argument $1" >&2; exit 2 ;;
  esac
done
sha() { if command -v sha256sum >/dev/null; then sha256sum "$1" | cut -d' ' -f1; else shasum -a 256 "$1" | cut -d' ' -f1; fi; }
WANT=$(sed -n 's/^expected_sha256 *= *//p' "$ROOT/rom_identity.txt" | tr -d '" ')
cd "$ROOT"
git submodule update --init --recursive
[ "$SHADERS" = 1 ] && bash "$ROOT/scripts/fetch_shaders.sh"
if [ -n "$ROM" ]; then
  [ "$(sha "$ROM")" = "$WANT" ] || { echo "ERROR: $ROM is not Mega Man: The Wily Wars (USA) (sha256 mismatch, want $WANT)" >&2; exit 1; }
  cp "$ROM" "$ROOT/game/mmww.bin"
  echo "ROM verified and copied to game/mmww.bin (ignored by git)."
fi
echo "=== Setup done. See README.md, 'Build from source'. ==="
