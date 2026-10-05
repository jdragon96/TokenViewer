#!/usr/bin/env bash
# Installs, updates or removes TokenViewer.app (spec 5.2).
#   curl -fsSL https://github.com/<owner>/TokenViewer/releases/latest/download/install.sh | bash
#   ./install.sh --from-source     build this checkout and install it
#   ./install.sh --uninstall       remove the app, its login item, settings and logs
# Environment: TV_REPO=owner/name, TV_RELEASE_BASE=<url> (defaults to the latest GitHub release).
set -euo pipefail

BUNDLE_ID="com.tokenviewer.TokenViewer"
APP_NAME="TokenViewer.app"
ASSET="TokenViewer-macos.zip"
# The release workflow rewrites this line with the GitHub repository (owner/name).
RELEASE_REPO=""
WORK_DIR=""

say() { printf '==> %s\n' "$*"; }
fail() { printf 'error: %s\n' "$*" >&2; exit 1; }
cleanup() { [[ -n "$WORK_DIR" ]] && rm -rf "$WORK_DIR"; return 0; }
trap cleanup EXIT

usage() {
    sed -n '2,6p' "${BASH_SOURCE[0]:-/dev/null}" 2>/dev/null | sed 's/^# \{0,1\}//'
}

# Empty when the script is piped into bash.
script_dir() {
    local source="${BASH_SOURCE[0]:-}"
    if [[ -n "$source" && -f "$source" ]]; then
        (cd "$(dirname "$source")" && pwd)
    fi
}

resolve_repo() {
    if [[ -n "${TV_REPO:-}" ]]; then
        echo "$TV_REPO"
    elif [[ -n "$RELEASE_REPO" ]]; then
        echo "$RELEASE_REPO"
    else
        local dir
        dir="$(script_dir)"
        if [[ -n "$dir" ]]; then
            git -C "$dir" remote get-url origin 2>/dev/null | sed -E 's#^(git@github\.com:|https://github\.com/)##; s#\.git$##' || true
        fi
    fi
}

install_dir() {
    if [[ -w /Applications ]]; then
        echo /Applications
    else
        mkdir -p "$HOME/Applications"
        echo "$HOME/Applications"
    fi
}

quit_app() {
    pgrep -x TokenViewer >/dev/null || return 0
    osascript -e "tell application id \"$BUNDLE_ID\" to quit" >/dev/null 2>&1 || true
    for _ in $(seq 1 50); do
        pgrep -x TokenViewer >/dev/null || return 0
        sleep 0.1
    done
    pkill -x TokenViewer || true
}

place_app() {
    local source_app="$1" destination
    destination="$(install_dir)/$APP_NAME"
    quit_app
    rm -rf "$destination"
    ditto "$source_app" "$destination"
    say "설치했습니다: $destination"
    open "$destination"
}

install_release() {
    local base="${TV_RELEASE_BASE:-}"
    if [[ -z "$base" ]]; then
        local repo
        repo="$(resolve_repo)"
        [[ "$repo" == */* ]] || fail "GitHub 저장소를 알 수 없습니다. TV_REPO=owner/TokenViewer 로 지정하세요."
        base="https://github.com/$repo/releases/latest/download"
    fi
    WORK_DIR="$(mktemp -d)"
    say "내려받는 중: $base/$ASSET"
    curl -fsSL -o "$WORK_DIR/$ASSET" "$base/$ASSET"
    curl -fsSL -o "$WORK_DIR/$ASSET.sha256" "$base/$ASSET.sha256"
    local expected actual
    expected="$(awk '{print $1}' "$WORK_DIR/$ASSET.sha256")"
    actual="$(shasum -a 256 "$WORK_DIR/$ASSET" | awk '{print $1}')"
    [[ -n "$expected" && "$expected" == "$actual" ]] || fail "SHA-256 이 맞지 않습니다."
    ditto -x -k "$WORK_DIR/$ASSET" "$WORK_DIR/unzipped"
    place_app "$WORK_DIR/unzipped/$APP_NAME"
}

install_from_source() {
    local dir
    dir="$(script_dir)"
    [[ -n "$dir" && -x "$dir/build-app.sh" ]] || fail "--from-source 는 저장소 안의 install.sh 로 실행하세요."
    "$dir/build-app.sh"
    place_app "$dir/../dist/$APP_NAME"
}

uninstall() {
    local found=0 dir app
    for dir in /Applications "$HOME/Applications"; do
        app="$dir/$APP_NAME"
        [[ -d "$app" ]] || continue
        found=1
        quit_app
        "$app/Contents/MacOS/TokenViewer" --unregister-login-item || true
        rm -rf "$app"
        say "지웠습니다: $app"
    done
    defaults delete "$BUNDLE_ID" >/dev/null 2>&1 || true
    rm -rf "$HOME/Library/Logs/TokenViewer"
    if [[ $found == 0 ]]; then
        say "설치된 앱이 없습니다. 설정과 로그만 지웠습니다."
    fi
}

case "${1:-}" in
    "") install_release ;;
    --from-source) install_from_source ;;
    --uninstall) uninstall ;;
    -h|--help) usage ;;
    *) usage; exit 1 ;;
esac
