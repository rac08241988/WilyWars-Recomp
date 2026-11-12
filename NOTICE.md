# Notices

- `src/frontend/` (options menu, shader loader, display helpers, side-panel
  widget) and `scripts/stage_shaders.py`, `scripts/stage_slang_shaders.py`,
  `scripts/fetch_shaders.sh` are carried forward from rac08241988's earlier
  recomp ports. Parts of them were originally ported from a SuperMarioWorldRecomp /
  snesrecomp add-on (Required Notice: Copyright (c) 2026 Matthew Stanley),
  which is under the PolyForm Noncommercial License 1.0.0; treat these parts
  as PolyForm Noncommercial 1.0.0 (non-commercial use only).
- `segagenesisrecomp/` (submodule): PolyForm Noncommercial 1.0.0 plus the
  additional permissions in its `PERMISSIONS.md`
  (Required Notice: Copyright (c) 2026 Matthew Stanley). Its bundled
  third-party code keeps its own terms (ymfm BSD-3-Clause, superzazu/z80 MIT,
  SDL2 zlib, minicoro Unlicense/MIT-0, xxHash BSD-2-Clause, tomlc99 MIT); see
  its `THIRD-PARTY-LICENSES.md`.
- `recomp-ui/` (submodule): MIT.
- `third_party/SDL2-mingw-2.28.5/`: SDL2 (zlib license), the official MinGW
  development package, used for the llvm-mingw cross-build.
- libretro/glsl-shaders and libretro/slang-shaders are fetched at setup and are
  not stored in this repository; each shader keeps its own license header.
  librashader is MPL-2.0 (`vendor/librashader/LICENSE.md`).
- Mega Man: The Wily Wars is the property of Capcom. No ROM, generated C, or
  other ROM-derived data is distributed in this repository.

Because the framework is PolyForm Noncommercial, any binary built from this
repository is for non-commercial use only and must be free to download and play.
