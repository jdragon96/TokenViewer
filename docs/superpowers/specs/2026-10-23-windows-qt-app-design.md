# TokenViewer Windows (Qt) 설계

- 작성일: 2026-10-23
- 상태: 구현 완료 (CI 와 실제 설치는 아직 확인 전)
- 대상 플랫폼: Windows 10 (1903 이상) · Windows 11, x64. Qt 5.15.2 (LGPLv3) + MinGW 8.1, C++17
- 기준 문서: `2026-10-05-token-viewer-design.md` (Qt v0.1.0, 이하 "v0.1.0 스펙"), 아이콘은 `2026-10-05-macos-swift-app-design.md` 2.1

## 1. 목적과 범위

v0.1.0 Qt 앱을 Windows 작업 표시줄 알림 영역(트레이) 앱으로 옮긴다. macOS 는 Swift 앱이 맡으므로 Qt 코드에서 macOS 전용 부분은 뺀다.

**성공 기준**

1. v0.1.0 스펙의 성공 기준 1–4 를 그대로 지킨다 (트레이 숫자가 `/usage` 의 5시간 % 와 같음, Claude Code 가 꺼져도 갱신, 한도 실패해도 로그 목록, 토큰은 읽기만).
2. 관리자 권한 없이 설치·업데이트·제거가 된다 (PowerShell 한 줄).
3. 사용자가 따로 설치할 것이 없다. OpenSSL 등 Qt 밖의 DLL 을 넣지 않는다 (MinGW 런타임 제외).
4. 이 PC 에서 빌드·테스트·패키징이 된다 (Qt 5.15.2 `mingw81_64`, CMake, Ninja).

**범위 밖**

- Ubuntu. 코드는 Qt 로 두되 이번에는 Windows 만 빌드·확인한다.
- Authenticode 서명, winget/Scoop, MSI 설치 관리자, 자동 업데이트, ARM64.
- 알림(토스트). v0.1.0 과 같이 아이콘 색으로 대신한다.

## 2. 동작

v0.1.0 스펙 2절(화면), 3절(데이터 소스), 5절(데이터 규칙), 6절(갱신 정책), 7절(오류 처리)을 **그대로 따른다.** 다른 점만 적는다.

