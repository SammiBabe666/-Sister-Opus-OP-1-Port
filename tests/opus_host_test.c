/* sammi.sister host test: the plugin's own sources (sister_opus.c, sister_page.c, SISTER's DSP) against a mock of the
 * Opus 2 core, under ASan/UBSan. Source-level only: not the OP-1, not the Opus core, not the Blackfin.
 *
 * Copyright (C) 2026 SammiBabe666. GPL-3.0-or-later with the OP-1 firmware permission (NOTICE).
 *
 *   tests/run_host_tests.sh   (builds and runs this, SISTER's own sister_test.c, and the upstream comparison)
 *
 * Checks:
 *  1. the effect registers with SISTER's defaults and limits;
 *  2. synth, drum and master blocks through the Opus glue are sample-for-sample SISTER's si_process (kennel's
 *     sister.c path) with the same knobs and key, with the fast area poisoned (the core does not clear it), and the
 *     master separate from the synth;
 *  3. MIX 0 is exactly dry through the glue, mono and stereo;
 *  4. init restarts the instance as kennel's init does;
 *  5. shift + green: semitone steps (snapped), cents after a green push, clamped at +-24 semitones, the word law of
 *     kennel's OP1_PRESS_PAGE_SEMI round-tripping every cent;
 *  6. the page: drawn only on frames after `view`, every stroke inside the drawing, the labels and the key text,
 *     the still art drawn once and then put back. */
#include "opus_plugin.h"
#include "op1fw.h"
#include "sister_dsp.h"
#include "sister_opus.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------------- the mock core ---------------- */
static const struct opus_effect *reg;
static void (*h_begin)(void);
static void (*h_under)(unsigned short *);
static int (*h_key)(int, int);
static int shift;
static unsigned allocs;
static unsigned strokes, strokes_bad, texts, art_draws, puts_;
static char text_seen[32][24];
static unsigned strokes_colour, strokes_grey, art_rgb;
static unsigned char fake_slot[2];
extern unsigned sister_host_cycles;

int opus_effect_register(struct opl_self *self, const struct opus_effect *e) { (void)self; reg = e; return 8; }
void *opus_alloc(struct opl_self *self, unsigned bytes) { (void)self; allocs += bytes; return calloc(1, bytes); }
int opus_art_text(struct opl_self *self, const char *name, const char *svg, unsigned len)
{
    (void)self;
    assert(!strcmp(name, "sister.svg"));
    assert(strlen(svg) == len && !strncmp(svg, "<svg", 4));
    return 0;
}
int opus_art_ready(int a) { return a == 0; }
void opus_art_draw(int a) { assert(a == 0); art_draws++; }
static unsigned short frame[376 * 188];
unsigned short *opus_page_pixels(int *rows) { *rows = 376; return frame; }
int opus_page_keep(unsigned short *out, unsigned cap, int x0, int x1, int y0, int y1)
{
    assert(out && cap >= 1024 && x0 == 0 && x1 == 375 && y0 == 0 && y1 == 187);
    out[0] = art_rgb == 0x0B9A9E || !art_rgb ? 0x1234 : 0x4321;   /* which art: normal or grey */
    return 1000;
}
static unsigned short last_put;
void opus_page_put(const unsigned short *runs, int used) { assert((runs[0] == 0x1234 || runs[0] == 0x4321) && used == 1000); last_put = runs[0]; puts_++; }
void opus_on_frame_begin(struct opl_self *self, void (*fn)(void)) { (void)self; h_begin = fn; }
void opus_on_frame_under(struct opl_self *self, void (*fn)(unsigned short *)) { (void)self; h_under = fn; }
void opus_on_key(struct opl_self *self, int (*fn)(int, int)) { (void)self; h_key = fn; }
int opus_art_colour_slots(int a, unsigned c, unsigned char **slots, int max)
{
    assert(a == 0 && max > 0);
    slots[0] = &fake_slot[c == 0xFFFFFF];
    return 1;
}
void opus_art_set_colour(unsigned char **slots, int n, unsigned c) { assert(n == 1 && slots[0]); if (slots[0] == &fake_slot[0]) art_rgb = c; }   /* the teal slot */
void opus_page_stroke_line(int x0, int y0, int x1, int y1, int width, int colour)
{
    const unsigned c = (unsigned)colour & 0xFFFFFF;
    if ((c >> 16) == (c >> 8 & 255) && (c >> 16) == (c & 255)) strokes_grey++; else strokes_colour++;
    strokes++;
    /* units x 256: the drawing is 320 x 160; SISTER keeps to rows 1..154 */
    if (x0 < 0 || x1 < 0 || x0 > 320 * 256 || x1 > 320 * 256 || y0 < 256 - 1 || y1 < 256 - 1 || y0 > 154 * 256 + 1 ||
        y1 > 154 * 256 + 1 || width < 200 || width > 9 * 256)
        strokes_bad++;
}
void opus_page_text(struct opl_self *self, int slot, int x, int y, int size, int colour, const char *text)
{
    (void)self; (void)colour;
    assert(slot >= 0 && slot < 32 && size >= 6 && size <= 48 && x >= 0 && y >= 0 && y + size <= 146 && x < 320);
    assert(strlen(text) < 24);
    for (const char *p = text; *p; p++) assert(!(*p >= 'A' && *p <= 'Z'));   /* the font: capitals are icons */
    strcpy(text_seen[slot], text);
    texts++;
}
void opus_page_text_right(struct opl_self *self, int slot, int x, int y, int size, int colour, const char *text)
{
    (void)self; (void)colour; (void)x;
    assert(slot >= 0 && slot < 32 && size >= 6 && size <= 48 && y + size <= 146);
    strcpy(text_seen[slot], text);
}
unsigned opus_accent_now(int i) { static const unsigned c[4] = { 0x698EFF, 0x00ED95, 0xADADFF, 0xFF3A5D }; return c[i & 3]; }
int fw_shift_held(void) { return shift; }

