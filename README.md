# Mega Man: The Wily Wars Recompiled (Genesis)

> **Work in progress (v0.1.0, not released).** It boots to the title screen and
> the file-select screen on Windows (tested under Wine 9.22). The menu, shaders,
> one-click builder and widescreen from the other ports are still being moved over.
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
- Planned (carried over from the other ports): one-click builder with ROM
  picker, in-game Options side panel (Esc / F12 / pad Guide), the full libretro
  shader set via librashader, opt-in 16:9 widescreen, save states, rewind, Xbox
  and keyboard input.

## Play on Windows (no coding needed)

Not released yet. When it is, the steps will be the same as the other ports:
download the zip from Releases, open the exe, click **Browse For ROM**, pick
`Mega Man - The Wily Wars (USA) (Mega Drive Mini, Genesis Mini).md`, let it
build, then click **Play**.

## Controls & Shortcuts

Current framework defaults (the Options menu and Xbox layout come later).
The Genesis pad map is positional.

| Action | Xbox controller | Keyboard |
|---|---|---|
| D-pad | D-pad | Arrow keys |
| Genesis A / B / C | B / A / X | Z / X / C |
| Start | Menu (Start) | Enter |

## Shaders

Not wired in yet. The libretro slang and GLSL preset sets and librashader are
staged from the other ports (see [`SHADERS.md`](SHADERS.md)). **Shaders will only
work on the Vulkan and OpenGL renderers.**

## Tested On

The project owner's PC:

- CPU: AMD Ryzen 7 5800X3D
- GPU: NVIDIA GeForce RTX 3070 Ti (8 GB)
- RAM: 32 GB dual-channel
- Storage: NVMe SSD
- OS: Windows and AtlasOS
- Renderers: Vulkan and OpenGL

## Known limitations

- **Early build.** It boots to the title screen and file select. Stages have
  not been tested yet.
- **Saves.** The cartridge saves to a serial EEPROM, which is not emulated yet,
  so passwords and save files may not stick. Use save states once the menu is in.
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
- [recomp-ui](https://github.com/mstan/recomp-ui): the launcher.
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
