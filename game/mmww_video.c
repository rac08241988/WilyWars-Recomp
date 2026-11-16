/* Opt-in 16:9 widescreen through the framework's GameVideo hook (default Off).
 *
 * The VDP renders the authentic row untouched; this adapter widens it to 16:9
 * at the Genesis pixel aspect (H32 256 -> 342, H40 320 -> 424) and fills the
 * side columns with the REAL stage, or plain black where it can't:
 *
 *  - Plane A (the level): Wily Wars only streams one 16-px metatile column past
 *    the 256-px view, so the side columns are rebuilt every frame from the
 *    game's own level data, exactly the way its column streamer ($3DEA, called
 *    from the camera at $2E42/$2ECC) would draw them: camera struct $FFA1DA ->
 *    room grid (+4, width +$1E, height +$20) -> screen table (+8) -> 16x15
 *    metatile screen -> metatile table (+$10, 4 nametable words each). The
 *    result goes into a shadow copy of the plane-A nametable (VRAM is never
 *    written), at the same plane position the game would use, and the side
 *    pixels are drawn from it with the VDP's own scroll and tile patterns.
 *  - Self-check: each frame two fully visible columns are rebuilt the same way
 *    and compared with VRAM. Only when the model matches what is on screen
 *    (i.e. a stage is really running) are the sides drawn; title, menus, stage
 *    select, cutscenes and anything else get plain black bars.
 *  - Beside rooms the level doesn't continue into (one-screen shafts like
 *    Elec Man's, level ends) the room's outer wall/backdrop metatile columns
 *    are continued outward (build_filler): translated, never flipped, scenery
 *    only. Other room rows during a vertical transition stay black, never a
 *    wrapped/duplicated copy of the plane.
 *  - Plane B (parallax backdrop) is used in the sides only when it is a seamless
 *    256-px loop (the hardware wrap is then genuine); otherwise the side is black.
 *  - Sprites and the HUD stay inside 4:3.
 *
 * Presentation only: no guest state is touched. Off = the plain native row.
 */
#include "game_video.h"
#include "video/genesis_vdp.h"
#include "video/genesis_dac.h"
#include "../src/gl/mmww_gl.h"
#include <string.h>

/* 16:9 at the Genesis pixel aspect: H40 (320) -> 424, H32 (256) -> 342.
 * Wily Wars runs its stages in H32. The runner always asks width() with
 * native 320, so the width follows the mode seen on the previous frame. */
enum { WIDE_W40 = 424, WIDE_W32 = 342 };
static int s_on, s_native_w = 256;

static int v_configure(const char *mode)
{
    if (!mode) return 0;
    if (!strcmp(mode, "off")) s_on = 0;
    else if (!strcmp(mode, "16:9") || !strcmp(mode, "fit") || !strcmp(mode, "on") ||
             !strcmp(mode, "adaptive")) s_on = 1;
    else return 0;
    return 1;
}
static int v_enabled(void) { return s_on; }
static int v_width(int dw, int dh, int nw, int nh)
{
    (void)dw; (void)dh; (void)nh;
    (void)nw;
    return s_on ? (s_native_w == 320 ? WIDE_W40 : WIDE_W32) : 0;
}

static unsigned vram16(const GVDP *v, unsigned a)
{
    a &= 0xFFFEu;
    return (unsigned)v->vram[a] << 8 | v->vram[a + 1];
}

/* One plane pixel: returns CRAM index (0 = transparent in its line) and
 * priority via *pri. */
