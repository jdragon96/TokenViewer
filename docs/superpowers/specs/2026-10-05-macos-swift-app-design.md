# TokenViewer macOS (Swift) 설계

- 작성일: 2026-10-05
- 상태: 승인됨
- 대상 플랫폼: macOS 14 Sonoma 이상, Swift 6 (Xcode 26.4.1 / Swift 6.3.1 에서 개발), arm64 + x86_64
- 기준 문서: `2026-10-05-token-viewer-design.md` (Qt v0.1.0, 이하 "v0.1.0 스펙")

## 1. 목적과 범위

macOS 배포판을 Qt 대신 Swift 로 다시 만든다. 이유는 크기다. Qt6 를 앱에 넣으면 20MB 이상(Homebrew Qt 는 의존 라이브러리 포함 약 65MB)이지만,
Swift 는 런타임과 SwiftUI·AppKit 이 OS 에 들어 있어 앱이 1–2MB 다. Qt 앱은 Windows/Ubuntu 전용으로 남긴다 (별도 작업).

**성공 기준**

1. v0.1.0 스펙의 성공 기준 1–4 를 그대로 지킨다 (메뉴바 % 가 `/usage` 와 같음, Claude Code 가 꺼져도 갱신, 한도 실패해도 로그 목록, 토큰은 읽기만).
2. universal `.app` 이 **3MB 이하**다.
3. 사용자가 따로 설치할 것이 없다. 외부 Swift 패키지 의존성 0.
4. 터미널에서 빌드·테스트·설치·제거가 된다 (`swift test`, `build-app.sh`, `install.sh`).
5. GitHub 에 공개할 수 있는 상태다 (`LICENSE`, README 의 macOS 설치 절, 내부 문서 없음).

**범위 밖**

- Developer ID 서명·공증 실행. 스크립트에 환경 변수 자리만 만든다.
- Homebrew cask. 공증이 된 뒤에 한다.
- Qt 앱의 Windows/Ubuntu 이식과 Qt 코드의 `qt/` 이동. 다음 작업이다.
- 자동 업데이트 (Sparkle 등). 다시 설치 스크립트를 실행하면 된다.
- 라이선스 고지 창. 외부 라이브러리가 없다.

## 2. 동작

v0.1.0 스펙의 2절(화면), 3절(데이터 소스), 5절(데이터 규칙), 6절(갱신 정책), 7절(오류 처리)을 **그대로 따른다.**
문구, 색, 크기, 임계치, 기간, 귀속 규칙, 가격 계산이 모두 같다. 다른 점만 적는다.

| 항목 | v0.1.0 (Qt) | Swift |
| --- | --- | --- |
| 최소 macOS | 11.0 | 14.0 |
| 설정 창 바닥 | `오픈소스 라이선스 · Qt 정보` + 버전 | 버전 + `GitHub` 링크 |
| 로그인 시 자동 실행 | `~/Library/LaunchAgents/<id>.plist` 를 직접 쓴다 | `SMAppService.mainApp` 등록/해제 |
| 드롭다운 창 | `Qt::Popup` | 테두리 없는 `NSPanel` + SwiftUI 내용 |
| 메뉴바 아이콘 | Objective-C++ `NSStatusItem` | Swift `NSStatusItem` (같은 그림 규칙) |
| 설정 저장 | `QSettings` | `UserDefaults` (같은 도메인, 같은 키. 4.3) |

번들 ID 는 `com.tokenviewer.TokenViewer` 로 Qt 와 같다. 설정이 그대로 이어진다.

## 3. 구조

### 3.1. 폴더

