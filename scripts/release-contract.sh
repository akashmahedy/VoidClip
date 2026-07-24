#!/usr/bin/env bash
# Static regression checks for the public installer/release contract.
set -euo pipefail

grep -Fq 'REPO="akashmahedy/VoidClip"' scripts/install.sh
grep -Fq 'APP="voidclip"' scripts/install.sh
grep -Fq 'APP_ID="io.github.akashmahedy.VoidClip"' scripts/install.sh
grep -Fq 'sha256sum *.deb *.rpm *.tar.gz *.AppImage' .github/workflows/release.yml
grep -Fq 'packaging/io.github.akashmahedy.VoidClip.desktop' \
  .github/workflows/release.yml

if grep -Fq 'raw.githubusercontent.com/Walkercito/CopyClip' README.md scripts/install.sh ||
  grep -RqF 'packaging/dev.walkercito.CopyClip' .github; then
  echo "FAIL: public install/release files still target upstream CopyClip" >&2
  exit 1
fi

echo "release contract: repository, identity, and checksum coverage OK"
