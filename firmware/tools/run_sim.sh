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
cmake -S sim -B build-sim -DLVGL_DIR="$PWD/$DEPS/lvgl" -DCJSON_DIR="$PWD/$DEPS/cJSON" -DSTB_DIR="$PWD/$DEPS" -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build build-sim -j"$(nproc 2>/dev/null || sysctl -n hw.ncpu)" >/dev/null
./build-sim/deskling_sim sim/fixtures "$OUT" "${2:-}"
