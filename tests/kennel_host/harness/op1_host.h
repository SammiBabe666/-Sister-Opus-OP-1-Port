/* Stand-in for kennel's harness/op1_host.h (not shipped in hundekraft-3.1.1-source.tar.gz), written for this port's
 * tests only: SISTER's own sister_test.c includes op1_plugin.h with OP1_HOST and uses none of these; they only let
 * op1_plugin.h's inline helpers compile on the host.
 * Copyright (C) 2026 SammiBabe666. GPL-3.0-or-later with the OP-1 firmware permission (NOTICE). */
#ifndef OP1_HOST_H
#define OP1_HOST_H
#include <stdint.h>
static uint8_t op1_host_voices[6 * 0x24] __attribute__((unused));
static int16_t op1_host_vel[8] __attribute__((unused));
#define OP1_VOICE_BASE ((uintptr_t)op1_host_voices)
#define OP1_VOICE_VEL  op1_host_vel
static inline void op1_node_set_transform(void *node, const int32_t *m) { (void)node; (void)m; }
#endif
