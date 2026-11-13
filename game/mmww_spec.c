/*
 * mmww_spec.c — GameSpec for Mega Man: The Wily Wars (USA, Mega Drive Mini /
 * Genesis Mini release), 2 MB, flat $000000-$1FFFFF.
 *
 * Vectors: Entry $000228, V-int $000418 (sets flag $FFBEBA), H-int $0003F2.
 * The game is compiled C that dispatches through RAM function pointers, so
 * the Tier-3 interpreter floor is always on: a static-dispatch miss runs
 * correctly on the interpreter and is logged for the next regeneration.
 */
#include "game_spec.h"
#include "game_video.h"
#include "genesis_runtime.h"
#include "mmww_eeprom.h"
#include <stddef.h>
#include <stdint.h>

static void mmww_call_entry_point(void) { recomp_call_addr(0x000228u); }
static void mmww_call_vblank(void)      { mmww_eeprom_frame(); recomp_call_addr(0x000418u); }
static void mmww_call_hblank(void)      { recomp_call_addr(0x0003F2u); }

/* No save states in this port (the game's own EEPROM saves are the saves).
 * The runner then hides its state menu entries, hotkeys, pad quick slots
 * and rewind, and refuses state files. */
static const char *mmww_no_states(void) { return "this port has no save states; use the game's own saves"; }

extern const GameVideo g_mmww_video;   /* game/mmww_video.c */

const GameSpec g_game_spec = {
    .display_name        = "Mega Man: The Wily Wars",
    .short_name          = "MMWW",
    .expected_rom_crc32  = 0x0CD405DBu,
    .expected_rom_size   = 0x200000u,
    .tier3_floor_default = 1,
    .video               = &g_mmww_video,  /* opt-in 16:9, default Off */
    .video_default       = "off",
    .call_entry_point    = mmww_call_entry_point,
    .state_unavailable_reason = mmww_no_states,
    .call_vblank         = mmww_call_vblank,
    .call_hblank         = mmww_call_hblank,
    .sram_loaded         = mmww_eeprom_sram_loaded,
    .rb_state_save       = mmww_eeprom_rb_save,
    .rb_state_load       = mmww_eeprom_rb_load,
};