/* ---------------- helpers ---------------- */
static uint32_t rng = 1;
static int16_t rnd(void) { rng = rng * 1664525u + 1013904223u; return (int16_t)(rng >> 16); }

struct inst { struct opus_fx fx; };
static void make(struct inst *in, int stereo, void *bulk, void *fast)
{
    struct opus_need n = { 0, 0, 0 };
    reg->need(stereo, &n);
    in->fx.state = calloc(1, n.state);
    in->fx.bulk = bulk ? bulk : calloc(1, n.bulk);
    if (fast) in->fx.fast = fast;
    else { in->fx.fast = aligned_alloc(32, (n.fast + 31) & ~31u); memset(in->fx.fast, 0xA5, n.fast); }   /* not cleared */
    in->fx.stereo = stereo;
    reg->init(&in->fx);
}

int main(void)
{
    struct opl_self self = { sizeof(struct opl_self), "sammi.sister", "/yaffs2/mod/sammi.sister", 0 };
    assert(opl_init(&self) == 0);
    /* 1 */
    assert(reg && !strcmp(reg->name, "dark sister") && reg->size == sizeof(struct opus_effect));
    static const short defs[8] = { 19661, 6554, 0, 11469, 0, 0, 16384, 0 };
    assert(!memcmp(reg->defaults, defs, sizeof defs));
    for (int i = 0; i < 4; i++) assert(reg->limits[i][0] == 0 && reg->limits[i][1] == 32767 && reg->limits[i][2] > 0);
    for (int i = 4; i < 8; i++) assert(reg->limits[i][2] == 0);
    struct opus_need nm = { 0, 0, 0 }, ns = { 0, 0, 0 };
    reg->need(0, &nm); reg->need(1, &ns);
    printf("need: state %u, bulk %u (si_state %u), fast %u mono / %u master; page memory %u\n", nm.state, nm.bulk,
           (unsigned)sizeof(si_state), nm.fast, ns.fast, allocs);
    assert(nm.fast == SI_L1_BYTES && ns.fast == SI_L1_CORE && nm.bulk >= sizeof(si_state));
    puts("1 registration, defaults, limits, needs: PASS");

    /* 2: synth then drum (sharing bulk and fast, as the core gives them), the master separate */
    struct inst syn, drm, mst;
    make(&syn, 0, 0, 0);
    make(&drm, 0, syn.fx.bulk, syn.fx.fast);
    make(&mst, 1, 0, 0);
    static si_state ref_m, ref_s;
    si_init(&ref_m); si_init(&ref_s);
    short par[8] = { 19661, 6554, 0, 11469, 0, 0, 16384, 0 }, mpar[8] = { 30000, 30000, 20000, 25000, 0, 0, 20000, 0 };
    int16_t l[64], r[64], a[64], b[64];
    for (unsigned blk = 0; blk < 6000; blk++) {
        if (blk % 97 == 0) { unsigned k = (blk / 97) % 4; par[k] = (short)((unsigned)rnd() >> 1); }
        if (blk % 331 == 0) par[6] = (short)sister_semi_word((int)(blk % 4801) - 2400);
        if (blk % 211 == 0) { unsigned k = (blk / 211) % 4; mpar[k] = (short)((unsigned)rnd() >> 1); }
        int32_t q[4];
        /* mono: synth for the first half, then the drum takes over the same reverb (one runs at a time) */
        for (int i = 0; i < 64; i++) a[i] = l[i] = (int16_t)(rnd() / 3);
        reg->mono(blk < 3000 ? &syn.fx : &drm.fx, l, par);
        for (int k = 0; k < 4; k++) q[k] = par[k];
        si_process(&ref_m, a, 0, q, sister_key_of(par[6]));
        assert(!memcmp(l, a, sizeof l));
        /* master */
        for (int i = 0; i < 64; i++) { a[i] = l[i] = (int16_t)(rnd() / 3); b[i] = r[i] = (int16_t)(rnd() / 3); }
        reg->stereo(&mst.fx, l, r, mpar);
        for (int k = 0; k < 4; k++) q[k] = mpar[k];
        si_process(&ref_s, a, b, q, sister_key_of(mpar[6]));
        assert(!memcmp(l, a, sizeof l) && !memcmp(r, b, sizeof r));
    }
    puts("2 6000 mono + 6000 master blocks through the glue equal si_process exactly (fast area poisoned): PASS");

    /* 3: MIX 0 exactly dry */
    {
        struct inst m0, s0;
        make(&m0, 0, 0, 0); make(&s0, 1, 0, 0);
        for (unsigned blk = 0; blk < 2000; blk++) {
            short p[8] = { (short)(blk * 37 & 32767), (short)(blk * 101 & 32767), (short)(blk * 53 & 32767), 0, 0, 0,
                           (short)(blk * 13 & 32767), 0 };
            for (int i = 0; i < 64; i++) { a[i] = l[i] = rnd(); b[i] = r[i] = rnd(); }
            reg->mono(&m0.fx, l, p);
            assert(!memcmp(l, a, sizeof l));
            for (int i = 0; i < 64; i++) l[i] = a[i];
            reg->stereo(&s0.fx, l, r, p);
            assert(!memcmp(l, a, sizeof l) && !memcmp(r, b, sizeof r));
        }
    }
    puts("3 MIX 0 exactly dry through the glue (2000 blocks, mono and master, other knobs moving): PASS");

    /* 4: init restarts: after init, the output equals a fresh si_init state's */
    {
        reg->init(&syn.fx);
        si_init(&ref_m);
        for (unsigned blk = 0; blk < 800; blk++) {
            int32_t q[4] = { par[0], par[1], par[2], par[3] };
            for (int i = 0; i < 64; i++) a[i] = l[i] = (int16_t)(rnd() / 3);
            reg->mono(&syn.fx, l, par);
            si_process(&ref_m, a, 0, q, sister_key_of(par[6]));
            assert(!memcmp(l, a, sizeof l));
        }
    }
    puts("4 init restarts the instance as kennel's init (800 blocks equal): PASS");

    /* 5: shift + green */
    {
        for (int c = -2400; c <= 2400; c++) assert(sister_semi_cents(sister_semi_word(c)) == c);
        assert(sister_key_of(0) == 0 && sister_key_of(16384) == 0);
        short p[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };          /* an unset word: C3 */
        assert(reg->shift_knob(p, 0, 3) == 0 && reg->shift_knob(p, 2, 1) == 0);   /* orange: DARK (test 8) */
        assert(reg->shift_knob(p, 1, 7) == 1 && sister_key_of(p[6]) == 700);
        assert(reg->shift_knob(p, 1, -2) == 1 && sister_key_of(p[6]) == 500);
        assert(reg->shift_knob(p, 1, 100) == 1 && sister_key_of(p[6]) == 2400);
        assert(reg->shift_knob(p, 1, -100) == 1 && sister_key_of(p[6]) == -2300);   /* C#1: word 0 would be C3 */
        assert(reg->shift_knob(p, 1, 0) == 1 && sister_key_of(p[6]) == -2300);
        /* a green push on the page: cents */
        short vp[8] = { 19661, 6554, 0, 11469, 0, 0, 16384, 0 };
        struct opus_fx_view v;
        h_begin(); reg->view(vp, &v); h_begin();
        assert(h_key(0x3C, 1) == 1 && h_key(0x3C, 0) == 1 && sister_key_cents_mode == 1);
        p[6] = (short)sister_semi_word(700);
        assert(reg->shift_knob(p, 1, 13) == 1 && sister_key_of(p[6]) == 713);
        assert(reg->shift_knob(p, 1, -20) == 1 && sister_key_of(p[6]) == 693);
        p[6] = (short)sister_semi_word(-2300);
        assert(reg->shift_knob(p, 1, -500) == 1 && sister_key_of(p[6]) == -2399 && p[6] > 0);
        p[6] = (short)sister_semi_word(700);
        h_begin(); reg->view(vp, &v); h_begin();
        assert(h_key(0x3C, 1) == 1 && h_key(0x3C, 0) == 1 && sister_key_cents_mode == 0);
        assert(reg->shift_knob(p, 1, 1) == 1 && sister_key_of(p[6]) == 800);   /* snapped back to whole semitones */
        /* off the page, a green push is the OS's */
        for (int i = 0; i < 10; i++) h_begin();
        assert(h_key(0x3C, 1) == 0 && h_key(0x3C, 0) == 0 && h_key(0x3B, 1) == 0);
    }
    puts("5 shift + green: semitones, cents after a green push, clamps, word law round-trips every cent: PASS");

    /* 6: the page */
    {
        struct opus_fx_view v;
        unsigned s0 = strokes;
        for (int i = 0; i < 5; i++) { h_begin(); h_under(frame); }
        assert(strokes == s0);                            /* no view: nothing drawn */
        short vp[8] = { 19661, 6554, 0, 11469, 0, 0, 16384, 0 };
        unsigned frames_drawn = 0, maxs = 0;
        for (int f = 0; f < 400; f++) {
            sister_host_cycles += 400000u * 33u;
            if (f == 100) vp[1] = 32767;                   /* PIPES */
            if (f == 200) { vp[0] = 32767; vp[2] = 32767; vp[3] = 32767; }
            if (f == 300) { vp[0] = 0; vp[1] = 16000; vp[2] = 0; vp[3] = 0; }
            if (f == 350) shift = 1;
            h_begin();
            const unsigned before = strokes;
            h_under(frame);
            if (strokes > before) { frames_drawn++; if (strokes - before > maxs) maxs = strokes - before; }
            reg->view(vp, &v);
            for (int i = 0; i < 4; i++) assert(v.label[i] && v.value[i] && !v.label[i][0] && !v.value[i][0]);
            assert(v.waves == 0 && !v.caption);
            /* keep the reverb ringing for the telemetry */
            for (int i = 0; i < 64; i++) l[i] = (int16_t)(f < 150 ? rnd() / 2 : 0);
            reg->mono(&syn.fx, l, vp);
        }
        assert(frames_drawn == 399 && strokes_bad == 0);
        assert(art_draws == 1 && puts_ == frames_drawn - 1);
        assert(!strcmp(text_seen[0], "space") && !strcmp(text_seen[3], "swell") && !strcmp(text_seen[4], "mix"));
        assert(!strcmp(text_seen[1], "00"));
        printf("  green label with shift held: \"%s\"\n", text_seen[2]);
        assert(!strncmp(text_seen[2], "key ", 4));
        shift = 0;
        h_begin(); h_under(frame); reg->view(vp, &v);
        for (int i = 0; i < 60; i++) { h_begin(); h_under(frame); reg->view(vp, &v); }
        assert(!strcmp(text_seen[2], "voice"));
        printf("  %u frames drawn, at most %u strokes a frame, %u texts, art drawn %u time(s)\n", frames_drawn, maxs, texts,
               art_draws);
    }
    puts("6 page: drawn only after view, strokes inside the drawing, labels, key text, art kept: PASS");

    /* 7: on and off. The OS calls the process only while the effect is on: blocks each frame = on, none = off. */
    {
        struct opus_fx_view v;
        short vp[8] = { 19661, 6554, 0, 11469, 0, 0, 16384, 0 };
        unsigned draws0 = art_draws;
        for (int cycle = 0; cycle < 6; cycle++) {
            for (int phase = 0; phase < 2; phase++) {          /* 0 on, 1 off */
                for (int f = 0; f < 8; f++) {
                    sister_host_cycles += 400000u * 33u;
                    if (!phase) for (int b = 0; b < 23; b++) { for (int i = 0; i < 64; i++) l[i] = (int16_t)(rnd() / 4); reg->mono(&syn.fx, l, vp); }
                    if (phase && f == 2) vp[0] = (short)(vp[0] ^ 0x1000);   /* a knob turned while off: still on the page */
                    strokes_colour = strokes_grey = 0;
                    h_begin(); h_under(frame); reg->view(vp, &v);
                    if (f >= 4) {                                  /* settled: the state shows */
                        if (phase) assert(strokes_colour == 0 && strokes_grey > 300 && last_put == 0x4321);
                        else { if (!(strokes_colour > 250 && last_put == 0x1234)) fprintf(stderr, "cycle %d f %d colour %u grey %u put %x\n", cycle, f, strokes_colour, strokes_grey, last_put); assert(strokes_colour > 250 && last_put == 0x1234); }
                    }
                }
            }
        }
        printf("  6 on/off cycles; art re-kept %u times in all (once per colour)\n", art_draws - draws0);
        assert(art_draws - draws0 <= 2);
    }
    puts("7 effect off (no process calls): every FX stroke and the art grey; on again: colours back, 6 cycles: PASS");
    /* 8: DARK (shift + orange, word 7) */
    {
        short p[8] = { 0, 0, 0, 0, 0, 0, 16384, 0 };
        assert(reg->shift_knob(p, 3, 5) == 1 && p[7] == 5 * 1311);
        assert(reg->shift_knob(p, 3, -10) == 1 && p[7] == 0);
        assert(reg->shift_knob(p, 3, 100) == 1 && p[7] == 32767);
        assert(reg->shift_knob(p, 2, 1) == 0);
        /* wet only, noise bursts: high-band energy of the wet with DARK 0 vs 99, and the tail ends in silence */
        double hi[2] = { 0, 0 }, all[2] = { 0, 0 };
        for (int dkr = 0; dkr < 2; dkr++) {
            struct inst d; make(&d, 1, 0, 0);
            short pp[8] = { 19661, 6554, 0, 32767, 0, 0, 16384, dkr ? 32767 : 0 };
            int16_t pl = 0; unsigned last = 0;
            for (unsigned blk = 0; blk < 9000; blk++) {
                for (int i = 0; i < 64; i++) { l[i] = blk < 700 ? (int16_t)(rnd() / 3) : 0; r[i] = blk < 700 ? (int16_t)(rnd() / 3) : 0; }
                reg->stereo(&d.fx, l, r, pp);
                for (int i = 0; i < 64; i++) {
                    if (blk > 100 && blk < 700) { const double dd = l[i] - pl; hi[dkr] += dd * dd; all[dkr] += (double)l[i] * l[i]; }
                    pl = l[i];
                    if (l[i] || r[i]) last = blk;
                }
            }
            assert(last + 400 < 9000);
            printf("  dark %s: high/total energy %.4f, last non-zero block %u\n", dkr ? "99" : " 0", hi[dkr] / all[dkr], last);
        }
        assert(hi[1] / all[1] < 0.25 * hi[0] / all[0]);
    }
    puts("8 dark: shift + orange sets word 7, the wet loses most of its top, tails still end in exact silence: PASS");
    puts("sammi.sister Opus host test PASS");
    return 0;
}