```
shared/prices.json            ← resources/prices.json 을 옮김. Qt 의 TokenViewer.qrc 는 ../shared/prices.json 을 가리킨다
macos/
  Package.swift               swift-tools-version 6.0, platforms .macOS(.v14), 의존성 없음
  Info.plist                  번들 정보 원본 (build-app.sh 가 버전을 넣는다)
  Sources/
    TokenViewerCore/          라이브러리. Foundation · CoreGraphics · CoreText 만 쓴다 (AppKit·SwiftUI·Security 없음)
      Model/       UsageLimits, Credential, TokenRecord, Breakdown, FetchResult
      Analysis/    LogParser, PriceTable, TokenAggregator, LogStore
      Service/     UsageResponseParser, CredentialParser, RefreshPolicy, SettingsStore
      Worker/      UsageFetcher (actor), LogScanner (actor)
      Render/      StatusIconRenderer, Formatters
    TokenViewer/              실행 파일. 시스템과 UI
      App/         main.swift, AppDelegate, AppState
      System/      KeychainReader, UsageClient, LoginItem, ErrorLog
      UI/          StatusItemController, PopupPanel, PopupView, LimitMeterView,
                   BreakdownListView, SettingsWindow, SettingsView
  Tests/TokenViewerCoreTests/ Swift Testing, Fixtures/
  scripts/        build-app.sh, install.sh
.github/workflows/macos.yml
LICENSE                       MIT
```

### 3.2. 단위

| 단위 | 하는 일 | 의존 |
| --- | --- | --- |
| `LogParser` | jsonl 한 줄 → 사용 레코드 / 세션 제목 / 해당 없음 (v0.1.0 5.1) | 순수 |
| `LogStore` | 파일별 읽은 위치·키를 기억하고 새로 붙은 부분만 읽는다. `(message.id, requestId)` 중복 제거, 31일 지난 레코드 정리 | FileManager |
| `PriceTable` | `prices.json` 을 읽고 레코드 → 금액 (v0.1.0 5.5) | 순수 |
| `TokenAggregator` | 레코드 + 기간 + 기준 → 상위 5개 + 기타 (v0.1.0 5.3, 5.4) | 순수 |
| `UsageResponseParser` | `/api/oauth/usage` 응답 → `UsageLimits` (v0.1.0 3.1, 관대한 파서) | 순수 |
| `CredentialParser` | credentials JSON → accessToken·만료·요금제 이름 (v0.1.0 3.2) | 순수 |
| `RefreshPolicy` | 상태 + 기본 주기 + 이전 지연 → 다음 지연, 자동 재시도 여부 (v0.1.0 6, 7) | 순수 |
| `SettingsStore` | `UserDefaults` 래퍼. 값 검증 (허용 주기, 5% 단위, 빨강 > 주황) | Foundation |
| `UsageFetcher` | 주기 조회 루프, 새로고침 요청 합치기, 429 대기. 시스템 호출은 클로저로 주입 | 위 셋 |
| `LogScanner` | 시작 시 전체, 60초마다·요청 시 증분 스캔. 요청한 탭·기간으로 집계 | LogStore, TokenAggregator, PriceTable |
| `StatusIconRenderer` | 아이콘 상태 + 메뉴바 밝기 + 배율 → `CGImage` + 템플릿 여부 (v0.1.0 2.1) | CoreGraphics, CoreText |
| `Formatters` | `$12.40`, `2:13 후 리셋`, `목 9:00 리셋`, `n분 전`, `%` | Foundation |
| `KeychainReader` | `SecItemCopyMatching` 으로 `Claude Code-credentials` 를 읽는다. 없으면 `~/.claude/.credentials.json` | Security |
| `UsageClient` | `URLSession` 으로 GET, 10초 타임아웃 → HTTP 상태 + 본문 | Foundation |
| `LoginItem` | `SMAppService.mainApp` register/unregister, 상태 조회. 앱 인자 `--unregister-login-item` 이면 해제만 하고 끝낸다 (제거 스크립트용) | ServiceManagement |
| `ErrorLog` | 형식이 바뀐 응답 원문을 `~/Library/Logs/TokenViewer/` 에 남긴다 | Foundation |
| `AppState` | `@MainActor @Observable`. 한도, 요금제, 조회 상태, 마지막 성공·오류, 현재 목록, 설정 | 위 전부 연결 |
| `StatusItemController` | `NSStatusItem`. 상태가 바뀔 때만 아이콘을 다시 그린다. 클릭 → 드롭다운 토글 | AppKit |
| `PopupPanel` · `PopupView` 외 | v0.1.0 2.2 드롭다운, 2.3 설정 창 | AppKit, SwiftUI |

