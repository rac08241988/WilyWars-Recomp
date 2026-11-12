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
#include "genesis_runtime.h"
#include <stddef.h>
#include <stdint.h>

static void mmww_call_entry_point(void) { recomp_call_addr(0x000228u); }
static void mmww_call_vblank(void)      { recomp_call_addr(0x000418u); }
static void mmww_call_hblank(void)      { recomp_call_addr(0x0003F2u); }

const GameSpec g_game_spec = {
    .display_name        = "Mega Man: The Wily Wars",
    .short_name          = "MMWW",
    .expected_rom_crc32  = 0x0CD405DBu,
    .expected_rom_size   = 0x200000u,
    .tier3_floor_default = 1,
    .call_entry_point    = mmww_call_entry_point,
    .call_vblank         = mmww_call_vblank,
    .call_hblank         = mmww_call_hblank,
};