| 항목 | v0.1.0 (macOS Qt) | Windows |
| --- | --- | --- |
| 트레이 아이콘 | `NSStatusItem`, 막대 2줄 + 5시간 % (가로로 긴 그림) | `QSystemTrayIcon`, 정사각형. 둥근 상자 + 5시간 숫자 + 주간 테두리 (2.1) |
| 아이콘 색 | 평소엔 템플릿 이미지 | 템플릿이 없다. 작업 표시줄 밝기에 맞춘 색으로 직접 그린다 (2.1) |
| 툴팁 | 없음 | `Claude 사용량` 다음 줄에 `5시간 42% · 주간 18%` (값이 없으면 `한도 정보 없음`) |
| 클릭 | 왼쪽 클릭 | 왼쪽·오른쪽 클릭 모두 드롭다운을 연다/닫는다. 별도 우클릭 메뉴는 없다 |
| 드롭다운 위치 | 아이콘 아래 | 2.2 |
| 어디에 썼나 목록 | 상위 5개 + `기타 N개` 한 줄 (줄 수만큼 높이가 바뀜) | 항상 4칸 (2.9) |
| 화면 테마 | 시스템 팔레트 | Fusion 스타일 + Windows 앱 모드(밝게/어둡게)에 맞춘 팔레트 (2.3) |
| 로그인 정보 | Keychain → `~/.claude/.credentials.json` | `<Claude 설정 폴더>/.credentials.json` 만 (2.4). Keychain 거부 상태는 생기지 않는다 |
| 세션 로그 | `~/.claude/projects` | `<Claude 설정 폴더>/projects` (2.4) |
| HTTPS | `QNetworkAccessManager` | WinHTTP (2.5) |
| 로그인 시 자동 실행 | `~/Library/LaunchAgents/<id>.plist` | `HKCU\Software\Microsoft\Windows\CurrentVersion\Run` 의 값 `TokenViewer` (2.6) |
| 설정 저장 | `QSettings` (plist) | `QSettings` 기본값 = 레지스트리 `HKCU\Software\TokenViewer\TokenViewer`. 키는 같다 |
| 응답 원문 로그 | `~/Library/Logs/TokenViewer/` | `%LOCALAPPDATA%\TokenViewer\TokenViewer\logs\`. 드롭다운 안내 문구도 이 경로를 보여 준다 |
| 프로젝트 귀속 | `cwd` 문자열 그대로 비교 | 구분자(`\`, `/`)와 대소문자를 무시하고 비교 (2.7) |
| 실행 | 앱 번들, Dock 없음 | 콘솔 창 없는 실행 파일. 한 번만 실행된다 (2.8) |
| 고해상도 | Retina | `AA_EnableHighDpiScaling`, `AA_UseHighDpiPixmaps`, 배율 반올림 `PassThrough` (125%·150% 그대로) |
| 앱 아이콘 | 없음 | 빌드할 때 코드로 그린 `.ico` 를 실행 파일 리소스로 넣는다 (5.1) |

### 2.1. 트레이 아이콘

Swift 앱의 2026-10-18 디자인을 정사각형으로 옮긴다. Windows 트레이 아이콘은 16×16 (100%) – 32×32 (200%) 정사각형이라 v0.1.0 의 가로 막대 그림은 들어가지 않는다.

- 둥근 정사각형 테두리 = 주간 사용량. 9시 방향에서 시작해 반시계 방향(먼저 아래쪽)으로 채운다. 한 바퀴 = 주간 한도 소진.
  채우지 않은 부분은 글자색 30% 의 흐린 선.
- 가운데 숫자 = 5시간 사용량 % (반올림, `%` 없이). 값이 없으면 `—`. 굵은 시스템 글꼴, 상자 안에 들어가는 가장 큰 크기 (`100` 은 더 작게 보인다).
- 색: 숫자는 5시간, 테두리는 주간 값으로 각각 v0.1.0 2.1 의 주황·빨강 규칙(임계치·색 값)을 따른다. 평소 색은 작업 표시줄 글자색(어두우면 흰색, 밝으면 검정).
- 갱신 실패가 15분 넘게 이어지거나 값이 없으면 그림 전체를 50% 투명도로. 선마다가 아니라 다 그린 뒤 한 번에 흐리게 한다 (흐린 선이 진행 선 밑으로 비치지 않게).
- 비율 (한 변 `s` 픽셀): 선 두께 `s/8` 를 정수 픽셀로 반올림 (20px → 3px), 모서리 반지름 `s/4`, 테두리는 선 두께 절반만큼 안쪽에 그린다.
  숫자와 테두리 사이 여백은 24px 부터 `floor(s/16)`, 16·20px 은 0 (여백을 두면 획이 1px 로 가늘어진다).
- 숫자는 Segoe UI Bold (시스템 글꼴은 한국어 Windows 에서 맑은 고딕이 나온다), 힌팅을 켜고 실제로 칠해진 픽셀 기준으로 맞추고 가운데 둔다.
  0–99 는 크기마다 한 글자 크기를 쓴다 (값마다 크기가 바뀌지 않게). `100` 은 자간을 1px 줄인다.
- `QIcon` 에 16, 20, 24, 32, 40, 48, 64 픽셀을 각각 그려 넣는다. 크기마다 따로 그려야 작은 크기에서 숫자가 선명하다.
  화면 배율이 바뀌면 아이콘을 다시 넣는다 (Qt 는 아이콘을 넣을 때 크기를 고른다).
- 작업 표시줄 밝기: 레지스트리 `HKCU\Software\Microsoft\Windows\CurrentVersion\Themes\Personalize` 의 `SystemUsesLightTheme` (1 = 밝음). 값이 없으면 어두움 (1903 이전 기본값).
  앱의 5초 틱마다 다시 읽고, 바뀌었을 때만 다시 그린다.

### 2.2. 드롭다운 위치

- 가로: 아이콘 가운데에 맞추고, 아이콘이 있는 화면의 사용 가능 영역(작업 표시줄 제외) 안으로 여백 8px 를 두고 넣는다.
- 세로: 아이콘이 화면 아래쪽 절반에 있으면 아이콘 **위** (아래 가장자리 = 아이콘 위 − 4px), 위쪽 절반이면 아래 (v0.1.0 과 같다). 그 뒤 사용 가능 영역 안으로 넣는다.
- 위에 띄운 상태에서 내용이 바뀌어 높이가 달라지면 아래 가장자리를 그대로 두고 위로 늘어난다.
- 아이콘 위치를 모르면 (`QSystemTrayIcon::geometry()` 가 비어 있으면) 클릭으로 열 때는 마우스 위치를, 다시 실행으로 열 때 (2.8) 는 주 화면 사용 가능 영역의 오른쪽 아래 (알림 영역 기본 자리) 를 아이콘 자리로 쓴다.

### 2.3. 화면 테마

Windows 11 플라이아웃처럼 보이게 한다 (2026-10-24, 첫 실행에서 "지저분하다"는 의견을 받고 고침).

- 스타일은 항상 Fusion (밝게/어둡게를 팔레트만으로 바꾸기 위해서다. windowsvista 스타일은 팔레트를 무시한다).
- 레지스트리 같은 키의 `AppsUseLightTheme` (0 = 어둡게, 없으면 밝게). 팔레트는 둘 다 코드로 정한다 (Fusion 기본 팔레트는 베이지 회색이라 쓰지 않는다).

  | | 밝게 | 어둡게 |
  | --- | --- | --- |
  | 배경 | `#f9f9f9` | `#2c2c2c` |
  | 글자 / 보조 글자 | `#1a1a1a` / `#5f5f5f` | 흰색 / `#c8c8c8` |
  | 입력란 | 흰색 | `#383838` (더 어두우면 Fusion 체크박스 테두리가 안 보인다) |
  | 강조 | `#2a78d6` | `#3987e5` |

  구분선은 글자색 10%. 주황·빨강은 v0.1.0 과 같다.
