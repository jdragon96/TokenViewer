# Claude Code Token Visualizer

- Check the remaining token usage

## How to use?

### MacOS

요구: macOS 11+, Qt 5.15 (개발 PC 는 Anaconda Qt 5.15.2), CMake 3.21+, Ninja.

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DQt5_DIR=/opt/homebrew/anaconda3/lib/cmake/Qt5
cmake --build build
ctest --test-dir build --output-on-failure
open build/TokenViewer.app
```

네이티브 메뉴바 아이템이 기본값입니다 (`-DTV_NATIVE_TRAY=ON`). IDE(clangd)는 `build/compile_commands.json` 을 찾습니다. 예: `ln -s build/compile_commands.json .` (루트 심볼릭 링크는 이미 gitignore 에 있습니다).

macOS 메뉴바에서 Claude Code 요금제 한도(5시간·주간)를 보여 주고, 클릭하면 프로젝트·모델·세션별 API 환산 비용을 보여 줍니다.

- 설계: `docs/superpowers/specs/2026-10-05-token-viewer-design.md`
- 한도는 Claude Code 로그인 정보(Keychain)로 비공식 사용량 API 를 불러 옵니다. 토큰은 읽기만 하고 저장하지 않습니다.
- 비용은 `~/.claude/projects` 의 세션 로그로 계산합니다.

#### 처음 실행할 때

- macOS 가 Keychain 의 `Claude Code-credentials` 접근 허용을 묻습니다. "항상 허용"을 누르세요. 다시 빌드하면 서명이 바뀌어 한 번 더 물을 수 있습니다.
- 설정의 "로그인 시 자동 실행"은 `~/Library/LaunchAgents/com.tokenviewer.TokenViewer.plist` 를 만듭니다.

#### 배포

배포 패키지는 아직 없습니다. 만들 때는 `docs/LicenseCompliance.md` (공식 Qt 빌드, `macdeployqt`, 고지)를 따릅니다.

### Windows

```bash

```

### Ubuntu

```bash

```
