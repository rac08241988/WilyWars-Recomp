#!/usr/bin/env bash
# Apply this port's small engine patches, in name order. Safe to re-run.
#   segagenesisrecomp-cart-io.patch        cartridge I/O hook (save EEPROM, game/mmww_eeprom.c)
#   segagenesisrecomp-present-hooks.patch  presentation hooks (OpenGL presenter, panel, src/gl/)
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT/segagenesisrecomp"
for p in "$ROOT"/patches/segagenesisrecomp-*.patch; do
  if git apply --reverse --check "$p" 2>/dev/null; then echo "already applied: $(basename "$p")"
  else git apply "$p" && echo "applied: $(basename "$p")"; fi
done