`TokenViewerCore` 는 AppKit·SwiftUI·Security·ServiceManagement 를 쓰지 않는다. 그래서 전부 단위 테스트할 수 있다.
시스템에 닿는 일(Keychain, 네트워크, 로그인 항목, 화면)은 `TokenViewer` 에만 있다.

## 4. 데이터 흐름과 동시성

```
          ┌──────────── AppState (@MainActor @Observable) ────────────┐
          │ limits, planLabel, fetchStatus, lastSuccessAt, lastError,  │
          │ breakdown(현재 탭·기간), settings                           │
          └──▲──────────────────────▲────────────────────┬────────────┘
             │ 결과 전달             │ 결과 전달           │ 읽기 / 변경 감지
   UsageFetcher (actor)       LogScanner (actor)    StatusItemController → StatusIconRenderer
   ← KeychainReader, UsageClient   LogStore → TokenAggregator   PopupView · SettingsView
```

### 4.1. 한도 (`UsageFetcher`)

- 주입받는 것: `loadCredential() async -> CredentialResult`, `requestUsage(token) async -> HTTP 결과`.
  테스트는 가짜를 넣고, 시간은 짧은 주기(0.05–0.1초)로 실제 시간을 쓴다.
- `Task` 하나가 루프를 돈다: 조회 → `RefreshPolicy` 로 다음 지연 계산 → 지연만큼 기다리거나 새로고침 요청이 오면 깬다.
- 조회 중에 들어온 새로고침 요청은 그 조회의 결과로 처리된 것으로 본다 (Qt 커밋 b19e534 와 같다).
- 드롭다운을 열 때는 마지막 시도가 30초 넘었을 때만 요청한다.
- Keychain 거부면 자동 재시도하지 않고, 사용자가 `다시 확인` 을 눌렀을 때만 조회한다.
- 429 면 지연을 2배씩, 최대 30분. 다른 상태는 설정 주기. 설정 주기가 바뀌면 다음 대기부터 새 값을 쓴다.
- `KeychainReader` 는 전용 `DispatchQueue` 에서 `SecItemCopyMatching` 을 부르고 continuation 으로 결과를 넘긴다.
  허용 창이 떠 있는 동안 Swift 동시성 스레드 풀을 막지 않기 위해서다.
- accessToken 은 요청하는 동안만 메모리에 둔다. refreshToken 은 읽지 않는다. 로그·설정·파일에 토큰을 쓰지 않는다.
- 결과(`FetchResult`)는 MainActor 의 `AppState` 로 넘긴다.

### 4.2. 로그 (`LogScanner`)

- 루트: `~/.claude/projects`. 하위 `<sessionId>/subagents/agent-*.jsonl` 포함.
- 시작할 때 30일 안에 수정된 파일 전체, 이후 60초마다 증분, 드롭다운을 열 때 증분.
- 증분 규칙(v0.1.0 7): 쓰다 만 마지막 줄은 마지막 완전한 줄바꿈까지만 읽는다. 파일이 작아지면 처음부터, 사라지면 그 파일의 레코드를 뺀다.
- 파일은 4MB 단위로 읽는다. `"usage"` 문자열이 없는 줄은 JSON 파싱 전에 버린다.
- 스캔 뒤, 그리고 탭·기간이 바뀔 때 현재 탭·기간의 `Breakdown` 만 만들어 `AppState` 로 넘긴다.
  "이번 5시간 창" 시작은 `AppState` 의 `five_hour.resets_at − 5시간`, 없으면 지금 − 5시간.

### 4.3. 설정

`UserDefaults.standard` (도메인 `com.tokenviewer.TokenViewer`). Qt `QSettings` 가 macOS 에서 쓰는 키와 같다.

