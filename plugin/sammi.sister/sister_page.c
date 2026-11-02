/* sammi.sister: SISTER's screen on the Opus effect page.
 *
 * Copyright (C) 2026 Hundekraft (the screen: its design, geometry, easing, level response and every drawing step,
 * sister_screen.c in OP-1 Workshop 3.1.1) and SammiBabe666 (this port to Opus's drawing calls).
 * GPL-3.0-or-later with the OP-1 firmware permission (NOTICE).
 *
 * After the owner's sketch: a gothic nave of column pairs and pointed arches whose depth follows SPACE, the rose window
 * behind the nun's head like a halo (its petals sing CHOIR's drifting vowels or stand as PIPES' tuned pipes, greener
 * with VOICE, lit by the wet level), the nun, three candles whose flames grow with SWELL and flicker with the tail, MIX
 * as the red bar. Geometry from gen_svg.py (sister_geom.h).
 *
 * What changed for Opus, and only that:
 *  - kennel draws an OS screen (op1_screen_t); Opus draws an effect's page itself from `view`. Here `view` leaves the
 *    four corners empty and the drawing goes under the page (opus_on_frame_under: under the OS's popups and its "off"
 *    overlay), on the frames that follow a `view` call: the page is showing.
 *  - The still art (sister.svg, unchanged) is the Opus art service's (opus_art_text), drawn once and its pixels kept
 *    (opus_page_keep / opus_page_put), as kennel's static-prefix cache keeps it.
 *  - op1_raster_line (Q10 units, widths Q10 panel pixels) is opus_page_stroke_line (Q8 units, widths Q8 panel pixels);
 *    op1_text_* is opus_page_text (same places and sizes). The labels take the encoders' colours as the user's theme
 *    sets them (stock: kennel's blue, green, white and red); the mix bar the fourth encoder's.
 *  - kennel's knobs (op1_fx_knobs_live) are view's par (the LFO in); the telemetry is the instance that ran.
 *  - The key shows on the green label while shift is held (shift + green turns it) and for a moment after a change,
 *    where kennel showed it on green's press page.
 */
#include "opus_plugin.h"
#include "op1fw.h"
#include "sister_geom.h"
#include "sister_opus.h"
#include "sister_art.h"

static const uint8_t white[4] = { 255, 255, 255, 255 };
static const uint8_t green[4] = { 0x00, 0xED, 0x95, 255 };
static const uint8_t teal[4] = { 0x0B, 0x9A, 0x9E, 255 };

static int32_t e_depth = -1, e_xc, e_vow[2], e_lit, e_mix, e_flame[3], e_flick[3], e_green, e_fast, e_slow, e_mid, e_breath;
static uint32_t frames, rng = 0x2545F491u;

/* the page: frame clock (display thread) and what `view` saw */
static volatile unsigned frame_no, view_frame = 0x80000000u, key_until;
static short view_par[8];
static unsigned last_cycles;
volatile int sister_key_cents_mode;
static int green_taken;

/* the still art and its kept pixels */
#define KEPT_WORDS (24 * 1024)
static int art = -1;
static unsigned short *kept, *kept_grey;
static int kept_used = -1, kept_grey_used = -1, kept_rows;
static unsigned char *slot_teal[64], *slot_white[8];
static int n_teal = -1, n_white;

#define VX (SISTER_VPX * 4)                 /* Q10 */
#define VY (SISTER_VPY * 4)

/* Off: the OS has stopped calling the effect's process (sister_inst.blocks stands still), so the page shows SISTER
   switched off: every FX part (the art, the nave, petals, flames, the mix bar) in grey at its own brightness, and the
   tail-driven motion settled at the still sketch. The labels keep their colours, as the OS's own pages do (the OS's
   "off" overlay dims the whole frame over this, as on every effect page). */
static int fx_off;
static unsigned grey_of(unsigned c)
{
    const unsigned y = ((c >> 16 & 255) * 77 + (c >> 8 & 255) * 150 + (c & 255) * 29) >> 8;
    const unsigned g = y * 205 >> 8;                 /* 80 %: less emphasis, still readable */
    return g << 16 | g << 8 | g;
}
static int col(unsigned c) { return OPUS_RGB(fx_off ? grey_of(c) : c); }
static int rgb(const uint8_t *c) { return col(((unsigned)c[0] << 16) | ((unsigned)c[1] << 8) | c[2]); }

