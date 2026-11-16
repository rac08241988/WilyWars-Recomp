# Changelog

## v1.0.1 — 2026-11-16

- Widescreen now shows the real stage in the side areas; black bars where it
  can't (replaces the mirrored fill). The side columns are rebuilt each frame
  from the game's own level data and checked against the screen; title, menus,
  stage select, cutscenes and vertical room scrolling show plain black.
- Beside one-screen-wide rooms (e.g. Elec Man's vertical shafts) and past the
  ends of a level, the sides show matching stage scenery: the room's own edge
  wall/backdrop metatile columns continued outward with the stage's tiles and
  palette (never flipped; ledges, ladders, spikes and doors are not copied). Widescreen Off is unchanged (pixel-identical to the
  original).

## v1.0.0 — 2026-11-12

- First release: Windows x64 recomp with one-click builder (bring your own
  ROM), side panel with librashader shaders, opt-in 16:9 widescreen, EEPROM
  saves, Check for updates, box-art icon.