| 키 | 값 | 기본 |
| --- | --- | --- |
| `refresh.intervalMinutes` | 1 · 3 · 5 · 10 | 3 |
| `alert.warnPercent` | 50–95, 5 단위 | 70 |
| `alert.criticalPercent` | 주황 초과 ~100, 5 단위 | 90 (주황 이하면 주황 + 5) |
| `app.launchAtLogin` | Bool | true |
| `popup.dimension` | 0 프로젝트 · 1 모델 · 2 세션 | 0 |
| `popup.period` | 0 5시간 창 · 1 오늘 · 2 7일 · 3 30일 | 0 |

잘못된 값이 들어 있으면 기본값을 쓴다 (Qt `Settings` 와 같은 규칙).
`app.launchAtLogin` 은 원하는 상태를 기억하고, 앱 시작 때 `SMAppService` 상태를 그 값에 맞춘다.

## 5. 빌드·묶기·설치

### 5.1. `macos/scripts/build-app.sh`

1. `swift build -c release --arch arm64 --arch x86_64 -Xswiftc -Osize` (universal).
2. `TokenViewer.app/Contents/{MacOS/TokenViewer, Info.plist, Resources/prices.json}` 로 묶는다. `strip -x` 로 심볼을 지운다.
3. 버전: `TV_VERSION` → `git describe --tags --abbrev=0` (앞의 `v` 제거) → `0.0.0-dev`. `plutil` 로 `CFBundleShortVersionString`·`CFBundleVersion` 에 넣는다.
4. 서명: `codesign --force --options runtime --sign "${TV_SIGN_IDENTITY:--}"`. 값이 없으면 ad-hoc.
5. `TV_NOTARY_PROFILE` 이 있으면 `xcrun notarytool submit --keychain-profile … --wait` → `xcrun stapler staple`.
6. 결과: `macos/dist/TokenViewer.app`, `macos/dist/TokenViewer-macos.zip` (`ditto -c -k --keepParent`), `TokenViewer-macos.zip.sha256`. `.app` 크기를 출력하고 3MB 를 넘으면 경고한다.

### 5.2. `macos/scripts/install.sh`

| 실행 | 동작 |
| --- | --- |
| `curl -fsSL https://github.com/<owner>/TokenViewer/releases/latest/download/install.sh \| bash` | 최신 릴리스의 `TokenViewer-macos.zip` 과 `.sha256` 을 받아 확인 → 앱 종료 → 설치 → 실행 |
| `./install.sh --from-source` | `build-app.sh` 실행 → 같은 위치에 설치 → 실행 |
| `./install.sh --uninstall` | 앱 종료 → 로그인 항목 해제(앱의 `--unregister-login-item` 인자) → 앱, 설정(`defaults delete`), `~/Library/Logs/TokenViewer` 삭제 |

- 설치 위치: `/Applications` 에 쓸 수 있으면 거기, 아니면 `~/Applications`. sudo 를 쓰지 않는다.
- 저장소 주소: 릴리스 워크플로가 `install.sh` 를 릴리스 파일로 올릴 때 `${{ github.repository }}` 를 넣는다.
  저장소 안의 `install.sh` 를 직접 실행하면 `git remote get-url origin` 에서 읽는다. `TV_REPO` 로 덮어쓸 수 있다.
- curl 로 받은 파일에는 격리 속성이 붙지 않아 ad-hoc 서명으로도 Gatekeeper 창 없이 실행된다.
  대신 업데이트할 때마다 Keychain 허용 창이 다시 뜬다 (서명이 바뀌므로). README 에 적는다.

### 5.3. CI (`.github/workflows/macos.yml`)

- `macos/**`, `shared/**`, 워크플로 파일이 바뀐 push·PR: `swift test`.
- `v*` 태그: `swift test` → `build-app.sh` → GitHub Release 에 `TokenViewer-macos.zip`, `.sha256`, `install.sh` 를 올린다.
- 러너: `macos-15` (Xcode 16 이상, Swift 6). `Package.swift` 는 tools-version 6.0 이라 그 위 버전에서도 빌드된다.

## 6. 공개 준비

