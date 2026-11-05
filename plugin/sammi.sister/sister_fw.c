/* sammi.sister: the firmware calls (see sister_fw.h).
 * Copyright (C) 2026 SammiBabe666. GPL-3.0-or-later with the OP-1 firmware permission (NOTICE). */
#include "op1fw.h"
#include "sister_fw.h"

int sister_shift_held(void) { return fw_shift_held(); }
