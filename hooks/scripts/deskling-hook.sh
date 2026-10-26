#!/usr/bin/env bash
# Forwards a Claude Code hook event (JSON on stdin) to the Deskling relay.
#
#   deskling-hook.sh        fire-and-forget (status events)
#   deskling-hook.sh sync   quick round trip, prints the relay's reply
#                           (Stop / UserPromptSubmit pick up messages queued on the screen)
#   deskling-hook.sh live   fire-and-forget tool activity (what the session is doing now)
#   deskling-hook.sh wait   PermissionRequest / AskUserQuestion: wait for an answer
#                           on the screen and print it for Claude Code
#
# Settings come from the plugin's options (CLAUDE_PLUGIN_OPTION_*), or from
# DESKLING_* environment variables, or from ~/.deskling/config, or from a deskling.conf
# inside the plugin itself (the personal copy you can download from your relay's /setup
# page carries one, so cloud and Cowork sessions that never see your machine can report).
# The hook never fails Claude Code: any problem just means no screen update.

# shellcheck disable=SC1090
[ -n "${CLAUDE_PLUGIN_ROOT:-}" ] && [ -f "${CLAUDE_PLUGIN_ROOT}/deskling.conf" ] && . "${CLAUDE_PLUGIN_ROOT}/deskling.conf"
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

case "$(uname -s 2>/dev/null)" in
  Darwin) OS=mac ;;
  Linux) OS=linux ;;
  MINGW* | MSYS* | CYGWIN*) OS=windows ;;
  *) OS=unknown ;;
esac

# Sessions in Anthropic's cloud (Claude Code on the web, cloud Cowork) run in a throwaway
# container with a random hostname: call them "Cloud" instead.
LABEL=""
if [ "${CLAUDE_CODE_REMOTE:-}" = "true" ]; then
  OS=cloud
  [ -z "$NAME" ] && NAME="Cloud"
  case "${CLAUDE_CODE_ENTRYPOINT:-}" in *cowork*) LABEL="Cowork" ;; esac
fi
[ -z "$NAME" ] && NAME="$(hostname 2>/dev/null || echo computer)"
NAME="${NAME%%.*}" # drop .local and domain suffixes

# The folder the session started in names it, wherever Claude has cd'd to since.
PROJECT=""
[ -n "${CLAUDE_PROJECT_DIR:-}" ] && PROJECT="$(basename "$CLAUDE_PROJECT_DIR" 2>/dev/null)"

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
    -H "X-Deskling-Label: ${LABEL}" \
    -H "X-Deskling-Project: ${PROJECT}" \
    --data-binary @- 2>/dev/null || true
elif [ "$1" = "sync" ]; then
  curl -sSf --max-time 4 -X POST "${URL}/hook" \
    -H "Authorization: Bearer ${TOKEN}" \
    -H "Content-Type: application/json" \
    -H "X-Deskling-Host: ${NAME}" \
    -H "X-Deskling-OS: ${OS}" \
    -H "X-Deskling-Label: ${LABEL}" \
    -H "X-Deskling-Project: ${PROJECT}" \
    --data-binary @- 2>/dev/null || true
else
  Q=""
  [ "$1" = "live" ] && Q="?live=1"
  curl -sSf --max-time 5 -o /dev/null -X POST "${URL}/hook${Q}" \
    -H "Authorization: Bearer ${TOKEN}" \
    -H "Content-Type: application/json" \
    -H "X-Deskling-Host: ${NAME}" \
    -H "X-Deskling-OS: ${OS}" \
    -H "X-Deskling-Label: ${LABEL}" \
    -H "X-Deskling-Project: ${PROJECT}" \
    --data-binary @- 2>/dev/null || true
fi
exit 0
