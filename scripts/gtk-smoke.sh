#!/usr/bin/env bash
# Exercise the installed GTK application under a virtual X11 + D-Bus session.
# This checks more than process liveness: the real window must appear, an X11
# clipboard change must reach SQLite, plain Delete must be harmless in search,
# and Alt+Delete must invoke the deliberate remove action.
set -euo pipefail

app="${1:?usage: gtk-smoke.sh <installed-voidclip-binary>}"
test_root="$(mktemp -d /tmp/voidclip-gtk-smoke.XXXXXX)"
cleanup() { rm -rf "${test_root}"; }
trap cleanup EXIT

mkdir -p "${test_root}/data/voidclip" "${test_root}/config"
cat >"${test_root}/data/voidclip/settings.json" <<'EOF'
{
  "first_run_completed": true,
  "show_panel_icon": false,
  "start_at_login": false
}
EOF

timeout 20s dbus-run-session -- xvfb-run -a env \
  XDG_DATA_HOME="${test_root}/data" \
  XDG_CONFIG_HOME="${test_root}/config" \
  VOIDCLIP_STANDALONE=1 \
  GDK_BACKEND=x11 \
  bash scripts/gtk-smoke-session.sh "${app}" "${test_root}"

if grep -Eiq 'fatal|segmentation fault|symbol lookup error|error while loading shared libraries' \
  "${test_root}/voidclip.log"; then
  echo "FAIL: packaged GTK app logged a fatal error"
  cat "${test_root}/voidclip.log"
  exit 1
fi

echo "smoke: window, clipboard capture, safe Delete, and deliberate Alt+Delete passed"
