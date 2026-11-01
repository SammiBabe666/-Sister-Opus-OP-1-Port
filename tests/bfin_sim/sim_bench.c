/* sammi.sister on the GNU Blackfin simulator: the plugin's compiled glue (sister_opus.c) and SISTER's DSP with its
 * Blackfin kernels (or, with -DSI_NO_ASM, their C references), built with the Opus kit's flags, run through the
 * effect's own mono/stereo/init calls in the nine regimes of SISTER's budget_check.py (same knobs, same input, same
 * output hash). Prints, per regime and position: the output hash, the init block, the worst block and the average,
 * in the simulator's CYCLES.
 *
 * What this is: instruction-level execution of the real Blackfin code, and a cycle count from the simulator's own
 * model (bfin-elf-run: no SDRAM, cache or display-DMA stalls). It is not the OP-1, not the Opus core and not a
 * device measurement. The host build (cc -DSIM_HOST) prints the same hashes for the comparison.
 *
 * Copyright (C) 2026 SammiBabe666. GPL-3.0-or-later with the OP-1 firmware permission (NOTICE). */
#include "opus_plugin.h"
#include "sister_dsp.h"
#include "sister_opus.h"

#ifdef SIM_HOST
#include <stdio.h>
#include <stdlib.h>
#define CYC(v) ((v) = 0)
#define L1
static void out(const char *s) { fputs(s, stdout); }
void sim_exit(int code);
void sim_exit(int code) { exit(code); }
#else
#define CYC(v) __asm__ volatile ("%0 = CYCLES;" : "=d"(v))
#define L1                                  /* bfin-elf-run's libgloss mode maps no L1; it models no memory stalls either */
static int sys(int fn, int *args)
{
    int r;
    __asm__ volatile ("P0 = %1; R0 = %2; EXCPT 0; %0 = R0;" : "=d"(r) : "d"(fn), "d"(args) : "P0", "R0", "memory");
    return r;
}
/* output is gathered and written once at the end (many small writes came out garbled on bfin-elf-run) */
static char obuf[8192];
static unsigned on;
static void out(const char *s) { while (*s && on < sizeof obuf) obuf[on++] = *s++; }
static void flush(void) { int a[3] = { 1, (int)obuf, (int)on }; sys(5, a); on = 0; }
void sim_exit(int code);
void sim_exit(int code) { int a[1] = { code }; flush(); sys(1, a); }
#endif

/* ---- the core's side, as far as the effect needs it ---- */
static const struct opus_effect *reg;
int opus_effect_register(struct opl_self *self, const struct opus_effect *e) { (void)self; reg = e; return 8; }
/* the page is not part of this benchmark (sister_page.c is the display thread's) */
volatile int sister_key_cents_mode;
void sister_page_init(struct opl_self *self) { (void)self; }
void sister_view(const short *par, struct opus_fx_view *v) { (void)par; (void)v; }
void sister_key_touched(void) { }

static struct sister_inst st[2] __attribute__((aligned(32)));
static struct sister_bulk bulk[2] __attribute__((aligned(32)));
static unsigned char fast_m[SI_L1_BYTES] L1 __attribute__((aligned(32)));
static unsigned char fast_s[SI_L1_CORE] L1 __attribute__((aligned(32)));

static void put_u(const char *k, unsigned v, int hex)
{
    char b[24], *p = b + 23;
    *p = 0;
    do { unsigned d = hex ? v & 15 : v % 10; *--p = (char)(d < 10 ? '0' + d : 'a' + d - 10); v = hex ? v >> 4 : v / 10; } while (v);
    out(" "); out(k); out("="); if (hex) out("0x"); out(p);
}

