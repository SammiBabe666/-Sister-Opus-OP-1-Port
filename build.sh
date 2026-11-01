#!/usr/bin/env bash
# Builds dist/sammi.sister.opmod with the Opus 2 plugin kit (LizardMan245/opus @ d7f28b07) and a bfin-elf toolchain.
#   OP1_BFIN_TOOLCHAIN=/path/to/toolchain ./build.sh [OPUS_REPO_DIR]
set -euo pipefail
ROOT=$(cd "$(dirname "$0")" && pwd); OPUS=${1:-$ROOT/.opus}
[ -d "$OPUS" ] || { git clone https://github.com/LizardMan245/opus "$OPUS"; git -C "$OPUS" checkout d7f28b07cf455bb3986acd96a870cfd067c99d19; }
python3 "$ROOT/tools/gen_art.py"
mkdir -p "$ROOT/dist"
python3 "$OPUS/plugin-kit/plugin_tool.py" build "$ROOT/plugin/sammi.sister" --out "$ROOT/dist/sammi.sister.opmod"
python3 "$OPUS/plugin-kit/plugin_tool.py" show "$ROOT/dist/sammi.sister.opmod"