static int page_showing(void) { return frame_no - view_frame <= 2u; }

/* Q10 screen coordinates; clipped to the visible rows like Father's helper (kennel's line, unchanged but the call) */
static void line(int x, int y, int xx, int yy, int width, int colour)
{
    if ((y < 1024 && yy < 1024) || (y > 154 * 1024 && yy > 154 * 1024)) return;
    if (y < 1024 || y > 154 * 1024) { int b = y < 1024 ? 1024 : 154 * 1024; x += (int)((int64_t)(xx - x) * (b - y) / (yy - y)); y = b; }
    if (yy < 1024 || yy > 154 * 1024) { int b = yy < 1024 ? 1024 : 154 * 1024; xx += (int)((int64_t)(x - xx) * (b - yy) / (y - yy)); yy = b; }
    opus_page_stroke_line((x + 2) >> 2, (y + 2) >> 2, (xx + 2) >> 2, (yy + 2) >> 2, (width + 2) >> 2, colour);
}
static void cline(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int colour, int mode);
/* a line and its mirror about the axis */
static void line2(int x, int y, int xx, int yy, int colour)
{
    cline(x, y, xx, yy, colour, 0);
    cline(2 * VX - x, y, 2 * VX - xx, yy, colour, 0);
}
/* ---- the live parts against the still art (sister.svg holds the ring, the quatrefoil and the nun's outline, drawn
   first): the nave stays outside the ring's outer edge, where the black disc used to hide it, and the petals and pipes
   inside its inner edge, where the ring used to cover them; both stop half a pixel outside the nun's outline, where
   her black fill used to hide them. Segments are walked in steps of at most 2 px and each visible stretch is drawn as
   one line, its ends found by bisection. ---- */
#define RX (SISTER_ROSE_X * 4)
#define RY (SISTER_ROSE_Y * 4)
static int32_t nun_half(int32_t y)               /* y Q10 -> the veil's half width Q10 (0 outside her rows) */
{
    const int32_t y8 = y >> 2;
    const unsigned n = sizeof sister_veil / sizeof sister_veil[0];
    if (y8 < sister_veil[0].y || y8 > SISTER_HEM_Y) return 0;
    for (unsigned i = 1; i < n; i++)
        if (y8 <= sister_veil[i].y) {
            const int32_t y0 = sister_veil[i - 1].y, y1 = sister_veil[i].y, x0 = sister_veil[i - 1].x, x1 = sister_veil[i].x;
            const int32_t x = y1 > y0 ? x0 + (x1 - x0) * (y8 - y0) / (y1 - y0) : x1;
            return (SISTER_VPX - x) * 4;
        }
    return (SISTER_VPX - sister_veil[n - 1].x) * 4;
}
/* hidden: mode 0 (the nave) inside the ring's outer edge or behind the nun; mode 1 (petals) outside the ring's inner
   edge or behind the nun. Q10 in; the distance test in Q4 stays inside 32 bits */