- 글꼴: Qt 5 는 Windows 기본 GUI 글꼴을 앱 글꼴로 가져오는데, 한국어 Windows 에서는 굴림 9pt 라 라틴·한글 모두 굴림으로 거칠게 나온다.
  앱 전체를 Segoe UI + 맑은 고딕(한글 대체) 9pt (12px) 로 정한다. 본문 12px, 목록 줄 13px, 제목 14px semibold, 미터 % 18px semibold. 12px 밑으로는 쓰지 않는다.
- 드롭다운 창은 불투명하게 그린다 (반투명 창은 ClearType 이 꺼진다). Windows 11 은 DWM 에 둥근 모서리를 요청하고 테두리 색을 직접 정한다 (사용자의 강조색 테두리 설정에 물들지 않게).
  요청이 실패하면 (Windows 10) 네모 창에 1px 테두리. 폭은 300px 그대로, 높이는 보통 상태에서 100% 기준 371px 로 고정된다 (2.9).
- 컨트롤: 모서리 4px, Windows 11 의 hover/pressed 색. 탭은 트랙 위에 선택된 칸이 떠 있는 모양. 새로고침·설정 아이콘은 Segoe Fluent Icons (Windows 11) / Segoe MDL2 Assets (Windows 10), 둘 다 없으면 `↻` `⚙`.
  설정 창은 어두운 모드에서 어두운 제목 표시줄을 요청한다.
- 앱 시작 때 적용하고, 5초 틱에서 바뀌었으면 다시 적용한다.

### 2.4. Claude 설정 폴더

- 환경 변수 `CLAUDE_CONFIG_DIR` 가 있으면 그 폴더, 없으면 `%USERPROFILE%\.claude`.
- 로그인 정보는 그 안의 `.credentials.json` 만 읽는다 (Windows 의 Claude Code 는 Keychain 같은 저장소 없이 이 파일을 쓴다). 형식·만료·요금제 규칙은 v0.1.0 3.2 와 같다.
  파일이 없으면 "로그인 필요". refreshToken 은 읽지 않는다.

### 2.5. HTTPS: WinHTTP

공식 Qt 5.15.2 Windows 빌드의 `QNetworkAccessManager` 는 OpenSSL **1.1.1** DLL 이 있어야 HTTPS 를 한다. 1.1.1 은 2023-09 에 지원이 끝났고 고지도 늘어난다.
사용량 API 는 GET 한 번이라 Windows 내장 WinHTTP 로 부른다. TLS 는 OS 가 맡고, 시스템 프록시 설정(WinHTTP 자동 프록시)을 따른다.

- 헤더·타임아웃(10초)·HTTP 상태 해석은 v0.1.0 3.1, 7절과 같다 (`UsageApiClient::MapHttpResult` 를 그대로 쓴다).
- 타임아웃은 "응답 시간 초과", 이름 풀이·연결·TLS 실패는 각각 한국어 이유를 `NETWORK_ERROR` 의 상세로 넘긴다.
- Qt Network 는 계속 링크한다 (`QLocalServer` 2.8, 테스트의 `QTcpServer`).

