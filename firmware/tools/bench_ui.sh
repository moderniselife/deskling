#!/usr/bin/env bash
# Times every frame of the main UI interactions with device-like rendering (RGB565, 1280x100
# bands), so a change can be checked for speed before it goes on the board.
#   tools/bench_ui.sh                 timings per scene
#   tools/bench_ui.sh frames <dir>    also dump a PNG every 50 ms of every scene
# Compare two frame dumps with tools/frame_diff.py <before> <after> to make sure an
# optimisation didn't change what's on screen. The host is much faster than the ESP32-P4,
# so compare scenes and before/after rather than reading the numbers as device timings.
set -euo pipefail
cd "$(dirname "$0")/.."
DEPS=${DESKLING_SIM_DEPS:-.sim-deps}
[ -d "$DEPS/lvgl" ] || tools/run_sim.sh /dev/null >/dev/null 2>&1 || true
cmake -S sim -B build-bench -DDK_BENCH=ON -DLVGL_DIR="$PWD/$DEPS/lvgl" -DCJSON_DIR="$PWD/$DEPS/cJSON" -DSTB_DIR="$PWD/$DEPS" -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build build-bench -j"$(nproc 2>/dev/null || sysctl -n hw.ncpu)" >/dev/null
if [ "${1:-}" = frames ]; then export DK_FRAMES="${2:?frames dir}"; fi
./build-bench/deskling_sim sim/fixtures /tmp | grep -E " fr |total|host timings"