static int plane_px(const GVDP *v, int plane, int line, int sx, int *pri)
{
    static const int cells[4] = {32, 64, 32, 128};
    int wc = cells[v->reg[16] & 3], hc = cells[(v->reg[16] >> 4) & 3];
    unsigned base = plane ? ((unsigned)(v->reg[4] & 7) << 13)
                          : ((unsigned)(v->reg[2] & 0x38) << 10);
    unsigned hs_base = (unsigned)(v->reg[13] & 0x3F) << 10;
    int hmode = v->reg[11] & 3, idx = hmode == 0 ? 0 : hmode == 2 ? (line & ~7) : line;
    int hs = (int)(vram16(v, hs_base + (unsigned)idx * 4u + (plane ? 2u : 0u)) & 0x3FF);
    if (hs & 0x200) hs -= 0x400;
    int vs = v->vsram[plane ? 1 : 0] & 0x3FF;
    int pw = wc * 8, ph = hc * 8;
    int px = ((sx - hs) % pw + pw) % pw, py = ((line + vs) % ph + ph) % ph;
    unsigned e = vram16(v, base + (unsigned)((py >> 3) * wc + (px >> 3)) * 2u);
    int r = py & 7, c = px & 7;
    if (e & 0x1000) r = 7 - r;
    if (e & 0x0800) c = 7 - c;
    unsigned pat = (e & 0x7FFu) * 32u + (unsigned)r * 4u + (unsigned)(c >> 1);
    int nib = (c & 1) ? (v->vram[pat & 0xFFFFu] & 15) : (v->vram[pat & 0xFFFFu] >> 4);
    *pri = (e >> 15) & 1;
    return nib ? (int)(((e >> 13) & 3) * 16 + nib) : 0;
}

/* ------------------------------------------------- level-data model */
extern uint8_t g_rom[0x400000];
extern uint8_t g_ram[0x010000];

static unsigned rd8(uint32_t a)
{
    a &= 0xFFFFFFu;
    if (a >= 0xFF0000u) return g_ram[a & 0xFFFFu];
    return a < 0x400000u ? g_rom[a] : 0;
}
static unsigned rd16(uint32_t a) { return rd8(a) << 8 | rd8(a + 1); }
static uint32_t rd32(uint32_t a) { return (uint32_t)rd16(a) << 16 | rd16(a + 2); }

enum { CAM = 0xFFA1DA, ROWOFS_TAB = 0x6CAA2, MARGIN_MAX = 64 };

static uint16_t s_shadow[32][64];   /* plane-A nametable as the wide view would need it */
static uint8_t  s_colok[64];        /* 1 = column holds real stage for the current frame */
static int      s_band_row0;        /* first cell row of the current room row (30 rows) */
static int      s_ok, s_pass, s_fail, s_b_loop;

/* Screen data pointer for room (col,row) of the level grid, 0 if none ($37B8). */
static uint32_t room_screen(int col, int row)
{
    unsigned w = rd16(CAM + 0x1E), h = rd16(CAM + 0x20);
    if (col < 0 || row < 0 || (unsigned)col >= w || (unsigned)row >= h) return 0;
    unsigned id = rd16(rd32(CAM + 4) + (uint32_t)(row * w + col) * 2u) & 0xFF;
    if (id == 0xFF) return 0;
    return rd32(rd32(CAM + 8) + id * 4u);
}

/* Build the 15-metatile column the game's streamer would draw at level x
 * (16-aligned) for room row `row`, as (cell row, 2 words) pairs into out[30][2].
 * Returns 0 when the room doesn't exist. */
static int build_column(int x, int row, uint16_t out[30][2], int *row0)
{
    uint32_t scr = room_screen(x >> 8, row);
    if (!scr || scr >= 0x400000u) return 0;
    uint32_t mt = rd32(CAM + 0x10);
    unsigned ofs = rd16(ROWOFS_TAB + (uint32_t)(row & 0x7FFF) * 2u);
    *row0 = (int)((ofs >> 7) & 31);
    int mc = (x >> 4) & 15;
    for (int r = 0; r < 15; r++) {
        unsigned m = rd8(scr + (uint32_t)(r * 16 + mc));
        uint32_t e = mt + m * 8u;
        out[r * 2][0] = (uint16_t)rd16(e);     out[r * 2][1] = (uint16_t)rd16(e + 2);
        out[r * 2 + 1][0] = (uint16_t)rd16(e + 4); out[r * 2 + 1][1] = (uint16_t)rd16(e + 6);
    }
    return 1;
}
static void put_column(int x, uint16_t col[30][2], int row0, int flag)
{
    int c = (x >> 3) & 63;
    for (int r = 0; r < 30; r++) {
        int pr = (row0 + r) & 31;
        s_shadow[pr][c] = col[r][0];
        s_shadow[pr][(c + 1) & 63] = col[r][1];
    }
    s_colok[c] = s_colok[(c + 1) & 63] = (uint8_t)flag;
}
/* Scenery beside rooms the level doesn't continue into (one-screen shafts,
 * level ends). Not a mirror: the room's outer two metatile columns are
 * translated outward (same order, never flipped), keeping only scenery:
 * background metatiles (collision 0) and solid metatiles that are part of a
 * wall/ground mass at least two metatiles tall. Anything else at the edge (platform lips, ladders,
 * spikes, doors) is replaced by the nearest kept scenery metatile above or
 * below it (or the room's commonest background metatile). */
