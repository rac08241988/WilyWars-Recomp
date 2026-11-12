# Frontend carried over from the earlier ports (not built yet)

These files come from the SNES ports' frontend: the side-panel Options menu
(`dc_options.*`, `ui/rpanel.*`), the librashader loader (`dc_libra.*`), display
geometry (`dc_display.*`), 120 Hz motion (`dc_interp.*`) and default config.

They still call the snesrecomp desktop host (`snesrecomp_desktop_*`,
`SnesModalLayerView`, OpenGL presenter hooks). The Genesis runner presents
through SDL_Renderer, so the port plan is:

1. Add an OpenGL (and Vulkan) presenter path to the runner's video output,
   behind a small host-contract shim (`host_contract.c` style) so these files
   call `frontend_*` functions instead of `snesrecomp_desktop_*`.
2. Wire `dc_libra` to that presenter (live preview behind the panel).
3. Hook Esc / F12 / pad Guide to open the rpanel side panel; map its rows to
   the runner's existing settings, save states (`local_states.c`) and rewind.
4. Replace SNES-only rows (PPU options, SNES pixel sizes) with Genesis ones.
5. Widescreen: use the framework's per-game video hook (`GameVideo`), not the
   SNES `dc_widescreen.c`, which was dropped.
