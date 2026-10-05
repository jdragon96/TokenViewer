#!/usr/bin/env bash
# Builds TokenViewer.app into macos/dist (spec 5.1).
#   ./build-app.sh            universal release build, signed, zipped and packed into a drag-to-install DMG
#   ./build-app.sh --debug    host-arch debug build for quick manual checks
# Environment: TV_VERSION, TV_REPO (owner/name), TV_SIGN_IDENTITY (default ad-hoc), TV_NOTARY_PROFILE.
set -euo pipefail

MACOS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
REPO_ROOT="$(cd "$MACOS_DIR/.." && pwd)"
DIST="$MACOS_DIR/dist"
APP="$DIST/TokenViewer.app"
ZIP="$DIST/TokenViewer-macos.zip"
DMG="$DIST/TokenViewer-macos.dmg"
SIZE_LIMIT_KB=3072

BUILD_FLAGS=(-c release --arch arm64 --arch x86_64 -Xswiftc -Osize)
IS_RELEASE=1
if [[ "${1:-}" == "--debug" ]]; then
    BUILD_FLAGS=(-c debug)
    IS_RELEASE=0
fi

VERSION="${TV_VERSION:-$(git -C "$REPO_ROOT" describe --tags --abbrev=0 2>/dev/null || true)}"
VERSION="${VERSION#v}"
VERSION="${VERSION:-0.0.0-dev}"
REPO="${TV_REPO:-$(git -C "$REPO_ROOT" remote get-url origin 2>/dev/null | sed -E 's#^(git@github\.com:|https://github\.com/)##; s#\.git$##' || true)}"

cd "$MACOS_DIR"
swift build "${BUILD_FLAGS[@]}"
BIN_DIR="$(swift build "${BUILD_FLAGS[@]}" --show-bin-path)"

rm -rf "$APP" "$ZIP" "$ZIP.sha256" "$DMG" "$DMG.sha256"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
cp "$BIN_DIR/TokenViewer" "$APP/Contents/MacOS/TokenViewer"
cp "$MACOS_DIR/Info.plist" "$APP/Contents/Info.plist"
cp "$REPO_ROOT/shared/prices.json" "$APP/Contents/Resources/prices.json"

# The app icon is drawn by a script, like the menu bar item, so the repository holds no image files.
ICON_WORK="$(mktemp -d)"
swift "$MACOS_DIR/scripts/make-icon.swift" "$ICON_WORK/AppIcon.iconset"
iconutil -c icns "$ICON_WORK/AppIcon.iconset" -o "$APP/Contents/Resources/AppIcon.icns"
rm -r "$ICON_WORK"

PLIST="$APP/Contents/Info.plist"
plutil -replace CFBundleShortVersionString -string "$VERSION" "$PLIST"
# CFBundleVersion only allows dot-separated numbers.
plutil -replace CFBundleVersion -string "${VERSION%%-*}" "$PLIST"
if [[ -n "$REPO" ]]; then
    plutil -replace TVRepository -string "$REPO" "$PLIST"
fi
plutil -lint "$PLIST" >/dev/null

if [[ $IS_RELEASE == 1 ]]; then
    strip -x "$APP/Contents/MacOS/TokenViewer"
fi

SIGN_IDENTITY="${TV_SIGN_IDENTITY:--}"
CODESIGN_ARGS=(--force --options runtime --sign "$SIGN_IDENTITY")
if [[ "$SIGN_IDENTITY" != "-" ]]; then
    CODESIGN_ARGS+=(--timestamp)
fi
codesign "${CODESIGN_ARGS[@]}" "$APP"

if [[ -n "${TV_NOTARY_PROFILE:-}" ]]; then
    ditto -c -k --keepParent "$APP" "$DIST/notarize.zip"
    xcrun notarytool submit "$DIST/notarize.zip" --keychain-profile "$TV_NOTARY_PROFILE" --wait
    xcrun stapler staple "$APP"
    rm -f "$DIST/notarize.zip"
fi

ditto -c -k --keepParent "$APP" "$ZIP"
(cd "$DIST" && shasum -a 256 "$(basename "$ZIP")" > "$(basename "$ZIP").sha256")

# The DMG is for browser downloads: the app next to an Applications shortcut, ready to drag over.
if [[ $IS_RELEASE == 1 ]]; then
    DMG_ROOT="$(mktemp -d)"
    ditto "$APP" "$DMG_ROOT/TokenViewer.app"
    ln -s /Applications "$DMG_ROOT/Applications"
    hdiutil create -volname TokenViewer -srcfolder "$DMG_ROOT" -fs HFS+ -format UDZO -ov -quiet "$DMG"
    rm -r "$DMG_ROOT"
    if [[ "$SIGN_IDENTITY" != "-" ]]; then
        codesign --force --sign "$SIGN_IDENTITY" --timestamp "$DMG"
    fi
    if [[ -n "${TV_NOTARY_PROFILE:-}" ]]; then
        xcrun notarytool submit "$DMG" --keychain-profile "$TV_NOTARY_PROFILE" --wait
        xcrun stapler staple "$DMG"
    fi
    (cd "$DIST" && shasum -a 256 "$(basename "$DMG")" > "$(basename "$DMG").sha256")
fi

SIZE_KB="$(du -sk "$APP" | awk '{print $1}')"
echo "TokenViewer.app $VERSION: ${SIZE_KB} KB [$(lipo -archs "$APP/Contents/MacOS/TokenViewer")]"
if [[ $IS_RELEASE == 1 ]]; then
    echo "TokenViewer-macos.dmg: $(( $(stat -f%z "$DMG") / 1024 )) KB"
fi
if [[ $IS_RELEASE == 1 && $SIZE_KB -gt $SIZE_LIMIT_KB ]]; then
    echo "warning: TokenViewer.app is larger than 3 MB" >&2
fi
