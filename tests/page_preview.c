/* sammi.sister page preview: runs the plugin's sister_page.c (and the glue and DSP, for the telemetry) against a mock
 * core that records every stroke and text instead of drawing, and writes them as SVG fragments; tests/page_preview.py
 * puts each over the still art (sister.svg) and renders PNGs. A preview of the drawing code's output on the host, in
 * a browser's renderer: not the OP-1's renderer, font or panel.
 *
 * Copyright (C) 2026 SammiBabe666. GPL-3.0-or-later with the OP-1 firmware permission (NOTICE).
 *   cc -I SDK -I plugin/sammi.sister tests/page_preview.c plugin/sammi.sister/{sister_opus,sister_page,sister_fw,sister_dsp,sister_kern}.c
 *   ./a.out OUT_DIR */
#include "opus_plugin.h"
#include "op1fw.h"
#include "sister_dsp.h"
#include "sister_opus.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const struct opus_effect *reg;
static void (*h_begin)(void);
static void (*h_under)(unsigned short *);
static int shift;
static FILE *svg;
extern unsigned sister_host_cycles;

int opus_effect_register(struct opl_self *self, const struct opus_effect *e) { (void)self; reg = e; return 8; }
void *opus_alloc(struct opl_self *self, unsigned bytes) { (void)self; return calloc(1, bytes); }
int opus_art_text(struct opl_self *self, const char *name, const char *s, unsigned len) { (void)self; (void)name; (void)s; (void)len; return 0; }
int opus_art_ready(int a) { return a == 0; }
void opus_art_draw(int a) { (void)a; }
static unsigned short frame[376 * 188];
unsigned short *opus_page_pixels(int *rows) { *rows = 376; return frame; }
int opus_page_keep(unsigned short *out, unsigned cap, int x0, int x1, int y0, int y1) { (void)out; (void)cap; (void)x0; (void)x1; (void)y0; (void)y1; return 1; }
void opus_page_put(const unsigned short *runs, int used) { (void)runs; (void)used; }
void opus_on_frame_begin(struct opl_self *self, void (*fn)(void)) { (void)self; h_begin = fn; }
void opus_on_frame_under(struct opl_self *self, void (*fn)(unsigned short *)) { (void)self; h_under = fn; }
void opus_on_key(struct opl_self *self, int (*fn)(int, int)) { (void)self; (void)fn; }
int opus_art_colour_slots(int a, unsigned c, unsigned char **slots, int max) { static unsigned char f; (void)a; (void)c; (void)max; slots[0] = &f; return 1; }
void opus_art_set_colour(unsigned char **slots, int n, unsigned c) { (void)slots; (void)n; (void)c; }
static const char *col(int c)
{
    static char b[16];
    static const unsigned acc[4] = { 0x698EFF, 0x00ED95, 0xFFFFFF, 0xFF3A5D };   /* stock-looking encoder colours */
    if (c <= -10 && c >= -13) { snprintf(b, sizeof b, "#%06X", acc[-10 - c]); return b; }
    if (c > 0 && (c & 0x01000000)) { snprintf(b, sizeof b, "#%06X", c & 0xFFFFFF); return b; }
    return "#FFFFFF";
}
void opus_page_stroke_line(int x0, int y0, int x1, int y1, int width, int colour)
{
    if (svg)
        fprintf(svg, "<line x1=\"%.3f\" y1=\"%.3f\" x2=\"%.3f\" y2=\"%.3f\" stroke=\"%s\" stroke-width=\"%.3f\" stroke-linecap=\"round\"/>\n",
                x0 / 256.0, y0 / 256.0, x1 / 256.0, y1 / 256.0, col(colour), width / 256.0 * 320.0 / 376.0);
}
void opus_page_text(struct opl_self *self, int slot, int x, int y, int size, int colour, const char *text)
{
    (void)self; (void)slot;
    if (svg)
        fprintf(svg, "<text x=\"%d\" y=\"%.2f\" font-size=\"%d\" fill=\"%s\" class=\"t\">%s</text>\n", x, y + size * 0.86, size,
                col(colour), text);
}
void opus_page_text_right(struct opl_self *self, int slot, int x, int y, int size, int colour, const char *text)
{
    (void)self; (void)slot;
    if (svg) fprintf(svg, "<text x=\"%d\" y=\"%.2f\" font-size=\"%d\" fill=\"%s\" class=\"t\" text-anchor=\"end\">%s</text>\n", x, y + size * 0.86, size, col(colour), text);
}
unsigned opus_accent_now(int i) { static const unsigned c[4] = { 0x698EFF, 0x00ED95, 0xFFFFFF, 0xFF3A5D }; return c[i & 3]; }
int fw_shift_held(void) { return shift; }
static int mock_page = 1, mock_owner = 0, mock_instr = 0;
int opus_fx_page_shown(void) { return mock_page; }
int opus_fx_owner(void) { return mock_owner; }
int opus_instrument_now(void) { return mock_instr; }


