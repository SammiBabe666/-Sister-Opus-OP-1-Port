/* sammi.sister: Hundekraft's SISTER (OP-1 Workshop 3.1.1, kennel) as a native Opus 2 effect.
 *
 * Copyright (C) 2026 Hundekraft (SISTER: sister_dsp.c, sister_kern.c, sister_asm.S, sister_tables.h, sister_geom.h,
 * sister.svg, the screen it draws) and SammiBabe666 (this Opus port: this file, sister_page.c).
 * GPL-3.0-or-later with the OP-1 firmware permission (NOTICE). Built against the Opus 2 plugin kit (GPL-3.0).
 *
 * The DSP is SISTER's own, file for file (sister_dsp.c and its kernels are unchanged). This file replaces kennel's
 * glue (sister.c) with Opus's effect service:
 *
 *   kennel (R246 filter slot, FX_sister)          Opus 2 (opus_effect_register)
 *   op1_fx_scratch(pos): 176,460 B per position   bulk: one si_state per instance memory (synth and drum share it:
 *                                                  R246 runs one of them at a time, so one reverb serves both)
 *   op1_l1_claim(): the L1 pool partition          fast: the same layout (SI_L1_BYTES mono, SI_L1_CORE master),
 *                                                  filled once (the tables) the first time a block sees it
 *   op1_fx_param_q15(): the filter's range table   limits {0, 32767}: the words are already 0..32767
 *   params[6] (green's press page, PAGE_SEMI)      params[6], same word law (op1_semi_word / op1_semi_cents), set
 *                                                  with shift + green (shift_knob); a green push picks semitones or
 *                                                  cents for it, as kennel's shift did on the press page
 *   SCREEN_sister (an OS screen)                   sister_page.c: the same drawing under the effect page
 */
#include "opus_plugin.h"
#include "op1fw.h"
#include "sister_dsp.h"
#include "sister_kern.h"
#include "sister_opus.h"

#ifdef __bfin__
_Static_assert(SI_KERN_OFFSETS_OK, "the kernels read their structs by offset");
#endif
_Static_assert(SI_L1_BYTES <= 8320 && SI_L1_CORE <= 4160, "SISTER's L1 layout (as kennel's partitions)");

/* freestanding: the compiler may emit these for a struct copy or a cleared array (rules.md, Memory); the host tests
   link the C library's */
#ifdef __bfin__
void *memcpy(void *d, const void *s, unsigned n);
void *memset(void *d, int c, unsigned n);
void *memcpy(void *d, const void *s, unsigned n)
{
    unsigned char *a = d;
    const unsigned char *b = s;
    while (n--)
        *a++ = *b++;
    return d;
}
void *memset(void *d, int c, unsigned n)
{
    unsigned char *a = d;
    while (n--)
        *a++ = (unsigned char)c;
    return d;
}
#endif

struct opl_self *sister_self;
struct sister_inst *volatile sister_last[2];   /* the last mono and the last master instance that ran (the page) */

/* kennel's OP1_PRESS_PAGE_SEMI word law (op1_plugin.h, HAIL's PITCH), unchanged: centre 16384 = 0 with a dead band of
 * +-600, then 15784 units for 24 semitones each way; a whole semitone or cent round-trips exactly */
#define SEMI_DEAD 600
#define SEMI_SPAN (16384 - SEMI_DEAD)
int sister_semi_word(int cents)
{
    if (cents > 2400)
        cents = 2400;
    if (cents < -2400)
        cents = -2400;
    if (!cents)
        return 16384;
    const int a = cents < 0 ? -cents : cents, u = SEMI_DEAD + (a * SEMI_SPAN + 2399) / 2400;
    return cents < 0 ? 16384 - u : (16384 + u > 32767 ? 32767 : 16384 + u);
}
int sister_semi_cents(int word)
{
    int pv = word - 16384;
    pv = pv > SEMI_DEAD ? pv - SEMI_DEAD : (pv < -SEMI_DEAD ? pv + SEMI_DEAD : 0);
    const int a = pv < 0 ? -pv : pv, c = (a * 2400 + SEMI_SPAN / 2) / SEMI_SPAN;
    return pv < 0 ? -c : c;
}
/* word 6: the PIPES key. 0 (an unset word: a fresh slot) means C3, as in kennel (sister.c key_of) */
int sister_key_of(int w) { return w <= 0 ? 0 : sister_semi_cents(w); }

static int clampq(int v) { return v < 0 ? 0 : v > 32767 ? 32767 : v; }

/* ---- one instance's memory ---- */
static void need(int stereo, struct opus_need *n)
{
    n->state = sizeof(struct sister_inst);
    n->bulk = sizeof(struct sister_bulk);
    n->fast = stereo ? SI_L1_CORE : SI_L1_BYTES;         /* the master partition is smaller in kennel too */
}

