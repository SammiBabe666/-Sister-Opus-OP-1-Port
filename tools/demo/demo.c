/* sammi.sister demo: runs the plugin's own code (sister_opus.c, sister_page.c, SISTER's DSP) on the host against a
 * mock Opus core, frame by frame at 30 fps: each frame processes 1472 stereo samples (23 blocks) of a generated dry
 * part through the effect and records what the page draws. Writes OUT/frames/NNNN.svg (the page's live strokes and
 * texts; "<!--off-->" first when the effect is off), OUT/caption.txt (one caption per frame) and OUT/audio.raw
 * (stereo s16, 44,160 Hz: 30 frames of 1472 samples a second). tools/demo/render.py makes the video.
 * A host preview of the plugin's output, not an OP-1 recording: the OS's own effect page, font and "off" overlay are
 * not part of it.
 * Copyright (C) 2026 SammiBabe666. GPL-3.0-or-later with the OP-1 firmware permission (NOTICE). */
#include "opus_plugin.h"
#include "op1fw.h"
#include "sister_dsp.h"
#include "sister_opus.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const struct opus_effect *reg;
static void (*h_begin)(void);
static void (*h_under)(unsigned short *);
static FILE *svg;
extern unsigned sister_host_cycles;

int opus_effect_register(struct opl_self *self, const struct opus_effect *e) { (void)self; reg = e; return 8; }
void *opus_alloc(struct opl_self *self, unsigned bytes) { (void)self; return calloc(1, bytes); }
int opus_art_text(struct opl_self *self, const char *n, const char *s, unsigned len) { (void)self; (void)n; (void)s; (void)len; return 0; }
int opus_art_ready(int a) { return a == 0; }
void opus_art_draw(int a) { (void)a; }
static unsigned short frame[376 * 188];
unsigned short *opus_page_pixels(int *rows) { *rows = 376; return frame; }
int opus_page_keep(unsigned short *o, unsigned c, int a, int b, int d, int e) { (void)o; (void)c; (void)a; (void)b; (void)d; (void)e; return 1; }
void opus_page_put(const unsigned short *r, int u) { (void)r; (void)u; }
int opus_art_colour_slots(int a, unsigned c, unsigned char **slots, int max) { static unsigned char f; (void)a; (void)c; (void)max; slots[0] = &f; return 1; }
void opus_art_set_colour(unsigned char **slots, int n, unsigned c) { (void)slots; (void)n; (void)c; }
void opus_on_frame_begin(struct opl_self *self, void (*fn)(void)) { (void)self; h_begin = fn; }
void opus_on_frame_under(struct opl_self *self, void (*fn)(unsigned short *)) { (void)self; h_under = fn; }
void opus_on_key(struct opl_self *self, int (*fn)(int, int)) { (void)self; (void)fn; }
static const unsigned acc[4] = { 0x698EFF, 0x00ED95, 0xFFFFFF, 0xFF3A5D };   /* stock encoder colours */
static const char *col(int c)
{
    static char b[16];
    if (c <= -10 && c >= -13) { snprintf(b, sizeof b, "#%06X", acc[-10 - c]); return b; }
    if (c > 0 && (c & 0x01000000)) { snprintf(b, sizeof b, "#%06X", c & 0xFFFFFF); return b; }
    return "#FFFFFF";
}
void opus_page_stroke_line(int x0, int y0, int x1, int y1, int width, int colour)
{
    if (svg) fprintf(svg, "<line x1=\"%.3f\" y1=\"%.3f\" x2=\"%.3f\" y2=\"%.3f\" stroke=\"%s\" stroke-width=\"%.3f\" stroke-linecap=\"round\"/>\n",
                     x0 / 256.0, y0 / 256.0, x1 / 256.0, y1 / 256.0, col(colour), width / 256.0 * 320.0 / 376.0);
}
void opus_page_text(struct opl_self *self, int slot, int x, int y, int size, int colour, const char *text)
{
    (void)self; (void)slot;
    if (svg) fprintf(svg, "<text x=\"%d\" y=\"%.2f\" font-size=\"%d\" fill=\"%s\" class=\"t\">%s</text>\n", x, y + size * 0.86, size, col(colour), text);
}
void opus_page_text_right(struct opl_self *self, int slot, int x, int y, int size, int colour, const char *text)
{
    (void)self; (void)slot;
    if (svg) fprintf(svg, "<text x=\"%d\" y=\"%.2f\" font-size=\"%d\" fill=\"%s\" class=\"t\" text-anchor=\"end\">%s</text>\n", x, y + size * 0.86, size, col(colour), text);
}
unsigned opus_accent_now(int i) { return acc[i & 3]; }
int fw_shift_held(void) { return 0; }
static int mock_page = 1, mock_owner = 16, mock_instr = 0;   /* the demo shows the master's page */
int opus_fx_page_shown(void) { return mock_page; }
int opus_fx_owner(void) { return mock_owner; }
int opus_instrument_now(void) { return mock_instr; }


