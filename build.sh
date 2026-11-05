#!/usr/bin/env bash
# Builds dist/darksister.opmod with the Opus 2.0 beta 4 plugin kit (opus2-plugin-kit-beta4-rc1.zip, from the
# opus2-beta4-rc1 release; core 2.30) and the shared bfin-elf toolchain (GCC 13.3.0 + binutils 2.38):
#   . ~/mnt/Documents/op1-toolchain/env.sh
#   ./build.sh KIT          KIT: the unzipped opus2-plugin-kit folder (or an opus repo with plugin-kit/ inside)
set -euo pipefail
: "${OP1_BFIN_TOOLCHAIN:?set it: . ~/mnt/Documents/op1-toolchain/env.sh}"
ROOT=$(cd "$(dirname "$0")" && pwd)
KIT=${1:?give the plugin kit folder (opus2-plugin-kit from opus2-plugin-kit-beta4-rc1.zip)}
[ -f "$KIT/plugin_tool.py" ] || KIT=$KIT/plugin-kit
[ -f "$KIT/plugin_tool.py" ] || { echo "no plugin_tool.py in $1" >&2; exit 1; }
python3 "$ROOT/tools/gen_art.py"
mkdir -p "$ROOT/dist"
python3 "$KIT/plugin_tool.py" build "$ROOT/plugin/sammi.sister" --out "$ROOT/dist/darksister.opmod"
python3 "$KIT/plugin_tool.py" show "$ROOT/dist/darksister.opmod"
