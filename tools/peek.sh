#!/usr/bin/env bash
# Run the game offscreen with an input script and grab a screenshot as PNG.
# usage: tools/peek.sh SCRIPT FRAME [TOTAL_FRAMES] -> build/peek_<FRAME>.png, log in build/peek.log
set -u
root="$(cd "$(dirname "$0")/.." && pwd)"
script="$1"; frame="$2"; total="${3:-$((frame + 20))}"
XDG_DATA_HOME="${PEEK_DATA:-$root/build/peek-data}" "$root/build/cmake/pikmin" --disc "${PIKMIN_DISC:-$HOME/Downloads/GPIP01/game.iso}" --offscreen --fast \
    --frames "$total" --input-script "$script" --screenshot "$frame:$root/build/peek_$frame.ppm" \
    > "$root/build/peek_stdout.log" 2> "$root/build/peek.log"
code=$?
grep -E 'ppc_hle_missing|ppc_unimpl|ppc_bad_access|watchdog|ERROR|deadlock' "$root/build/peek.log" | head -3
[ -f "$root/build/peek_$frame.ppm" ] && python3 -I "$root/tools/ppm2png.py" "$root/build/peek_$frame.ppm" "$root/build/peek_$frame.png" 3
echo "exit=$code"
