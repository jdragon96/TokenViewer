# TokenViewer

Claude Code 요금제 한도(5시간·주간)를 메뉴바(macOS)·알림 영역(Windows)에 늘 보여 주는 앱입니다.
클릭하면 리셋 시각과, 로컬 세션 로그로 계산한 프로젝트·모델·세션별 API 환산 비용을 보여 줍니다.

| 플랫폼 | 상태 | 위치 |
| --- | --- | --- |
| macOS 14 이상 (Apple Silicon, Intel) | 사용 가능. Swift 네이티브 앱, 약 1.4MB | `macos/` |
| Windows 10 (1903 이상) · 11, x64 | 사용 가능. Qt 5.15 앱, zip 약 12MB | `qt/` |
| Ubuntu | 준비 중 (Qt) | `qt/` |

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

## Windows

### 설치

PowerShell 에서:

```powershell
irm https://github.com/jdragon96/TokenViewer/releases/latest/download/install.ps1 | iex
```

최신 릴리스의 zip 을 내려받아 SHA-256 을 확인하고 `%LOCALAPPDATA%\Programs\TokenViewer` 에 설치한 뒤, 시작 메뉴에 바로가기를 만들고 실행합니다.
관리자 권한은 쓰지 않습니다. 같은 명령으로 업데이트합니다 (실행 중인 앱은 먼저 끕니다).

브라우저로 받기 (zip):

