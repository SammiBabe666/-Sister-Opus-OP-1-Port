#!/usr/bin/env bash
# Builds tests/bfin_sim/sim_bench.c for the Blackfin twice (SISTER's assembly kernels; their C references with
# -DSI_NO_ASM) with the Opus kit's compiler flags, runs both on the GNU simulator, builds it for the host, and checks
# that all three print the same output hashes.
#   tests/bfin_sim/run_sim.sh TOOLCHAIN_DIR SIM_RUN OPUS_SDK_DIR OUT_DIR
# Copyright (C) 2026 SammiBabe666. GPL-3.0-or-later with the OP-1 firmware permission (NOTICE).
set -euo pipefail
TC=$1; RUN=$2; SDK=$3; OUT=$4
HERE=$(cd "$(dirname "$0")" && pwd); P=$HERE/../../plugin/sammi.sister
mkdir -p "$OUT"
CFLAGS=(-mcpu=bf524-any -O2 -ffreestanding -fno-builtin -Wall -Wextra -Werror -fno-common)
SRC=("$HERE/sim_bench.c" "$P/sister_opus.c" "$P/sister_dsp.c" "$P/sister_kern.c" "$P/sister_asm.S")
for v in asm cref; do
  extra=(); [ $v = cref ] && extra=(-DSI_NO_ASM)
  objs=()
  for s in "${SRC[@]}"; do
    o=$OUT/$v-$(basename "$s").o
    "$TC/bin/bfin-elf-gcc" "${CFLAGS[@]}" "${extra[@]}" -I "$SDK" -I "$P" -c -o "$o" "$s"
    objs+=("$o")
  done
  "$TC/bin/bfin-elf-gcc" -mcpu=bf524-any -nostdlib -T "$HERE/sim.ld" -o "$OUT/bench-$v.elf" "$HERE/crt0.S" "${objs[@]}" -lgcc
  "$RUN" "$OUT/bench-$v.elf" > "$OUT/bench-$v.txt"
done
cc -O2 -DSIM_HOST -I "$SDK" -I "$P" -o "$OUT/bench-host" "$HERE/sim_bench.c" "$P/sister_opus.c" "$P/sister_dsp.c" "$P/sister_kern.c"
"$OUT/bench-host" > "$OUT/bench-host.txt"
h() { grep -o 'regime.*hash=0x[0-9a-f]*' "$1"; }
if [ "$(h "$OUT/bench-asm.txt")" = "$(h "$OUT/bench-cref.txt")" ] && [ "$(h "$OUT/bench-asm.txt")" = "$(h "$OUT/bench-host.txt")" ]; then
  echo "PASS: Blackfin asm kernels, Blackfin C references and the host build give identical output in all 18 runs"
else
  echo "FAIL: output hashes differ"; diff <(h "$OUT/bench-asm.txt") <(h "$OUT/bench-cref.txt") || true
  diff <(h "$OUT/bench-asm.txt") <(h "$OUT/bench-host.txt") || true; exit 1
fi
cat "$OUT/bench-asm.txt"