static int build_filler(int x, int row, int cam, int native_w, uint16_t out[30][2], int *row0)
{
    int rc, k, src;
    if (x < cam) { rc = cam >> 8; if (x >= rc * 256) return 0; k = (rc * 256 - x) >> 4; src = (k & 1) ? 1 : 0; }
    else { rc = (cam + native_w - 1) >> 8; if (x < rc * 256 + 256) return 0; k = (x - rc * 256 - 240) >> 4; src = (k & 1) ? 14 : 15; }
    if (k < 1 || k > 6) return 0;
    uint32_t scr = room_screen(rc, row);
    if (!scr || scr >= 0x400000u) return 0;
    uint32_t mt = rd32(CAM + 0x10), at = rd32(CAM + 0x14);
    if (at >= 0x400000u) return 0;
    /* commonest background (collision 0) metatile in the room */
    unsigned cnt[256] = {0}, bg = 0, best = 0;
    for (int i = 0; i < 240; i++) { unsigned m = rd8(scr + (uint32_t)i); if (!rd8(at + m)) cnt[m]++; }
    for (int m = 0; m < 256; m++) if (cnt[m] > best) { best = cnt[m]; bg = (unsigned)m; }
    if (!best) return 0;
    unsigned ofs = rd16(ROWOFS_TAB + (uint32_t)(row & 0x7FFF) * 2u);
    *row0 = (int)((ofs >> 7) & 31);
    unsigned ms[15]; int keep[15];
    for (int r = 0; r < 15; r++) {
        unsigned m = rd8(scr + (uint32_t)(r * 16 + src)), a = rd8(at + m);
        keep[r] = a == 0;
        if (a == 1) {
            unsigned up = r > 0 ? rd8(at + rd8(scr + (uint32_t)((r - 1) * 16 + src))) : 1;
            unsigned dn = r < 14 ? rd8(at + rd8(scr + (uint32_t)((r + 1) * 16 + src))) : 1;
            keep[r] = up == 1 || dn == 1;  /* thick wall/ground/canopy, not a one-tile ledge */
        }
        ms[r] = m;
    }
    for (int r = 0; r < 15; r++) {
        unsigned m = ms[r];
        if (!keep[r]) {   /* fill the gap with the nearest scenery above/below */
            m = bg;
            for (int d = 1; d < 15; d++) {
                if (r - d >= 0 && keep[r - d]) { m = ms[r - d]; break; }
                if (r + d < 15 && keep[r + d]) { m = ms[r + d]; break; }
            }
        }
        uint32_t e = mt + m * 8u;
        out[r * 2][0] = (uint16_t)rd16(e);     out[r * 2][1] = (uint16_t)rd16(e + 2);
        out[r * 2 + 1][0] = (uint16_t)rd16(e + 4); out[r * 2 + 1][1] = (uint16_t)rd16(e + 6);
    }
    return 1;
}

