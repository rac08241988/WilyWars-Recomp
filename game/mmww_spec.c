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
#include "mmww_eeprom.h"
#include <stddef.h>
#include <stdint.h>

static void mmww_call_entry_point(void) { recomp_call_addr(0x000228u); }
static void mmww_call_vblank(void)      { mmww_eeprom_frame(); recomp_call_addr(0x000418u); }
static void mmww_call_hblank(void)      { recomp_call_addr(0x0003F2u); }

#include <stdio.h>
#include <stdlib.h>
extern uint8_t g_ram[0x10000];
/* Save-state boundary: the fiber parked in the main loop's own vblank wait
 * ($000F5C), i.e. the guest stack is at main()'s frame level. Frames that park
 * inside a nested wait (palette fades, DMA waits) cannot be resumed at the
 * main loop, so the panel defers saving until this holds. */
int mmww_state_boundary(void) { return g_cpu.A[7] == 0xFF0400u; }
uint32_t mmww_guest_sp(void) { return g_cpu.A[7]; }

/* MMWW_STACKDUMP=1: print the restored guest stack after a state load. */
void mmww_dump_stack(void);
static void mmww_state_loaded(void)
{
    if (!getenv("MMWW_STACKDUMP")) return;
    mmww_dump_stack();
}
void mmww_dump_stack(void)
{
    uint32_t sp = g_cpu.A[7];
    fprintf(stderr, "[MMWW] loaded A7=$%06X\n", sp);
    for (uint32_t a = sp; a + 3 < 0xFF0414u && a >= 0xFF0000u; a += 2) {
        uint32_t o = a & 0xFFFFu;
        uint32_t v = (uint32_t)g_ram[o] << 24 | (uint32_t)g_ram[o + 1] << 16 |
                     (uint32_t)g_ram[o + 2] << 8 | g_ram[o + 3];
        if (v >= 0x200 && v < 0x200000) fprintf(stderr, "[MMWW]   $%06X: $%06X\n", a, v);
    }
}

const GameSpec g_game_spec = {
    .display_name        = "Mega Man: The Wily Wars",
    .short_name          = "MMWW",
    .expected_rom_crc32  = 0x0CD405DBu,
    .expected_rom_size   = 0x200000u,
    .tier3_floor_default = 1,
    /* Native state loads restart the game fiber at the main loop's vblank wait
     * (the host C stack is not part of a save state). */
    .resume_main_loop_pc = 0x000F56u,
    .call_entry_point    = mmww_call_entry_point,
    .on_state_loaded     = mmww_state_loaded,
    .call_vblank         = mmww_call_vblank,
    .call_hblank         = mmww_call_hblank,
    .sram_loaded         = mmww_eeprom_sram_loaded,
    .rb_state_save       = mmww_eeprom_rb_save,
    .rb_state_load       = mmww_eeprom_rb_load,
};
