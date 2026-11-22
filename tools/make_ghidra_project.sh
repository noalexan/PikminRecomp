#!/usr/bin/env bash
# (Re)build the Ghidra project ghidra/PikminGPIP01 from build/main.elf using the
# Gekko/Broadway language (GameCubeLoader extension) with r13/r2 preset.
# WARNING: -overwrite replaces the existing program, discarding edits made in it.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
: "${GHIDRA_INSTALL_DIR:=/opt/ghidra}"
: "${JAVA_HOME:=/usr/lib/jvm/java-25-openjdk}"
export JAVA_HOME

mkdir -p "$root/ghidra"
exec "$GHIDRA_INSTALL_DIR/support/analyzeHeadless" "$root/ghidra" PikminGPIP01 \
	-import "$root/build/main.elf" \
	-processor PowerPC:BE:32:Gekko_Broadway -cspec default \
	-scriptPath "$root/tools/ghidra" \
	-preScript SetSmallDataBases.java \
	-overwrite "$@"
