/* sammi.sister: the Opus glue's shared declarations (sister_opus.c, sister_page.c).
 * Copyright (C) 2026 SammiBabe666 (the Opus port); SISTER (C) 2026 Hundekraft.
 * GPL-3.0-or-later with the OP-1 firmware permission (NOTICE). */
#ifndef SISTER_OPUS_H
#define SISTER_OPUS_H
#include <stdint.h>
#include "sister_dsp.h"

/* An instance's own state (the core's `state`, cleared): kennel's per-position line (the knob cache and the screen's
 * telemetry, si_pos_t) and whether it is the master's. */
struct sister_inst {
    si_pos_t pos;
    int stereo;
};

/* An instance's bulk: SISTER's whole state (kennel's FX scratch). Synth and drum share one (the core gives their two
 * instances one bulk and one fast area: they never run in the same block), so they share one reverb, as one position's
 * scratch serves whichever FX is active in R246. fast_filled: the fast area whose tables are in (0: none yet). */
struct sister_bulk {
    void *fast_filled;
    uint32_t pad[7];                       /* s starts 32-byte aligned, as the core's bulk */
    si_state s;
};

extern struct opl_self *sister_self;
extern struct sister_inst *volatile sister_last[2];   /* [0] mono (synth/drum), [1] master: the last that ran */
extern volatile int sister_key_cents_mode;            /* a green push on the page: shift + green moves cents */

int sister_semi_word(int cents);
int sister_semi_cents(int word);
int sister_key_of(int word);

/* sister_page.c */
struct opus_fx_view;
void sister_view(const short *par, struct opus_fx_view *v);
void sister_page_init(struct opl_self *self);
void sister_key_touched(void);
#endif
