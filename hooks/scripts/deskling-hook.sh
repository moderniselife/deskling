#!/usr/bin/env bash
# Forwards a Claude Code hook event (JSON on stdin) to the Deskling relay.
#
#   deskling-hook.sh        fire-and-forget (status events)
#   deskling-hook.sh sync   quick round trip, prints the relay's reply
#                           (Stop / UserPromptSubmit pick up messages queued on the screen)
#   deskling-hook.sh stop   Stop: sync, or wait for the screen while "Keep listening" is on
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
STREAM="${CLAUDE_PLUGIN_OPTION_STREAM:-${DESKLING_STREAM:-1}}"

BODY="$(cat)"
field() { # field NAME: a top-level string field of the hook's JSON (good enough for ids and paths)
  printf '%s' "$BODY" | sed -n "s/.*\"$1\" *: *\"\\([^\"]*\\)\".*/\\1/p" | head -n 1
}
SID="$(field session_id)"
TP="$(field transcript_path | sed 's/\\\\/\\/g')"
case "$TP" in [A-Za-z]:\\*) command -v cygpath >/dev/null 2>&1 && TP="$(cygpath -u "$TP")" ;; esac
STATE="${HOME}/.deskling/state"

post() { # post QUERY MAXTIME: send the hook event, print the relay's reply
  printf '%s' "$BODY" | curl -sSf --max-time "$2" -X POST "${URL}/hook$1" \
    -H "Authorization: Bearer ${TOKEN}" \
    -H "Content-Type: application/json" \
    -H "X-Deskling-Host: ${NAME}" \
    -H "X-Deskling-OS: ${OS}" \
    -H "X-Deskling-Label: ${LABEL}" \
    -H "X-Deskling-Project: ${PROJECT}" \
    --data-binary @- 2>/dev/null
}

# Replying to a bit of Claude's message: the app attaches the quoted words as a file that only
# exists here. Send them (from stdin's file references) so the screen can show what you replied to.
send_quotes() {
  grep -o '/[^"\\]*excerpt_from_previous_claude_message\.txt' | sort -u | head -n 5 | while read -r Q; do
    [ -f "$Q" ] && head -c 1200 "$Q" | curl -sSf --max-time 3 -o /dev/null -X POST "${URL}/quote?sid=${SID}&name=$(basename "$Q")" \
      -H "Authorization: Bearer ${TOKEN}" -H "Content-Type: text/plain; charset=utf-8" --data-binary @- 2>/dev/null
  done
}

# ---- the conversation as it happens
# New lines of the session's transcript go to the relay, which keeps Claude's words, thoughts,
# steps and pictures for the screen. Tool output (file contents, command output) is filtered out
# here and never sent, except images. One upload at a time per session; the offset is remembered.
stream_transcript() {
  [ "$STREAM" = "0" ] && return 0
  [ -n "$SID" ] && [ -n "$TP" ] && [ -f "$TP" ] || return 0
  mkdir -p "$STATE" 2>/dev/null || return 0
  LOCK="$STATE/$SID.lock"
  find "$LOCK" -maxdepth 0 -mmin +1 -exec rmdir {} \; 2>/dev/null
  mkdir "$LOCK" 2>/dev/null || return 0
  SIZE=$(wc -c <"$TP" | tr -d ' ')
  OFF=$(cat "$STATE/$SID.off" 2>/dev/null)
  case "$OFF" in '' | *[!0-9]*) OFF=$((SIZE > 65536 ? SIZE - 65536 : 0)) ;; esac
  [ "$OFF" -gt "$SIZE" ] && OFF=0
  if [ "$SIZE" -gt "$OFF" ]; then
    CHUNK="$STATE/$SID.chunk"
    tail -c +$((OFF + 1)) "$TP" | head -c 2097152 >"$CHUNK"
    GOT=$(wc -c <"$CHUNK" | tr -d ' ')
    PART=0
    # Only whole lines: leave a half-written last line for next time.
    [ "$(tail -c 1 "$CHUNK" | od -An -c | tr -d ' ')" != '\n' ] && PART=$(tail -n 1 "$CHUNK" | wc -c | tr -d ' ')
    USE=$((GOT - PART))
    [ "$USE" -le 0 ] && [ "$GOT" -ge 2097152 ] && USE=$GOT # one enormous line: skip past it
    if [ "$USE" -gt 0 ]; then
      head -c "$USE" "$CHUNK" | grep '"operation":"remove"' | send_quotes
      if head -c "$USE" "$CHUNK" |
        grep -v -e '"type":"tool_result"' -e '"type":"attachment"' -e '^{"type":"\(last-prompt\|mode\|atis-latch\|frame-link\)"' |
        cat - <(head -c "$USE" "$CHUNK" | grep '"type":"tool_result"' | grep '"type":"image"') 2>/dev/null |
        curl -sSf --max-time 20 -o /dev/null -X POST "${URL}/transcript?sid=${SID}" \
          -H "Authorization: Bearer ${TOKEN}" -H "Content-Type: application/x-ndjson" --data-binary @- 2>/dev/null; then
        echo $((OFF + USE)) >"$STATE/$SID.off"
      fi
    fi
    rm -f "$CHUNK"
  fi
  rmdir "$LOCK" 2>/dev/null
  return 0
}

