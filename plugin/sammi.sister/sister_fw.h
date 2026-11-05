/* sammi.sister: the plugin's only use of the OP-1 firmware's own functions (op1fw.h), kept in one place so a change
 * in a later Opus (Beta 4 or after) is a one-file edit. Everything else goes through Opus's documented services.
 * Copyright (C) 2026 SammiBabe666. GPL-3.0-or-later with the OP-1 firmware permission (NOTICE). */
#ifndef SISTER_FW_H
#define SISTER_FW_H
int sister_shift_held(void);               /* 1 while shift is held (display thread) */
#endif