static int hidden(int32_t x, int32_t y, int mode)
{
    const int32_t dx = (x - RX) >> 6, dy = (y - RY) >> 6;
    const int32_t d2 = dx * dx + dy * dy;
    if (mode ? d2 > (SISTER_PETAL_MAX_R >> 4) * (SISTER_PETAL_MAX_R >> 4) : d2 < (SISTER_NAVE_R >> 4) * (SISTER_NAVE_R >> 4)) return 1;
    const int32_t h = nun_half(y);
    return h && (x > VX ? x - VX : VX - x) < h + 512;
}
static void cline(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int colour, int mode)
{
    if (!mode) {                                    /* the nave: most of it is clear of the rose and the nun */
        const int32_t lo = x0 < x1 ? x0 : x1, hi = x0 < x1 ? x1 : x0, top = y0 < y1 ? y0 : y1, bot = y0 < y1 ? y1 : y0;
        const int32_t rr = SISTER_NAVE_R * 4;
        const int rose = hi > RX - rr && lo < RX + rr && bot > RY - rr && top < RY + rr;
        const int nun = bot > sister_veil[0].y * 4 && top < SISTER_HEM_Y * 4 && hi > VX - 30 * 1024 && lo < VX + 30 * 1024;
        if (!rose && !nun) { line(x0, y0, x1, y1, 1024, colour); return; }
    }
    const int32_t ax = x1 > x0 ? x1 - x0 : x0 - x1, ay = y1 > y0 ? y1 - y0 : y0 - y1;
    const int n = (int)((ax > ay ? ax : ay) >> 11) + 1;            /* steps of at most 2 px */
    int32_t sx = x0, sy = y0, px = x0, py = y0;
    int pv = !hidden(x0, y0, mode);
    for (int k = 1; k <= n; k++) {
        const int32_t qx = x0 + (x1 - x0) * k / n, qy = y0 + (y1 - y0) * k / n;      /* |d| * k < 2^31 */
        const int qv = !hidden(qx, qy, mode);
        if (qv != pv) {                                /* the edge between p and q: 8 halvings, under 1/100 px */
            int32_t vx = pv ? px : qx, vy = pv ? py : qy, hx = pv ? qx : px, hy = pv ? qy : py;
            for (int i = 0; i < 8; i++) {
                const int32_t mx = (vx + hx) >> 1, my = (vy + hy) >> 1;
                if (hidden(mx, my, mode)) { hx = mx; hy = my; } else { vx = mx; vy = my; }
            }
            if (pv) line(sx, sy, vx, vy, 1024, colour);    /* a visible stretch ends */
            else { sx = vx; sy = vy; }                       /* one begins */
        }
        px = qx; py = qy; pv = qv;
    }
    if (pv) line(sx, sy, x1, y1, 1024, colour);
}
static void shade(uint8_t *o, const uint8_t *c, int32_t q15)
{
    for (unsigned i = 0; i < 3; i++) o[i] = (uint8_t)(1 + (((int32_t)c[i] - 1) * q15 >> 15));
    o[3] = 255;
}
static void blend(uint8_t *o, const uint8_t *a, const uint8_t *b, int32_t t15)
{
    for (unsigned i = 0; i < 3; i++) o[i] = (uint8_t)(a[i] + (((int32_t)b[i] - a[i]) * t15 >> 15));
    o[3] = 255;
}
/* one easing step: v moves toward t by a (Q15) of the way, at least one unit */
static int32_t ease(int32_t v, int32_t t, int32_t a)
{
    int32_t d = (int32_t)(((int64_t)(t - v) * a) >> 15);
    if (!d && t != v) d = t > v ? 1 : -1;
    return v + d;
}
/* log2 of x in Q8 (x > 0): the integer part from the leading bit, then 8 mantissa bits (linear in between) */
static int32_t log2q8(uint32_t x)
{
    const int b = 31 - __builtin_clz(x);
    const uint32_t m = b >= 8 ? x >> (b - 8) : x << (8 - b);
    return b * 256 + (int32_t)(m & 255);
}
static int32_t persp(int32_t z) { return (int32_t)((4096 * 4096) / (4096 + z)); }   /* depth Q12 -> scale Q12 */

#ifdef __bfin__
static inline unsigned cycles(void)
{
    unsigned c;
    __asm__ volatile ("%0 = CYCLES;" : "=d"(c));
    return c;
}
#else
unsigned sister_host_cycles;                       /* the host tests (tests/opus_host_test.c) set the clock */
static inline unsigned cycles(void) { return sister_host_cycles; }
#endif

/* the instance this page shows: the master's when its last knobs are the page's and the mono one's are not, the mono
   one's when its are; otherwise (knobs turned while it is off, an LFO between the two copies) the last one chosen */
static const struct sister_inst *shown(const int32_t k[4])
{
    static const struct sister_inst *last;
    const struct sister_inst *m = sister_last[1], *s = sister_last[0];
#define SAME(i) ((i) && (i)->pos.valid && (i)->pos.raw[0] == k[0] && (i)->pos.raw[1] == k[1] && (i)->pos.raw[2] == k[2] && (i)->pos.raw[3] == k[3])
    if (SAME(m) && !SAME(s)) last = m;
    else if (SAME(s)) last = s;
    else if (!last) last = s ? s : m;
#undef SAME
    return last;
}