/* Fraction check: does VRAM hold this model column? */
static int column_matches(const GVDP *v, int x, int row)
{
    uint16_t col[30][2]; int row0;
    if (!build_column(x, row, col, &row0)) return 0;
    unsigned base = (unsigned)(v->reg[2] & 0x38) << 10;
    int c = (x >> 3) & 63, same = 0;
    for (int r = 0; r < 30; r++) {
        int pr = (row0 + r) & 31;
        same += vram16(v, base + (unsigned)(pr * 64 + c) * 2u) == col[r][0];
        same += vram16(v, base + (unsigned)(pr * 64 + ((c + 1) & 63)) * 2u) == col[r][1];
    }
    return same >= 54;   /* >= 90% of the 60 cells (allows a few live tile edits) */
}

static int floor16(int x) { return x >= 0 ? x & ~15 : -((-x + 15) & ~15); }

/* Once per frame (line 0). */
static void model_frame(const GVDP *v, int native_w, int margin)
{
    static const int cells[4] = {32, 64, 32, 128};
    int wc = cells[v->reg[16] & 3], hc = cells[(v->reg[16] >> 4) & 3];
    int ok = 0;
    memset(s_colok, 0, sizeof s_colok);
    if (native_w == 256 && wc == 64 && hc == 32 && (v->reg[1] & 0x40) &&
        rd16(CAM + 0x1E) && rd16(CAM + 0x1E) < 64 && rd16(CAM + 0x20) && rd16(CAM + 0x20) < 64) {
        int cam = (int16_t)rd16(CAM + 0x46), camy = (int16_t)rd16(CAM + 0x4E);
        int row = (int16_t)rd16(CAM + 0x1A);
        /* the screen must be settled on one room row (no vertical transition) */
        int settled = row >= 0 && camy == row * 240;
        if (settled) {
            int l = floor16(cam) + 32, r = floor16(cam + native_w) - 48;
            ok = column_matches(v, l, row) && column_matches(v, r, row);
        }
        if (ok) {
            unsigned base = (unsigned)(v->reg[2] & 0x38) << 10;
            for (int y = 0; y < 32; y++)
                for (int x = 0; x < 64; x++)
                    s_shadow[y][x] = (uint16_t)vram16(v, base + (unsigned)(y * 64 + x) * 2u);
            uint16_t col[30][2]; int row0 = 0;
            int lo = floor16(cam - margin - 16), hi = floor16(cam + native_w + margin + 16);
            for (int x = lo; x <= hi; x += 16)
                if (build_column(x, row, col, &row0)) put_column(x, col, row0, 1);
                else if (build_filler(x, row, cam, native_w, col, &row0)) put_column(x, col, row0, 2);
            s_band_row0 = (int)((rd16(ROWOFS_TAB + (uint32_t)row * 2u) >> 7) & 31);
            /* plane B: genuine only when it is a seamless 256-px loop */
            unsigned bb = (unsigned)(v->reg[4] & 7) << 13;
            s_b_loop = 1;
            for (int y = 0; y < 32 && s_b_loop; y++)
                for (int x = 0; x < 32; x++)
                    if (vram16(v, bb + (unsigned)(y * 64 + x) * 2u) !=
                        vram16(v, bb + (unsigned)(y * 64 + x + 32) * 2u)) { s_b_loop = 0; break; }
        }
    }
    /* hysteresis: on after 2 matching frames, off after 3 failing ones */
    if (ok) { s_fail = 0; if (++s_pass >= 2) s_ok = 1; }
    else    { s_pass = 0; if (++s_fail >= 3) s_ok = 0; }
    if (!ok && s_ok) { /* keep last good shadow for the short grace period */ }
}

static int plane_hs(const GVDP *v, int plane, int line)
{
    unsigned hs_base = (unsigned)(v->reg[13] & 0x3F) << 10;
    int hmode = v->reg[11] & 3, idx = hmode == 0 ? 0 : hmode == 2 ? (line & ~7) : line;
    int hs = (int)(vram16(v, hs_base + (unsigned)idx * 4u + (plane ? 2u : 0u)) & 0x3FF);
    if (hs & 0x200) hs -= 0x400;
    return hs;
}

