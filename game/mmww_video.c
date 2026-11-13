/* Opt-in 16:9 widescreen through the framework's GameVideo hook (default Off).
 *
 * The VDP renders the authentic 320-pixel row; this adapter widens it to 424
 * pixels (16:9 at the Genesis pixel aspect) by drawing the extra columns on
 * each side straight from the scroll planes in VRAM: same nametables, scroll
 * size, per-line/cell/full H-scroll and V-scroll the VDP uses, with plane
 * priority. Presentation only: no guest state is touched.
 *
 * Known limits (it's why the default is Off): sprites are not drawn in the
 * side columns, and the game streams level tiles only a little past the 4:3
 * edge, so the outer columns can show stale tiles until the camera scrolls.
 */
#include "game_video.h"
#include "video/genesis_vdp.h"
#include "video/genesis_dac.h"
#include "../src/gl/mmww_gl.h"
#include <string.h>

enum { WIDE_W = 424 };
static int s_on;

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
    return (s_on && nw == 320) ? WIDE_W : 0;
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

static void v_scanline(const struct GVDP *vdp, int line, const uint32_t *native,
                       int native_w, uint32_t *out, int width)
{
    int margin = (width - native_w) / 2;
    if (margin < 0) margin = 0;
    memcpy(out + margin, native, (size_t)native_w * sizeof *out);
    int display = vdp->reg[1] & 0x40;
    for (int x = 0; x < margin; x++) {
        out[x] = display ? side_px(vdp, line, x - margin) : 0xFF000000u;
        int r = margin + native_w + x;
        if (r < width) out[r] = display ? side_px(vdp, line, native_w + x) : 0xFF000000u;
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