### 2.6. 로그인 시 자동 실행

- 켜기: `Run` 키의 값 `TokenViewer` = `"<실행 파일 전체 경로>"` (따옴표 포함, `\` 구분자). 이미 같은 값이면 쓰지 않는다. 경로가 다르면 덮어쓴다.
- 끄기: 값이 있으면 지운다.
- 실행 파일 옆에 `Qt5Core.dll` 이 없으면 (빌드 폴더에서 실행) 등록하지 않는다. 로그인 때 Qt DLL 을 못 찾아 실행되지 않기 때문이다 (v0.1.0 이 번들 밖 실행 파일을 등록하지 않은 것과 같은 이유).

### 2.7. 프로젝트 귀속

v0.1.0 5.3 의 규칙을 그대로 쓰되, 경로 비교는 `\` 를 `/` 로 바꾸고 끝의 `/` 를 떼고 대소문자를 무시한다.
VS Code 확장 등은 같은 폴더를 `c:\Users\…` 와 `C:\Users\…` 로 번갈아 기록한다. 이 둘은 한 프로젝트다.
표시 이름은 그 프로젝트에서 가장 최근 레코드의 원래 표기에서 마지막 폴더명(동명이면 `상위/이름`)을 쓴다.

### 2.8. 한 번만 실행

- 사용자별 `QLocalServer` (이름에 사용자 이름을 넣는다. Windows 이름 있는 파이프는 PC 전체에서 공유된다).
- 이미 실행 중일 때 다시 실행하면: 실행 중인 앱이 드롭다운을 열고, 새 프로세스는 바로 끝난다. Windows 11 은 새 트레이 아이콘을 `^` 안에 숨기므로 시작 메뉴로 다시 열 수 있어야 한다.
- `TokenViewer.exe --quit`: 실행 중인 앱을 정상 종료시키고 끝난다 (설치 스크립트가 업데이트·제거 전에 쓴다). 실행 중인 앱이 없으면 그냥 끝난다.

### 2.9. 어디에 썼나 목록: 4칸 고정 (2026-10-24 추가)

드롭다운이 아이콘 위에 뜨므로 높이가 바뀌면 위쪽 가장자리가 출렁인다. 탭(프로젝트/모델/세션)이나 기간을 바꿔도 크기가 그대로이도록 목록은 항상 4칸을 차지한다.

- 4개 이하: 모두 이름으로 보여 주고 남는 칸은 비워 둔다.
- 5개 이상: 상위 3개 + 4번째 칸에 `기타 N개` (N = 전체 − 3, 금액은 접힌 것들의 합, 막대는 1위 대비). 합계가 빠지지 않고, `기타 1개` 처럼 하나만 접히는 일이 없다.
- 기록이 없으면 같은 4칸 높이 안에 빈 상태 문구를 보여 준다.
- 칸 높이는 글꼴 크기에서 계산한다 (100%·150% 에서 같은 비율).

macOS Swift 앱은 v0.1.0 규칙(상위 5개 + 기타)을 그대로 쓴다.

## 3. 구조

### 3.1. 폴더

Swift 스펙 1절에서 "다음 작업"으로 남긴 대로 Qt 코드를 `qt/` 로 옮긴다.

```
shared/prices.json                 (그대로) qt/resources/TokenViewer.qrc 는 ../../shared/prices.json 을 가리킨다
macos/                             (그대로)
qt/
  CMakeLists.txt
  src/  tests/  resources/         (최상위에서 옮김)
  tools/MakeIcon.cpp               앱 아이콘 .ico 생성 (5.1)
  scripts/package-windows.ps1      Release 빌드 → windeployqt → zip (5.2)
  scripts/install.ps1              설치·업데이트·제거 (5.3)
.github/workflows/windows.yml      테스트·릴리스 (5.4)
```

- 지운다: `src/UI/TrayIconMac.mm`, `resources/Info.plist.in`, CMake 의 `TV_NATIVE_TRAY`·`MACOSX_BUNDLE`·`-framework`, `CredentialStore` 의 Keychain 코드, LaunchAgent 용 `LoginItem`.
- `EFetchStatus::KEYCHAIN_DENIED` 와 그 처리는 남긴다 (Windows 에서는 생기지 않을 뿐 동작에 영향이 없다).

### 3.2. 바뀌거나 새로 생기는 단위

| 단위 | 하는 일 |
| --- | --- |
| `ClaudePaths` (새) | 2.4 의 Claude 설정 폴더, `.credentials.json`, `projects` 경로 |
| `CredentialStore` | 2.4. 파일 경로를 받아 읽는 함수를 두고 테스트한다 |
| `LoginItem` | 2.6. `Run` 키는 `QSettings*` 로 주입받는다 (테스트는 임시 ini 파일) |
| `WinHttpClient` (새) | 2.5. URL·헤더·타임아웃 → HTTP 상태·본문·오류 |
| `UsageApiClient` | `FetchBlocking` 이 `WinHttpClient` 를 쓴다 |
| `TokenAggregator` | 2.7 |
| `TrayIconPainter` | 2.1 의 그림, 툴팁 문구 |
| `TrayIcon` | `QSystemTrayIcon`, 크기별 `QIcon`, 툴팁, 작업 표시줄 밝기 |
| `WindowsTheme` (새) | `Personalize` 키(주입받은 `QSettings`)에서 작업 표시줄 밝기·앱 모드를 읽는다. 어두운 팔레트 |
| `UsagePopup` | 2.2. 위치 계산은 순수 함수로 테스트한다 |
| `SingleInstance` (새) | 2.8 |
| `Application`, `main.cpp` | 위 단위 연결, 고해상도 속성, 테마 적용, 오류 로그 경로 |

## 4. 테스트

Qt Test, `ctest`. 실제 API·레지스트리 `Run` 키·`%USERPROFILE%\.claude` 는 건드리지 않는다 (경로와 `QSettings` 를 주입하고 임시 폴더를 쓴다).
테스트는 `QT_QPA_PLATFORM=offscreen` 에서 돌고, CTest 가 Qt `bin` 을 `PATH` 앞에 넣는다.
콘솔 창이 없으면 Qt Test 가 `OutputDebugString` 으로만 써서 CTest 에 아무것도 안 남으므로 `QT_FORCE_STDERR_LOGGING=1` 도 준다.
offscreen 플랫폼은 Windows 에서 글꼴을 못 찾으므로 아이콘 테스트는 `QT_QPA_FONTDIR=%WINDIR%\Fonts` 를 준다 (FreeType 으로 그려서 앱의 GDI 와 획이 조금 다르다. 16·20px 모양은 손으로 확인한다).
Windows C 런타임은 `TZ=Asia/Seoul` 을 잘못 읽으므로 서울 시간대가 필요한 테스트는 Windows 에서 `KST-9` 를 쓴다.

| 대상 | 확인할 것 |
| --- | --- |
| `ClaudePaths` | `CLAUDE_CONFIG_DIR` 있음/없음 |
| `CredentialStore` | 파일 없음 → 로그인 필요, 정상 파일, 기존 파서 테스트 |
| `LoginItem` | 값 쓰기(따옴표·`\`), 같은 값이면 그대로, 끄면 지움, `Qt5Core.dll` 없으면 등록 안 함 |
| `WinHttpClient` | 루프백 `QTcpServer` (다른 스레드): 200 + 본문 + 요청 헤더, 429 그대로, 응답 없음 → 타임아웃, 닫힌 포트 → 네트워크 오류 |
| `TokenAggregator` | `c:\…\Proj` 와 `C:\…\Proj` 한 줄, `C:\a\App` / `D:\b\App` 구분, 끝 구분자 |
| `TrayIconPainter` | 크기별 픽셀: 테두리 진행 위치(9시→아래→오른쪽), 흐린 선, 주황·빨강, 밝은/어두운 작업 표시줄 색, stale·빈 상태 50%, 숫자 문구, 툴팁 |
| `WindowsTheme` | 값 0/1/없음, 어두운 팔레트의 창 색이 어둡다 |
| `UsagePopup` | 위치: 아래 작업 표시줄 → 위, 위 작업 표시줄 → 아래, 화면 끝 넣기, 측면 작업 표시줄. 위에 띄운 뒤 높이가 바뀌어도 아래 가장자리 유지 |
| `SingleInstance` | 두 번째 인스턴스가 보낸 `activate`/`quit` 을 첫 번째가 받는다, 실행 중인 것이 없으면 보내기 실패 |

**손으로 확인**: 밝은/어두운 작업 표시줄에서 아이콘 (100%·150%), 클릭 열고 닫기·바깥 클릭 닫기, 드롭다운이 아이콘 위에 뜸,
툴팁, 다시 실행 → 드롭다운, `--quit`, 트레이 % 와 `/usage` 일치, 설치·업데이트·제거, 로그인 시 자동 실행.

## 5. 빌드·배포

### 5.1. 앱 아이콘

- `qt/tools/MakeIcon.cpp` 를 빌드 중에 실행해 `TokenViewer.ico` (16–256px PNG 를 담은 ICO) 를 빌드 폴더에 만든다. 저장소에 그림 파일을 두지 않는다.
- 그림: 어두운 둥근 정사각형 바탕에 트레이 아이콘과 같은 둥근 테두리 진행선(파랑 `#3987e5`, 약 2/3)과 가운데 막대 두 개. 글자는 넣지 않는다 (빌드 중에 글꼴 없이 그리기 위해서다).
- `.rc` 의 `IDI_ICON1` 로 넣는다. Qt 창(설정 창 등)도 이 아이콘을 쓴다.

### 5.2. `qt/scripts/package-windows.ps1`

1. MinGW + Ninja 로 Release 구성·빌드, `ctest` (끌 수 있는 옵션).
2. 버전: `TV_VERSION` 환경 변수 → `git describe --tags --abbrev=0` (앞 `v` 제거) → CMake 프로젝트 버전.
3. `windeployqt --debug --no-translations --no-system-d3d-compiler --no-opengl-sw --no-angle --compiler-runtime`. 쓰지 않는 플러그인(`bearer`, `iconengines`, `imageformats`, `styles`)은 지운다 (Fusion 은 `Qt5Widgets.dll` 안에 있다).
   MinGW 용 windeployqt 는 strip 된 바이너리만 release 로 보는데 Qt 의 플러그인은 strip 되어 있지 않아 `--release` 로는 플랫폼 플러그인을 못 찾는다.
   Qt 5.15.2 MinGW 는 release DLL 만 있으므로 `--debug` 로도 같은 파일이 들어간다. 스크립트가 Qt 설치본과 해시로 같은지 확인한다.
4. `THIRD_PARTY_NOTICES.md` 와 `licenses/` (LGPLv3, GPLv3, GCC Runtime Library Exception 전문) 를 함께 넣는다.
5. 결과: `qt/dist/TokenViewer/`, `qt/dist/TokenViewer-windows.zip`, `TokenViewer-windows.zip.sha256`. 크기를 출력한다.
6. 설치 프로그램 (5.6): `qt/dist/TokenViewer-windows-setup.exe` 와 `.sha256`. `-SkipInstaller` 로 건너뛴다. `ISCC.exe` 는 `-IsccPath`, `TV_ISCC`, PATH, 사용자별·Program Files 기본 위치 순으로 찾는다.

### 5.3. `qt/scripts/install.ps1`

PowerShell 한 줄 설치도 5.6 의 설치 프로그램을 받아 조용히 실행한다. 두 경로가 같은 상태 (설정 → 앱 의 항목과 언인스톨러) 를 남기게 하기 위해서다 (2026-10-24).

| 실행 | 동작 |
| --- | --- |
| `irm https://github.com/<owner>/TokenViewer/releases/latest/download/install.ps1 \| iex` | 최신 릴리스의 `TokenViewer-windows-setup.exe` 와 `.sha256` 을 받아 확인 → `/VERYSILENT /CURRENTUSER` 로 실행 → 앱 실행 |
| `install.ps1 -FromSource` | `package-windows.ps1` 실행 → 만든 설치 프로그램을 같은 방식으로 실행 |
| `install.ps1 -Uninstall` | 설치 프로그램의 제거 항목이 있으면 언인스톨러를 조용히 실행, 없으면 (zip 으로 설치한 예전 방식) 폴더·바로가기를 직접 지움 → `Run` 값, 설정(`HKCU\Software\TokenViewer`), `%LOCALAPPDATA%\TokenViewer` 삭제 |

- 설치 위치: `%LOCALAPPDATA%\Programs\TokenViewer`. 관리자 권한을 쓰지 않는다. `TV_INSTALL_DIR` 로 바꿀 수 있다 (`/DIR=`). `-NoLaunch` 면 실행하지 않는다.
- 앱 종료는 설치 프로그램이 한다 (5.6).
- 저장소 주소: 릴리스 워크플로가 `install.ps1` 을 올릴 때 `$ReleaseRepo` 줄에 `${{ github.repository }}` 를 넣는다. 직접 실행하면 `git remote get-url origin`, `TV_REPO` 로 덮어쓸 수 있다.
- PowerShell 로 받은 파일에는 인터넷 영역 표시가 붙지 않아 SmartScreen 경고 없이 실행된다. 브라우저로 zip 을 받으면 처음 한 번 "추가 정보 → 실행"이 필요하다 (README).
- 끝에 Windows 11 은 아이콘이 `^` 안에 숨는다는 안내를 출력한다.

### 5.4. CI (`.github/workflows/windows.yml`)

- `qt/**`, `shared/**`, 워크플로 파일이 바뀐 push·PR: Qt 5.15.2 `win64_mingw81` + MinGW 8.1 설치 (`jurplel/install-qt-action`, MIT) → 구성·빌드·`ctest`.
- `v*` 태그: `package-windows.ps1` → GitHub Release 에 zip, 설치 프로그램, 각 `.sha256`, `install.ps1` 을 올린다. macOS 워크플로와 같은 릴리스를 함께 만들므로, 릴리스가 이미 있으면(또는 만드는 사이에 생기면) 그 릴리스에 올린다.
  러너 `windows-2022` 에는 Inno Setup 6.7.1 이 미리 설치되어 있다 (없으면 `choco install innosetup --version=6.7.1`).

### 5.5. 라이선스

- Qt 5.15.2 (Core, Gui, Widgets, Network) 는 DLL 로 동봉한다 (LGPLv3, 정적 링크 금지 확인은 CMake 에 그대로 둔다).
- MinGW-w64 런타임 DLL: `libstdc++-6.dll`, `libgcc_s_seh-1.dll` (GPLv3 + GCC Runtime Library Exception), `libwinpthread-1.dll` (MIT 계열). 고지와 전문을 넣는다. (2026-10-23 사용자 결정: 배포도 MinGW 로 한다.)
- OpenSSL 은 넣지 않으므로 고지에서 뺀다. 앱 안 고지 화면(`오픈소스 라이선스 · Qt 정보`)은 그대로 둔다.
- 설치 프로그램은 Inno Setup 6 으로 만든다. 라이선스(https://jrsoftware.org/files/is/license.txt)는 상업적 사용을 포함해 어떤 목적이든 허용하고, 바이너리에 Inno 의 저작권 고지를 남기라고 한다 (설치 프로그램이 스스로 남긴다).
  6.5 부터 상업적 사용자에게 구매를 *요청*하지만 의무는 아니고, TokenViewer 는 비상업 MIT 프로젝트다.

### 5.6. 설치 프로그램 (`qt/installer/TokenViewer.iss`, 2026-10-24 추가)

- Inno Setup 6, 결과는 `TokenViewer-windows-setup.exe`. 버전·원본 폴더·아이콘은 `package-windows.ps1` 이 `/D` 로 넘긴다. 서명하지 않는다.
- 사용자별 설치: `PrivilegesRequired=lowest` (UAC 없음), `{autopf}\TokenViewer` (= `%LOCALAPPDATA%\Programs\TokenViewer`, install.ps1 과 같다), 시작 메뉴 `TokenViewer.lnk`. 바탕 화면 아이콘 없음.
- 설치 폴더를 고르는 페이지는 없다 (`DisableDirPage=yes`). 다른 파일이 있는 공용 폴더에 설치했다가 언인스톨러 없이 지울 때 남의 파일까지 지우는 일을 막기 위해서다. 폴더는 명령줄 `/DIR=` (install.ps1 의 `TV_INSTALL_DIR`) 로만 바꾼다.
  install.ps1 은 언인스톨러가 없을 때 기본 폴더나 `TV_INSTALL_DIR` 만 폴더째 지우고, 그 밖의 폴더는 제거 항목만 지운 뒤 폴더 경로를 알려 준다.
- `AppId` = `{CFD0D0E6-2DEE-4F55-95EB-05DE69CA8A73}` — **릴리스한 뒤에는 바꾸지 않는다** (업그레이드·제거가 이 값으로 이어진다). 게시자 `jy.seong`.
- Windows 10 1903 이상, `x64compatible` (ARM64 Windows 11 은 x64 에뮬레이션으로 설치된다. zip 과 같다). 한국어·영어, UI 언어로 고른다 (Inno 공식 `Korean.isl`). `WizardStyle=modern`, lzma2 압축.
- 설치·업그레이드·제거 전에 **설치 폴더에서 실행 중인** TokenViewer 만 찾아 (WMI 로 실행 파일 경로 비교) `--quit` 로 끄고, 5초 안에 안 끝나면 그 프로세스만 강제로 끝낸다. 다른 곳에서 실행 중인 TokenViewer 는 건드리지 않는다.
- 마침 페이지의 "TokenViewer 실행" (기본 켬). 조용히 설치할 때는 실행하지 않는다 (install.ps1 이 실행한다).
- 업그레이드할 때는 같은 폴더에 설치했던 경우에만 Qt 플러그인 등 하위 폴더를 비우고 다시 넣는다 (예전 파일이 남지 않게).
- 제거: 파일, 바로가기, 이 사본을 가리키는 `Run` 값을 지운다. 설정·로그가 있으면 지울지 묻는다 (기본 아니요). 조용히 제거할 때는 `/PURGE` 를 줄 때만 지운다.

## 6. 작업 단위

각 단위는 구현 에이전트(테스트 → 구현 → 전체 `ctest` 통과) → 리뷰 에이전트(코드 규칙 정리, 동작 그대로) 순서로 한다.

| 단위 | 내용 |
| --- | --- |
| A. Windows 빌드와 플랫폼 서비스 | `qt/` 이동, CMake(Windows/MinGW, CTest PATH), macOS 코드 제거, `ClaudePaths`, `CredentialStore`, `LoginItem`, 오류 로그 경로·안내 문구, 2.7 |
| B. WinHTTP | `WinHttpClient`, `UsageApiClient::FetchBlocking` |
| C. 트레이 아이콘 | `TrayIconPainter` 새 그림, `TrayIcon`, `WindowsTheme` (작업 표시줄 밝기·앱 모드 읽기, 어두운 팔레트) |
| D. 앱 껍데기 | 드롭다운 위치, 고해상도 속성, Fusion·팔레트 적용, `SingleInstance`, `--quit` |
| E. 배포 | 앱 아이콘, `package-windows.ps1`, `install.ps1`, CI, 고지·전문, README |
| F. 화면 다듬기 (2026-10-24 추가) | 2.3 의 글꼴·색·불투명 창·컨트롤, 2.9 의 4칸 고정 목록 |
| G. 설치 프로그램 (2026-10-24 추가) | 5.6 의 Inno Setup 설치 프로그램, install.ps1 이 그것을 쓰도록, CI 업로드 |

## 7. 위험

- **실제 로그·자격 증명을 보지 않았다.** 이 PC 의 `~/.claude` 는 확인하지 않고 설계했다. Windows 의 Claude Code 가 `.credentials.json` 에 v0.1.0 3.2 와 같은 형식으로 저장한다는 것, 로그의 `cwd` 가 `\` 구분자라는 것은 알려진 동작에 기댄다. 첫 실행에서 손으로 확인한다.
- Windows 11 은 새 트레이 아이콘을 숨긴다. 설정 → 개인 설정 → 작업 표시줄 → 기타 시스템 트레이 아이콘에서 켜거나 `^` 에서 끌어내야 한다 (README, 설치 스크립트 안내).
- 서명하지 않은 실행 파일: 브라우저로 받은 zip 은 SmartScreen 이 처음 한 번 막는다.
  **Smart App Control 이 켜진 PC** (이 개발 PC 가 그렇다) 는 평판 없는 서명 안 된 실행 파일을 "그래도 실행" 없이 막을 수 있다. 개발 중에도 새로 링크한 테스트 실행 파일이 가끔 막혔다 (다시 링크하면 풀림).
  확실한 해결은 Authenticode 서명뿐이라 범위 밖이다. README 에 적는다.
- `QSystemTrayIcon::geometry()` 가 배율·아이콘 숨김 상태에 따라 비거나 어긋날 수 있다. 2.2 의 마우스 위치 대체와 화면 안 넣기로 받는다.
- 16px 아이콘에서 세 자리 숫자(`100`)는 작다. 그때는 빨강이라 상태는 알 수 있다.
