/* Opt-in 16:9 widescreen through the framework's GameVideo hook (default Off).
 *
 * The VDP renders the authentic 320-pixel row untouched; this adapter widens
 * it to 16:9 at the Genesis pixel aspect (H32 256 -> 342, H40 320 -> 424). The side
 * columns are border fill drawn behind/around the game, never game draws:
 *
 *  - Stage background from VRAM where it is really there. Planes are 512 px
 *    wide, so the columns just past the 4:3 edge usually hold genuine level
 *    tiles. Every frame each nametable column is hashed; when it changes it is
 *    tagged with the level x it was written for (from the plane's unwrapped
 *    scroll). A side pixel is drawn from planes A/B only if both planes'
 *    columns are tagged with exactly the level x being shown, so stale tiles
 *    left over from an earlier wrap of the plane never appear.
 *  - Otherwise a mirrored, darkened copy of the 4:3 edge (the Magical Tetris
 *    "stage" border trick) hides the seam.
 *  - Sprites and the HUD stay inside 4:3: nothing is ever drawn from the
 *    sprite table in the side columns, so no pop-in or culling artefacts.
 *
 * Presentation only: no guest state is touched. Off = the plain 320 row.
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
static int plane_hs(const GVDP *v, int plane, int line);
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

static uint32_t side_px(const GVDP *v, int line, int sx)
{
    int pa, pb;
    int a = plane_px(v, 0, line, sx, &pa), b = plane_px(v, 1, line, sx, &pb);
    int idx;
    if (a && (pa >= pb || !b)) idx = a;
    else if (b) idx = b;
    else idx = v->reg[7] & 0x3F;
    return genesis_dac_cram_to_argb(v->cram[idx & 63], GENESIS_DAC_NORMAL);
}

/* ---------------------------------------------------- column validity */
typedef struct {
    uint32_t hash[128];
    int32_t  world[128];   /* level x (multiple of 8) the column was written for */
    uint8_t  tagged[128];
    int32_t  cam;          /* unwrapped level x of screen x = 0 */
    int      prev_hs, have, pw;
} PlaneTrack;
static PlaneTrack s_pt[2];

static int plane_hs(const GVDP *v, int plane, int line)
{
    unsigned hs_base = (unsigned)(v->reg[13] & 0x3F) << 10;
    int hmode = v->reg[11] & 3, idx = hmode == 0 ? 0 : hmode == 2 ? (line & ~7) : line;
    int hs = (int)(vram16(v, hs_base + (unsigned)idx * 4u + (plane ? 2u : 0u)) & 0x3FF);
    return hs;
}
static int floor_div(int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }
static int wrap_near(int x, int center, int pw)
{   /* the value congruent to x (mod pw) nearest to center */
    int d = ((x - center) % pw + pw) % pw;
    if (d >= pw / 2) d -= pw;
    return center + d;
}

/* Once per frame (line 0): follow each plane's scroll and re-tag changed columns. */
static void track_frame(const GVDP *v)
{
    static const int cells[4] = {32, 64, 32, 128};
    int wc = cells[v->reg[16] & 3], hc = cells[(v->reg[16] >> 4) & 3];
    for (int p = 0; p < 2; p++) {
        PlaneTrack *t = &s_pt[p];
        int pw = wc * 8;
        int hs = plane_hs(v, p, 0);
        if (!t->have || t->pw != pw) {
            memset(t, 0, sizeof *t);
            t->pw = pw; t->have = 1;
            t->cam = -hs; t->prev_hs = hs;
        } else {
            int d = -(hs - t->prev_hs);              /* camera moves opposite to hscroll */
            d = ((d % 1024) + 1024 + 512) % 1024 - 512;
            t->cam += d; t->prev_hs = hs;
        }
        unsigned base = p ? ((unsigned)(v->reg[4] & 7) << 13) : ((unsigned)(v->reg[2] & 0x38) << 10);
        for (int c = 0; c < wc && c < 128; c++) {
            uint32_t h = 2166136261u;
            int blank = 0;
            for (int r = 0; r < hc; r++) {
                unsigned e = vram16(v, base + (unsigned)(r * wc + c) * 2u);
                if (!(e & 0x7FFu)) blank++;
                h ^= e;
                h *= 16777619u;
            }
            if (h != t->hash[c]) {
                t->hash[c] = h;
                /* column c sits at plane x c*8; tag it with the level x nearest the view centre */
                t->world[c] = wrap_near(c * 8, t->cam + s_native_w / 2, pw);
                /* a foreground column that is mostly tile 0 was cleared, not
                 * streamed: the level hasn't been drawn there yet. (Wily Wars
                 * draws plane A only one column past the 256-px view.) */
                t->tagged[c] = !(p == 0 && blank * 2 >= hc);
            }
        }
    }
}

