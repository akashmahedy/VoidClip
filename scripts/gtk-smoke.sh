#!/usr/bin/env bash
# Launch the shipped GTK binary under a real virtual X11 + D-Bus session. The
# process must remain alive until timeout; an early exit catches missing runtime
# libraries, broken resources, invalid app IDs, and startup crashes.
set -euo pipefail

app="${1:?usage: gtk-smoke.sh <installed-voidclip-binary>}"
test_root="$(mktemp -d /tmp/voidclip-gtk-smoke.XXXXXX)"
cleanup() { rm -rf "${test_root}"; }
trap cleanup EXIT

mkdir -p "${test_root}/data/voidclip" "${test_root}/config"
cat >"${test_root}/data/voidclip/settings.json" <<'EOF'
{
  "first_run_completed": true,
  "show_panel_icon": false
}
EOF

set +e
timeout 5s dbus-run-session -- xvfb-run -a env \
  XDG_DATA_HOME="${test_root}/data" \
  XDG_CONFIG_HOME="${test_root}/config" \
  VOIDCLIP_STANDALONE=1 \
  GDK_BACKEND=x11 \
  "${app}" --background >"${test_root}/voidclip.log" 2>&1
status=$?
set -e

if [ "${status}" -ne 124 ]; then
  echo "FAIL: packaged GTK app exited before the smoke window completed"
  cat "${test_root}/voidclip.log"
  exit 1
fi
if grep -Eiq 'fatal|segmentation fault|symbol lookup error|error while loading shared libraries' \
  "${test_root}/voidclip.log"; then
  echo "FAIL: packaged GTK app logged a fatal startup error"
  cat "${test_root}/voidclip.log"
  exit 1
fi

echo "smoke: packaged GTK app stayed healthy under Xvfb + D-Bus"
