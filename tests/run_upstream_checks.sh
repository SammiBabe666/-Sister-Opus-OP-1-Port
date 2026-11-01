#!/usr/bin/env bash
# Runs SISTER's own host checks (Hundekraft's check.py: sister_test.c under ASan/UBSan, clicks, the pipes' tuning,
# VESPER's reverse swell, levels, stereo) on the DSP files this port ships (plugin/sammi.sister), in a scratch copy of
# kennel's layout. Needs Python 3 with numpy and a host C compiler.
#   tests/run_upstream_checks.sh OUT_DIR
# Copyright (C) 2026 SammiBabe666. GPL-3.0-or-later with the OP-1 firmware permission (NOTICE).
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd); ROOT=$HERE/..; OUT=$(mkdir -p "$1" && cd "$1" && pwd)
W=$(mktemp -d); trap 'rm -rf "$W"' EXIT
mkdir -p "$W/synth_sdk/plugins/sister" "$W/synth_sdk/harness"
cp "$HERE/kennel_host/op1_plugin.h" "$HERE/kennel_host/op1_l1.h" "$W/synth_sdk/"
cp "$HERE/kennel_host/harness/op1_host.h" "$W/synth_sdk/harness/"
# the shipped DSP (the port's copies), SISTER's own test and render tools and its kennel glue (sister_test.c includes it)
for f in sister_dsp.c sister_dsp.h sister_kern.c sister_kern.h sister_tables.h; do cp "$ROOT/plugin/sammi.sister/$f" "$W/synth_sdk/plugins/sister/"; done
for f in sister.c sister_test.c sister_render.c check.py; do cp "$ROOT/upstream/sister/$f" "$W/synth_sdk/plugins/sister/"; done
python3 "$W/synth_sdk/plugins/sister/check.py" "$OUT"