static int column_valid(const GVDP *v, int plane, int line, int sx)
{
    PlaneTrack *t = &s_pt[plane];
    if (!t->have) return 0;
    /* this line's own scroll (per-line / per-cell modes) relative to line 0 */
    int dl = -(plane_hs(v, plane, line) - t->prev_hs);
    dl = ((dl % 1024) + 1024 + 512) % 1024 - 512;
    int wx = t->cam + dl + sx;
    int col8 = floor_div(wx, 8) * 8;
    int c = ((col8 % t->pw) + t->pw) % t->pw / 8;
    return t->tagged[c] && t->world[c] == col8;
}

static uint32_t darken(uint32_t p)
{   /* ~55%: reads as border, hides the mirror seam */
    uint32_t rb = ((p & 0xFF00FFu) * 141u >> 8) & 0xFF00FFu;
    uint32_t g  = ((p & 0x00FF00u) * 141u >> 8) & 0x00FF00u;
    return 0xFF000000u | rb | g;
}

#include <stdio.h>
#include <stdlib.h>
static void ws_debug(const GVDP *v)
{   /* MMWW_WSDEBUG=<frame>: dump plane A columns around the right edge once */
    static int frame, done; const char *e = getenv("MMWW_WSDEBUG");
    if (!e || done || ++frame < atoi(e)) return;
    done = 1;
    static const int cells[4] = {32, 64, 32, 128};
    int wc = cells[v->reg[16] & 3], hc = cells[(v->reg[16] >> 4) & 3];
    unsigned base = (unsigned)(v->reg[2] & 0x38) << 10;
    PlaneTrack *t = &s_pt[0];
    for (int sx = -48; sx < s_native_w + 48; sx += 8) {
        int wx = t->cam + sx, col8 = floor_div(wx, 8) * 8;
        int c = ((col8 % t->pw) + t->pw) % t->pw / 8;
        fprintf(stderr, "[WS] sx=%4d c=%2d tagged=%d world=%d want=%d valid=%d :", sx, c, t->tagged[c], t->world[c], col8, column_valid(v,0,100,sx));
        for (int r = 0; r < hc && r < 32; r++) fprintf(stderr, " %04X", vram16(v, base + (unsigned)(r * wc + c) * 2u));
        fprintf(stderr, "\n");
    }
}

static int side_valid(const GVDP *v, int line, int sx)
{
    return column_valid(v, 0, line, sx) && column_valid(v, 1, line, sx);
}

static void v_scanline(const struct GVDP *vdp, int line, const uint32_t *native,
                       int native_w, uint32_t *out, int width)
{
    int margin = (width - native_w) / 2;
    if (margin < 0) margin = 0;
    if (line == 0) { track_frame(vdp); s_native_w = native_w; ws_debug(vdp); }
    memcpy(out + margin, native, (size_t)native_w * sizeof *out);
    if (!(vdp->reg[1] & 0x40)) {
        for (int x = 0; x < margin; x++) {
            out[x] = 0xFF000000u;
            if (margin + native_w + x < width) out[margin + native_w + x] = 0xFF000000u;
        }
        return;
    }
    /* Walk outward from each 4:3 edge. Real stage tiles while the columns are
     * valid; from the first invalid column outward, the planes (no sprites,
     * no HUD) mirrored about the edge and darkened. */
    int ok = 1;
    for (int k = 0; k < margin; k++) {                 /* left: screen x = -1-k */
        int sx = -1 - k;
        if (ok && !side_valid(vdp, line, sx)) ok = 0;
        out[margin - 1 - k] = ok ? side_px(vdp, line, sx)
                                 : darken(side_px(vdp, line, k % native_w));
    }
    ok = 1;
    for (int k = 0; k < margin && margin + native_w + k < width; k++) {   /* right */
        int sx = native_w + k;
        if (ok && !side_valid(vdp, line, sx)) ok = 0;
        out[margin + native_w + k] = ok ? side_px(vdp, line, sx)
                                        : darken(side_px(vdp, line, native_w - 1 - (k % native_w)));
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
