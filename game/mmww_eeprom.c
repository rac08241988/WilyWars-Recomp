/*
 * mmww_eeprom.c — the save chip of Mega Man: The Wily Wars.
 *
 * The game carries two save drivers ($06DB1A dispatches through the table at
 * $06DB4A). Its probe at $06DB6A reads bit 1 of $200001: when that bit reads
 * back high and steady the game uses its Microwire driver, which is what we
 * emulate: a 93C46-style serial EEPROM, 64 x 16-bit words (128 bytes).
 *
 * Lines at $200001 (byte writes): bit 2 = CS, bit 1 = SK (clock),
 * bit 0 = DI. Reads: bit 1 = 1 (selects this driver), bit 0 = DO.
 * Commands after a start bit (1), MSB first, 2 opcode + 6 address bits:
 *   10 aaaaaa READ  (dummy 0, then D15..D0)   01 aaaaaa WRITE (+16 data)
 *   11 aaaaaa ERASE                           00 11xxxx EWEN, 00 00xxxx EWDS
 *   00 10xxxx ERAL                            00 01xxxx WRAL (+16 data)
 * DO reads 1 (ready) whenever no data is being shifted out; writes complete
 * instantly. The contents persist to "<rom>.eep" beside the runner's .srm.
 * Clean-room implementation from the 93C46 command set; no emulator code.
 */
#include "mmww_eeprom.h"
#include "genesis_bus.h"
#include "host_atomic_file.h"
#include <stdio.h>
#include <string.h>

enum { EE_IDLE, EE_CMD, EE_READ, EE_WDATA, EE_DONE };

typedef struct {
    uint16_t mem[64];
    uint8_t  latch;      /* last value written to $200001 */
    uint8_t  state;
    uint8_t  nbits;
    uint8_t  dout;
    uint16_t cmd;
    uint16_t shift;
    uint8_t  addr;
    uint8_t  op;         /* 1 = WRITE, 4 = WRAL */
    uint8_t  ewen;
    uint8_t  pad[3];
} MmwwEeprom;

static MmwwEeprom s_ee;
static char s_path[1024];
static int  s_dirty;

static void ee_persist(void)
{
    uint8_t img[128];
    if (!s_path[0]) return;
    for (int i = 0; i < 64; i++) {
        img[i * 2]     = (uint8_t)(s_ee.mem[i] >> 8);
        img[i * 2 + 1] = (uint8_t)s_ee.mem[i];
    }
    if (!genesis_atomic_file_store(s_path, img, sizeof img))
        fprintf(stderr, "[EEPROM] save to %s failed\n", s_path);
    else
        fprintf(stderr, "[EEPROM] saved %s\n", s_path);
}

static void ee_store(uint8_t addr, uint16_t v, int all)
{
    if (all) for (int i = 0; i < 64; i++) s_ee.mem[i] = v;
    else s_ee.mem[addr & 63] = v;
    s_dirty = 1;                       /* flushed once per frame */
}

static void ee_rising_edge(int di)
{
    switch (s_ee.state) {
    case EE_IDLE:
    case EE_DONE:
        if (di) { s_ee.state = EE_CMD; s_ee.cmd = 0; s_ee.nbits = 0; }
        s_ee.dout = 1;
        break;
    case EE_CMD:
        s_ee.cmd = (uint16_t)((s_ee.cmd << 1) | (di & 1));
        if (++s_ee.nbits < 8) break;
        s_ee.addr = (uint8_t)(s_ee.cmd & 0x3F);
        switch ((s_ee.cmd >> 6) & 3) {
        case 2:                                    /* READ */
            s_ee.shift = s_ee.mem[s_ee.addr];
            s_ee.nbits = 0; s_ee.dout = 0;         /* dummy zero */
            s_ee.state = EE_READ;
            break;
        case 1:                                    /* WRITE */
            s_ee.op = 1; s_ee.shift = 0; s_ee.nbits = 0; s_ee.state = EE_WDATA;
            break;
        case 3:                                    /* ERASE */
            ee_store(s_ee.addr, 0xFFFF, 0); s_ee.state = EE_DONE; s_ee.dout = 1;
            break;
        default:
            switch ((s_ee.addr >> 4) & 3) {
            case 3: s_ee.ewen = 1; break;          /* EWEN */
            case 0: s_ee.ewen = 0; break;          /* EWDS */
            case 2: ee_store(0, 0xFFFF, 1); break; /* ERAL */
            case 1: s_ee.op = 4; s_ee.shift = 0; s_ee.nbits = 0;
                    s_ee.state = EE_WDATA; return; /* WRAL */
            }
            s_ee.state = EE_DONE; s_ee.dout = 1;
            break;
        }
        break;
    case EE_READ:
        s_ee.dout = (uint8_t)((s_ee.shift >> 15) & 1);
        s_ee.shift = (uint16_t)(s_ee.shift << 1);
        if (++s_ee.nbits == 16) {                  /* sequential read */
            s_ee.addr = (uint8_t)((s_ee.addr + 1) & 63);
            s_ee.shift = s_ee.mem[s_ee.addr];
            s_ee.nbits = 0;
        }
        break;
    case EE_WDATA:
        s_ee.shift = (uint16_t)((s_ee.shift << 1) | (di & 1));
        if (++s_ee.nbits == 16) {
            ee_store(s_ee.addr, s_ee.shift, s_ee.op == 4);
            s_ee.state = EE_DONE; s_ee.dout = 1;
        }
        break;
    }
}

