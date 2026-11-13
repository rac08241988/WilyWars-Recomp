#!/usr/bin/env bash
# Apply this port's small engine patch (cartridge I/O hook used by the save
# EEPROM in game/mmww_eeprom.c). Safe to re-run.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT/segagenesisrecomp"
for p in "$ROOT"/patches/segagenesisrecomp-*.patch; do
  if git apply --reverse --check "$p" 2>/dev/null; then echo "already applied: $(basename "$p")"
  else git apply "$p" && echo "applied: $(basename "$p")"; fi
done
