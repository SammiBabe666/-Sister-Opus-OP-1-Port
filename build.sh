#!/usr/bin/env bash
# Builds dist/darksister.opmod with the Opus 2 plugin kit (LizardMan245/opus @ d7f28b07, Opus 2.0 beta 3.2) and the
# shared bfin-elf toolchain (GCC 13.3.0 + binutils 2.38): . ~/mnt/Documents/op1-toolchain/env.sh first.
#   ./build.sh [OPUS_REPO_DIR]   (OPUS_REPO_DIR: the opus repo, plugin-kit/ inside)
set -euo pipefail
: "${OP1_BFIN_TOOLCHAIN:?set it: . ~/mnt/Documents/op1-toolchain/env.sh}"
ROOT=$(cd "$(dirname "$0")" && pwd); OPUS=${1:-$ROOT/.opus}
[ -d "$OPUS" ] || { git clone https://github.com/LizardMan245/opus "$OPUS"; git -C "$OPUS" checkout d7f28b07cf455bb3986acd96a870cfd067c99d19; }
python3 "$ROOT/tools/gen_art.py"
mkdir -p "$ROOT/dist"
python3 "$OPUS/plugin-kit/plugin_tool.py" build "$ROOT/plugin/sammi.sister" --out "$ROOT/dist/darksister.opmod"
python3 "$OPUS/plugin-kit/plugin_tool.py" show "$ROOT/dist/darksister.opmod"
