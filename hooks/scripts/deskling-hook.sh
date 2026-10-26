#!/usr/bin/env bash
# Forwards a Claude Code hook event (JSON on stdin) to the Deskling relay.
#
#   deskling-hook.sh        fire-and-forget (most events)
#   deskling-hook.sh wait   PermissionRequest: wait for Allow/Deny on the screen
#                           and print the decision for Claude Code
#
# Settings come from the plugin's options (CLAUDE_PLUGIN_OPTION_*), or from
# DESKLING_* environment variables, or from ~/.deskling/config.
# The hook never fails Claude Code: any problem just means no screen update.

CONF="${HOME}/.deskling/config"
# shellcheck disable=SC1090
[ -f "$CONF" ] && . "$CONF"

URL="${CLAUDE_PLUGIN_OPTION_RELAY_URL:-${DESKLING_URL:-}}"
TOKEN="${CLAUDE_PLUGIN_OPTION_TOKEN:-${DESKLING_TOKEN:-}}"
NAME="${CLAUDE_PLUGIN_OPTION_DEVICE_NAME:-${DESKLING_NAME:-}}"
WAIT="${CLAUDE_PLUGIN_OPTION_APPROVAL_WAIT:-${DESKLING_WAIT:-30}}"

if [ -z "$URL" ] || [ -z "$TOKEN" ]; then
  cat >/dev/null
  exit 0
fi

[ -z "$NAME" ] && NAME="$(hostname 2>/dev/null || echo computer)"
NAME="${NAME%%.*}" # drop .local and domain suffixes

case "$(uname -s 2>/dev/null)" in
  Darwin) OS=mac ;;
  Linux) OS=linux ;;
  MINGW* | MSYS* | CYGWIN*) OS=windows ;;
  *) OS=unknown ;;
esac

URL="${URL%/}"
WAIT="${WAIT%%.*}"
case "$WAIT" in '' | *[!0-9]*) WAIT=30 ;; esac

if [ "$1" = "wait" ]; then
  if [ "$WAIT" -le 0 ]; then
    # Screen approvals are off: the Notification hook still shows the alert.
    cat >/dev/null
    exit 0
  fi
  # Prints the relay's decision JSON (or nothing, so the terminal prompt appears).
  curl -sSf --max-time $((WAIT + 10)) -X POST "${URL}/hook?wait=${WAIT}" \
    -H "Authorization: Bearer ${TOKEN}" \
    -H "Content-Type: application/json" \
    -H "X-Deskling-Host: ${NAME}" \
    -H "X-Deskling-OS: ${OS}" \
    --data-binary @- 2>/dev/null || true
else
  curl -sSf --max-time 5 -o /dev/null -X POST "${URL}/hook" \
    -H "Authorization: Bearer ${TOKEN}" \
    -H "Content-Type: application/json" \
    -H "X-Deskling-Host: ${NAME}" \
    -H "X-Deskling-OS: ${OS}" \
    --data-binary @- 2>/dev/null || true
fi
exit 0