/* Every factory call (a sound loading, the instrument or slot changing), sometimes in the audio interrupt: as kennel's
 * init, only the scalar state and the workspace are cleared here (si_init); the rings are cleared lazily by the first
 * blocks while the wet path stays silent and the dry follows the mix law. */
static void init(struct opus_fx *fx)
{
    struct sister_inst *in = (struct sister_inst *)fx->state;
    struct sister_bulk *b = (struct sister_bulk *)fx->bulk;
    in->pos.valid = 0;
    in->stereo = fx->stereo;
    if (b)
        si_init(&b->s);
}

static void process(struct opus_fx *fx, short *l, short *r, const short *par)
{
    struct sister_inst *in = (struct sister_inst *)fx->state;
    struct sister_bulk *b = (struct sister_bulk *)fx->bulk;
    if (!b)
        return;                                          /* no memory: the dry passes */
    si_pos_t *g = &in->pos;
    int32_t q[4];
    /* the words are 0..32767 (limits), so no range conversion divides here; the cache stays for the key and the page */
    for (unsigned i = 0; i < 4; i++) {
        if (!g->valid || g->raw[i] != par[i]) {
            g->raw[i] = par[i];
            g->norm[i] = (int16_t)clampq(par[i]);
        }
        q[i] = g->norm[i];
    }
    if (!g->valid || g->raw_key != par[6]) {
        g->raw_key = par[6];
        g->key_cents = (int16_t)sister_key_of(par[6]);
    }
    g->valid = 1;
    /* the L1 pool: the fast area (not cleared by the core) gets its tables the first time a block sees it; synth and
       drum share it with the bulk, so one flag in the bulk says it is filled */
    const uint32_t bytes = fx->stereo ? SI_L1_CORE : SI_L1_BYTES;
    int fresh = 0;
    if (fx->fast && b->fast_filled != fx->fast) {
        b->fast_filled = fx->fast;
        fresh = 1;
    }
    si_process_l1(&b->s, l, r, q, g->key_cents, fx->fast, bytes, fresh);
    g->peak = (int16_t)b->s.peak;
    g->level = (int16_t)b->s.wet_level;
    g->vowel[0] = (int16_t)b->s.vowel[0];
    g->vowel[1] = (int16_t)b->s.vowel[1];
    in->blocks++;
    sister_last[fx->stereo ? 1 : 0] = in;
}

static void mono(struct opus_fx *fx, short *buf, const short *par) { process(fx, buf, 0, par); }
static void stereo(struct opus_fx *fx, short *l, short *r, const short *par) { process(fx, l, r, par); }

/* shift + green on the effect's page: the PIPES key, a semitone a detent (snapped, as kennel's PAGE_SEMI), or a cent a
 * detent after a green push (sister_key_cents_mode). The other encoders keep the OS's fine steps under shift. */
static int shift_knob(short *par, int encoder, int detents)
{
    if (encoder != 1)
        return 0;
    if (!detents)
        return 1;
    int c = sister_key_of(par[6]);
    if (sister_key_cents_mode)
        c += detents;
    else {
        int semi = (c + (c < 0 ? -50 : 50)) / 100 + detents;
        c = semi * 100;
    }
    if (c > 2400)
        c = 2400;
    /* word 0 means C3 (an unset word), and -24 semitones is word 0: as in kennel the lowest key is C#1 (a cent above
       C1 in cents) */
    if (c < -2300)
        c = sister_key_cents_mode ? (c < -2399 ? -2399 : c) : -2300;
    par[6] = (short)sister_semi_word(c);
    sister_key_touched();
    return 1;
}

/* Sister's defaults (plugin.json): SPACE 60, VOICE 20, SWELL 0, MIX 35; page-2 words 5..7 as page2_defaults (word 6
 * C3). The encoders move 4 % a detent, the OS's own steps for an effect (bitcrush's tone and mix); shift + blue, white
 * and orange are the OS's fine steps. Word 4 is unused (kennel's selector), 5 and 7 unused, step 0 for all four. */
static const struct opus_effect SISTER = {
    .size = sizeof(struct opus_effect),
    .name = "sister",
    .defaults = { 19661, 6554, 0, 11469, 0, 0, 16384, 0 },
    .limits = { { 0, 32767, 10485 }, { 0, 32767, 10485 }, { 0, 32767, 10485 }, { 0, 32767, 10485 },
                { 0, 32767, 0 }, { 0, 32767, 0 }, { 0, 32767, 0 }, { 0, 32767, 0 } },
    .need = need,
    .init = init,
    .mono = mono,
    .stereo = stereo,
    .view = sister_view,
    .shift_knob = shift_knob,
};

int opl_init(struct opl_self *self)
{
    sister_self = self;
    if (opus_effect_register(self, &SISTER) < 0)
        return -1;
    sister_page_init(self);                              /* the page's art and hooks; the sound works without them */
    return 0;
}
