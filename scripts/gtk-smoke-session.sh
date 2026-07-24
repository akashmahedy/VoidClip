#!/usr/bin/env bash
set -euo pipefail

app="${1:?missing app}"
test_root="${2:?missing test root}"
log="${test_root}/voidclip.log"
database="${test_root}/data/voidclip/history.db"

"${app}" >"${log}" 2>&1 &
app_pid=$!
cleanup() {
  kill "${app_pid}" 2>/dev/null || true
  wait "${app_pid}" 2>/dev/null || true
}
trap cleanup EXIT

window=""
for _ in $(seq 1 50); do
  window="$(xdotool search --name '^VoidClip$' 2>/dev/null | head -1 || true)"
  [ -n "${window}" ] && break
  sleep 0.1
done
[ -n "${window}" ] || {
  echo "FAIL: VoidClip window did not appear"
  cat "${log}"
  exit 1
}
for _ in $(seq 1 50); do
  xwininfo -id "${window}" 2>/dev/null | grep -q 'Map State: IsViewable' && break
  sleep 0.1
done
xwininfo -id "${window}" | grep -q 'Map State: IsViewable' || {
  echo "FAIL: VoidClip window was created but never became visible"
  cat "${log}"
  exit 1
}
xdotool windowfocus --sync "${window}"
sleep 0.1

printf 'packaged GTK clipboard smoke' | xclip -selection clipboard
for _ in $(seq 1 50); do
  if [ -f "${database}" ] &&
    [ "$(sqlite3 "${database}" "SELECT COUNT(*) FROM entries WHERE content = 'packaged GTK clipboard smoke';")" = 1 ]; then
    break
  fi
  sleep 0.1
done
[ "$(sqlite3 "${database}" "SELECT COUNT(*) FROM entries;")" = 1 ] ||
  { echo "FAIL: clipboard change was not stored"; exit 1; }

# SearchEntry has focus when the window opens. Plain Delete edits search text and
# must never remove the selected history item.
xdotool key --clearmodifiers Delete
sleep 0.2
[ "$(sqlite3 "${database}" "SELECT COUNT(*) FROM entries;")" = 1 ] ||
  { echo "FAIL: plain Delete removed a clip"; exit 1; }

xdotool key --clearmodifiers alt+Delete
for _ in $(seq 1 30); do
  [ "$(sqlite3 "${database}" "SELECT COUNT(*) FROM entries;")" = 0 ] && break
  sleep 0.1
done
[ "$(sqlite3 "${database}" "SELECT COUNT(*) FROM entries;")" = 0 ] ||
  { echo "FAIL: Alt+Delete did not remove the selected clip"; exit 1; }
