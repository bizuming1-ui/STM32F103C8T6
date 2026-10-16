#!/usr/bin/env bash
# ===========================================================================
# simulator/build.sh
# ---------------------------------------------------------------------------
# Compiles the REAL firmware sources (the same 20 .c/.h files that go into
# Keil) into a wasm module the browser can run, together with sim_hal.c.
#
# The only build-level difference from the Keil project:
#   -DFW_SIMULATOR      selects the simulator-only #if blocks
#   -Isimulator/hal     provides a stub stm32f10x.h (no SPL, no startup file)
#   -Dmain=Fw_Main      wasm-ld always special-cases the symbol "main"; renaming
#                       it is what lets main.c export itself as `fw_step`
#                       (the board build keeps the plain `int main(void)`).
#
# Requirements: zig (>=0.13) on PATH, or `pip install ziglang`.
# Usage:  bash simulator/build.sh
# ===========================================================================
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="$ROOT/simulator/fw_sim.wasm"

if command -v zig >/dev/null 2>&1; then
  ZIG=zig
elif [ -x "$(python3 -c 'import ziglang,os;print(os.path.dirname(ziglang.__file__))' 2>/dev/null)/zig" ]; then
  ZIG="$(python3 -c 'import ziglang,os;print(os.path.dirname(ziglang.__file__))')/zig"
else
  echo "zig not found: pip install ziglang" >&2
  exit 1
fi

cd "$ROOT"
"$ZIG" cc --target=wasm32-freestanding -O2 -DFW_SIMULATOR=1 \
  -Isimulator/hal -I. \
  -nostdlib -Wl,--no-entry \
  -Dmain=Fw_Main \
  -Wl,--initial-memory=4194304 -Wl,--max-memory=16777216 \
  -o "$OUT" \
  main.c OLED.c Key.c LED.c DS18B20.c Scene.c animation.c \
  yuexin_cat_frames.c yuexin_cat_roll.c yuexin_cat_angry.c \
  simulator/sim_hal.c

ls -l "$OUT"