/* ---- the dry part: soft electric-piano chords and a few plucks, mono-ish, with gaps so the tails are heard ---- */
#define SR 44160.0
static double env(double t, double len) { return t < 0 ? 0 : t < 0.005 ? t / 0.005 : exp(-(t - 0.005) * 3.0 / len); }
static double note(double t, double f) { return sin(2 * M_PI * f * t) + 0.35 * sin(4 * M_PI * f * t) * exp(-t * 4) + 0.12 * sin(6 * M_PI * f * t) * exp(-t * 7); }
static double mtof(int m) { return 440.0 * pow(2.0, (m - 69) / 12.0); }
static void dry(double t, double *l, double *r)
{
    /* a two-bar loop at 90 bpm: chord stabs on 1 and the "and" of 2, a pluck motif after (bar = 2.667 s) */
    static const int chords[4][4] = { { 48, 55, 60, 64 }, { 45, 52, 57, 64 }, { 41, 48, 53, 60 }, { 43, 50, 55, 62 } };
    static const int plk[6] = { 72, 76, 79, 81, 79, 76 };
    const double bar = 2.6667, beat = bar / 4;
    double sl = 0, sr = 0;
    for (int k = -1; k <= 0; k++) {
        const double tb = fmod(t, bar) - k * bar;               /* this and the previous bar ring on */
        const int bi = ((int)floor(t / bar) + k) & 3;
        if (t - fmod(t, bar) + k * bar < 0) continue;
        for (int h = 0; h < 2; h++) {
            const double st = tb - (h ? 1.5 * beat : 0), e = env(st, 0.9) * (h ? 0.6 : 1.0);
            if (e < 1e-4) continue;
            for (int n = 0; n < 4; n++) { const double v = note(st, mtof(chords[bi][n])) * e * 0.055; sl += v * (n & 1 ? 0.8 : 1.0); sr += v * (n & 1 ? 1.0 : 0.8); }
        }
        for (int p = 0; p < 6; p++) {
            const double st = tb - (2.0 + p * 0.25) * beat, e = env(st, 0.35);
            if (e < 1e-4) continue;
            const double v = note(st, mtof(plk[p])) * e * 0.07;
            sl += v * (p & 1 ? 0.7 : 1.0); sr += v * (p & 1 ? 1.0 : 0.7);
        }
    }
    *l = sl; *r = sr;
}