- `LICENSE`: MIT.
- `README.md`: 소개, macOS 설치(curl / 소스 / 제거), 앱이 읽는 것과 보내는 곳(Keychain accessToken, `~/.claude/projects`, `api.anthropic.com` 만), ad-hoc 서명이라 업데이트 때 Keychain 허용 창이 다시 뜬다는 안내, Windows·Ubuntu 는 준비 중.
- 이전 프로젝트의 내부 문서(`docs/LicenseCompliance.md`)는 지우고 git 기록에서도 뺐다 (2026-10-05).

## 7. 테스트

Swift Testing (`import Testing`), `cd macos && swift test`. 실제 Keychain·API·로그인 항목은 부르지 않는다.

| 대상 | 확인할 것 |
| --- | --- |
| `LogParser` | 정상 assistant 줄, `<synthetic>`, `ai-title`, 깨진 줄, `"usage"` 없는 줄, 캐시 5분/1시간 나눔, fast |
| `LogStore` | 임시 폴더: 증분 읽기, 쓰다 만 마지막 줄, 파일이 작아짐/사라짐, 30일 지난 파일, 루트 없음, subagent 경로의 세션 ID, 중복 키 |
| `TokenAggregator` | 자정 경계, 5시간 창 시작, 중복 제거, subagent 합산, 상위 5 + 기타 N, 동명 프로젝트, 세션 이름 |
| `PriceTable` | 모델별 금액, 1시간 캐시, fast 2배, 계열 대체 `≈`, 모르는 계열, 실제 `shared/prices.json` 로드 |
| `UsageResponseParser` | 정상, `used_percentage`, epoch `resets_at`, 필드 없음/`null`, 형식 변경 |
| `CredentialParser` | 정상, 필드 없음, 만료(ms/s), 요금제 이름 (`Max 5x` 등) |
| `RefreshPolicy` · `UsageFetcher` | 429 2배·최대 30분, Keychain 거부 시 멈춤, 조회 중 요청 합치기, 30초 규칙, 주기 변경 반영 |
| `SettingsStore` | 기본값, 허용 주기, 5% 단위, 빨강 > 주황, 잘못된 값 |
| `StatusIconRenderer` | 2배율 픽셀: 막대 채움 폭, 밝은/어두운 메뉴바 주황·빨강, 템플릿 여부, stale 50% |
| `Formatters` | 금액(`≈`), 토큰 수, 리셋 카운트다운/요일, `n분 전`, `%` |

**수동 확인**: 밝은/어두운 메뉴바 아이콘, 클릭 열고 닫기·바깥 클릭 닫기, Keychain 허용 창, 메뉴바 % 와 `/usage` 일치,
로그인 항목(시스템 설정 → 일반 → 로그인 항목), curl 설치 뒤 Gatekeeper 창 없이 실행, `--uninstall`, `.app` 3MB 이하.

## 8. 위험과 첫 작업

**첫 작업 = 확인 작업(스파이크).** 구현 계획 맨 앞에 둔다.

1. ad-hoc 서명 앱에서 `SMAppService.mainApp.register()` 가 되는지 (`/Applications`, `~/Applications`).
   안 되면 `LoginItem` 만 LaunchAgent plist 방식(Qt 와 같음)으로 바꾼다.
2. `swift build --arch arm64 --arch x86_64` 가 universal 바이너리를 만드는지. 안 되면 arm64 전용으로 하고 스펙을 고친다.
3. `NSPanel` 드롭다운이 메뉴바 아이콘 바로 아래에 뜨고, 바깥 클릭·다른 앱 전환 때 닫히는지. 전체 화면 앱 위에서도.

**남는 위험**

- 비공식 API 변경, Keychain 허용 창 반복: v0.1.0 스펙 9절과 같다.
- ad-hoc 서명: 업데이트마다 Keychain 허용 창. Developer ID 로 서명하면 사라진다 (`TV_SIGN_IDENTITY`).
- Qt 앱(v0.1.0)의 개발 빌드가 만든 `~/Library/LaunchAgents/com.tokenviewer.TokenViewer.plist` 가 이 PC 에 남아 있다.
  지우지 않으면 로그인 때 아이콘이 두 개 뜬다. 구현 계획에 손으로 지우는 단계를 둔다.
