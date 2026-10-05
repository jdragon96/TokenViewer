#!/usr/bin/env bash
# Points the Homebrew cask in jdragon96/homebrew-tap at a published release.
#   ./update-cask.sh 0.1.3            commit and push the new version and checksum (needs `gh auth login`)
#   TV_DRY_RUN=1 ./update-cask.sh 0.1.3   show the change without pushing
# Environment: TV_REPO (default jdragon96/TokenViewer), TV_TAP (default jdragon96/homebrew-tap).
set -euo pipefail

VERSION="${1:-}"
VERSION="${VERSION#v}"
REPO="${TV_REPO:-jdragon96/TokenViewer}"
TAP="${TV_TAP:-jdragon96/homebrew-tap}"
[[ -n "$VERSION" ]] || { echo "usage: $0 <version>" >&2; exit 2; }

SHA="$(curl -fsSL "https://github.com/$REPO/releases/download/v$VERSION/TokenViewer-macos.zip.sha256" 2>/dev/null | awk '{print $1}' || true)"
[[ ${#SHA} == 64 ]] || { echo "error: release v$VERSION has no TokenViewer-macos.zip.sha256" >&2; exit 1; }

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
git clone -q "https://github.com/$TAP.git" "$WORK/tap"
CASK="$WORK/tap/Casks/tokenviewer.rb"
sed -i '' -E "s/^  version \".*\"/  version \"$VERSION\"/; s/^  sha256 \".*\"/  sha256 \"$SHA\"/" "$CASK"

if git -C "$WORK/tap" diff --quiet; then
    echo "tokenviewer cask is already at $VERSION"
    exit 0
fi
if [[ -n "${TV_DRY_RUN:-}" ]]; then
    git -C "$WORK/tap" --no-pager diff
    exit 0
fi
git -C "$WORK/tap" commit -qam "tokenviewer $VERSION"
git -C "$WORK/tap" push -q
echo "tokenviewer cask now points at $VERSION"