if [ "$1" = "wait" ]; then
  ( stream_transcript ) >/dev/null 2>&1 &
  # Screen approvals off: the Notification hook still shows the alert.
  [ "$WAIT" -le 0 ] && exit 0
  # Prints the relay's decision JSON (or nothing, so the terminal prompt appears).
  post "?wait=${WAIT}" $((WAIT + 10)) || true
elif [ "$1" = "stop" ]; then
  stream_transcript >/dev/null 2>&1
  # Stop: like sync, but if you tapped "Keep listening" on the screen the relay holds this hook
  # until you send a message from there (it becomes Claude's next prompt) or listening ends.
  # The relay answers {"again":true} every ~85 s so no connection stays open too long.
  # If you type in the app or terminal meanwhile (Claude Code queues it), we let go at once so
  # your message runs.
  START=0
  [ -f "$TP" ] && START=$(wc -c <"$TP" | tr -d ' ')
  OUTF="$(mktemp 2>/dev/null || echo "/tmp/deskling.$$")"
  ROUND=0
  while :; do
    post "?hold=1&round=${ROUND}" 100 >"$OUTF" &
    CURL=$!
    while kill -0 "$CURL" 2>/dev/null; do
      sleep 1
      if [ -f "$TP" ] && [ "$(wc -c <"$TP" | tr -d ' ')" -gt "$START" ] &&
        tail -c +$((START + 1)) "$TP" | grep -q '"operation":"enqueue"'; then
        kill "$CURL" 2>/dev/null
        printf '{"session_id":"%s"}' "$SID" | curl -sSf --max-time 5 -o /dev/null -X POST "${URL}/listen/release" \
          -H "Authorization: Bearer ${TOKEN}" -H "Content-Type: application/json" --data-binary @- 2>/dev/null
        rm -f "$OUTF"
        exit 0
      fi
    done
    wait "$CURL" || { rm -f "$OUTF"; exit 0; }
    OUT="$(cat "$OUTF")"
    case "$OUT" in
      *'"again":true'*) ROUND=$((ROUND + 1)); [ "$ROUND" -gt 80 ] && break ;;
      *) printf '%s' "$OUT"; break ;;
    esac
  done
  rm -f "$OUTF"
elif [ "$1" = "sync" ]; then
  ( stream_transcript ) >/dev/null 2>&1 &
  # Replying to a quote from Claude's message: the app attaches the quoted bit as a file. Send its
  # words so the screen can show what you replied to (the relay gets them before your prompt).
  printf '%s' "$BODY" | send_quotes
  post "" 4 || true
else
  Q=""
  [ "$1" = "live" ] && Q="?live=1"
  post "$Q" 5 >/dev/null || true
  stream_transcript >/dev/null 2>&1
fi
exit 0