static uint32_t rng = 7;
static int16_t rnd(void) { rng = rng * 1664525u + 1013904223u; return (int16_t)(rng >> 16); }

/* one state: the knobs, an input level for the tail (0: silent), frames to settle, shift */
static void shot(const char *dir, const char *name, short k0, short k1, short k2, short k3, int level, int sh, int off)
{
    static struct opus_fx fx;
    static int made;
    if (!made) {
        struct opus_need n = { 0, 0, 0 };
        reg->need(0, &n);
        fx.state = calloc(1, n.state); fx.bulk = calloc(1, n.bulk); fx.fast = aligned_alloc(32, (n.fast + 31) & ~31u); fx.stereo = 0;
        reg->init(&fx);
        made = 1;
    }
    short par[8] = { k0, k1, k2, k3, 0, 0, 16384, 0 };
    struct opus_fx_view v;
    int16_t buf[64];
    shift = sh;
    for (int f = 0; f < 90; f++) {                        /* ~3 s at 30 frames a second, 22 blocks a frame */
        sister_host_cycles += 400000u * 33u;
        for (int b = 0; b < 22 && !(off && f >= 60); b++) {   /* off: the OS stops calling the process */
            for (int i = 0; i < 64; i++) buf[i] = level ? (int16_t)(rnd() / level) : 0;
            reg->mono(&fx, buf, par);
        }
        h_begin();
        char path[512];
        snprintf(path, sizeof path, "%s/%s.frag.svg", dir, name);
        svg = f == 89 ? fopen(path, "w") : 0;
        h_under(frame);
        if (svg) fclose(svg);
        svg = 0;
        reg->view(par, &v);
    }
}

int main(int argc, char **argv)
{
    if (argc < 2) return 2;
    struct opl_self self = { sizeof(struct opl_self), "sammi.sister", "/yaffs2/mod/sammi.sister", 0 };
    if (opl_init(&self)) return 1;
    shot(argv[1], "1-defaults-silent", 19661, 6554, 0, 11469, 0, 0, 0);
    shot(argv[1], "2-defaults-playing", 19661, 6554, 0, 11469, 6, 0, 0);
    shot(argv[1], "3-pipes-playing", 19661, 32767, 0, 20000, 6, 0, 0);
    shot(argv[1], "4-cathedral-vesper-wet", 32767, 16000, 32767, 32767, 6, 0, 0);
    shot(argv[1], "5-chapel-dry-shift-key", 0, 0, 0, 0, 0, 1, 0);
    shot(argv[1], "6-swell-50-playing", 19661, 6554, 16384, 11469, 6, 0, 0);
    shot(argv[1], "7-swell-99-playing", 19661, 6554, 32767, 11469, 6, 0, 0);
    shot(argv[1], "2-defaults-playing-OFF", 19661, 6554, 0, 11469, 6, 0, 1);
    shot(argv[1], "3-pipes-playing-OFF", 19661, 32767, 0, 20000, 6, 0, 1);
    return 0;
}