1. [최신 릴리스](https://github.com/jdragon96/TokenViewer/releases/latest)에서 `TokenViewer-windows.zip` 을 받아 압축을 풉니다. 설치 프로그램은 없고, 풀린 `TokenViewer` 폴더째 원하는 곳(예: `%LOCALAPPDATA%\Programs`)에 두면 됩니다.
2. 폴더 안의 `TokenViewer.exe` 를 실행합니다. 처음 한 번은 SmartScreen 이 막습니다 (아래 "처음 실행할 때").

소스에서 설치 (아래 "개발"의 Qt, MinGW, CMake, Ninja 필요):

```powershell
git clone https://github.com/jdragon96/TokenViewer.git
powershell -ExecutionPolicy Bypass -File TokenViewer\qt\scripts\install.ps1 -FromSource
```

저장소의 `.ps1` 파일은 Windows 기본 실행 정책 때문에 `-ExecutionPolicy Bypass` 로 실행합니다 (`irm | iex` 는 상관없습니다).
`-NoLaunch` 를 주면 설치만 하고 실행하지 않습니다. 환경 변수 `TV_INSTALL_DIR` 로 설치 위치를 바꿀 수 있습니다.

제거 (앱, 시작 메뉴 바로가기, 로그인 시 자동 실행, 설정, 로그):

```powershell
& ([scriptblock]::Create((irm https://github.com/jdragon96/TokenViewer/releases/latest/download/install.ps1))) -Uninstall
```

zip 으로 쓰던 앱은 설정(⚙)에서 "로그인 시 자동 실행"을 끄고 드롭다운의 "종료"를 누른 뒤 폴더를 지우면 됩니다.
그 뒤 위 제거 명령을 실행하면 남은 설정과 로그도 지웁니다.

### 처음 실행할 때

- Windows 11 은 새 트레이 아이콘을 작업 표시줄 오른쪽 `^` (숨겨진 아이콘 표시) 안에 숨깁니다.
  `^` 를 눌러 TokenViewer 아이콘을 작업 표시줄로 끌어다 놓거나, 설정 → 개인 설정 → 작업 표시줄 → 기타 시스템 트레이 아이콘에서 TokenViewer 를 켜세요.
- 아이콘을 왼쪽이나 오른쪽 클릭하면 드롭다운이 열리고 닫힙니다. 아이콘이 숨어 있을 때는 시작 메뉴에서 TokenViewer 를 다시 실행해도 드롭다운이 열립니다.
- 서명하지 않은 앱입니다. PowerShell 로 설치하면 SmartScreen 경고 없이 실행됩니다.
  브라우저로 받은 zip 은 처음 실행할 때 "Windows의 PC 보호" 창이 뜹니다. **"추가 정보" → "실행"** 을 누르면 그 뒤로는 묻지 않습니다.
- **스마트 앱 컨트롤**이 켜진 PC 에서는 평판 없는 서명 안 된 앱이 "실행" 선택지 없이 막힐 수 있습니다.
  Authenticode 서명이 없어서 생기는 일이고, 스마트 앱 컨트롤 설정(Windows 보안 → 앱 및 브라우저 컨트롤)에서만 풀 수 있습니다.
- 로그인 시 자동 실행이 기본으로 켜져 있습니다. 설정(⚙)에서 끌 수 있고, 작업 관리자의 "시작 앱"에도 보입니다.

### 앱이 읽는 것과 보내는 곳

- 읽기: `%USERPROFILE%\.claude\.credentials.json` (환경 변수 `CLAUDE_CONFIG_DIR` 가 있으면 그 폴더의 `.credentials.json`) 의 accessToken·만료 시각·요금제. refreshToken 은 읽지 않습니다.
- 읽기: 같은 폴더의 `projects` 에 있는 세션 로그 (토큰 수, 모델, 프로젝트 경로, 세션 제목).
- 보내기: `https://api.anthropic.com/api/oauth/usage` 한 곳. Windows 에 들어 있는 WinHTTP 로 보내며 시스템 프록시 설정을 따릅니다. 토큰은 이 요청에만 쓰고 저장하지 않습니다.
- 쓰기: 설정(레지스트리 `HKCU\Software\TokenViewer`), 로그인 시 자동 실행(`HKCU\Software\Microsoft\Windows\CurrentVersion\Run` 의 값 `TokenViewer`),
  한 번만 실행하기 위한 잠금 파일(`%LOCALAPPDATA%\TokenViewer\TokenViewer\`), API 응답 형식이 바뀌었을 때의 응답 본문(`%LOCALAPPDATA%\TokenViewer\TokenViewer\logs\`).

### 개발

Qt 5.15.2 MinGW 64비트 (`C:\Qt\5.15.2\mingw81_64`), MinGW 8.1 (`C:\Qt\Tools\mingw810_64`), CMake 3.22 이상, Ninja 가 필요합니다.
Qt 온라인 설치 프로그램에서 Qt 5.15.2 → MinGW 8.1.0 64-bit 와 Developer and Designer Tools → MinGW 8.1.0 64-bit 를 고르면 이 위치에 설치됩니다.

```powershell
$env:PATH = "C:\Qt\Tools\mingw810_64\bin;C:\Qt\5.15.2\mingw81_64\bin;$env:PATH"
cmake -S qt -B build-win -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH=C:/Qt/5.15.2/mingw81_64
cmake --build build-win                                 # 앱과 테스트, 앱 아이콘(.ico)을 빌드합니다
ctest --test-dir build-win --output-on-failure          # 단위 테스트
powershell -ExecutionPolicy Bypass -File qt\scripts\package-windows.ps1   # Release 빌드 + 테스트 + zip + sha256 (qt\dist)
```

`package-windows.ps1` 은 `-SkipTests` 로 테스트를 건너뛰고, `TV_VERSION` 이 없으면 최신 git 태그를 버전으로 씁니다.
스마트 앱 컨트롤이 켜진 PC 에서는 새로 링크한 테스트 실행 파일이나 빌드 중에 도는 아이콘 도구(`MakeIcon.exe`)가 가끔 막힙니다. 그 파일을 지우고 다시 빌드하면 풀립니다.
`v*` 태그를 올리면 GitHub Actions 가 릴리스에 `TokenViewer-windows.zip`, `.sha256`, `install.ps1` 을 올립니다.

설계: `docs/superpowers/specs/2026-10-23-windows-qt-app-design.md`

## Ubuntu (준비 중)

Qt 코드(`qt/`)를 Ubuntu 에서도 쓸 계획이지만 아직 빌드·확인하지 않았습니다.

설계: `docs/superpowers/specs/2026-10-05-token-viewer-design.md`

## 라이선스

MIT. Windows 앱은 Qt (LGPLv3) 를 DLL 로 동적 링크하고 MinGW-w64 런타임 DLL 을 함께 넣습니다.
고지는 `qt/resources/THIRD_PARTY_NOTICES.md` (앱의 설정 → "오픈소스 라이선스", 설치 폴더의 `THIRD_PARTY_NOTICES.md`), 라이선스 전문은 설치 폴더의 `licenses\` 에 있습니다.
