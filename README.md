# Mega Man: The Wily Wars Recompiled (Genesis)

> **v0.1.0 (not released yet).** Boots, menus and stages run on Windows (tested
> under Wine 9.22), with the side menu, shaders, opt-in widescreen, cartridge
> saves and the one-click builder.
>
> This project ships no game code or ROM data. You supply your own cartridge dump.

An unofficial Windows PC port of **Mega Man: The Wily Wars** (Sega Genesis, USA),
built on the [segagenesisrecomp](https://github.com/mstan/segagenesisrecomp)
static-recompilation framework. The 68000 game code is translated to C ahead of
time and built into a native Windows x64 exe.

## Features

- Native Windows x64 exe, generated and built from your own ROM.
- Framework hardware: VDP, YM2612 (ymfm), SN76489 PSG and Z80 sound CPU.
  Any routine the static pass missed runs on the built-in 68000 interpreter
  fallback, so the game keeps running while coverage improves.
- One-click builder (`MMWW-Setup.exe`) with a ROM picker; the game is
  generated and built on your PC.
- In-game side panel (Esc / F12 / pad Guide): shaders from the libretro slang
  set via librashader (live preview, default Off), sharp/smooth filter, opt-in
  16:9 widescreen (default Off), quit. Widescreen keeps the game, sprites and
  HUD at 4:3 and fills the side columns with the stage: real tiles where the
  game has drawn them, otherwise a darkened mirror of the stage edge.
- **Check for updates** in the side panel: asks GitHub for the latest release
  and, if there is a newer one, opens its page in your browser (nothing is
  downloaded or installed automatically).
- Window and exe icon from the USA Genesis box art.
- Saves: the cartridge's own EEPROM saves, kept in `mmww.eep` beside the exe.
  There are no save states.

## Play on Windows (no coding needed)

1. Download the release zip and unzip it anywhere (for example `C:\Games\WilyWars`).
2. Double-click **`MMWW-Setup.exe`** and pick your ROM:
   `Mega Man - The Wily Wars (USA) (Mega Drive Mini, Genesis Mini).md`
   (2,097,152 bytes, CRC32 `0cd405db`). Setup refuses any other dump.
3. First run only: setup downloads a portable compiler (about 300 MB download,
   900 MB on disk, from the official llvm-mingw, CMake and Ninja GitHub
   releases), translates the game to C on your PC and builds it. This takes a
   few minutes; progress is shown in the window and saved to `setup.log`.
4. The game starts. Next time, open `MMWW-Setup.exe` again (it just starts the
   game) or `game\WilyWarsGenesisRecomp.exe` directly.

`MMWW-Setup.exe --rebuild` builds again (for example after updating the zip).
Your saves (`game\mmww.eep`) and settings (`game\mmww_video.ini`) are kept.

## Controls & Shortcuts

The Genesis pad map is positional.

| Action | Xbox controller | Keyboard |
|---|---|---|
| D-pad | D-pad | Arrow keys |
| Genesis A / B / C | B / A / X | Z / X / C |
| Start | Menu (Start) | Enter |
| Side panel (pause) | Guide | Esc or F12 |
| Fast-forward (hold) | Back | Tab |
| Fullscreen | | F11 or Alt+Enter |

In the side panel: Up/Down picks a row, Left/Right changes it (shaders preview
live), Enter applies, Esc resumes.

## Shaders

The side panel's **Shader** row lists the libretro slang presets run through
librashader on the OpenGL renderer (see [`SHADERS.md`](SHADERS.md)). Default is
Off. If `renderer=sdl` is set in `mmww_video.ini`, shaders are not available.

## Tested On

Target machine (the project owner's PC); development testing ran under
Wine 9.22 on Linux:

- CPU: AMD Ryzen 7 5800X3D
- GPU: NVIDIA GeForce RTX 3070 Ti (8 GB)
- RAM: 32 GB dual-channel
- Storage: NVMe SSD
- OS: Windows and AtlasOS
- Renderer: OpenGL 3.3

## Known limitations

- **Early build.** Boots, menus and Mega Man 1 stages are tested; Mega Man 2
  and 3 are less covered by the native pass (more of them runs on the
  interpreter, which is slower but correct).
- **No save states.** Use the game's own save files.
- **Widescreen sides are a fill.** The game only draws one tile column past the
  4:3 view, so the sides are mostly a darkened mirror of the stage edge.
- **Partly interpreted.** The game is compiled C that jumps through function
  pointers, so the first static pass finds few routines. The rest runs on the
  interpreter fallback until coverage is fed back into `game/game.toml`.

## Build from source (Linux host, Windows exe)

```sh
git clone --recursive <this repository>
cd WilyWarsGenesisRecomp
scripts/setup.sh --rom "/path/to/Mega Man - The Wily Wars (USA) (Mega Drive Mini, Genesis Mini).md"
cmake -S segagenesisrecomp/recompiler -B build-host -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-host
cmake -S . -B build-win -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_TOOLCHAIN_FILE=$PWD/cmake/llvm-mingw.cmake \
      -DGENESIS_RECOMPILER_EXE=$PWD/build-host/GenesisRecomp
cmake --build build-win
```

The recompiled C is generated inside the build tree from your ROM and is
never committed.

## ROM identity

| | |
|---|---|
| File | `Mega Man - The Wily Wars (USA) (Mega Drive Mini, Genesis Mini).md` |
| Header title | `MEGAMAN THE WILY WARS` (serial `GM T-12053 -00`, region U) |
| Mapping | Flat 2 MB, EEPROM save at `$200001` |
| Size | 2,097,152 bytes |
| CRC32 | `0cd405db` |
| SHA-1 | `26ad72719991e94dbe477c78e65d68f4a4fe51e6` |
| SHA-256 | `c3e5e56c1252de5a1ff3bb68b47366120f2c85d333a9ca0eb6df3dab1b03e583` |

The `.md` extension is the Mega Drive ROM format, not Markdown. The header
checksum ($CB82) does not match the data ($23AD) on this release; that is
expected and the game does not check it.

## Credits

- [segagenesisrecomp](https://github.com/mstan/segagenesisrecomp) by Matthew
  Stanley and contributors: the Genesis static-recompilation framework,
  including ymfm, superzazu/z80 and SDL2 (see its `THIRD-PARTY-LICENSES.md`).
- [recomp-ui](https://github.com/mstan/recomp-ui): side-panel UI and fonts.
- recomp-updater (rac08241988): the Check for updates helper.
- [miniz](https://github.com/richgel999/miniz) (MIT): zip extraction in the setup program.
- Box art: libretro-thumbnails (USA Genesis cover), used for the icon.
- Options menu, shader and display code carried forward from rac08241988's
  earlier recomp ports (see [`NOTICE.md`](NOTICE.md)).
- libretro slang/GLSL shaders and librashader.

## License

This project's own source is under the MIT license in [`LICENSE`](LICENSE).
The options menu and shader parts carried forward from earlier ports are
PolyForm Noncommercial 1.0.0 (see [`NOTICE.md`](NOTICE.md)). The framework is
PolyForm Noncommercial 1.0.0 with its `PERMISSIONS.md` grant, so binaries are
for non-commercial use only. None of this covers the game, which is not
distributed here. Mega Man is a trademark of Capcom; this project is not
affiliated with Capcom or Sega.

## One-click Windows builder

`tools/package_windows.sh <llvm-mingw bin> <GenesisRecomp.exe> <shaders dir>` assembles
`dist/WilyWarsRecomp-win64/` (local folder; ~66 MB): `MMWW-Setup.exe`, the Windows
recompiler in `tools\`, the project/engine sources in `src\` and the librashader shaders.
It refuses to finish if a ROM, any 2 MB image, or generated C ends up inside.

On the player's PC `MMWW-Setup.exe` shows the ROM picker, checks size + CRC32 0CD405DB,
downloads pinned llvm-mingw 20261006 / CMake 3.31.8 / Ninja 1.12.1 into `toolchain\`,
generates and builds the game into `game\`, then launches it. Later runs just launch;
`--rebuild` rebuilds. `MMWW_SETUP_ROM` / `MMWW_SETUP_NO_LAUNCH` are for automated tests.