/* on: the shown instance's process ran within the last frames (a block is 1.45 ms, a frame ~33 ms: ~23 blocks a frame;
   4 frames without a block, ~0.13 s, is off). No instance yet: off. */
static int running(const struct sister_inst *in)
{
    static const struct sister_inst *seen;
    static uint32_t last_blocks;
    static unsigned idle = 1000;
    if (!in) return 0;
    const uint32_t b = in->blocks;
    if (in != seen) { seen = in; last_blocks = b; idle = 3; }   /* a new instance: one frame to show it runs */
    else if (b != last_blocks) { last_blocks = b; idle = 0; }
    else if (idle < 1000) idle++;
    return idle < 4;
}

/* the still art: drawn once on the cleared frame (its normal colours, or grey for off: two kept copies), its lit pixels
   kept, put back every frame after */
static void still(void)
{
    int rows;
    if (art < 0 || !kept || !opus_page_pixels(&rows))
        return;
    if (rows != kept_rows)
        kept_used = kept_grey_used = -1;
    const int grey = fx_off && kept_grey;
    unsigned short *const buf = grey ? kept_grey : kept;
    int *const used = grey ? &kept_grey_used : &kept_used;
    if (*used < 0) {
        if (!opus_art_ready(art))
            return;
        if (n_teal < 0) {                            /* sister.svg's two colours: teal and white */
            n_teal = opus_art_colour_slots(art, 0x0B9A9E, slot_teal, 64);
            n_white = opus_art_colour_slots(art, 0xFFFFFF, slot_white, 8);
        }
        opus_art_set_colour(slot_teal, n_teal, grey ? grey_of(0x0B9A9E) : 0x0B9A9E);
        opus_art_set_colour(slot_white, n_white, grey ? grey_of(0xFFFFFF) : 0xFFFFFF);
        opus_art_draw(art);
        *used = opus_page_keep(buf, KEPT_WORDS, 0, rows - 1, 0, rows / 2 - 1);
        kept_rows = rows;                            /* *used < 0: it does not fit, drawn again each frame */
        return;                                      /* this frame has it drawn already */
    }
    opus_page_put(buf, *used);
}