int main(void);
int main(void)
{
    struct opl_self self = { sizeof(struct opl_self), "sammi.sister", "/yaffs2/mod/sammi.sister", 0 };
    if (opl_init(&self) || !reg) { out("register failed\n"); return 1; }
    static const int pos_list[2] = { 0, 2 };
    const unsigned renders = 1200;
    for (int reg_no = 0; reg_no <= 8; reg_no++)
        for (int pi = 0; pi < 2; pi++) {
            const int stereo = pos_list[pi] == 2;
            struct opus_fx fx = { &st[stereo], &bulk[stereo], stereo ? (void *)fast_s : (void *)fast_m, stereo };
            for (unsigned i = 0; i < sizeof st[stereo]; i++) ((unsigned char *)&st[stereo])[i] = 0;
            for (unsigned i = 0; i < sizeof bulk[stereo]; i++) ((unsigned char *)&bulk[stereo])[i] = 0;
            unsigned char *f = stereo ? fast_s : fast_m;
            for (unsigned i = 0; i < (stereo ? sizeof fast_s : sizeof fast_m); i++) f[i] = 0xA5;   /* not cleared */
            short params[8];
            for (int i = 0; i < 8; i++) params[i] = reg->defaults[i];
            params[4] = 0;
            params[6] = 16384;
            if (reg_no == 1) for (int i = 0; i < 4; i++) params[i] = 0;
            if (reg_no == 2) for (int i = 0; i < 4; i++) params[i] = 32767;
            if (reg_no == 3) { params[0] = 32767; params[1] = 16384; params[2] = 32767; params[3] = 29000; }
            if (reg_no == 6) params[1] = 32767;
            if (reg_no == 7) params[3] = 0;
            static short lbuf[64], rbuf[64];
            uint32_t rng = 0x12345u, blk = 0, b0 = 0, bmax = 0, bat = 0, bsum = 0, bn = 0, t0, t1, hash = 2166136261u;
            CYC(t0); reg->init(&fx); CYC(t1); blk = t1 - t0;
            for (uint32_t r = 0; r < renders; r++) {
                if (reg_no == 4) { for (int i = 0; i < 4; i++) { rng = rng * 1664525u + 1013904223u; params[i] = (int16_t)(rng >> 17); }
                                   rng = rng * 1664525u + 1013904223u; params[6] = (int16_t)(rng >> 17); }
                if (reg_no == 6 && r % 16 == 0) { rng = rng * 1664525u + 1013904223u; params[6] = (int16_t)(600 + (rng >> 17) % 31000); }
                if (reg_no == 5 && r && r % 128 == 0) { uint32_t a, b; CYC(a); reg->init(&fx); CYC(b); blk += b - a; }
                const int on = reg_no != 8 || (r & 63) < 8;
                for (int i = 0; i < 64; i++) {
                    rng = rng * 1664525u + 1013904223u; lbuf[i] = on ? (int16_t)((int32_t)(rng >> 16) - 32768) / 3 : 0;
                    rng = rng * 1664525u + 1013904223u; rbuf[i] = on ? (int16_t)((int32_t)(rng >> 16) - 32768) / 3 : 0;
                }
                { uint32_t a, b; CYC(a);
                  if (stereo) reg->stereo(&fx, lbuf, rbuf, params); else reg->mono(&fx, lbuf, params);
                  CYC(b); blk += b - a; }
                for (int i = 0; i < 64; i++) { hash = (hash ^ (uint16_t)lbuf[i]) * 16777619u; if (stereo) hash = (hash ^ (uint16_t)rbuf[i]) * 16777619u; }
                if (r == 0) b0 = blk; else if (blk > bmax) { bmax = blk; bat = r; }
                bsum += blk; bn++; blk = 0;
            }
            out("regime "); { char d[2] = { (char)('0' + reg_no), 0 }; out(d); } out(stereo ? " master" : " mono  ");
            put_u("hash", hash, 1);
#ifndef SIM_HOST
            put_u("init_block", b0, 0); put_u("worst", bmax, 0); put_u("at", bat, 0); put_u("avg", bsum / bn, 0);
#endif
            out("\n");
        }
    out("done\n");
    return 0;
}
