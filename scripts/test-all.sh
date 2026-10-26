#!/usr/bin/env bash
# Every automated check in the repo, in one go. Run from anywhere:
#   scripts/test-all.sh            relay, hooks, runner, OpenCode plugin, simulator (+ AddressSanitizer), pictures, web pages
#   scripts/test-all.sh --quick    skip the slow AddressSanitizer simulator run
#   scripts/test-all.sh --firmware also build the firmware for every board (needs ESP-IDF in the shell)
#
# Needs: Node 22+, a C compiler + cmake (simulator), Playwright with Chromium (web pages).
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
QUICK=0
FIRMWARE=0
for a in "$@"; do
  case "$a" in
    --quick) QUICK=1 ;;
    --firmware) FIRMWARE=1 ;;
  esac
done
OUT="$(mktemp -d)"
RESULTS=()
FAILED=0

step() { # step "name" command...
  local name="$1"
  shift
  printf '\n\033[1m== %s\033[0m\n' "$name"
  local t0=$SECONDS
  if "$@" >"$OUT/$name.log" 2>&1; then
    RESULTS+=("PASS  $name ($((SECONDS - t0))s)")
    grep -E "^(PASS|FAIL)" "$OUT/$name.log" | grep -c PASS | xargs printf '%s checks passed\n'
  else
    RESULTS+=("FAIL  $name ($((SECONDS - t0))s): see $OUT/$name.log")
    FAILED=$((FAILED + 1))
    grep -E "FAIL|Error|error" "$OUT/$name.log" | head -20
  fi
}

relay() {
  cd "$ROOT/relay"
  [ -d node_modules ] || npm ci --silent
  npx tsc --noEmit -p . || return 1
  local port=$((20000 + RANDOM % 20000))
  npx wrangler dev --port "$port" --persist-to "$OUT/relay-state" --var DESKLING_TOKEN:dev-token >"$OUT/wrangler.log" 2>&1 &
  local pid=$!
  for _ in $(seq 60); do curl -s "http://127.0.0.1:$port/setup" >/dev/null && break; sleep 1; done
  DESKLING_URL="http://127.0.0.1:$port" DESKLING_TOKEN=dev-token node test/smoke.mjs
  local rc=$?
  # Deskling in a browser (/app), against the same relay.
  [ $rc = 0 ] && { DESKLING_URL="http://127.0.0.1:$port" DESKLING_TOKEN=dev-token node "$ROOT/tests/web/app.test.mjs"; rc=$?; }
  kill "$pid" 2>/dev/null
  pkill -P "$pid" 2>/dev/null
  return $rc
}

sim() { cd "$ROOT/firmware" && tools/run_sim.sh "$OUT/sim"; }
sim_asan() { cd "$ROOT/firmware" && DESKLING_SIM_ASAN=1 tools/run_sim.sh "$OUT/sim-asan"; }
pictures() {
  # The board's PNG decoding, under valgrind when it's there, on a screenshot the simulator just drew.
  cd "$ROOT/firmware"
  cmake --build build-sim --target imgtest >/dev/null || return 1
  local png="$OUT/sim/06_sessions.png"
  [ -f "$png" ] || return 1
  if command -v valgrind >/dev/null; then valgrind -q --error-exitcode=9 ./build-sim/imgtest "$png" || return 1; else ./build-sim/imgtest "$png" || return 1; fi
  echo "PASS  decodes and shrinks a real screenshot the way the board does"
}
web() {
  cd "$ROOT/tests/web"
  node site.test.mjs && node portal.test.mjs
}
firmware_all() {
  cd "$ROOT/firmware"
  for v in p4r1-lcd1 p4r1-lcd2 p4r3-lcd1 p4r3-lcd2; do
    local b="$OUT/build-$v"
    idf.py -B "$b" -DSDKCONFIG="$b/sdkconfig" -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;config/${v%-*}.defaults;config/${v#*-}.defaults" build >"$OUT/fw-$v.log" 2>&1 || { tail -30 "$OUT/fw-$v.log"; return 1; }
    echo "PASS  firmware builds for $v"
  done
}

step relay relay
step hooks node "$ROOT/hooks/test/hook.test.mjs"
step runner node "$ROOT/hooks/runner/test/runner.test.mjs"
step opencode node "$ROOT/hooks/opencode/test/opencode.test.mjs"
step simulator sim
[ $QUICK = 1 ] || step simulator-asan sim_asan
step pictures pictures
step web web
[ $FIRMWARE = 1 ] && step firmware firmware_all

printf '\n\033[1m== Summary\033[0m\n'
printf '%s\n' "${RESULTS[@]}"
[ $FAILED = 0 ] && echo "all green" || echo "$FAILED failed"
exit $FAILED
