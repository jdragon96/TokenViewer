# TokenViewer

Claude Code 요금제 한도(5시간·주간)를 메뉴바에 늘 보여 주는 앱입니다.
클릭하면 리셋 시각과, 로컬 세션 로그로 계산한 프로젝트·모델·세션별 API 환산 비용을 보여 줍니다.

| 플랫폼 | 상태 | 위치 |
| --- | --- | --- |
| macOS 14 이상 (Apple Silicon, Intel) | 사용 가능. Swift 네이티브 앱, 약 1.4MB | `macos/` |
| Windows, Ubuntu | 준비 중 (Qt) | 저장소 최상위 |

## macOS

### 설치

```bash
curl -fsSL https://github.com/jdragon96/TokenViewer/releases/latest/download/install.sh | bash
```

최신 릴리스를 내려받아 SHA-256 을 확인하고 `/Applications` (쓸 수 없으면 `~/Applications`) 에 설치한 뒤 실행합니다.
sudo 는 쓰지 않습니다. 같은 명령으로 업데이트합니다.

Homebrew:

```bash
brew install --cask jdragon96/tap/tokenviewer
```

업데이트는 `brew upgrade --cask tokenviewer`, 제거는 `brew uninstall --cask tokenviewer` (설정과 로그까지는 `--zap`).
Homebrew 로 받은 앱도 DMG 처럼 처음 한 번 "그래도 열기"가 필요합니다 (아래 "처음 실행할 때").

브라우저로 받기 (DMG):

1. [최신 릴리스](https://github.com/jdragon96/TokenViewer/releases/latest)에서 `TokenViewer-macos.dmg` 를 받아 엽니다.
2. TokenViewer 를 옆의 Applications 폴더로 끌어다 놓습니다.
3. 응용 프로그램 폴더에서 TokenViewer 를 엽니다. 처음 한 번은 macOS 가 막습니다 (아래 "처음 실행할 때").

소스에서 설치 (Xcode 16 이상 필요):

```bash
git clone https://github.com/jdragon96/TokenViewer.git
TokenViewer/macos/scripts/install.sh --from-source
```

제거: 메뉴바 드롭다운에서 "종료"를 누른 뒤, 응용 프로그램 폴더의 TokenViewer 를 휴지통으로 끌어다 놓으면 됩니다.
설정과 로그까지 지우려면 (앱, 로그인 항목, 설정, 로그):

```bash
curl -fsSL https://github.com/jdragon96/TokenViewer/releases/latest/download/install.sh | bash -s -- --uninstall
```

### 처음 실행할 때

- macOS 가 Keychain 의 `Claude Code-credentials` 접근을 묻습니다. "항상 허용"을 누르세요.
- 릴리스 앱은 Apple Developer ID 가 아닌 ad-hoc 서명입니다. 그래서 **업데이트할 때마다 Keychain 허용 창이 한 번 더 뜹니다.**
- curl 로 설치하면 Gatekeeper 경고 없이 실행됩니다. DMG 나 Homebrew 로 설치하면 처음 열 때 "Apple 이 악성 코드가 없음을 확인할 수 없습니다"가 뜹니다.
  시스템 설정 → 개인정보 보호 및 보안 → 아래쪽의 **"그래도 열기"** 를 누르고 한 번 더 열면, 그 뒤로는 묻지 않습니다.
  터미널에 익숙하다면 `xattr -dr com.apple.quarantine /Applications/TokenViewer.app` 로도 됩니다.
- 로그인 시 자동 실행이 기본으로 켜져 있습니다. 설정(⚙)이나 시스템 설정 → 일반 → 로그인 항목에서 끌 수 있습니다.

### 앱이 읽는 것과 보내는 곳

- 읽기: Keychain `Claude Code-credentials` (없으면 `~/.claude/.credentials.json`) 의 accessToken·만료 시각·요금제. refreshToken 은 읽지 않습니다.
- 읽기: `~/.claude/projects` 의 세션 로그 (토큰 수, 모델, 프로젝트 경로, 세션 제목).
- 보내기: `https://api.anthropic.com/api/oauth/usage` 한 곳. 토큰은 이 요청에만 쓰고 저장하지 않습니다.
- 쓰기: 설정(`~/Library/Preferences/com.tokenviewer.TokenViewer.plist`), API 응답 형식이 바뀌었을 때의 응답 본문(`~/Library/Logs/TokenViewer/`).

사용량 API 는 공식 문서에 없는 API 라서 바뀔 수 있습니다. 바뀌면 드롭다운에 안내가 뜨고, 로그 기반 목록은 계속 동작합니다.

### 개발

```bash
cd macos
swift test                       # 단위 테스트
scripts/build-app.sh --debug     # dist/TokenViewer.app (빠른 디버그 빌드)
scripts/build-app.sh             # universal 릴리스 빌드 + zip + dmg + sha256
```

`TV_SIGN_IDENTITY="Developer ID Application: …"` 와 `TV_NOTARY_PROFILE=<notarytool 프로필>` 을 주면 서명·공증까지 합니다.
`v*` 태그를 올리면 GitHub Actions 가 릴리스에 zip, dmg, sha256, install.sh 를 올립니다.
릴리스가 올라간 뒤 `scripts/update-cask.sh <버전>` 으로 Homebrew cask ([jdragon96/homebrew-tap](https://github.com/jdragon96/homebrew-tap)) 를 갱신합니다.

설계: `docs/superpowers/specs/2026-10-05-macos-swift-app-design.md`

## Windows, Ubuntu (Qt, 준비 중)

Qt 5.15 + CMake 코드는 저장소 최상위에 있습니다. 지금은 macOS 에서 개발용으로만 빌드됩니다.

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DQt5_DIR=<Qt5 설치 경로>/lib/cmake/Qt5
cmake --build build
ctest --test-dir build --output-on-failure
```

설계: `docs/superpowers/specs/2026-10-05-token-viewer-design.md`

## 라이선스

MIT. Qt 빌드는 Qt (LGPLv3) 를 동적으로 링크합니다 (`resources/THIRD_PARTY_NOTICES.md`).