static void ee_write8(uint32_t addr, uint8_t v)
{
    if (!(addr & 1)) return;                       /* device is on the odd byte */
    uint8_t old = s_ee.latch;
    s_ee.latch = v;
    if (!(v & 4)) {                                /* CS low: deselect */
        s_ee.state = EE_IDLE; s_ee.dout = 1;
        return;
    }
    if (!(old & 4)) { s_ee.state = EE_IDLE; s_ee.dout = 1; }
    if ((v & 2) && !(old & 2)) ee_rising_edge(v & 1);
}

static uint16_t ee_read16(uint32_t addr)
{
    (void)addr;
    uint8_t b = (uint8_t)(0x02 | (s_ee.dout & 1));
    return (uint16_t)((b << 8) | b);
}

void mmww_eeprom_install(void)
{
    g_cart_io.lo = 0x200000u;
    g_cart_io.hi = 0x200001u;
    g_cart_io.write8 = ee_write8;
    g_cart_io.read16 = ee_read16;
    s_ee.state = EE_IDLE;
    s_ee.dout = 1;
}

void mmww_eeprom_sram_loaded(const char *srm_path, const uint8_t *data, size_t size, int loaded)
{
    (void)data; (void)size; (void)loaded;
    mmww_eeprom_install();
    for (int i = 0; i < 64; i++) s_ee.mem[i] = 0xFFFF;   /* blank chip */
    snprintf(s_path, sizeof s_path, "%s", srm_path ? srm_path : "mmww.srm");
    char *dot = strrchr(s_path, '.');
    if (dot && strlen(dot) == 4) strcpy(dot, ".eep");
    else strncat(s_path, ".eep", sizeof s_path - strlen(s_path) - 1);
    FILE *f = fopen(s_path, "rb");
    uint8_t img[128];
    if (f) {
        size_t got = fread(img, 1, sizeof img, f);
        fclose(f);
        if (got == sizeof img) {
            for (int i = 0; i < 64; i++)
                s_ee.mem[i] = (uint16_t)((img[i * 2] << 8) | img[i * 2 + 1]);
            fprintf(stderr, "[EEPROM] loaded %s\n", s_path);
            return;
        }
        fprintf(stderr, "[EEPROM] ignoring %s (size %zu != 128)\n", s_path, got);
    } else {
        fprintf(stderr, "[EEPROM] no save yet (%s), blank chip\n", s_path);
    }
}

size_t mmww_eeprom_rb_save(void *dst, size_t cap)
{
    if (!dst) return sizeof s_ee;
    if (cap < sizeof s_ee) return 0;
    memcpy(dst, &s_ee, sizeof s_ee);
    return sizeof s_ee;
}

int mmww_eeprom_rb_load(const void *src, size_t len)
{
    if (len != sizeof s_ee) return 0;
    memcpy(&s_ee, src, sizeof s_ee);
    return 1;
}

/* Called once per frame (V-int): write the chip image after the game's
 * save sequence, not once per word. */
void mmww_eeprom_frame(void)
{
    if (s_dirty && (s_ee.latch & 4) == 0) { s_dirty = 0; ee_persist(); }
}
