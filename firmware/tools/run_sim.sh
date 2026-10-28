#!/usr/bin/env bash
# Builds the host simulator and renders every screen to PNG (plus animation frames with --frames).
#   tools/run_sim.sh [out dir] [--frames]
set -euo pipefail
cd "$(dirname "$0")/.."
DEPS=${DESKLING_SIM_DEPS:-.sim-deps}
OUT=${1:-sim-out}
mkdir -p "$DEPS"
[ -d "$DEPS/lvgl" ] || git clone -q --depth 1 --branch v9.6.0 https://github.com/lvgl/lvgl.git "$DEPS/lvgl"
[ -d "$DEPS/cJSON" ] || git clone -q --depth 1 https://github.com/DaveGamble/cJSON.git "$DEPS/cJSON"
[ -f "$DEPS/stb_image_write.h" ] || curl -sSfL -o "$DEPS/stb_image_write.h" https://raw.githubusercontent.com/nothings/stb/master/stb_image_write.h
# DESKLING_SIM_ASAN=1: build with AddressSanitizer (slower; catches memory bugs) into build-sim-asan.
B=build-sim
FLAGS=()
if [ "${DESKLING_SIM_ASAN:-0}" = "1" ]; then
  B=build-sim-asan
  FLAGS=(-DCMAKE_BUILD_TYPE=Debug "-DCMAKE_C_FLAGS=-fsanitize=address -fno-omit-frame-pointer -g" "-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address")
else
  FLAGS=(-DCMAKE_BUILD_TYPE=Release)
fi
cmake -S sim -B "$B" -DLVGL_DIR="$PWD/$DEPS/lvgl" -DCJSON_DIR="$PWD/$DEPS/cJSON" -DSTB_DIR="$PWD/$DEPS" "${FLAGS[@]}" >/dev/null
cmake --build "$B" -j"$(nproc 2>/dev/null || sysctl -n hw.ncpu)" >/dev/null
"./$B/deskling_sim" sim/fixtures "$OUT" "${2:-}"
# Coming back from a quiet fresh start (ui/health.h), and (not under AddressSanitizer: too slow)
# an hour of simulated running that must not build up animations, objects or memory.
DK_QUIET_START=1 "./$B/deskling_sim" sim/fixtures "$OUT" | grep -E '^(PASS|FAIL)'
if [ "${DESKLING_SIM_ASAN:-0}" != "1" ]; then
  DK_SOAK="${DESKLING_SIM_SOAK:-60}" "./$B/deskling_sim" sim/fixtures "$OUT/soak" | grep -E '^(PASS|FAIL|soak)'
fi
