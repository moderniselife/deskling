#!/usr/bin/env bash
# Cursor hook for Deskling. Forwards Cursor's hook JSON (stdin) to the relay and
# prints the relay's reply, which Cursor reads as the hook result.
#
#   deskling-cursor.sh            status events (beforeSubmitPrompt, afterFileEdit, stop)
#   deskling-cursor.sh approve    beforeShellExecution / beforeMCPExecution: wait for
#                                 Allow/Deny on Deskling, otherwise Cursor asks as usual
#
# Settings: DESKLING_URL / DESKLING_TOKEN / DESKLING_NAME / DESKLING_WAIT,
# from the environment or ~/.deskling/config.

CONF="${HOME}/.deskling/config"
# shellcheck disable=SC1090
[ -f "$CONF" ] && . "$CONF"
URL="${DESKLING_URL:-}"
TOKEN="${DESKLING_TOKEN:-}"
NAME="${DESKLING_NAME:-$(hostname 2>/dev/null || echo computer)}"
NAME="${NAME%%.*}"
WAIT="${DESKLING_WAIT:-30}"

if [ -z "$URL" ] || [ -z "$TOKEN" ]; then
  cat >/dev/null
  [ "$1" = "approve" ] && echo '{"continue":true,"permission":"ask"}' || echo '{"continue":true}'
  exit 0
fi

case "$(uname -s 2>/dev/null)" in
  Darwin) OS=mac ;; Linux) OS=linux ;; MINGW* | MSYS* | CYGWIN*) OS=windows ;; *) OS=unknown ;;
esac
URL="${URL%/}"

if [ "$1" = "approve" ]; then
  out=$(curl -sSf --max-time $((WAIT + 10)) -X POST "${URL}/hook?approve=1&wait=${WAIT}" \
    -H "Authorization: Bearer ${TOKEN}" -H "Content-Type: application/json" \
    -H "X-Deskling-Host: ${NAME}" -H "X-Deskling-OS: ${OS}" -H "X-Deskling-Source: cursor" \
    --data-binary @- 2>/dev/null)
  # Fail safe: if the relay is unreachable, let Cursor ask as it normally would.
  [ -n "$out" ] && echo "$out" || echo '{"continue":true,"permission":"ask"}'
else
  out=$(curl -sSf --max-time 4 -X POST "${URL}/hook" \
    -H "Authorization: Bearer ${TOKEN}" -H "Content-Type: application/json" \
    -H "X-Deskling-Host: ${NAME}" -H "X-Deskling-OS: ${OS}" -H "X-Deskling-Source: cursor" \
    --data-binary @- 2>/dev/null)
  [ -n "$out" ] && echo "$out" || echo '{"continue":true}'
fi
exit 0