/* Side pixel from the shadow plane A + VRAM plane B. Returns 0 if not real stage. */
static int side_px2(const GVDP *v, int line, int sx, uint32_t *out)
{
    /* plane A from the shadow nametable */
    int hs = plane_hs(v, 0, line), vs = v->vsram[0] & 0x3FF;
    int px = ((sx - hs) % 512 + 512) % 512, py = ((line + vs) % 256 + 256) % 256;
    int c = px >> 3, cr = py >> 3;
    if (!s_colok[c]) return 0;
    if (((cr - s_band_row0) & 31) >= 30) return 0;
    unsigned e = s_shadow[cr][c];
    int r = py & 7, cc = px & 7;
    if (e & 0x1000) r = 7 - r;
    if (e & 0x0800) cc = 7 - cc;
    unsigned pat = (e & 0x7FFu) * 32u + (unsigned)r * 4u + (unsigned)(cc >> 1);
    int nib = (cc & 1) ? (v->vram[pat & 0xFFFFu] & 15) : (v->vram[pat & 0xFFFFu] >> 4);
    int a = nib ? (int)(((e >> 13) & 3) * 16 + nib) : 0, pa = (e >> 15) & 1;
    int idx;
    if (a && pa) idx = a;
    else {
        int pb = 0, b = 0;
        if (s_b_loop) b = plane_px(v, 1, line, sx, &pb);
        else if (!a && s_colok[c] != 2) return 0;   /* backdrop would be a guess */
        if (a && (pa >= pb || !b)) idx = a;
        else if (b) idx = b;
        else idx = v->reg[7] & 0x3F;
    }
    *out = genesis_dac_cram_to_argb(v->cram[idx & 63], GENESIS_DAC_NORMAL);
    return 1;
}

#include <stdio.h>
#include <stdlib.h>
static void ws_debug(const GVDP *v)
{   /* MMWW_WSDEBUG=1: log the model state once a second */
    static int frame; const char *e = getenv("MMWW_WSDEBUG");
    if (!e || (++frame % 60)) return;
    fprintf(stderr, "[WS] f=%d ok=%d cam=%d camy=%d row=%d W=%u H=%u bloop=%d band=%d hs=%d vs=%d w=%d\n",
            frame, s_ok, (int16_t)rd16(CAM + 0x46), (int16_t)rd16(CAM + 0x4E), (int16_t)rd16(CAM + 0x1A),
            rd16(CAM + 0x1E), rd16(CAM + 0x20), s_b_loop, s_band_row0, plane_hs(v, 0, 100), v->vsram[0], s_native_w);
}

static void v_scanline(const struct GVDP *vdp, int line, const uint32_t *native,
                       int native_w, uint32_t *out, int width)
{
    int margin = (width - native_w) / 2;
    if (margin < 0) margin = 0;
    if (margin > MARGIN_MAX) margin = MARGIN_MAX;
    if (line == 0) { s_native_w = native_w; model_frame(vdp, native_w, margin); ws_debug(vdp); }
    memcpy(out + margin, native, (size_t)native_w * sizeof *out);
    int rm = width - margin - native_w;
    for (int x = 0; x < margin; x++) out[x] = 0xFF000000u;
    for (int x = 0; x < rm; x++) out[margin + native_w + x] = 0xFF000000u;
    if (!s_ok || !(vdp->reg[1] & 0x40)) return;
    /* Walk outward from each 4:3 edge; from the first column without real
     * stage outward the side stays black (no islands, no wrap copies). */
    for (int k = 0; k < margin; k++) {
        uint32_t p;
        if (!side_px2(vdp, line, -1 - k, &p)) break;
        out[margin - 1 - k] = p;
    }
    for (int k = 0; k < rm; k++) {
        uint32_t p;
        if (!side_px2(vdp, line, native_w + k, &p)) break;
        out[margin + native_w + k] = p;
    }
}

const GameVideo g_mmww_video = {
    .configure = v_configure,
    .enabled = v_enabled,
    .width = v_width,
    .scanline = v_scanline,
};

int mmww_widescreen_get(void) { return s_on; }
void mmww_widescreen_set(int on) { v_configure(on ? "16:9" : "off"); }

int mmww_video_native_w(void) { return s_native_w; }
