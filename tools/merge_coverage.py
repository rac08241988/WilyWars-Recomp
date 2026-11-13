#!/usr/bin/env python3
"""Fold runtime coverage back into game/ for the next generation.

usage: merge_coverage.py <run-dir> [exec-coverage.txt ...]

<run-dir> is the folder the game ran in (it holds floor_coverage.toml and
dispatch_misses.toml written by the runner). Their [functions].extra entries
are merged into game/mmww_floor.discovery.toml (deduplicated, sorted). Any
--exec-coverage-out files (GENESIS_FORCE_INTERP=1 runs) are unioned into
game/mmww_executed_pcs.txt, which game.toml reads as runtime_exec_file.
Only addresses are stored, never ROM bytes. The recompiler's validator still
gates every lead before it becomes a function.
"""
import os, re, sys
here = os.path.dirname(os.path.abspath(__file__))
game = os.path.join(here, "..", "game")
run = sys.argv[1]
addrs = set()
disc = os.path.join(game, "mmww_floor.discovery.toml")
for path in (disc, os.path.join(run, "floor_coverage.toml"), os.path.join(run, "dispatch_misses.toml")):
    if os.path.exists(path):
        txt = open(path).read()
        m = re.search(r"\[functions\].*?extra\s*=\s*\[(.*?)\]", txt, re.S)
        if m:
            addrs |= {int(a, 16) for a in re.findall(r"0x([0-9A-Fa-f]+)", m.group(0))}
addrs = sorted(a for a in addrs if a < 0x200000 and a % 2 == 0)
with open(disc, "w") as f:
    f.write("# Runtime discovery leads (Tier-3 floor coverage + dispatch misses),\n")
    f.write("# merged by tools/merge_coverage.py. Addresses only; validator-gated.\n")
    f.write("[functions]\nextra = [\n")
    for a in addrs: f.write("  0x%06X,\n" % a)
    f.write("]\n")
pcs = set()
out = os.path.join(game, "mmww_executed_pcs.txt")
for path in [out] + sys.argv[2:]:
    if os.path.exists(path):
        for line in open(path):
            line = line.strip()
            if line and not line.startswith("#"):
                v = int(line, 16)
                if v < 0x200000: pcs.add(v)
with open(out, "w") as f:
    for v in sorted(pcs): f.write("%06X\n" % v)
print("discovery leads: %d, executed PCs: %d" % (len(addrs), len(pcs)))