static void draw(void)
{
    int32_t k[4];
    for (int i = 0; i < 4; i++) k[i] = view_par[i] < 0 ? 0 : view_par[i];
    static const si_pos_t none;
    const struct sister_inst *in = shown(k);
    fx_off = !running(in);
    const si_pos_t *P = in ? &in->pos : &none;
    const unsigned c = cycles();
    int dt = last_cycles ? (int)((c - last_cycles) / 400000u) : 33;   /* 400 MHz: ms */
    last_cycles = c;
    if (dt <= 0 || dt > 200) dt = 33;
    const int32_t a = (int32_t)((int64_t)dt * 32768 / (dt + 110));        /* ~110 ms time constant */
    const int32_t af = (int32_t)((int64_t)dt * 32768 / (dt + 45));         /* flames, faster */
    frames++;

    /* targets: the depth step between column pairs (Q12, 0.17 .. 0.64, 0.34 at the default SPACE 60) */
    const int32_t t_depth = SISTER_D_MIN + (int32_t)((((int64_t)k[0] * k[0] >> 15) * SISTER_D_K) >> 15);
    int32_t t_xc = (k[1] - 9830) * 32767 / 13107;                            /* CHOIR -> PIPES, as the DSP */
    if (t_xc < 0) t_xc = 0;
    if (t_xc > 32767) t_xc = 32767;
    if (e_depth < 0) { e_depth = t_depth; e_xc = t_xc; e_mix = k[3]; e_green = k[1]; e_vow[0] = P->vowel[0]; e_vow[1] = P->vowel[1]; }
    e_depth = ease(e_depth, t_depth, a);
    e_xc = ease(e_xc, t_xc, a);
    e_green = ease(e_green, k[1], a);
    e_mix = ease(e_mix, k[3], a);
    for (int v = 0; v < 2; v++) {                  /* the vowel position wraps at 3: ease the short way round */
        int32_t d = P->vowel[v] - e_vow[v];
        if (d > 6144) d -= 12288;
        if (d < -6144) d += 12288;
        e_vow[v] += (int32_t)(((int64_t)d * a) >> 15);
        if (e_vow[v] < 0) e_vow[v] += 12288;
        if (e_vow[v] >= 12288) e_vow[v] -= 12288;
    }
    /* the tail's level, screen side: the wet block peak in octaves above 4 (Q8), so it reads the same at any
       loudness. Three followers: fast (~40 ms up, ~120 ms down), slow (~60 ms up, ~700 ms down) and an even ~300 ms
       one. The slow one lights the petals, sets how far they sing and how much the flames flicker (all of it at 6
       octaves, peak 256); the fast one's lead over the even one makes the petals breathe with the playing at any
       level. Silence returns all to 0: the still sketch. */
    const int32_t pk = fx_off ? 0 : P->peak;      /* off: no tail; the motion settles at the still sketch */
    const int32_t lg = pk > 4 ? log2q8((uint32_t)pk) - 512 : 0;            /* 0 .. 3328 */
    const int32_t a_up = (int32_t)((int64_t)dt * 32768 / (dt + 40)), a_fd = (int32_t)((int64_t)dt * 32768 / (dt + 120));
    const int32_t a_su = (int32_t)((int64_t)dt * 32768 / (dt + 60)), a_sd = (int32_t)((int64_t)dt * 32768 / (dt + 700));
    e_fast = ease(e_fast, lg, lg > e_fast ? a_up : a_fd);
    e_slow = ease(e_slow, lg, lg > e_slow ? a_su : a_sd);
    e_mid = ease(e_mid, lg, (int32_t)((int64_t)dt * 32768 / (dt + 300)));
    e_lit = e_slow >= 1536 ? 32767 : e_slow * 32767 / 1536;
    {                                                                         /* breath: 1 px per octave of lead, +-1.2 px */
        int32_t b = e_fast - e_mid;
        if (b > 307) b = 307;
        if (b < -307) b = -307;
        e_breath = ease(e_breath, b, af);
    }

    still();                                   /* floor, candles, ring, quatrefoil, nun: all static */
    const int c_teal = rgb(teal);

    /* ---- the nave, far to near: per level its pointed transverse arch, the lancet on each wall between it and
       the nearer pair, and its columns (levels 0..3) ---- */
    for (int f = 5; f >= 0; f--) {
        const int32_t s = persp(f * e_depth);
        {                                                                     /* the transverse arch */
            const int32_t sc = f > 3 ? persp(3 * e_depth) : s;                  /* 4 and 5 spring from level 3's pair */
            const int32_t sx = VX - (SISTER_ARCH_X * sc >> 10), sy = VY + (SISTER_CAP_Y * sc >> 10);
            const int32_t ty = SISTER_APEX_V * 4 - (SISTER_APEX_A * s >> 10);
            int32_t px = sx, py = sy;
            for (unsigned j = 1; j < sizeof sister_profile / sizeof sister_profile[0]; j++) {
                const int32_t x = sx + (int32_t)(((int64_t)(VX - sx) * sister_profile[j][0]) >> 12);
                const int32_t y = sy - (int32_t)(((int64_t)(sy - ty) * sister_profile[j][1]) >> 12);
                line2(px, py, x, y, c_teal);
                px = x; py = y;
            }
        }
        if (f >= 1 && f <= 3) {                                               /* the lancets, both legs */
            const unsigned nl = sizeof sister_legs / sizeof sister_legs[0];
            for (int leg = 0; leg < 2; leg++) {
                int32_t px = 0, py = 0;
                for (unsigned j = 0; j < nl; j++) {
                    const int32_t u = sister_legs[leg ? nl - 1 - j : j][0], v = sister_legs[leg ? nl - 1 - j : j][1];
                    const int32_t wu = leg ? 2 * SISTER_LANCET_U - (SISTER_LANCET_U * u >> 12) : SISTER_LANCET_U * u >> 12;
                    const int32_t ls = persp(f * e_depth + (wu * e_depth >> 12));
                    const int32_t hy = SISTER_CAP_Y + ((SISTER_LANCET_Y - SISTER_CAP_Y) * v >> 12);
                    const int32_t x = VX - (SISTER_WALL_X * ls >> 10), y = VY + (hy * ls >> 10);
                    if (j) line2(px, py, x, y, c_teal);
                    px = x; py = y;
                }
            }
        }
        if (f <= 3) {                                                         /* the columns */
            const unsigned n = f <= 1 ? sizeof sister_col / sizeof sister_col[0] : SISTER_COL_FAR;
            for (unsigned i = 0; i < n; i++)
                line2(VX + (sister_col[i][0] * s >> 10), VY + (sister_col[i][1] * s >> 10),
                      VX + (sister_col[i][2] * s >> 10), VY + (sister_col[i][3] * s >> 10), c_teal);
        }
    }

    /* ---- the rose window's petals (its ring and quatrefoil are the SVG's) ---- */
    {
        const int cx = RX, cy = RY;
        /* petals: green (paler at low VOICE), lit by the tail; CHOIR's are the sketch's lobes when silent and sing the
           drifting vowels with the level (aah tall, ooh round, eeh wide; alternate petals for the left and right
           voices), PIPES' are pipes of the root, fifth and octave; between the two ends both, crossfading */
        uint8_t base[4], pc[4], cc[4], gp[4];
        static const uint8_t pale[4] = { 0xA8, 0xF0, 0xD4, 255 }, glow[4] = { 0xB4, 0xFF, 0xE0, 255 };
        blend(base, pale, green, 26214 + (int32_t)(((int64_t)e_green * 6553) >> 15));     /* 80 % green at VOICE 0 */
        shade(base, base, 30147);                                                          /* 92 % when silent */
        blend(pc, base, glow, (int32_t)(((int64_t)e_lit * 11469) >> 15));                 /* lit: up to 35 % toward glow */
        const int32_t tc = (32767 - e_xc) * 2 > 32767 ? 32767 : (32767 - e_xc) * 2, tp = e_xc * 2 > 32767 ? 32767 : e_xc * 2;
        shade(cc, pc, tc);
        shade(gp, pc, tp);
        const int c_cc = rgb(cc), c_gp = rgb(gp);
        for (unsigned i = 0; i < 12; i++) {
            if (i == 6) continue;                                                        /* straight down: behind the veil */
            const int32_t dx = sister_petal_dir[i][0], dy = sister_petal_dir[i][1];      /* Q14, radial */
#define PT(rr, t, X, Y) do { X = cx + (int32_t)(((int64_t)dx * (rr) - (int64_t)dy * (t)) >> 12); \
                             Y = cy + (int32_t)(((int64_t)dy * (rr) + (int64_t)dx * (t)) >> 12); } while (0)
            if (e_xc < 31000) {
                const int32_t p = e_vow[i & 1];
                const unsigned vi = (unsigned)p >> 12, vj = vi == 2 ? 0 : vi + 1;
                const int32_t fr = p & 4095;
                /* Q8 head centre, radial and tangential radius: aah, ooh, eeh */
                static const int16_t vc[3] = { 4147, 4019, 4070 }, vr[3] = { 1101, 768, 717 }, vt[3] = { 742, 768, 1024 };
                const int32_t c0 = vc[vi] + (((vc[vj] - vc[vi]) * fr) >> 12), r0 = vr[vi] + (((vr[vj] - vr[vi]) * fr) >> 12),
                              t0 = vt[vi] + (((vt[vj] - vt[vi]) * fr) >> 12);
                const int32_t hc = SISTER_PETAL_C + (int32_t)(((int64_t)(c0 - SISTER_PETAL_C) * e_lit) >> 15) + (i & 1 ? e_breath : e_breath * 3 / 5);
                const int32_t hr = SISTER_PETAL_R + (int32_t)(((int64_t)(r0 - SISTER_PETAL_R) * e_lit) >> 15);
                const int32_t ht = SISTER_PETAL_R + (int32_t)(((int64_t)(t0 - SISTER_PETAL_R) * e_lit) >> 15);
                int32_t px, py, x, y;
                PT(SISTER_PETAL_IN, -SISTER_PETAL_STEM, px, py);
                for (unsigned j = 0; j < sizeof sister_arc / sizeof sister_arc[0]; j++) {
                    PT(hc + (hr * sister_arc[j][0] >> 12), ht * sister_arc[j][1] >> 12, x, y);
                    cline(px, py, x, y, c_cc, 1);
                    px = x; py = y;
                }
                PT(SISTER_PETAL_IN, SISTER_PETAL_STEM, x, y);
                cline(px, py, x, y, c_cc, 1);
            }
            if (e_xc > 1700) {                                                           /* a pipe from the hub */
                static const int16_t len[3] = { 3328, 2227, 1664 };                         /* Q8: 13, 8.7, 6.5 px */
                const int32_t L = len[i % 3] + e_breath / 2, w = 410;
                int32_t q[4][2];
                PT(SISTER_PETAL_IN, -w, q[0][0], q[0][1]);
                PT(SISTER_PETAL_IN + L, -w, q[1][0], q[1][1]);
                PT(SISTER_PETAL_IN + L, w, q[2][0], q[2][1]);
                PT(SISTER_PETAL_IN, w, q[3][0], q[3][1]);
                for (unsigned j = 0; j < 3; j++) cline(q[j][0], q[j][1], q[j + 1][0], q[j + 1][1], c_gp, 1);
                int32_t m0x, m0y, m1x, m1y;                                                 /* the pipe's mouth */
                PT(SISTER_PETAL_IN + L * 3 / 10, -w, m0x, m0y);
                PT(SISTER_PETAL_IN + L * 3 / 10, w, m1x, m1y);
                cline(m0x, m0y, m1x, m1y, c_gp, 1);
            }
#undef PT
        }
    }

    /* ---- the candles: flames at the sketch's size at SWELL 0, up to twice that at 99 (VESPER), flickering with the tail ---- */
    {
        const int c_white = rgb(white);
        for (unsigned c3 = 0; c3 < 3; c3++) {
            if (!fx_off && (frames + c3 * 3) % 4 == 0) {                         /* a new flicker target now and then, eased toward */
                rng = rng * 1664525u + 1013904223u;
                e_flick[c3] = (int32_t)(rng >> 17) - 16384;            /* -16384 .. 16383 */
            }
            static int32_t fk[3];
            fk[c3] = ease(fk[c3], (int32_t)(((int64_t)e_flick[c3] * e_lit) >> 15), af);
            const int32_t h0 = sister_candle[c3][2], w0 = sister_candle[c3][3];          /* Q8 */
            /* height Q8: the sketch's at SWELL 0, twice at 99, +-1.6 px of flicker at full tail */
            const int32_t t = h0 + (int32_t)(((int64_t)k[2] * h0) >> 15) + (int32_t)(((int64_t)fk[c3] * 410) >> 14);
            e_flame[c3] = ease(e_flame[c3] ? e_flame[c3] : t, t, af);
            const int32_t h = e_flame[c3] < 512 ? 512 : e_flame[c3], w = w0 + ((h - h0) >> 2);
            const int32_t sway = (int32_t)(((int64_t)fk[c3] * 160) >> 14);              /* the tip leans with the flicker */
            const int32_t bx = sister_candle[c3][0] * 4, by = sister_candle[c3][1] * 4;
            const unsigned nfl = sizeof sister_flame / sizeof sister_flame[0];
            for (int side = -1; side <= 1; side += 2) {
                int32_t px = 0, py = 0;
                for (unsigned j = 0; j < nfl; j++) {
                    const int32_t yq = sister_flame[j].y;                 /* 0 .. -256 (Q8 of the height) */
                    const int32_t x = bx + side * (int32_t)(((int64_t)sister_flame[j].x * w) >> 6) - (int32_t)(((int64_t)sway * yq) >> 6);
                    const int32_t y = by + (int32_t)(((int64_t)yq * h) >> 6);
                    if (j) line(px, py, x, y, 1024, c_white);
                    px = x; py = y;
                }
            }
        }
    }

    /* ---- MIX bar (Father's place) and the corner labels ---- */
    {
        const unsigned e4 = opus_accent_now(3);
        const unsigned dim = (((e4 >> 16 & 255) * 80 / 255) << 16) | (((e4 >> 8 & 255) * 80 / 255) << 8) | ((e4 & 255) * 80 / 255);
        const int height = (int)(((int64_t)e_mix * 46 + 16383) >> 15);
        line(302 * 1024, 36 * 1024, 302 * 1024, 82 * 1024, 9 * 1024, col(dim));   /* kennel's dim red at stock */
        if (height) line(302 * 1024, (82 - height) * 1024, 302 * 1024, 82 * 1024, 9 * 1024, col(e4));
    }
    const unsigned value = ((unsigned)k[0] * 99u + 16383u) >> 15;
    char number[3] = { (char)('0' + value / 10), (char)('0' + value % 10), 0 };
    struct opl_self *me = sister_self;
    opus_page_text(me, 0, 14, 11, 12, OPUS_COLOR_E1, "space");
    opus_page_text(me, 1, 14, 27, 30, OPUS_COLOR_TEXT, number);
    if (fw_shift_held() || (int)(key_until - frame_no) > 0 || sister_key_cents_mode) {   /* the PIPES key */
        static const char names[12][3] = { "c", "c#", "d", "d#", "e", "f", "f#", "g", "g#", "a", "a#", "b" };   /* the font has no capitals */
        const int32_t cents = sister_key_of(view_par[6]);
        int32_t semi = (cents + 2400 + 50) / 100;                /* 0 .. 48 from C1 */
        if (semi < 0) semi = 0;
        if (semi > 48) semi = 48;
        const int32_t off = cents + 2400 - semi * 100;           /* -50 .. +49 cents from the shown note */
        char kt[16] = "key ";
        unsigned n = 4;
        for (const char *p = names[semi % 12]; *p; p++) kt[n++] = *p;
        kt[n++] = (char)('1' + semi / 12);
        if (off || sister_key_cents_mode) {
            const int32_t ao = off < 0 ? -off : off;
            kt[n++] = ' ';
            kt[n++] = off < 0 ? '-' : '+';
            if (ao >= 10) kt[n++] = (char)('0' + ao / 10);
            kt[n++] = (char)('0' + ao % 10);
            kt[n++] = 'c';
        }
        kt[n] = 0;
        opus_page_text(me, 2, 14, 126, 12, OPUS_COLOR_E2, kt);
    } else
        opus_page_text(me, 2, 14, 126, 12, OPUS_COLOR_E2, "voice");
    opus_page_text(me, 3, 260, 126, 12, OPUS_COLOR_E3, "swell");
    opus_page_text(me, 4, 284, 11, 12, OPUS_COLOR_E4, "mix");
    /* The OS's own effect page draws over this: its "off" overlay dims it when the effect is off. */
}