/* ---- the timeline (seconds): knob moves, effect on/off, captions ---- */
static double ramp(double t, double a, double b, double v0, double v1)
{
    if (t <= a) return v0;
    if (t >= b) return v1;
    const double u = (t - a) / (b - a);
    return v0 + (v1 - v0) * (u * u * (3 - 2 * u));
}
#define DUR 62.0
static void timeline(double t, short par[8], int *on, const char **cap, int *play)
{
    double sp = 60, vo = 20, sw = 0, mx = 35, dk = 0;
    *on = 1; *play = t > 3.0 && t < 56.5;
    *cap = "";
    if (t < 4.0) *cap = "TITLE";
    else if (t < 10) *cap = "sister: a stereo cathedral reverb whose tail sings, rings in tune and swells backwards";
    else if (t < 17) { sp = ramp(t, 10.5, 15.5, 60, 99); *cap = "blue \xc2\xb7 space: chapel to cathedral, the nave deepens"; }
    else if (t < 25) { sp = 99; vo = ramp(t, 17.5, 23.5, 20, 99); *cap = "green \xc2\xb7 voice: the choir's drifting vowels become tuned pipes"; }
    else if (t < 32) { sp = ramp(t, 25, 26, 99, 75); vo = ramp(t, 25, 26.5, 99, 45); sw = ramp(t, 25.5, 30.5, 0, 99); *cap = "white \xc2\xb7 swell: the tail plays backwards (vesper), the candles flare"; }
    else if (t < 37) { sp = 75; vo = 45; sw = ramp(t, 32, 33, 99, 30); mx = ramp(t, 32.5, 36, 35, 70); *cap = "orange \xc2\xb7 mix: dry to wet"; }
    else if (t < 49) {
        sp = 75; vo = 45; sw = 30; mx = 70;
        dk = t < 44 ? ramp(t, 38, 43, 0, 99) : ramp(t, 46, 48.5, 99, 55);
        *cap = t < 40 ? "new, only on opus \xc2\xb7 shift + orange \xc2\xb7 dark"
             : t < 44.5 ? "dark: a darker tank, pipes and tail \xe2\x80\x94 night falls on the nave"
             : "the moon rises, the candles burn low and amber \xc2\xb7 exclusive to the opus version";
    }
    else if (t < 54) { sp = 75; vo = 45; sw = 30; mx = 70; dk = 55; *on = 0; *cap = "fx off: the dry sound passes and the page greys out"; }
    else { sp = 75; vo = 45; sw = 30; mx = 70; dk = 55; *cap = t < 58 ? "fx on again: colours, motion and the night return" : "END"; }
    const double v[4] = { sp, vo, sw, mx };
    for (int i = 0; i < 4; i++) par[i] = (short)(v[i] * 32767 / 99 + 0.5);
    par[4] = par[5] = 0; par[6] = 16384; par[7] = (short)(dk * 32767 / 99 + 0.5);
}

int main(int argc, char **argv)
{
    if (argc < 2) return 2;
    struct opl_self self = { sizeof(struct opl_self), "sammi.sister", "/yaffs2/mod/sammi.sister", 0 };
    if (opl_init(&self)) return 1;
    struct opus_need n = { 0, 0, 0 };
    reg->need(1, &n);
    struct opus_fx fx = { calloc(1, n.state), calloc(1, n.bulk), aligned_alloc(32, (n.fast + 31) & ~31u), 1 };
    reg->init(&fx);
    char path[600];
    snprintf(path, sizeof path, "%s/audio.raw", argv[1]);
    FILE *au = fopen(path, "wb");
    snprintf(path, sizeof path, "%s/caption.txt", argv[1]);
    FILE *cp = fopen(path, "w");
    if (!au || !cp) return 1;
    const int frames = (int)(DUR * 30);
    long s = 0;
    for (int f = 0; f < frames; f++) {
        short par[8]; int on, play; const char *cap;
        timeline(f / 30.0, par, &on, &cap, &play);
        for (int b = 0; b < 23; b++) {
            short l[64], r[64], out[128];
            for (int i = 0; i < 64; i++, s++) {
                double a = 0, c = 0;
                if (play) dry(s / SR - 3.0, &a, &c);
                l[i] = (short)lrint(a * 32767); r[i] = (short)lrint(c * 32767);
            }
            if (on) reg->stereo(&fx, l, r, par);     /* off: the OS passes the dry and calls nothing */
            for (int i = 0; i < 64; i++) { out[2 * i] = l[i]; out[2 * i + 1] = r[i]; }
            fwrite(out, 2, 128, au);
        }
        sister_host_cycles += 400000u * 33u;
        h_begin();
        snprintf(path, sizeof path, "%s/frames/%04d.svg", argv[1], f);
        svg = fopen(path, "w");
        h_under(frame);
        fclose(svg); svg = 0;
        struct opus_fx_view v;
        reg->view(par, &v);
        fprintf(cp, "%s\n", cap);
    }
    fclose(au); fclose(cp);
    return 0;
}