/* ---- the hooks ---- */
static void frame_begin(void) { frame_no++; }

static void frame_under(unsigned short *buf)
{
    (void)buf;
    if (!page_showing()) {
        e_depth = -1;                          /* the page comes back at its targets, as kennel's screen init */
        return;
    }
    draw();
}

/* the effect page: the corners and the middle are ours (drawn under it); remember the page's params */
void sister_view(const short *par, struct opus_fx_view *v)
{
    static const char *const empty = "";
    for (int i = 0; i < 8; i++) view_par[i] = par[i];
    view_frame = frame_no;
    for (int i = 0; i < 4; i++) {
        v->label[i] = empty;
        v->value[i] = empty;
    }
    v->waves = 0;
    v->caption = 0;
}

void sister_key_touched(void) { key_until = frame_no + 45; }        /* ~1.5 s at the display's ~30 frames a second */

/* a green push while SISTER's page shows: shift + green moves the key by semitones or by cents (kennel's press page:
   a turn a semitone, shift + turn a cent). The release of a push taken is taken too. */
static int key(int code, int pressed)
{
    if (code != 0x3C)
        return 0;
    if (pressed) {
        if (!page_showing())
            return 0;
        sister_key_cents_mode = !sister_key_cents_mode;
        sister_key_touched();
        green_taken = 1;
        return 1;
    }
    if (green_taken) {
        green_taken = 0;
        return 1;
    }
    return 0;
}

void sister_page_init(struct opl_self *self)
{
    kept = (unsigned short *)opus_alloc(self, 2 * KEPT_WORDS);
    kept_grey = (unsigned short *)opus_alloc(self, 2 * KEPT_WORDS);   /* 0: off keeps the normal art, live parts grey */
    art = opus_art_text(self, "sister.svg", sister_svg, SISTER_SVG_LEN);
    opus_on_frame_begin(self, frame_begin);
    opus_on_frame_under(self, frame_under);
    opus_on_key(self, key);
}
