# TokenViewer macOS (Swift) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Qt v0.1.0 과 같은 동작의 macOS 메뉴바 앱을 Swift 로 다시 만들고, 3MB 이하 universal `.app` 을 터미널에서 빌드·설치·제거할 수 있게 한다.

**Architecture:** SwiftPM 패키지 하나(`macos/`)에 라이브러리 `TokenViewerCore`(순수 로직 + actor 워커, 단위 테스트 대상)와 실행 파일 `TokenViewer`(Keychain·URLSession·SMAppService·AppKit·SwiftUI)를 둔다.
`AppController` 가 워커 두 개(`UsageFetcher`, `LogScanner`)의 결과를 `@MainActor @Observable AppState` 에 넣고, 메뉴바 아이콘·드롭다운·설정 창은 그 상태를 읽는다.
앱 묶기·서명·설치는 셸 스크립트 두 개, 릴리스는 GitHub Actions 가 한다.

**Tech Stack:** Swift 6 (tools-version 6.0), Swift Testing, Foundation, CoreGraphics, CoreText, AppKit, SwiftUI, Security, ServiceManagement, bash, GitHub Actions.

**Spec:** `docs/superpowers/specs/2026-10-05-macos-swift-app-design.md` (동작 세부는 그 안에서 가리키는 `docs/superpowers/specs/2026-10-05-token-viewer-design.md` 2·3·5·6·7절)

## Global Constraints

- 최소 macOS 14.0: `Package.swift` 의 `.macOS(.v14)`, `Info.plist` 의 `LSMinimumSystemVersion` = `14.0`.
- `swift-tools-version: 6.0` (Swift 6 언어 모드). 외부 Swift 패키지 의존성 0.
- `TokenViewerCore` 는 `AppKit`, `SwiftUI`, `Security`, `ServiceManagement` 를 import 하지 않는다.
- 번들 ID `com.tokenviewer.TokenViewer`. 설정 키는 `refresh.intervalMinutes`, `alert.warnPercent`, `alert.criticalPercent`, `app.launchAtLogin`, `popup.dimension`, `popup.period`.
- 화면 문구는 Qt v0.1.0 과 같은 한국어 문구를 그대로 쓴다 (각 작업의 코드에 그대로 적혀 있다).
- 색: 주황 밝은 `#c98500` / 어두운 `#fab219`, 빨강 밝은 `#d03b3b` / 어두운 `#ff6b6b`, 미터 파랑 밝은 `#2a78d6` / 어두운 `#3987e5`.
- accessToken 은 로그·파일·설정에 쓰지 않는다. refreshToken 은 읽지 않는다.
- universal(`arm64` + `x86_64`) `.app` 3MB 이하.
- 주석은 "왜"만 짧게, 영어로 (Qt 코드와 같은 밀도). 커밋 메시지는 `feat:`/`fix:`/`test:`/`docs:`/`chore:` 형식이고 끝에 `Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>` 를 붙인다.
- 모든 Swift 명령은 `macos/` 에서 실행한다 (`cd macos`).

## Review Focus

1. 4MB 읽기 단위 경계에 걸친 로그 한 줄 → 잘리지 않고 한 레코드로 읽혀야 한다. (Task 6 `readsLinesAcrossChunkBoundaries`)
2. 한글 폴더명·세션 제목 (`/Users/me/작업/앱`) → 그대로 표시돼야 한다. (Task 5 `keepsNonAsciiNames`, Task 7 `keepsNonAsciiProjectNames`)
3. Mac 이 오래 잠들었다 깨어남 → 마지막 상태가 OK 여도 15분 지난 값은 흐리게. (Task 9 `staleAfterSleepEvenIfLastStatusOk`)
4. 사용량 API 가 200 과 함께 HTML·빈 본문을 줌 (프록시·캡티브 포털) → 앱이 죽지 않고 "응답 형식이 바뀌었습니다". (Task 8 `notJsonIsBadResponse`)
5. Keychain 허용 창을 오래 내버려 둠 → 그동안 새로고침 요청이 쌓여 허용 창이 여러 개 뜨면 안 된다. (Task 11 `refreshWhileCredentialPendingDoesNotStack`)

---

## File Structure

```
.gitignore                                   (수정) macos/.build, macos/.swiftpm, macos/dist
shared/prices.json                           (이동) resources/prices.json
resources/TokenViewer.qrc                    (수정) ../shared/prices.json 을 alias 로
tests/TestPriceTable.cpp, TestTokenAggregator.cpp, TestUsagePopup.cpp   (수정) shared/prices.json 경로
LICENSE                                      MIT
README.md                                    (다시 씀)
.github/workflows/macos.yml                  테스트·릴리스
docs/superpowers/notes/2026-10-05-swift-spike.md   스파이크 결과
macos/
  Package.swift
  Info.plist
  scripts/build-app.sh                       빌드 → .app → 서명(→ 공증) → zip
  scripts/install.sh                         curl 설치 / --from-source / --uninstall
  Sources/TokenViewerCore/
    AppConstants.swift                       번들 ID, URL, 주기 상수
    Model/UsageLimits.swift                  LimitWindow, UsageLimits
    Model/FetchResult.swift                  FetchStatus, FetchResult, HTTPResponse
    Model/Credential.swift                   Credential, CredentialStatus, CredentialResult, SecretLookup
    Model/TokenRecord.swift                  TokenRecord, LogSnapshot
    Model/Breakdown.swift                    BreakdownDimension, BreakdownPeriod, BreakdownRow, Breakdown
    Model/JSONValues.swift                   ISODate, JSONValues
    Analysis/PriceTable.swift
    Analysis/LogParser.swift
    Analysis/LogStore.swift
    Analysis/TokenAggregator.swift
    Service/CredentialParser.swift
    Service/UsageResponseParser.swift
    Service/RefreshPolicy.swift
    Service/UsageStatus.swift                LimitDisplayState, UsageStatus (Qt SystemStatus)
    Service/StatusText.swift                 안내·바닥 문구
    Service/SettingsStore.swift              SettingsValues, SettingsStore
    Worker/WakeSignal.swift
    Worker/UsageFetcher.swift                UsageFetchEnvironment, UsageFetcher
    Worker/LogScanner.swift                  BreakdownQuery, LogScanner
    Render/Formatters.swift
    Render/Palette.swift                     RGBA, BarLevel, MenuBarAppearance, Palette
    Render/StatusIconRenderer.swift          StatusIconState, StatusIconImage, StatusIconRenderer
  Sources/TokenViewer/
    main.swift
    App/AppDelegate.swift
    App/AppController.swift
    App/AppState.swift
    App/AppInfo.swift
    System/KeychainReader.swift
    System/UsageClient.swift
    System/LoginItem.swift
    System/ErrorLog.swift
    UI/StatusItemController.swift
    UI/PopupPanel.swift
    UI/PopupController.swift
    UI/PopupView.swift
    UI/LimitMeterView.swift
    UI/BreakdownListView.swift
    UI/SettingsView.swift
    UI/SettingsWindowController.swift
    UI/RGBA+Color.swift
  Tests/TokenViewerCoreTests/
    TestSupport.swift                        경로·날짜·임시 폴더·로그 줄·Recorder·Gate·waitUntil
    Fixtures/usage_response.json             tests/fixtures 에서 복사
    FormattersTests.swift, ISODateTests.swift, PriceTableTests.swift, LogParserTests.swift,
    LogStoreTests.swift, TokenAggregatorTests.swift, CredentialParserTests.swift,
    UsageResponseParserTests.swift, RefreshPolicyTests.swift, UsageStatusTests.swift,
    StatusTextTests.swift, SettingsStoreTests.swift, WakeSignalTests.swift,
    UsageFetcherTests.swift, LogScannerTests.swift, StatusIconRendererTests.swift
```

---

### Task 1: 패키지 뼈대, 공유 가격표, 앱 묶기 스크립트

**Files:**
- Create: `macos/Package.swift`, `macos/Info.plist`, `macos/Sources/TokenViewerCore/AppConstants.swift`, `macos/Sources/TokenViewer/main.swift`, `macos/scripts/build-app.sh`
- Move: `resources/prices.json` → `shared/prices.json`
- Modify: `resources/TokenViewer.qrc`, `tests/TestPriceTable.cpp:45`, `tests/TestTokenAggregator.cpp:57`, `tests/TestUsagePopup.cpp:64`, `.gitignore`

**Interfaces:**
- Produces: `AppConstants` (`bundleIdentifier`, `keychainService`, `usageURL`, `usageBetaHeader`, `requestTimeout`, `logScanInterval`, `wakeRefreshDelay`, `clockTickInterval`), `build-app.sh [--debug]` → `macos/dist/TokenViewer.app`, `TokenViewer-macos.zip`, `.sha256`.

- [ ] **Step 1: 가격표를 공유 위치로 옮기고 Qt 쪽 경로를 고친다**

```bash
mkdir -p shared && git mv resources/prices.json shared/prices.json
sed -i '' 's#<file>prices.json</file>#<file alias="prices.json">../shared/prices.json</file>#' resources/TokenViewer.qrc
sed -i '' 's#"resources/prices.json"#"shared/prices.json"#' tests/TestPriceTable.cpp tests/TestTokenAggregator.cpp tests/TestUsagePopup.cpp
printf 'macos/.build/\nmacos/.swiftpm/\nmacos/dist/\n' >> .gitignore
```

- [ ] **Step 2: Qt 빌드·테스트가 그대로인지 확인한다**

Run: `cmake --build build && ctest --test-dir build --output-on-failure`
Expected: 빌드 성공, 테스트 전부 통과.

- [ ] **Step 3: 패키지와 Info.plist 를 만든다**

`macos/Package.swift`:

```swift
// swift-tools-version: 6.0
import PackageDescription

let package = Package(
    name: "TokenViewer",
    platforms: [.macOS(.v14)],
    targets: [
        .target(name: "TokenViewerCore"),
        .executableTarget(name: "TokenViewer", dependencies: ["TokenViewerCore"]),
    ]
)
```

`macos/Info.plist`:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleDevelopmentRegion</key>
    <string>ko</string>
    <key>CFBundleDisplayName</key>
    <string>TokenViewer</string>
    <key>CFBundleExecutable</key>
    <string>TokenViewer</string>
    <key>CFBundleIdentifier</key>
    <string>com.tokenviewer.TokenViewer</string>
    <key>CFBundleInfoDictionaryVersion</key>
    <string>6.0</string>
    <key>CFBundleName</key>
    <string>TokenViewer</string>
    <key>CFBundlePackageType</key>
    <string>APPL</string>
    <key>CFBundleShortVersionString</key>
    <string>0.0.0</string>
    <key>CFBundleVersion</key>
    <string>0.0.0</string>
    <key>LSApplicationCategoryType</key>
    <string>public.app-category.developer-tools</string>
    <key>LSMinimumSystemVersion</key>
    <string>14.0</string>
    <key>LSUIElement</key>
    <true/>
    <key>NSAppSleepDisabled</key>
    <true/>
    <key>NSHighResolutionCapable</key>
    <true/>
    <key>NSPrincipalClass</key>
    <string>NSApplication</string>
</dict>
</plist>
```

`macos/Sources/TokenViewerCore/AppConstants.swift`:

```swift
import Foundation

public enum AppConstants {
    public static let bundleIdentifier = "com.tokenviewer.TokenViewer"
    public static let keychainService = "Claude Code-credentials"
    public static let usageURL = URL(string: "https://api.anthropic.com/api/oauth/usage")!
    public static let usageBetaHeader = "oauth-2025-04-20"
    public static let requestTimeout: TimeInterval = 10
    public static let logScanInterval: TimeInterval = 60
    public static let wakeRefreshDelay: TimeInterval = 8
    public static let clockTickInterval: TimeInterval = 5
}
```

`macos/Sources/TokenViewer/main.swift` (Task 15 에서 바꾼다):

```swift
import AppKit

let application = NSApplication.shared
application.setActivationPolicy(.accessory)
let statusItem = NSStatusBar.system.statusItem(withLength: NSStatusItem.variableLength)
statusItem.button?.title = "TV"
application.run()
```

- [ ] **Step 4: 디버그 빌드를 확인한다**

Run: `cd macos && swift build`
Expected: `Build complete!`

- [ ] **Step 5: 앱 묶기 스크립트를 쓴다**

`macos/scripts/build-app.sh`:

```bash
#!/usr/bin/env bash
# Builds TokenViewer.app into macos/dist (spec 5.1).
#   ./build-app.sh            universal release build, signed, zipped
#   ./build-app.sh --debug    host-arch debug build for quick manual checks
# Environment: TV_VERSION, TV_REPO (owner/name), TV_SIGN_IDENTITY (default ad-hoc), TV_NOTARY_PROFILE.
set -euo pipefail

MACOS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
REPO_ROOT="$(cd "$MACOS_DIR/.." && pwd)"
DIST="$MACOS_DIR/dist"
APP="$DIST/TokenViewer.app"
ZIP="$DIST/TokenViewer-macos.zip"
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

rm -rf "$APP" "$ZIP" "$ZIP.sha256"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
cp "$BIN_DIR/TokenViewer" "$APP/Contents/MacOS/TokenViewer"
cp "$MACOS_DIR/Info.plist" "$APP/Contents/Info.plist"
cp "$REPO_ROOT/shared/prices.json" "$APP/Contents/Resources/prices.json"

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

SIZE_KB="$(du -sk "$APP" | awk '{print $1}')"
echo "TokenViewer.app $VERSION: ${SIZE_KB} KB [$(lipo -archs "$APP/Contents/MacOS/TokenViewer")]"
if [[ $IS_RELEASE == 1 && $SIZE_KB -gt $SIZE_LIMIT_KB ]]; then
    echo "warning: TokenViewer.app is larger than 3 MB" >&2
fi
```

```bash
chmod +x macos/scripts/build-app.sh
```

- [ ] **Step 6: universal 앱이 나오는지 확인한다**

Run: `macos/scripts/build-app.sh && lipo -archs macos/dist/TokenViewer.app/Contents/MacOS/TokenViewer && codesign -dv macos/dist/TokenViewer.app 2>&1 | grep -E "Signature|flags"`
Expected: 마지막 줄에 `x86_64 arm64`, `Signature=adhoc`, `flags=...runtime`. `TokenViewer.app ...: N KB` 가 3072 이하.

Run: `open macos/dist/TokenViewer.app && sleep 2 && pgrep -x TokenViewer && pkill -x TokenViewer`
Expected: PID 한 줄 (메뉴바에 `TV` 가 떴다가 사라진다).

- [ ] **Step 7: 커밋**

```bash
git add .gitignore shared resources/TokenViewer.qrc tests/TestPriceTable.cpp tests/TestTokenAggregator.cpp tests/TestUsagePopup.cpp macos
git commit -m "chore: add the Swift package skeleton and share the price table

Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>"
```

---

### Task 2: 스파이크 — 로그인 항목, 드롭다운 패널

스펙 8절 1·3번을 확인한다. 이 작업의 코드는 버린다. 결과만 노트로 남긴다.

**Files:**
- Create: `docs/superpowers/notes/2026-10-05-swift-spike.md`
- Temporary: `macos/Sources/TokenViewer/main.swift` (끝에 되돌린다)

- [ ] **Step 1: main.swift 를 스파이크 코드로 잠시 바꾼다**

```swift
import AppKit
import ServiceManagement
import SwiftUI

let application = NSApplication.shared
application.setActivationPolicy(.accessory)

if CommandLine.arguments.contains("--spike-login") {
    let service = SMAppService.mainApp
    print("bundle:", Bundle.main.bundleURL.path)
    print("before:", service.status.rawValue)
    do {
        try service.register()
        print("register: ok")
    } catch {
        print("register error:", error)
    }
    print("after:", service.status.rawValue)
    try? service.unregister()
    print("unregistered:", service.status.rawValue)
    exit(0)
}

let statusItem = NSStatusBar.system.statusItem(withLength: NSStatusItem.variableLength)
statusItem.button?.title = "TV"
let panel = NSPanel(contentRect: NSRect(x: 0, y: 0, width: 300, height: 120), styleMask: [.borderless], backing: .buffered, defer: true)
panel.level = .popUpMenu
panel.collectionBehavior = [.canJoinAllSpaces, .fullScreenAuxiliary]
panel.contentView = NSHostingView(rootView: Text("TokenViewer spike").frame(width: 300, height: 120).background(Color(nsColor: .windowBackgroundColor)))
DispatchQueue.main.asyncAfter(deadline: .now() + 1) {
    MainActor.assumeIsolated {
        let frame = statusItem.button?.window?.frame ?? .zero
        panel.setFrameTopLeftPoint(NSPoint(x: frame.midX - 150, y: frame.minY - 4))
        panel.orderFrontRegardless()
    }
}
application.run()
```

- [ ] **Step 2: /Applications 와 ~/Applications 에서 SMAppService 를 확인한다**

```bash
macos/scripts/build-app.sh --debug
ditto macos/dist/TokenViewer.app /Applications/TokenViewerSpike.app
/Applications/TokenViewerSpike.app/Contents/MacOS/TokenViewer --spike-login
mkdir -p ~/Applications && ditto macos/dist/TokenViewer.app ~/Applications/TokenViewerSpike.app
~/Applications/TokenViewerSpike.app/Contents/MacOS/TokenViewer --spike-login
```

Expected: 두 경우 모두 `register: ok`, `after: 1` (enabled) 또는 `2` (requiresApproval), `unregistered: 0`. 실패하면 오류 문구를 기록한다.

- [ ] **Step 3: 패널 위치를 화면 캡처로 확인한다**

```bash
open /Applications/TokenViewerSpike.app && sleep 3
screencapture -x -R 0,0,1800,260 "$TMPDIR/tv-spike.png"
pkill -f TokenViewerSpike.app
```

캡처를 열어(Read 도구) 메뉴바 `TV` 바로 아래에 "TokenViewer spike" 패널이 있는지 본다.

- [ ] **Step 4: 스파이크 앱을 지우고 main.swift 를 되돌린다**

```bash
rm -rf /Applications/TokenViewerSpike.app ~/Applications/TokenViewerSpike.app
git checkout macos/Sources/TokenViewer/main.swift
```

- [ ] **Step 5: 결과를 노트로 남긴다**

`docs/superpowers/notes/2026-10-05-swift-spike.md` 에 아래 형식으로 실제 결과를 쓴다.

```markdown
# Swift 스파이크 결과 (Task 2)

- 날짜: 2026-10-05
- 환경: macOS 26.5.2, Xcode 26.4.1, Swift 6.3.1, ad-hoc 서명

| 항목 | 결과 |
| --- | --- |
| universal 빌드 (`swift build --arch arm64 --arch x86_64`) | Task 1 Step 6 의 `lipo -archs` 출력 |
| `SMAppService.mainApp.register()` — /Applications | Step 2 출력 |
| `SMAppService.mainApp.register()` — ~/Applications | Step 2 출력 |
| `NSPanel` 이 메뉴바 아이템 아래에 뜸 | Step 3 캡처 판정 |

## 판정

- 로그인 항목: SMAppService 채택 / LaunchAgent 로 대체 (실패 시 Task 14 `LoginItem` 을 Qt `LoginItem.cpp` 방식으로 바꾼다)
- 바깥 클릭·다른 앱 전환 때 닫힘, 전체 화면 앱 위 표시는 Task 16 수동 확인으로 넘긴다 (클릭이 필요하다).
```

- [ ] **Step 6: 커밋**

```bash
git add docs/superpowers/notes/2026-10-05-swift-spike.md
git commit -m "docs: record Swift spike results

Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>"
```

---

### Task 3: 모델, 날짜 파싱, Formatters

**Files:**
- Modify: `macos/Package.swift` (테스트 타깃 추가)
- Create: `macos/Sources/TokenViewerCore/Model/{UsageLimits,FetchResult,Credential,TokenRecord,Breakdown,JSONValues}.swift`, `macos/Sources/TokenViewerCore/Render/Formatters.swift`
- Create: `macos/Tests/TokenViewerCoreTests/TestSupport.swift`, `FormattersTests.swift`, `ISODateTests.swift`, `Fixtures/usage_response.json`

**Interfaces:**
- Produces:
  - `struct LimitWindow { var percent: Double; var resetsAt: Date? }`, `struct UsageLimits { var fiveHour: LimitWindow?; var sevenDay: LimitWindow? }`
  - `enum FetchStatus { ok, notLoggedIn, keychainDenied, tokenExpired, rateLimited, networkError, serverError, badResponse }`
  - `struct FetchResult { status, limits, planLabel, detail, httpStatus, rawBody }`, `struct HTTPResponse { status: Int; body: Data; errorText: String? }`
  - `struct Credential { accessToken, expiresAt: Date?, subscriptionType, rateLimitTier }`, `enum CredentialStatus { ok, notFound, accessDenied, malformed, expired }`, `struct CredentialResult { status, credential: Credential? }`, `enum SecretLookup { found(Data), notFound, denied }`
  - `struct TokenRecord { key, timestamp, sessionId, projectPath, model, isFast, input, output, cacheWrite5m, cacheWrite1h, cacheRead }`, `struct LogSnapshot { records, sessionTitles }`
  - `enum BreakdownDimension: Int { project, model, session }` + `title`, `enum BreakdownPeriod: Int { fiveHourWindow, today, sevenDays, thirtyDays }` + `title`, `struct BreakdownRow`, `struct Breakdown { rows, otherCount, otherRow, totalUsd, displayRows }`
  - `enum ISODate { static func parse(_:) -> Date? }`, `enum JSONValues { static func number(_:) -> Double?; static func date(_:) -> Date? }` (internal)
  - `enum Formatters { usd(_:approximate:), tokens(_:), resetCountdown(_:now:), resetDay(_:now:timeZone:), age(since:now:), percent(_:), amount(_:), tooltip(_:) }`

- [ ] **Step 1: 테스트 타깃과 공용 도우미를 만든다**

`macos/Package.swift` 의 targets 를 바꾼다:

```swift
    targets: [
        .target(name: "TokenViewerCore"),
        .executableTarget(name: "TokenViewer", dependencies: ["TokenViewerCore"]),
        .testTarget(name: "TokenViewerCoreTests", dependencies: ["TokenViewerCore"], exclude: ["Fixtures"]),
    ]
```

```bash
mkdir -p macos/Tests/TokenViewerCoreTests/Fixtures
cp tests/fixtures/usage_response.json macos/Tests/TokenViewerCoreTests/Fixtures/usage_response.json
```

`macos/Tests/TokenViewerCoreTests/TestSupport.swift`:

```swift
import Foundation
import Testing
@testable import TokenViewerCore

enum TestSupport {
    /// The repository root, found from this file's path so tests read the real `shared/prices.json`.
    static let repoRoot = URL(fileURLWithPath: #filePath)
        .deletingLastPathComponent()
        .deletingLastPathComponent()
        .deletingLastPathComponent()
        .deletingLastPathComponent()

    static let seoul = TimeZone(identifier: "Asia/Seoul")!

    static func fixture(_ name: String) throws -> Data {
        try Data(contentsOf: repoRoot.appendingPathComponent("macos/Tests/TokenViewerCoreTests/Fixtures/\(name)"))
    }

    static func date(_ text: String) -> Date {
        ISO8601DateFormatter().date(from: text)!
    }

    static func isoString(_ date: Date) -> String {
        let formatter = ISO8601DateFormatter()
        formatter.formatOptions = [.withInternetDateTime, .withFractionalSeconds]
        return formatter.string(from: date)
    }
}

/// Polls until `condition` holds or the timeout passes, and returns the last answer.
func waitUntil(timeout: TimeInterval = 3, _ condition: @Sendable () async -> Bool) async -> Bool {
    let deadline = Date().addingTimeInterval(timeout)
    while Date() < deadline {
        if await condition() {
            return true
        }
        try? await Task.sleep(nanoseconds: 10_000_000)
    }
    return await condition()
}

actor Recorder<Value: Sendable> {
    private(set) var values: [Value] = []

    func append(_ value: Value) {
        values.append(value)
    }
}

/// Holds an async call until `open()`; `isWaiting` tells the test the call has arrived.
actor Gate {
    private var continuation: CheckedContinuation<Void, Never>?
    private var isOpen = false

    var isWaiting: Bool { continuation != nil }

    func pass() async {
        if isOpen {
            return
        }
        await withCheckedContinuation { continuation = $0 }
    }

    func open() {
        isOpen = true
        continuation?.resume()
        continuation = nil
    }
}

final class TemporaryDirectory {
    let url: URL

    init() throws {
        url = FileManager.default.temporaryDirectory.appendingPathComponent("TokenViewerTests-\(UUID().uuidString)")
        try FileManager.default.createDirectory(at: url, withIntermediateDirectories: true)
    }

    deinit {
        try? FileManager.default.removeItem(at: url)
    }

    @discardableResult
    func write(_ relativePath: String, _ lines: [String], terminated: Bool = true) throws -> URL {
        let file = url.appendingPathComponent(relativePath)
        try FileManager.default.createDirectory(at: file.deletingLastPathComponent(), withIntermediateDirectories: true)
        let text = lines.joined(separator: "\n") + (terminated && !lines.isEmpty ? "\n" : "")
        try Data(text.utf8).write(to: file)
        return file
    }

    func append(_ relativePath: String, _ text: String) throws {
        let handle = try FileHandle(forWritingTo: url.appendingPathComponent(relativePath))
        defer { try? handle.close() }
        try handle.seekToEnd()
        try handle.write(contentsOf: Data(text.utf8))
    }
}

/// Builds Claude Code session log lines in the shape recorded on 2026-10-05.
enum LogLines {
    static func assistant(
        id: String,
        requestId: String? = "req-1",
        sessionId: String? = "session-1",
        cwd: String = "/Users/me/work/app",
        model: String = "claude-opus-5-5",
        timestamp: String = "2026-10-05T06:00:00.000Z",
        input: Int = 100,
        output: Int = 50,
        cacheRead: Int = 1_000,
        cacheCreation: Int = 200,
        split: (fiveMinutes: Int, oneHour: Int)? = nil,
        speed: String? = nil
    ) -> String {
        var usage: [String: Any] = [
            "input_tokens": input,
            "output_tokens": output,
            "cache_read_input_tokens": cacheRead,
            "cache_creation_input_tokens": cacheCreation,
        ]
        if let split {
            usage["cache_creation"] = ["ephemeral_5m_input_tokens": split.fiveMinutes, "ephemeral_1h_input_tokens": split.oneHour]
        }
        if let speed {
            usage["speed"] = speed
        }
        var line: [String: Any] = [
            "type": "assistant",
            "cwd": cwd,
            "timestamp": timestamp,
            "uuid": "uuid-\(id)",
            "message": ["id": id, "model": model, "usage": usage],
        ]
        if let requestId {
            line["requestId"] = requestId
        }
        if let sessionId {
            line["sessionId"] = sessionId
        }
        return json(line)
    }

    static func title(sessionId: String, title: String) -> String {
        json(["type": "ai-title", "sessionId": sessionId, "aiTitle": title])
    }

    static func json(_ object: [String: Any]) -> String {
        let data = try! JSONSerialization.data(withJSONObject: object, options: [.sortedKeys])
        return String(decoding: data, as: UTF8.self)
    }
}
```

- [ ] **Step 2: 실패하는 테스트를 쓴다**

`macos/Tests/TokenViewerCoreTests/FormattersTests.swift`:

```swift
import Foundation
import Testing
@testable import TokenViewerCore

@Suite struct FormattersTests {
    private let now = TestSupport.date("2026-10-05T03:00:00Z")

    @Test func formatsUsd() {
        #expect(Formatters.usd(12.4, approximate: false) == "$12.40")
        #expect(Formatters.usd(1234.5, approximate: false) == "$1,234.50")
        #expect(Formatters.usd(3, approximate: true) == "≈$3.00")
        #expect(Formatters.usd(0.004, approximate: false) == "<$0.01")
        #expect(Formatters.usd(0, approximate: false) == "$0.00")
    }

    @Test func formatsTokens() {
        #expect(Formatters.tokens(999) == "999")
        #expect(Formatters.tokens(1_234) == "1.2K")
        #expect(Formatters.tokens(123_456) == "123K")
        #expect(Formatters.tokens(1_234_567) == "1.23M")
        #expect(Formatters.tokens(12_345_678) == "12.3M")
    }

    @Test func formatsResetCountdown() {
        #expect(Formatters.resetCountdown(nil, now: now) == "리셋 시각 없음")
        #expect(Formatters.resetCountdown(now.addingTimeInterval(30), now: now) == "곧 리셋")
        #expect(Formatters.resetCountdown(now.addingTimeInterval(25 * 60 + 10), now: now) == "25분 후 리셋")
        #expect(Formatters.resetCountdown(now.addingTimeInterval(2 * 3600 + 13 * 60), now: now) == "2:13 후 리셋")
        #expect(Formatters.resetCountdown(now.addingTimeInterval(3600 + 5 * 60), now: now) == "1:05 후 리셋")
    }

    @Test func formatsResetDay() {
        // 2026-10-08T00:00Z is Thursday 09:00 in Seoul.
        #expect(Formatters.resetDay(TestSupport.date("2026-10-08T00:00:00Z"), now: now, timeZone: TestSupport.seoul) == "목 9:00 리셋")
        #expect(Formatters.resetDay(now.addingTimeInterval(3 * 3600), now: now, timeZone: TestSupport.seoul) == "3:00 후 리셋")
        #expect(Formatters.resetDay(nil, now: now) == "리셋 시각 없음")
    }

    @Test func formatsAge() {
        #expect(Formatters.age(since: nil, now: now) == "")
        #expect(Formatters.age(since: now.addingTimeInterval(-30), now: now) == "방금")
        #expect(Formatters.age(since: now.addingTimeInterval(-18 * 60), now: now) == "18분 전")
        #expect(Formatters.age(since: now.addingTimeInterval(-3 * 3600 - 59), now: now) == "3시간 전")
        #expect(Formatters.age(since: now.addingTimeInterval(60), now: now) == "방금")
    }

    @Test func formatsPercent() {
        #expect(Formatters.percent(41.5) == "42%")
        #expect(Formatters.percent(120) == "100%")
        #expect(Formatters.percent(-3) == "0%")
    }

    @Test func formatsRowAmountAndTooltip() {
        var row = BreakdownRow()
        row.detail = "/Users/me/work/app"
        row.costUsd = 12.4
        row.input = 1_200
        row.output = 340
        row.cacheWrite = 5_000
        row.cacheRead = 2_000_000
        #expect(Formatters.amount(row) == "$12.40")
        #expect(Formatters.tooltip(row) == "/Users/me/work/app\n입력 1.2K · 출력 340\n캐시 쓰기 5.0K · 캐시 읽기 2.00M")
        row.isUnpriced = true
        #expect(Formatters.amount(row) == "2.01M 토큰")
    }
}
```

`macos/Tests/TokenViewerCoreTests/ISODateTests.swift`:

```swift
import Foundation
import Testing
@testable import TokenViewerCore

@Suite struct ISODateTests {
    @Test func parsesCommonForms() {
        let base = TestSupport.date("2026-10-05T06:50:00Z")
        #expect(ISODate.parse("2026-10-05T06:50:00Z") == base)
        #expect(ISODate.parse("2026-10-05T06:50:00.500Z") == base.addingTimeInterval(0.5))
        #expect(ISODate.parse("2026-10-05T15:50:00+09:00") == base)
    }

    @Test func cutsMicrosecondsToMilliseconds() throws {
        let parsed = try #require(ISODate.parse("2026-10-05T06:50:00.180191+00:00"))
        #expect(abs(parsed.timeIntervalSince(TestSupport.date("2026-10-05T06:50:00Z")) - 0.180) < 0.0005)
    }

    @Test func rejectsGarbage() {
        #expect(ISODate.parse("yesterday") == nil)
        #expect(ISODate.parse("") == nil)
    }

    @Test func readsJsonNumbersAndDates() {
        #expect(JSONValues.date(NSNumber(value: 1_791_183_000)) == Date(timeIntervalSince1970: 1_791_183_000))
        #expect(JSONValues.date(NSNumber(value: 1_791_183_000_000.0)) == Date(timeIntervalSince1970: 1_791_183_000))
        #expect(JSONValues.date("2026-10-05T06:50:00Z") == TestSupport.date("2026-10-05T06:50:00Z"))
        #expect(JSONValues.date(NSNumber(value: true)) == nil)
        #expect(JSONValues.number(NSNumber(value: false)) == nil)
        #expect(JSONValues.number(NSNumber(value: 42.5)) == 42.5)
        #expect(JSONValues.number("42") == nil)
    }
}
```

- [ ] **Step 3: 테스트가 실패하는지 확인한다**

Run: `cd macos && swift test --filter "FormattersTests|ISODateTests"`
Expected: 컴파일 실패 — `cannot find 'Formatters' in scope`, `cannot find 'ISODate' in scope` 등.

- [ ] **Step 4: 모델을 만든다**

`macos/Sources/TokenViewerCore/Model/UsageLimits.swift`:

```swift
import Foundation

/// One usage-limit window from the usage API. A window the API did not report is nil, not a zero.
public struct LimitWindow: Sendable, Equatable {
    public var percent: Double
    public var resetsAt: Date?

    public init(percent: Double, resetsAt: Date?) {
        self.percent = percent
        self.resetsAt = resetsAt
    }
}

public struct UsageLimits: Sendable, Equatable {
    public var fiveHour: LimitWindow?
    public var sevenDay: LimitWindow?

    public init(fiveHour: LimitWindow? = nil, sevenDay: LimitWindow? = nil) {
        self.fiveHour = fiveHour
        self.sevenDay = sevenDay
    }
}
```

`macos/Sources/TokenViewerCore/Model/FetchResult.swift`:

```swift
import Foundation

public enum FetchStatus: Sendable, Equatable {
    case ok
    case notLoggedIn
    case keychainDenied
    case tokenExpired
    case rateLimited
    case networkError
    case serverError
    case badResponse
}

public struct FetchResult: Sendable, Equatable {
    public var status: FetchStatus
    public var limits: UsageLimits
    public var planLabel: String
    public var detail: String
    public var httpStatus: Int
    /// Kept only for format-drift logging; never holds the request or its token.
    public var rawBody: Data

    public init(status: FetchStatus, limits: UsageLimits = UsageLimits(), planLabel: String = "", detail: String = "", httpStatus: Int = 0, rawBody: Data = Data()) {
        self.status = status
        self.limits = limits
        self.planLabel = planLabel
        self.detail = detail
        self.httpStatus = httpStatus
        self.rawBody = rawBody
    }
}

public struct HTTPResponse: Sendable, Equatable {
    /// 0 when no HTTP response arrived (offline, timeout).
    public var status: Int
    public var body: Data
    public var errorText: String?

    public init(status: Int, body: Data = Data(), errorText: String? = nil) {
        self.status = status
        self.body = body
        self.errorText = errorText
    }
}
```

`macos/Sources/TokenViewerCore/Model/Credential.swift`:

```swift
import Foundation

public struct Credential: Sendable, Equatable {
    public var accessToken: String
    public var expiresAt: Date?
    public var subscriptionType: String
    public var rateLimitTier: String

    public init(accessToken: String, expiresAt: Date?, subscriptionType: String, rateLimitTier: String) {
        self.accessToken = accessToken
        self.expiresAt = expiresAt
        self.subscriptionType = subscriptionType
        self.rateLimitTier = rateLimitTier
    }
}

public enum CredentialStatus: Sendable, Equatable {
    case ok
    case notFound
    case accessDenied
    case malformed
    case expired
}

public struct CredentialResult: Sendable, Equatable {
    public var status: CredentialStatus
    public var credential: Credential?

    public init(status: CredentialStatus, credential: Credential? = nil) {
        self.status = status
        self.credential = credential
    }
}

/// What the Keychain answered for Claude Code's credential item.
public enum SecretLookup: Sendable, Equatable {
    case found(Data)
    case notFound
    case denied
}
```

`macos/Sources/TokenViewerCore/Model/TokenRecord.swift`:

```swift
import Foundation

public struct TokenRecord: Sendable, Equatable {
    public var key: String
    public var timestamp: Date
    public var sessionId: String
    public var projectPath: String
    public var model: String
    public var isFast: Bool
    public var input: Int
    public var output: Int
    public var cacheWrite5m: Int
    public var cacheWrite1h: Int
    public var cacheRead: Int

    public init(
        key: String,
        timestamp: Date,
        sessionId: String = "",
        projectPath: String = "",
        model: String = "",
        isFast: Bool = false,
        input: Int = 0,
        output: Int = 0,
        cacheWrite5m: Int = 0,
        cacheWrite1h: Int = 0,
        cacheRead: Int = 0
    ) {
        self.key = key
        self.timestamp = timestamp
        self.sessionId = sessionId
        self.projectPath = projectPath
        self.model = model
        self.isFast = isFast
        self.input = input
        self.output = output
        self.cacheWrite5m = cacheWrite5m
        self.cacheWrite1h = cacheWrite1h
        self.cacheRead = cacheRead
    }
}

public struct LogSnapshot: Sendable, Equatable {
    public var records: [TokenRecord]
    public var sessionTitles: [String: String]

    public init(records: [TokenRecord] = [], sessionTitles: [String: String] = [:]) {
        self.records = records
        self.sessionTitles = sessionTitles
    }
}
```

`macos/Sources/TokenViewerCore/Model/Breakdown.swift`:

```swift
import Foundation

public enum BreakdownDimension: Int, Sendable, CaseIterable {
    case project = 0
    case model = 1
    case session = 2

    public var title: String {
        switch self {
        case .project: "프로젝트"
        case .model: "모델"
        case .session: "세션"
        }
    }
}

public enum BreakdownPeriod: Int, Sendable, CaseIterable {
    case fiveHourWindow = 0
    case today = 1
    case sevenDays = 2
    case thirtyDays = 3

    public var title: String {
        switch self {
        case .fiveHourWindow: "이번 5시간 창"
        case .today: "오늘"
        case .sevenDays: "7일"
        case .thirtyDays: "30일"
        }
    }
}

public struct BreakdownRow: Sendable, Equatable {
    public var label = ""
    /// The raw group key: project path, model id or session id. Shown in the tooltip.
    public var detail = ""
    public var costUsd = 0.0
    public var isApproximate = false
    public var isUnpriced = false
    public var input = 0
    public var output = 0
    public var cacheWrite = 0
    public var cacheRead = 0

    public init() {}

    public var totalTokens: Int { input + output + cacheWrite + cacheRead }
}

public struct Breakdown: Sendable, Equatable {
    public var rows: [BreakdownRow] = []
    public var otherCount = 0
    public var otherRow = BreakdownRow()
    public var totalUsd = 0.0

    public init() {}

    /// The top rows followed by the folded "기타 N개" row when there is one.
    public var displayRows: [BreakdownRow] { otherCount > 0 ? rows + [otherRow] : rows }
}
```

`macos/Sources/TokenViewerCore/Model/JSONValues.swift`:

```swift
import Foundation

/// ISO 8601 parsing for log timestamps and API reset times.
enum ISODate {
    // ISO8601DateFormatter is thread-safe; these are configured once and only read afterwards.
    nonisolated(unsafe) private static let fractional: ISO8601DateFormatter = {
        let formatter = ISO8601DateFormatter()
        formatter.formatOptions = [.withInternetDateTime, .withFractionalSeconds]
        return formatter
    }()
    nonisolated(unsafe) private static let whole = ISO8601DateFormatter()

    static func parse(_ text: String) -> Date? {
        if let date = fractional.date(from: text) ?? whole.date(from: text) {
            return date
        }
        // The usage API sends microseconds; cut the fraction to milliseconds and try again.
        guard let dot = text.firstIndex(of: ".") else {
            return nil
        }
        let digitsStart = text.index(after: dot)
        let digitsEnd = text[digitsStart...].firstIndex { !($0.isASCII && $0.isNumber) } ?? text.endIndex
        guard text.distance(from: digitsStart, to: digitsEnd) > 3 else {
            return nil
        }
        let millisecondsEnd = text.index(digitsStart, offsetBy: 3)
        return fractional.date(from: String(text[..<millisecondsEnd]) + String(text[digitsEnd...]))
    }
}

enum JSONValues {
    private static let millisecondEpochThreshold = 1e12

    /// A JSON number. Booleans are rejected even though Foundation bridges them to NSNumber.
    static func number(_ value: Any?) -> Double? {
        guard let number = value as? NSNumber, CFGetTypeID(number) != CFBooleanGetTypeID() else {
            return nil
        }
        return number.doubleValue
    }

    /// Epoch seconds, epoch milliseconds or an ISO 8601 string.
    static func date(_ value: Any?) -> Date? {
        if let epoch = number(value) {
            let seconds = epoch > millisecondEpochThreshold ? epoch / 1000 : epoch
            return Date(timeIntervalSince1970: seconds)
        }
        if let text = value as? String {
            return ISODate.parse(text)
        }
        return nil
    }
}
```

- [ ] **Step 5: Formatters 를 만든다**

`macos/Sources/TokenViewerCore/Render/Formatters.swift`:

```swift
import Foundation

public enum Formatters {
    private static let smallestCent = 0.005
    private static let noReset = "리셋 시각 없음"

    public static func usd(_ usd: Double, approximate: Bool) -> String {
        let prefix = approximate ? "≈" : ""
        if usd > 0 && usd < smallestCent {
            return prefix + "<$0.01"
        }
        return prefix + "$" + usd.formatted(.number.precision(.fractionLength(2)).locale(Locale(identifier: "en_US")))
    }

    public static func tokens(_ count: Int) -> String {
        if count < 1_000 {
            return String(count)
        }
        if count < 1_000_000 {
            return String(format: count < 100_000 ? "%.1fK" : "%.0fK", Double(count) / 1_000)
        }
        return String(format: count < 10_000_000 ? "%.2fM" : "%.1fM", Double(count) / 1_000_000)
    }

    public static func resetCountdown(_ resetsAt: Date?, now: Date) -> String {
        guard let resetsAt else {
            return noReset
        }
        let seconds = Int(resetsAt.timeIntervalSince(now))
        if seconds <= 60 {
            return "곧 리셋"
        }
        let hours = seconds / 3600
        let minutes = (seconds % 3600) / 60
        if hours > 0 {
            return String(format: "%d:%02d 후 리셋", hours, minutes)
        }
        return "\(minutes)분 후 리셋"
    }

    public static func resetDay(_ resetsAt: Date?, now: Date, timeZone: TimeZone = .current) -> String {
        guard let resetsAt else {
            return noReset
        }
        if resetsAt.timeIntervalSince(now) < 86_400 {
            return resetCountdown(resetsAt, now: now)
        }
        let formatter = DateFormatter()
        formatter.locale = Locale(identifier: "ko_KR")
        formatter.timeZone = timeZone
        formatter.dateFormat = "E H:mm"
        return formatter.string(from: resetsAt) + " 리셋"
    }

    public static func age(since past: Date?, now: Date) -> String {
        guard let past else {
            return ""
        }
        let seconds = max(0, Int(now.timeIntervalSince(past)))
        if seconds < 60 {
            return "방금"
        }
        if seconds < 3600 {
            return "\(seconds / 60)분 전"
        }
        return "\(seconds / 3600)시간 전"
    }

    public static func percent(_ percent: Double) -> String {
        "\(Int(min(max(percent, 0), 100).rounded()))%"
    }

    /// The amount column of a breakdown row: dollars, or tokens when no price is known.
    public static func amount(_ row: BreakdownRow) -> String {
        row.isUnpriced ? "\(tokens(row.totalTokens)) 토큰" : usd(row.costUsd, approximate: row.isApproximate)
    }

    public static func tooltip(_ row: BreakdownRow) -> String {
        "\(row.detail)\n입력 \(tokens(row.input)) · 출력 \(tokens(row.output))\n캐시 쓰기 \(tokens(row.cacheWrite)) · 캐시 읽기 \(tokens(row.cacheRead))"
    }
}
```

- [ ] **Step 6: 테스트가 통과하는지 확인한다**

Run: `cd macos && swift test --filter "FormattersTests|ISODateTests"`
Expected: `Test run with 11 tests passed`.

- [ ] **Step 7: 커밋**

```bash
git add macos/Package.swift macos/Sources/TokenViewerCore macos/Tests
git commit -m "feat: add Swift models, date parsing and formatters

Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>"
```

---

### Task 4: PriceTable

**Files:**
- Create: `macos/Sources/TokenViewerCore/Analysis/PriceTable.swift`, `macos/Tests/TokenViewerCoreTests/PriceTableTests.swift`
- Modify: `macos/Tests/TokenViewerCoreTests/TestSupport.swift` (`prices()` 추가)

**Interfaces:**
- Consumes: `TokenRecord` (Task 3)
- Produces: `struct ModelPrice`, `enum PriceMatch { exact, family, none }`, `struct CostResult { usd, isApproximate, isUnpriced }`, `struct PriceTable { init?(json: Data); asOf; models; findPrice(for:) -> (match: PriceMatch, price: ModelPrice?); cost(of: TokenRecord) -> CostResult }`

- [ ] **Step 1: TestSupport 에 prices() 를 넣고 실패하는 테스트를 쓴다**

`TestSupport` enum 안, `seoul` 아래에 다음을 넣는다:

```swift
    static func prices() throws -> PriceTable {
        let data = try Data(contentsOf: repoRoot.appendingPathComponent("shared/prices.json"))
        return try #require(PriceTable(json: data))
    }
```

`macos/Tests/TokenViewerCoreTests/PriceTableTests.swift`:

```swift
import Foundation
import Testing
@testable import TokenViewerCore

@Suite struct PriceTableTests {
    private func record(_ model: String, fast: Bool = false) -> TokenRecord {
        TokenRecord(key: "k", timestamp: Date(timeIntervalSince1970: 0), model: model, isFast: fast,
                    input: 1_000_000, output: 100_000, cacheWrite5m: 200_000, cacheWrite1h: 50_000, cacheRead: 2_000_000)
    }

    @Test func loadsSharedPrices() throws {
        let prices = try TestSupport.prices()
        #expect(prices.models.count == 7)
        #expect(prices.asOf == "2026-09-25")
    }

    @Test func findsExactModel() throws {
        let quote = try TestSupport.prices().findPrice(for: "claude-opus-5-5")
        #expect(quote.match == .exact)
        #expect(quote.price?.input == 4)
        #expect(quote.price?.cacheRead == 0.2)
    }

    @Test func findsDatedModel() throws {
        let quote = try TestSupport.prices().findPrice(for: "claude-haiku-4-5-20251001")
        #expect(quote.match == .exact)
        #expect(quote.price?.output == 5)
    }

    @Test func fallsBackToFamily() throws {
        let prices = try TestSupport.prices()
        #expect(prices.findPrice(for: "claude-opus-6").match == .family)
        #expect(prices.findPrice(for: "claude-opus-6").price?.id == "claude-opus-5-5")
        // "claude-opus-5" is a prefix of this id, but the rest is not a date suffix.
        #expect(prices.findPrice(for: "claude-opus-5-7").match == .family)
    }

    @Test func unknownFamilyIsUnpriced() throws {
        let prices = try TestSupport.prices()
        #expect(prices.findPrice(for: "gpt-5").match == .none)
        #expect(prices.cost(of: record("gpt-5")) == CostResult(isUnpriced: true))
    }

    @Test func calculatesCost() throws {
        // 1M×4 + 0.1M×20 + 0.2M×5 + 0.05M×8 + 2M×0.2 = 7.8
        let cost = try TestSupport.prices().cost(of: record("claude-opus-5-5"))
        #expect(abs(cost.usd - 7.8) < 1e-9)
        #expect(!cost.isApproximate)
        #expect(!cost.isUnpriced)
    }

    @Test func doublesFastOpus() throws {
        let cost = try TestSupport.prices().cost(of: record("claude-opus-5-5", fast: true))
        #expect(abs(cost.usd - 15.6) < 1e-9)
    }

    @Test func ignoresFastWithoutMultiplier() throws {
        // 1M×2 + 0.1M×10 + 0.2M×2.5 + 0.05M×4 + 2M×0.2 = 4.1
        let cost = try TestSupport.prices().cost(of: record("claude-sonnet-5-5", fast: true))
        #expect(abs(cost.usd - 4.1) < 1e-9)
    }

    @Test func marksFamilyCostApproximate() throws {
        let cost = try TestSupport.prices().cost(of: record("claude-opus-6"))
        #expect(cost.isApproximate)
        #expect(abs(cost.usd - 7.8) < 1e-9)
    }

    @Test func rejectsBrokenJson() {
        #expect(PriceTable(json: Data("{".utf8)) == nil)
        #expect(PriceTable(json: Data(#"{"models": []}"#.utf8)) == nil)
        #expect(PriceTable(json: Data(#"{"models": [{"family": "opus"}]}"#.utf8)) == nil)
    }

    @Test func defaultsMissingFastMultiplierToOne() {
        let prices = PriceTable(json: Data(#"{"models": [{"id": "claude-x-1", "family": "x", "input": 1}]}"#.utf8))
        #expect(prices?.models.first?.fastMultiplier == 1)
        #expect(prices?.models.first?.output == 0)
    }
}
```

- [ ] **Step 2: 테스트가 실패하는지 확인한다**

Run: `cd macos && swift test --filter PriceTableTests`
Expected: 컴파일 실패 — `cannot find type 'PriceTable' in scope`.

- [ ] **Step 3: PriceTable 을 만든다**

`macos/Sources/TokenViewerCore/Analysis/PriceTable.swift`:

```swift
import Foundation

public struct ModelPrice: Sendable, Equatable {
    public var id: String
    public var family: String
    public var input: Double
    public var output: Double
    public var cacheWrite5m: Double
    public var cacheWrite1h: Double
    public var cacheRead: Double
    public var fastMultiplier: Double
}

public enum PriceMatch: Sendable, Equatable {
    case exact
    case family
    case none
}

public struct CostResult: Sendable, Equatable {
    public var usd: Double
    public var isApproximate: Bool
    public var isUnpriced: Bool

    public init(usd: Double = 0, isApproximate: Bool = false, isUnpriced: Bool = false) {
        self.usd = usd
        self.isApproximate = isApproximate
        self.isUnpriced = isUnpriced
    }
}

/// USD per 1M tokens from `shared/prices.json` (v0.1.0 spec 5.5).
public struct PriceTable: Sendable {
    private static let tokensPerPriceUnit = 1_000_000.0

    public let asOf: String
    public let models: [ModelPrice]

    /// Nil when the JSON has no model with an id.
    public init?(json: Data) {
        guard let root = (try? JSONSerialization.jsonObject(with: json)) as? [String: Any],
              let entries = root["models"] as? [[String: Any]] else {
            return nil
        }
        let models = entries.compactMap { entry -> ModelPrice? in
            guard let id = entry["id"] as? String, !id.isEmpty else {
                return nil
            }
            func price(_ key: String, default fallback: Double = 0) -> Double {
                JSONValues.number(entry[key]) ?? fallback
            }
            return ModelPrice(id: id, family: entry["family"] as? String ?? "",
                              input: price("input"), output: price("output"),
                              cacheWrite5m: price("cacheWrite5m"), cacheWrite1h: price("cacheWrite1h"),
                              cacheRead: price("cacheRead"), fastMultiplier: price("fastMultiplier", default: 1))
        }
        guard !models.isEmpty else {
            return nil
        }
        self.models = models
        asOf = root["asOf"] as? String ?? ""
    }

    public func findPrice(for modelId: String) -> (match: PriceMatch, price: ModelPrice?) {
        for price in models where modelId == price.id || Self.isDatedVariant(modelId, of: price.id) {
            return (.exact, price)
        }
        let parts = modelId.split(separator: "-").map(String.init)
        for price in models where !price.family.isEmpty && parts.contains(price.family) {
            return (.family, price)
        }
        return (.none, nil)
    }

    public func cost(of record: TokenRecord) -> CostResult {
        let quote = findPrice(for: record.model)
        guard let price = quote.price else {
            return CostResult(isUnpriced: true)
        }
        let weighted = Double(record.input) * price.input
            + Double(record.output) * price.output
            + Double(record.cacheWrite5m) * price.cacheWrite5m
            + Double(record.cacheWrite1h) * price.cacheWrite1h
            + Double(record.cacheRead) * price.cacheRead
        var usd = weighted / Self.tokensPerPriceUnit
        if record.isFast {
            usd *= price.fastMultiplier
        }
        return CostResult(usd: usd, isApproximate: quote.match == .family)
    }

    /// `claude-haiku-4-5-20251001` is the dated release of `claude-haiku-4-5`.
    private static func isDatedVariant(_ modelId: String, of id: String) -> Bool {
        guard modelId.hasPrefix(id) else {
            return false
        }
        let suffix = modelId.dropFirst(id.count)
        return suffix.count == 9 && suffix.first == "-" && suffix.dropFirst().allSatisfy { $0.isASCII && $0.isNumber }
    }
}
```

- [ ] **Step 4: 테스트가 통과하는지 확인한다**

Run: `cd macos && swift test --filter PriceTableTests`
Expected: 11 tests passed.

- [ ] **Step 5: 커밋**

```bash
git add macos/Sources/TokenViewerCore/Analysis/PriceTable.swift macos/Tests/TokenViewerCoreTests
git commit -m "feat: price token usage from the shared price table

Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>"
```

---

### Task 5: LogParser

**Files:**
- Create: `macos/Sources/TokenViewerCore/Analysis/LogParser.swift`, `macos/Tests/TokenViewerCoreTests/LogParserTests.swift`

**Interfaces:**
- Consumes: `TokenRecord`, `ISODate` (Task 3)
- Produces: `enum ParsedLine { ignored, usage(TokenRecord), title(sessionId: String, title: String) }`, `enum LogParser { static func parse(_ line: Data, fallbackSessionId: String) -> ParsedLine; static func sessionId(fromPath: String) -> String }`

- [ ] **Step 1: 실패하는 테스트를 쓴다**

`macos/Tests/TokenViewerCoreTests/LogParserTests.swift`:

```swift
import Foundation
import Testing
@testable import TokenViewerCore

@Suite struct LogParserTests {
    private func parse(_ line: String, fallback: String = "fallback") -> ParsedLine {
        LogParser.parse(Data(line.utf8), fallbackSessionId: fallback)
    }

    private func usage(_ line: String, fallback: String = "fallback") throws -> TokenRecord {
        guard case .usage(let record) = parse(line, fallback: fallback) else {
            Issue.record("expected a usage line: \(line)")
            throw CancellationError()
        }
        return record
    }

    @Test func parsesAssistantUsage() throws {
        let record = try usage(LogLines.assistant(id: "msg-1", requestId: "req-9", input: 10, output: 20, cacheRead: 30, cacheCreation: 40))
        #expect(record.key == "msg-1|req-9")
        #expect(record.timestamp == TestSupport.date("2026-10-05T06:00:00Z"))
        #expect(record.sessionId == "session-1")
        #expect(record.projectPath == "/Users/me/work/app")
        #expect(record.model == "claude-opus-5-5")
        #expect(record.input == 10)
        #expect(record.output == 20)
        #expect(record.cacheRead == 30)
        #expect(record.cacheWrite5m == 40)
        #expect(record.cacheWrite1h == 0)
        #expect(!record.isFast)
    }

    @Test func splitsCacheWritesByLifetime() throws {
        let record = try usage(LogLines.assistant(id: "m", cacheCreation: 1_000, split: (fiveMinutes: 300, oneHour: 700)))
        #expect(record.cacheWrite5m == 300)
        #expect(record.cacheWrite1h == 700)
    }

    @Test func marksFastSpeed() throws {
        #expect(try usage(LogLines.assistant(id: "m", speed: "fast")).isFast)
    }

    @Test func keyWithoutRequestId() throws {
        #expect(try usage(LogLines.assistant(id: "msg-1", requestId: nil)).key == "msg-1")
    }

    @Test func usesFallbackSessionId() throws {
        #expect(try usage(LogLines.assistant(id: "m", sessionId: nil), fallback: "from-path").sessionId == "from-path")
    }

    @Test func ignoresSyntheticModel() {
        #expect(parse(LogLines.assistant(id: "m", model: "<synthetic>")) == .ignored)
    }

    @Test func ignoresNonAssistantLines() {
        #expect(parse(LogLines.json(["type": "user", "message": ["usage": ["input_tokens": 1]]])) == .ignored)
        #expect(parse(LogLines.json(["type": "assistant", "message": ["model": "claude-opus-5-5"]])) == .ignored)
    }

    @Test func ignoresBrokenJson() {
        #expect(parse(#"{"type": "assistant", "usage": "#) == .ignored)
        #expect(parse("") == .ignored)
    }

    @Test func parsesAiTitle() {
        #expect(parse(LogLines.title(sessionId: "s-7", title: "  로그 파서 고치기 ")) == .title(sessionId: "s-7", title: "로그 파서 고치기"))
        #expect(parse(LogLines.title(sessionId: "s-7", title: "   ")) == .ignored)
    }

    @Test func keepsNonAsciiNames() throws {
        let record = try usage(LogLines.assistant(id: "m", cwd: "/Users/me/작업/앱 🚀"))
        #expect(record.projectPath == "/Users/me/작업/앱 🚀")
    }

    @Test func findsSessionIdFromPath() {
        #expect(LogParser.sessionId(fromPath: "/p/-Users-me-app/abc-123.jsonl") == "abc-123")
        #expect(LogParser.sessionId(fromPath: "/p/-Users-me-app/abc-123/subagents/agent-1.jsonl") == "abc-123")
    }
}
```

- [ ] **Step 2: 테스트가 실패하는지 확인한다**

Run: `cd macos && swift test --filter LogParserTests`
Expected: 컴파일 실패 — `cannot find type 'ParsedLine' in scope`.

- [ ] **Step 3: LogParser 를 만든다**

`macos/Sources/TokenViewerCore/Analysis/LogParser.swift`:

```swift
import Foundation

public enum ParsedLine: Sendable, Equatable {
    case ignored
    case usage(TokenRecord)
    case title(sessionId: String, title: String)
}

/// Reads one Claude Code session log line (v0.1.0 spec 5.1, 5.2).
public enum LogParser {
    private static let usageMarker = Data("\"usage\"".utf8)
    private static let titleMarker = Data("\"ai-title\"".utf8)
    private static let syntheticModel = "<synthetic>"
    private static let subagentDirectory = "subagents"

    public static func parse(_ line: Data, fallbackSessionId: String) -> ParsedLine {
        // Most lines carry neither field; skip them before paying for a JSON parse.
        guard line.range(of: usageMarker) != nil || line.range(of: titleMarker) != nil,
              let object = (try? JSONSerialization.jsonObject(with: line)) as? [String: Any] else {
            return .ignored
        }
        let type = object["type"] as? String ?? ""
        var sessionId = object["sessionId"] as? String ?? ""
        if sessionId.isEmpty {
            sessionId = fallbackSessionId
        }

        if type == "ai-title" {
            let title = (object["aiTitle"] as? String ?? "").trimmingCharacters(in: .whitespacesAndNewlines)
            guard !title.isEmpty, !sessionId.isEmpty else {
                return .ignored
            }
            return .title(sessionId: sessionId, title: title)
        }

        guard type == "assistant",
              let message = object["message"] as? [String: Any],
              let usage = message["usage"] as? [String: Any], !usage.isEmpty,
              let model = message["model"] as? String, !model.isEmpty, model != syntheticModel,
              let timestamp = (object["timestamp"] as? String).flatMap(ISODate.parse) else {
            return .ignored
        }

        // Streaming writes the same response 2-5 times; message id + request id identifies it.
        var key = message["id"] as? String ?? ""
        if key.isEmpty {
            key = object["uuid"] as? String ?? ""
        }
        guard !key.isEmpty else {
            return .ignored
        }
        if let requestId = object["requestId"] as? String, !requestId.isEmpty {
            key += "|" + requestId
        }

        var record = TokenRecord(key: key, timestamp: timestamp, sessionId: sessionId,
                                 projectPath: object["cwd"] as? String ?? "", model: model)
        record.isFast = usage["speed"] as? String == "fast"
        record.input = count(usage, "input_tokens")
        record.output = count(usage, "output_tokens")
        record.cacheRead = count(usage, "cache_read_input_tokens")
        let creation = usage["cache_creation"] as? [String: Any] ?? [:]
        if creation["ephemeral_5m_input_tokens"] != nil || creation["ephemeral_1h_input_tokens"] != nil {
            record.cacheWrite5m = count(creation, "ephemeral_5m_input_tokens")
            record.cacheWrite1h = count(creation, "ephemeral_1h_input_tokens")
        } else {
            record.cacheWrite5m = count(usage, "cache_creation_input_tokens")
        }
        return .usage(record)
    }

    /// Subagent logs live in `<sessionId>/subagents/agent-*.jsonl` and count toward that parent session.
    public static func sessionId(fromPath path: String) -> String {
        let file = URL(fileURLWithPath: path)
        let parent = file.deletingLastPathComponent()
        if parent.lastPathComponent == subagentDirectory {
            return parent.deletingLastPathComponent().lastPathComponent
        }
        return file.deletingPathExtension().lastPathComponent
    }

    private static func count(_ object: [String: Any], _ key: String) -> Int {
        Int(JSONValues.number(object[key]) ?? 0)
    }
}
```

- [ ] **Step 4: 테스트가 통과하는지 확인한다**

Run: `cd macos && swift test --filter LogParserTests`
Expected: 11 tests passed.

- [ ] **Step 5: 커밋**

```bash
git add macos/Sources/TokenViewerCore/Analysis/LogParser.swift macos/Tests/TokenViewerCoreTests/LogParserTests.swift
git commit -m "feat: parse Claude Code session log lines in Swift

Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>"
```

---

### Task 6: LogStore (증분 읽기)

**Files:**
- Create: `macos/Sources/TokenViewerCore/Analysis/LogStore.swift`, `macos/Tests/TokenViewerCoreTests/LogStoreTests.swift`

**Interfaces:**
- Consumes: `LogParser`, `TokenRecord`, `LogSnapshot`
- Produces: `final class LogStore { init(chunkBytes: Int = LogStore.defaultChunkBytes); static let retentionDays = 31; var recordCount: Int; var snapshot: LogSnapshot; @discardableResult func scan(root: URL, now: Date, shouldContinue: () -> Bool = { true }) -> Bool }`

- [ ] **Step 1: 실패하는 테스트를 쓴다**

`macos/Tests/TokenViewerCoreTests/LogStoreTests.swift`:

```swift
import Foundation
import Testing
@testable import TokenViewerCore

@Suite struct LogStoreTests {
    private let now = TestSupport.date("2026-10-05T12:00:00Z")

    @Test func readsMainAndSubagentFiles() throws {
        let dir = try TemporaryDirectory()
        try dir.write("-Users-me-app/s1.jsonl", [LogLines.assistant(id: "a", sessionId: nil)])
        try dir.write("-Users-me-app/s1/subagents/agent-1.jsonl", [LogLines.assistant(id: "b", sessionId: nil)])
        let store = LogStore()
        #expect(store.scan(root: dir.url, now: now))
        #expect(store.recordCount == 2)
        #expect(Set(store.snapshot.records.map(\.sessionId)) == ["s1"])
    }

    @Test func appendsOnlyNewLines() throws {
        let dir = try TemporaryDirectory()
        try dir.write("p/s.jsonl", [LogLines.assistant(id: "a")])
        let store = LogStore()
        store.scan(root: dir.url, now: now)
        try dir.append("p/s.jsonl", LogLines.assistant(id: "b") + "\n")
        #expect(store.scan(root: dir.url, now: now))
        #expect(store.recordCount == 2)
        #expect(!store.scan(root: dir.url, now: now))
    }

    @Test func keepsLastCopyOfDuplicates() throws {
        let dir = try TemporaryDirectory()
        try dir.write("p/s.jsonl", [LogLines.assistant(id: "a", output: 1), LogLines.assistant(id: "a", output: 99)])
        let store = LogStore()
        store.scan(root: dir.url, now: now)
        #expect(store.recordCount == 1)
        #expect(store.snapshot.records.first?.output == 99)
    }

    @Test func waitsForPartialLine() throws {
        let dir = try TemporaryDirectory()
        try dir.write("p/s.jsonl", [LogLines.assistant(id: "a"), LogLines.assistant(id: "b")], terminated: false)
        let store = LogStore()
        store.scan(root: dir.url, now: now)
        #expect(store.recordCount == 1)
        try dir.append("p/s.jsonl", "\n")
        store.scan(root: dir.url, now: now)
        #expect(store.recordCount == 2)
    }

    @Test func readsLinesAcrossChunkBoundaries() throws {
        let dir = try TemporaryDirectory()
        try dir.write("p/s.jsonl", [LogLines.assistant(id: "a"), LogLines.assistant(id: "b"), LogLines.assistant(id: "c")])
        let store = LogStore(chunkBytes: 16)
        store.scan(root: dir.url, now: now)
        #expect(Set(store.snapshot.records.map(\.key)) == ["a|req-1", "b|req-1", "c|req-1"])
    }

    @Test func rereadsTruncatedFile() throws {
        let dir = try TemporaryDirectory()
        try dir.write("p/s.jsonl", [LogLines.assistant(id: "a"), LogLines.assistant(id: "b")])
        let store = LogStore()
        store.scan(root: dir.url, now: now)
        try dir.write("p/s.jsonl", [LogLines.assistant(id: "c")])
        #expect(store.scan(root: dir.url, now: now))
        #expect(store.snapshot.records.map(\.key) == ["c|req-1"])
    }

    @Test func dropsDeletedFile() throws {
        let dir = try TemporaryDirectory()
        try dir.write("p/one.jsonl", [LogLines.assistant(id: "a")])
        let deleted = try dir.write("p/two.jsonl", [LogLines.assistant(id: "b")])
        let store = LogStore()
        store.scan(root: dir.url, now: now)
        try FileManager.default.removeItem(at: deleted)
        #expect(store.scan(root: dir.url, now: now))
        #expect(store.snapshot.records.map(\.key) == ["a|req-1"])
    }

    @Test func skipsFilesOlderThanRetention() throws {
        let dir = try TemporaryDirectory()
        let file = try dir.write("p/old.jsonl", [LogLines.assistant(id: "a")])
        try FileManager.default.setAttributes([.modificationDate: now.addingTimeInterval(-40 * 86_400)], ofItemAtPath: file.path)
        let store = LogStore()
        #expect(!store.scan(root: dir.url, now: now))
        #expect(store.recordCount == 0)
    }

    @Test func prunesOldRecords() throws {
        let dir = try TemporaryDirectory()
        let old = TestSupport.isoString(now.addingTimeInterval(-32 * 86_400))
        try dir.write("p/s.jsonl", [LogLines.assistant(id: "old", timestamp: old), LogLines.assistant(id: "new")])
        let store = LogStore()
        store.scan(root: dir.url, now: now)
        #expect(store.snapshot.records.map(\.key) == ["new|req-1"])
    }

    @Test func missingRootIsEmpty() throws {
        let dir = try TemporaryDirectory()
        let store = LogStore()
        #expect(!store.scan(root: dir.url.appendingPathComponent("missing"), now: now))
        #expect(store.recordCount == 0)
    }

    @Test func keepsSessionTitles() throws {
        let dir = try TemporaryDirectory()
        try dir.write("p/s1.jsonl", [LogLines.title(sessionId: "s1", title: "로그 파서")])
        let store = LogStore()
        store.scan(root: dir.url, now: now)
        #expect(store.snapshot.sessionTitles == ["s1": "로그 파서"])
    }

    @Test func stopsWhenAskedTo() throws {
        let dir = try TemporaryDirectory()
        try dir.write("p/s.jsonl", [LogLines.assistant(id: "a")])
        let store = LogStore()
        #expect(!store.scan(root: dir.url, now: now, shouldContinue: { false }))
        #expect(store.recordCount == 0)
    }
}
```

- [ ] **Step 2: 테스트가 실패하는지 확인한다**

Run: `cd macos && swift test --filter LogStoreTests`
Expected: 컴파일 실패 — `cannot find 'LogStore' in scope`.

- [ ] **Step 3: LogStore 를 만든다**

`macos/Sources/TokenViewerCore/Analysis/LogStore.swift`:

```swift
import Foundation

/// Remembers how far each session log was read so a rescan parses only the lines added since (v0.1.0 spec 7).
public final class LogStore {
    public static let defaultChunkBytes = 4 * 1024 * 1024
    public static let retentionDays = 31

    private struct FileCursor {
        var offset: UInt64 = 0
        var keys: Set<String> = []
    }

    private static let resourceKeys: [URLResourceKey] = [.isRegularFileKey, .fileSizeKey, .contentModificationDateKey]
    private static let newline = UInt8(ascii: "\n")

    private let chunkBytes: Int
    private var cursors: [String: FileCursor] = [:]
    private var records: [String: TokenRecord] = [:]
    private var sessionTitles: [String: String] = [:]

    public init(chunkBytes: Int = LogStore.defaultChunkBytes) {
        self.chunkBytes = chunkBytes
    }

    public var recordCount: Int { records.count }

    public var snapshot: LogSnapshot { LogSnapshot(records: Array(records.values), sessionTitles: sessionTitles) }

    /// Reads new lines under `root`. Returns true when the snapshot changed.
    @discardableResult
    public func scan(root: URL, now: Date, shouldContinue: () -> Bool = { true }) -> Bool {
        var changed = false
        let cutoff = now.addingTimeInterval(-Double(Self.retentionDays) * 86_400)
        var seen = Set<String>()
        let enumerator = FileManager.default.enumerator(at: root, includingPropertiesForKeys: Self.resourceKeys)
        while let url = enumerator?.nextObject() as? URL {
            guard shouldContinue() else {
                return changed
            }
            guard url.pathExtension == "jsonl",
                  let values = try? url.resourceValues(forKeys: Set(Self.resourceKeys)),
                  values.isRegularFile == true else {
                continue
            }
            let path = url.path
            seen.insert(path)
            let size = UInt64(values.fileSize ?? 0)

            var cursor: FileCursor
            if let existing = cursors[path] {
                cursor = existing
            } else {
                // Claude Code deletes transcripts after 30 days; skip anything older on first sight.
                if let modified = values.contentModificationDate, modified < cutoff {
                    continue
                }
                cursor = FileCursor()
            }
            if size < cursor.offset {
                // Truncated or replaced: drop what this file contributed and read it again.
                for key in cursor.keys {
                    records.removeValue(forKey: key)
                }
                cursor = FileCursor()
                changed = true
            }
            if size > cursor.offset {
                changed = read(path: path, cursor: &cursor) || changed
            }
            cursors[path] = cursor
        }

        for path in cursors.keys where !seen.contains(path) {
            forget(path)
            changed = true
        }
        return prune(cutoff: cutoff) || changed
    }

    private func read(path: String, cursor: inout FileCursor) -> Bool {
        guard let handle = FileHandle(forReadingAtPath: path) else {
            return false
        }
        defer { try? handle.close() }
        do {
            try handle.seek(toOffset: cursor.offset)
        } catch {
            return false
        }

        let fallbackSessionId = LogParser.sessionId(fromPath: path)
        var changed = false
        var pending = Data()
        while let chunk = try? handle.read(upToCount: chunkBytes), !chunk.isEmpty {
            pending.append(chunk)
            var lineStart = pending.startIndex
            while let newline = pending[lineStart...].firstIndex(of: Self.newline) {
                switch LogParser.parse(pending[lineStart..<newline], fallbackSessionId: fallbackSessionId) {
                case .usage(let record):
                    // Later copies of the same response replace earlier ones.
                    records[record.key] = record
                    cursor.keys.insert(record.key)
                    changed = true
                case .title(let sessionId, let title):
                    sessionTitles[sessionId] = title
                    changed = true
                case .ignored:
                    break
                }
                cursor.offset += UInt64(newline - lineStart + 1)
                lineStart = pending.index(after: newline)
            }
            // Keep the unfinished last line; the offset stays before it until Claude Code finishes writing it.
            pending = Data(pending[lineStart...])
        }
        return changed
    }

    private func forget(_ path: String) {
        guard let cursor = cursors.removeValue(forKey: path) else {
            return
        }
        for key in cursor.keys {
            records.removeValue(forKey: key)
        }
    }

    private func prune(cutoff: Date) -> Bool {
        let before = records.count
        records = records.filter { $0.value.timestamp >= cutoff }
        return records.count != before
    }
}
```

- [ ] **Step 4: 테스트가 통과하는지 확인한다**

Run: `cd macos && swift test --filter LogStoreTests`
Expected: 12 tests passed.

- [ ] **Step 5: 커밋**

```bash
git add macos/Sources/TokenViewerCore/Analysis/LogStore.swift macos/Tests/TokenViewerCoreTests/LogStoreTests.swift
git commit -m "feat: scan session logs incrementally in Swift

Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>"
```

---

### Task 7: TokenAggregator

**Files:**
- Create: `macos/Sources/TokenViewerCore/Analysis/TokenAggregator.swift`, `macos/Tests/TokenViewerCoreTests/TokenAggregatorTests.swift`

**Interfaces:**
- Consumes: `LogSnapshot`, `PriceTable`, `CostResult`, `Breakdown*` (Task 3–4)
- Produces: `enum TokenAggregator { static let defaultTopCount = 5; static func aggregate(_ snapshot: LogSnapshot, prices: PriceTable?, dimension: BreakdownDimension, from start: Date?, topCount: Int = 5, timeZone: TimeZone = .current) -> Breakdown; static func periodStart(_ period: BreakdownPeriod, now: Date, fiveHourResetsAt: Date?, calendar: Calendar = .current) -> Date; static func modelDisplayName(_ modelId: String) -> String }`

- [ ] **Step 1: 실패하는 테스트를 쓴다**

`macos/Tests/TokenViewerCoreTests/TokenAggregatorTests.swift`:

```swift
import Foundation
import Testing
@testable import TokenViewerCore

@Suite struct TokenAggregatorTests {
    private let now = TestSupport.date("2026-10-05T12:00:00Z")

    private func record(_ key: String, project: String = "/w/app", model: String = "claude-opus-5-5", session: String = "s1",
                        at time: String = "2026-10-05T10:00:00Z", input: Int = 1_000_000) -> TokenRecord {
        TokenRecord(key: key, timestamp: TestSupport.date(time), sessionId: session, projectPath: project, model: model, input: input)
    }

    private func aggregate(_ records: [TokenRecord], titles: [String: String] = [:], by dimension: BreakdownDimension,
                           from start: Date? = nil) throws -> Breakdown {
        TokenAggregator.aggregate(LogSnapshot(records: records, sessionTitles: titles), prices: try TestSupport.prices(),
                                  dimension: dimension, from: start, timeZone: TestSupport.seoul)
    }

    @Test func groupsByProjectAndSortsByCost() throws {
        let breakdown = try aggregate([
            record("a", project: "/w/small", input: 1_000_000),
            record("b", project: "/w/big", input: 3_000_000),
            record("c", project: "/w/big", input: 1_000_000),
        ], by: .project)
        #expect(breakdown.rows.map(\.label) == ["big", "small"])
        #expect(breakdown.rows[0].detail == "/w/big")
        #expect(abs(breakdown.rows[0].costUsd - 16) < 1e-9)
        #expect(breakdown.rows[0].input == 4_000_000)
        #expect(abs(breakdown.totalUsd - 20) < 1e-9)
        #expect(breakdown.otherCount == 0)
    }

    @Test func groupsByModelWithDisplayNames() throws {
        let breakdown = try aggregate([
            record("a", model: "claude-opus-5-5"),
            record("b", model: "claude-haiku-4-5-20251001"),
        ], by: .model)
        #expect(breakdown.rows.map(\.label) == ["Opus 5.5", "Haiku 4.5"])
    }

    @Test func disambiguatesSameProjectNames() throws {
        let breakdown = try aggregate([record("a", project: "/a/x/app"), record("b", project: "/b/y/app")], by: .project)
        #expect(Set(breakdown.rows.map(\.label)) == ["x/app", "y/app"])
    }

    @Test func keepsNonAsciiProjectNames() throws {
        let breakdown = try aggregate([record("a", project: "/Users/me/작업/앱")], by: .project)
        #expect(breakdown.rows.map(\.label) == ["앱"])
    }

    @Test func sessionUsesTitleOrFallback() throws {
        let breakdown = try aggregate([
            record("a", session: "s1"),
            record("b", project: "/w/web", session: "s2", at: "2026-10-05T06:27:00Z"),
        ], titles: ["s1": "로그 파서"], by: .session)
        // 06:27Z is 15:27 in Seoul.
        #expect(Set(breakdown.rows.map(\.label)) == ["로그 파서", "web · 10/5 15:27"])
    }

    @Test func foldsBeyondTopCountIntoOther() throws {
        let records = (1...7).map { record("k\($0)", project: "/w/p\($0)", input: (8 - $0) * 1_000_000) }
        let breakdown = try aggregate(records, by: .project)
        #expect(breakdown.rows.map(\.label) == ["p1", "p2", "p3", "p4", "p5"])
        #expect(breakdown.otherCount == 2)
        #expect(breakdown.otherRow.label == "기타 2개")
        // p6 and p7 hold 2M and 1M input tokens at $4 per 1M.
        #expect(abs(breakdown.otherRow.costUsd - 12) < 1e-9)
        #expect(abs(breakdown.totalUsd - 112) < 1e-9)
        #expect(breakdown.displayRows.count == 6)
    }

    @Test func marksApproximateAndUnpriced() throws {
        let breakdown = try aggregate([
            record("a", project: "/w/family", model: "claude-opus-6"),
            record("b", project: "/w/unknown", model: "gpt-5"),
            record("c", project: "/w/mixed", model: "claude-opus-5-5"),
            record("d", project: "/w/mixed", model: "gpt-5"),
        ], by: .project)
        let rows = Dictionary(uniqueKeysWithValues: breakdown.rows.map { ($0.label, $0) })
        #expect(rows["family"]?.isApproximate == true)
        #expect(rows["unknown"]?.isUnpriced == true)
        #expect(rows["unknown"]?.costUsd == 0)
        #expect(rows["mixed"]?.isApproximate == true)
        #expect(rows["mixed"]?.isUnpriced == false)
    }

    @Test func filtersByPeriodStart() throws {
        let breakdown = try aggregate([
            record("early", project: "/w/early", at: "2026-10-05T08:00:00Z"),
            record("late", project: "/w/late", at: "2026-10-05T11:00:00Z"),
        ], by: .project, from: TestSupport.date("2026-10-05T10:00:00Z"))
        #expect(breakdown.rows.map(\.label) == ["late"])
    }

    @Test func calculatesFiveHourWindowStart() {
        let resets = now.addingTimeInterval(2 * 3600)
        #expect(TokenAggregator.periodStart(.fiveHourWindow, now: now, fiveHourResetsAt: resets) == resets.addingTimeInterval(-5 * 3600))
        // A reset time that already passed belongs to the previous window.
        #expect(TokenAggregator.periodStart(.fiveHourWindow, now: now, fiveHourResetsAt: now.addingTimeInterval(-60)) == now.addingTimeInterval(-5 * 3600))
        #expect(TokenAggregator.periodStart(.fiveHourWindow, now: now, fiveHourResetsAt: nil) == now.addingTimeInterval(-5 * 3600))
    }

    @Test func calculatesTodayFromLocalMidnight() {
        var calendar = Calendar(identifier: .gregorian)
        calendar.timeZone = TestSupport.seoul
        // 12:00Z is 21:00 in Seoul, so the local day began at 15:00Z the day before.
        #expect(TokenAggregator.periodStart(.today, now: now, fiveHourResetsAt: nil, calendar: calendar) == TestSupport.date("2026-10-04T15:00:00Z"))
    }

    @Test func calculatesDayPeriods() {
        #expect(TokenAggregator.periodStart(.sevenDays, now: now, fiveHourResetsAt: nil) == now.addingTimeInterval(-7 * 86_400))
        #expect(TokenAggregator.periodStart(.thirtyDays, now: now, fiveHourResetsAt: nil) == now.addingTimeInterval(-30 * 86_400))
    }

    @Test func formatsModelNames() {
        #expect(TokenAggregator.modelDisplayName("claude-opus-5-5") == "Opus 5.5")
        #expect(TokenAggregator.modelDisplayName("claude-haiku-4-5-20251001") == "Haiku 4.5")
        #expect(TokenAggregator.modelDisplayName("claude-fable-5") == "Fable 5")
        #expect(TokenAggregator.modelDisplayName("gpt-5") == "gpt-5")
    }
}
```

- [ ] **Step 2: 테스트가 실패하는지 확인한다**

Run: `cd macos && swift test --filter TokenAggregatorTests`
Expected: 컴파일 실패 — `cannot find 'TokenAggregator' in scope`.

- [ ] **Step 3: TokenAggregator 를 만든다**

`macos/Sources/TokenViewerCore/Analysis/TokenAggregator.swift`:

```swift
import Foundation

/// Groups records by project, model or session and ranks them by API-equivalent cost (v0.1.0 spec 5.3–5.5).
public enum TokenAggregator {
    public static let defaultTopCount = 5
    private static let unknownProject = "(알 수 없음)"
    private static let fiveHours: TimeInterval = 5 * 3600
    private static let day: TimeInterval = 86_400

    private struct Group {
        var row = BreakdownRow()
        var pricedCount = 0
        var unpricedCount = 0
        var firstSeen: Date?
        var projectPath = ""
    }

    public static func aggregate(_ snapshot: LogSnapshot, prices: PriceTable?, dimension: BreakdownDimension, from start: Date?,
                                 topCount: Int = defaultTopCount, timeZone: TimeZone = .current) -> Breakdown {
        let records = snapshot.records.filter { record in
            guard let start else {
                return true
            }
            return record.timestamp >= start
        }
        let projectLabels = projectLabels(for: records)

        var groups: [String: Group] = [:]
        for record in records {
            let key = switch dimension {
            case .project: record.projectPath
            case .model: record.model
            case .session: record.sessionId
            }
            var group = groups[key, default: Group()]
            let cost = prices?.cost(of: record) ?? CostResult(isUnpriced: true)
            group.row.costUsd += cost.usd
            group.row.isApproximate = group.row.isApproximate || cost.isApproximate
            if cost.isUnpriced {
                group.unpricedCount += 1
            } else {
                group.pricedCount += 1
            }
            group.row.input += record.input
            group.row.output += record.output
            group.row.cacheWrite += record.cacheWrite5m + record.cacheWrite1h
            group.row.cacheRead += record.cacheRead
            if group.firstSeen.map({ record.timestamp < $0 }) ?? true {
                group.firstSeen = record.timestamp
                group.projectPath = record.projectPath
            }
            groups[key] = group
        }

        let sessionTime = DateFormatter()
        sessionTime.locale = Locale(identifier: "en_US_POSIX")
        sessionTime.timeZone = timeZone
        sessionTime.dateFormat = "M/d HH:mm"

        var rows = groups.map { key, group -> BreakdownRow in
            var row = group.row
            row.detail = key
            row.isUnpriced = group.pricedCount == 0
            row.isApproximate = row.isApproximate || (group.pricedCount > 0 && group.unpricedCount > 0)
            switch dimension {
            case .project:
                row.label = labelOrUnknown(projectLabels[key])
            case .model:
                row.label = modelDisplayName(key)
            case .session:
                if let title = snapshot.sessionTitles[key], !title.isEmpty {
                    row.label = title
                } else {
                    let started = group.firstSeen.map { sessionTime.string(from: $0) } ?? ""
                    row.label = labelOrUnknown(projectLabels[group.projectPath]) + " · " + started
                }
            }
            return row
        }
        rows.sort(by: isRankedBefore)

        var breakdown = Breakdown()
        for (index, row) in rows.enumerated() {
            breakdown.totalUsd += row.costUsd
            if index < topCount {
                breakdown.rows.append(row)
            } else {
                add(row, to: &breakdown.otherRow)
                breakdown.otherCount += 1
            }
        }
        if breakdown.otherCount > 0 {
            breakdown.otherRow.label = "기타 \(breakdown.otherCount)개"
        }
        return breakdown
    }

    public static func periodStart(_ period: BreakdownPeriod, now: Date, fiveHourResetsAt: Date?, calendar: Calendar = .current) -> Date {
        switch period {
        case .fiveHourWindow:
            // A reset time that has already passed belongs to the previous window.
            if let resetsAt = fiveHourResetsAt, resetsAt > now {
                return resetsAt.addingTimeInterval(-fiveHours)
            }
            return now.addingTimeInterval(-fiveHours)
        case .today:
            return calendar.startOfDay(for: now)
        case .sevenDays:
            return now.addingTimeInterval(-7 * day)
        case .thirtyDays:
            return now.addingTimeInterval(-30 * day)
        }
    }

    /// `claude-opus-5-5` → `Opus 5.5`; ids that do not look like Claude models stay as they are.
    public static func modelDisplayName(_ modelId: String) -> String {
        guard let match = modelId.wholeMatch(of: #/claude-([a-z]+)((?:-\d{1,2})+)(?:-\d{8})?/#) else {
            return modelId
        }
        let family = String(match.1)
        let version = match.2.dropFirst().replacingOccurrences(of: "-", with: ".")
        return "\(family.prefix(1).uppercased())\(family.dropFirst()) \(version)"
    }

    private static func projectLabels(for records: [TokenRecord]) -> [String: String] {
        var pathsByName: [String: Set<String>] = [:]
        for record in records {
            pathsByName[lastComponent(record.projectPath), default: []].insert(record.projectPath)
        }
        var labels: [String: String] = [:]
        for (name, paths) in pathsByName {
            for path in paths {
                // Two projects share a folder name: prefix the parent folder to tell them apart.
                labels[path] = paths.count == 1 ? name : lastComponent((path as NSString).deletingLastPathComponent) + "/" + name
            }
        }
        return labels
    }

    private static func lastComponent(_ path: String) -> String {
        (path as NSString).lastPathComponent
    }

    private static func labelOrUnknown(_ label: String?) -> String {
        guard let label, !label.isEmpty else {
            return unknownProject
        }
        return label
    }

    private static func isRankedBefore(_ lhs: BreakdownRow, _ rhs: BreakdownRow) -> Bool {
        if lhs.costUsd != rhs.costUsd {
            return lhs.costUsd > rhs.costUsd
        }
        if lhs.totalTokens != rhs.totalTokens {
            return lhs.totalTokens > rhs.totalTokens
        }
        return lhs.label < rhs.label
    }

    private static func add(_ source: BreakdownRow, to target: inout BreakdownRow) {
        target.costUsd += source.costUsd
        target.isApproximate = target.isApproximate || source.isApproximate || source.isUnpriced
        target.input += source.input
        target.output += source.output
        target.cacheWrite += source.cacheWrite
        target.cacheRead += source.cacheRead
    }
}
```

- [ ] **Step 4: 테스트가 통과하는지 확인한다**

Run: `cd macos && swift test --filter TokenAggregatorTests`
Expected: 12 tests passed.

- [ ] **Step 5: 커밋**

```bash
git add macos/Sources/TokenViewerCore/Analysis/TokenAggregator.swift macos/Tests/TokenViewerCoreTests/TokenAggregatorTests.swift
git commit -m "feat: aggregate token cost by project, model and session in Swift

Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>"
```

---

### Task 8: CredentialParser, UsageResponseParser

**Files:**
- Create: `macos/Sources/TokenViewerCore/Service/CredentialParser.swift`, `macos/Sources/TokenViewerCore/Service/UsageResponseParser.swift`
- Create: `macos/Tests/TokenViewerCoreTests/CredentialParserTests.swift`, `macos/Tests/TokenViewerCoreTests/UsageResponseParserTests.swift`

**Interfaces:**
- Consumes: `Credential*`, `SecretLookup`, `FetchResult`, `HTTPResponse`, `UsageLimits`, `JSONValues` (Task 3)
- Produces: `enum CredentialParser { static func resolve(keychain: SecretLookup, readFile: () -> Data?, now: Date) -> CredentialResult; static func parse(_ json: Data, now: Date) -> CredentialResult; static func planLabel(subscriptionType: String, rateLimitTier: String) -> String }`, `enum UsageResponseParser { static func map(_ response: HTTPResponse) -> FetchResult; static func parse(_ body: Data) -> FetchResult }`

- [ ] **Step 1: 실패하는 테스트를 쓴다**

`macos/Tests/TokenViewerCoreTests/CredentialParserTests.swift`:

```swift
import Foundation
import Testing
@testable import TokenViewerCore

@Suite struct CredentialParserTests {
    private let now = TestSupport.date("2026-10-05T12:00:00Z")
    /// 2026-10-06T12:00Z in epoch milliseconds, one day after `now`.
    private let tomorrowMs = 1_791_288_000_000

    private func json(expiresAt: Any? = nil, token: String = "sk-test", type: String = "max", tier: String = "default_claude_max_5x") -> Data {
        let oauth: [String: Any] = [
            "accessToken": token,
            "refreshToken": "never-read",
            "expiresAt": expiresAt ?? tomorrowMs,
            "subscriptionType": type,
            "rateLimitTier": tier,
        ]
        return Data(LogLines.json(["claudeAiOauth": oauth]).utf8)
    }

    @Test func parsesTokenAndPlan() {
        let result = CredentialParser.parse(json(), now: now)
        #expect(result.status == .ok)
        #expect(result.credential?.accessToken == "sk-test")
        #expect(result.credential?.expiresAt == TestSupport.date("2026-10-06T12:00:00Z"))
        #expect(result.credential?.subscriptionType == "max")
        #expect(result.credential?.rateLimitTier == "default_claude_max_5x")
    }

    @Test func reportsExpiredToken() {
        let result = CredentialParser.parse(json(expiresAt: 1_791_100_000_000), now: now)
        #expect(result.status == .expired)
        #expect(result.credential?.subscriptionType == "max")
    }

    @Test func acceptsEpochSecondsAndIsoStrings() {
        #expect(CredentialParser.parse(json(expiresAt: 1_791_288_000), now: now).status == .ok)
        #expect(CredentialParser.parse(json(expiresAt: "2026-10-06T00:00:00Z"), now: now).status == .ok)
        #expect(CredentialParser.parse(json(expiresAt: "2026-10-01T00:00:00Z"), now: now).status == .expired)
    }

    @Test func missingTokenIsMalformed() {
        #expect(CredentialParser.parse(json(token: ""), now: now).status == .malformed)
        #expect(CredentialParser.parse(Data("nope".utf8), now: now).status == .malformed)
        #expect(CredentialParser.parse(Data(#"{"mcpOAuth": {}}"#.utf8), now: now).status == .malformed)
    }

    @Test func formatsPlanLabel() {
        #expect(CredentialParser.planLabel(subscriptionType: "max", rateLimitTier: "default_claude_max_5x") == "Max 5x")
        #expect(CredentialParser.planLabel(subscriptionType: "MAX", rateLimitTier: "max_20x") == "Max 20x")
        #expect(CredentialParser.planLabel(subscriptionType: "max", rateLimitTier: "") == "Max")
        #expect(CredentialParser.planLabel(subscriptionType: "pro", rateLimitTier: "") == "Pro")
        #expect(CredentialParser.planLabel(subscriptionType: "team", rateLimitTier: "default_claude_max_5x") == "Team")
        #expect(CredentialParser.planLabel(subscriptionType: "enterprise", rateLimitTier: "") == "Enterprise")
        #expect(CredentialParser.planLabel(subscriptionType: "free", rateLimitTier: "") == "")
    }

    @Test func resolvesKeychainThenFile() {
        #expect(CredentialParser.resolve(keychain: .found(json()), readFile: { nil }, now: now).status == .ok)
        #expect(CredentialParser.resolve(keychain: .denied, readFile: { json() }, now: now).status == .accessDenied)
        #expect(CredentialParser.resolve(keychain: .notFound, readFile: { json() }, now: now).status == .ok)
        #expect(CredentialParser.resolve(keychain: .notFound, readFile: { nil }, now: now).status == .notFound)
    }
}
```

`macos/Tests/TokenViewerCoreTests/UsageResponseParserTests.swift`:

```swift
import Foundation
import Testing
@testable import TokenViewerCore

@Suite struct UsageResponseParserTests {
    private func body(_ object: [String: Any]) -> Data {
        Data(LogLines.json(object).utf8)
    }

    @Test func parsesRecordedResponseShape() throws {
        let result = UsageResponseParser.parse(try TestSupport.fixture("usage_response.json"))
        #expect(result.status == .ok)
        #expect(result.limits.fiveHour?.percent == 42)
        #expect(result.limits.sevenDay?.percent == 18)
        let resetsAt = try #require(result.limits.fiveHour?.resetsAt)
        #expect(abs(resetsAt.timeIntervalSince(TestSupport.date("2026-10-05T07:40:00Z")) - 0.123) < 0.001)
    }

    @Test func acceptsUsedPercentageAndEpoch() {
        let result = UsageResponseParser.parse(body(["five_hour": ["used_percentage": 55.5, "resets_at": 1_791_201_600]]))
        #expect(result.status == .ok)
        #expect(result.limits.fiveHour == LimitWindow(percent: 55.5, resetsAt: Date(timeIntervalSince1970: 1_791_201_600)))
        #expect(result.limits.sevenDay == nil)
    }

    @Test func acceptsEpochMilliseconds() {
        let result = UsageResponseParser.parse(body(["seven_day": ["utilization": 1, "resets_at": 1_791_201_600_000]]))
        #expect(result.limits.sevenDay?.resetsAt == Date(timeIntervalSince1970: 1_791_201_600))
    }

    @Test func clampsOutOfRange() {
        let result = UsageResponseParser.parse(body(["five_hour": ["utilization": 130], "seven_day": ["utilization": -5]]))
        #expect(result.limits.fiveHour?.percent == 100)
        #expect(result.limits.sevenDay?.percent == 0)
    }

    @Test func nullWindowLeavesMeterEmpty() {
        let result = UsageResponseParser.parse(body(["five_hour": NSNull(), "seven_day": ["utilization": 10]]))
        #expect(result.status == .ok)
        #expect(result.limits.fiveHour == nil)
        #expect(result.limits.sevenDay?.percent == 10)
    }

    @Test func nullPercentLeavesMeterEmpty() {
        let result = UsageResponseParser.parse(body(["five_hour": ["utilization": NSNull()]]))
        #expect(result.status == .ok)
        #expect(result.limits.fiveHour == nil)
    }

    @Test func missingWindowsIsBadResponse() {
        let data = body(["other": 1])
        let result = UsageResponseParser.parse(data)
        #expect(result.status == .badResponse)
        #expect(result.detail == "five_hour / seven_day 필드가 없습니다")
        #expect(result.rawBody == data)
    }

    @Test func windowFormatDriftIsBadResponse() {
        let drifts: [Any] = ["42%", ["percent": 42], ["utilization": "42"]]
        for drift in drifts {
            let result = UsageResponseParser.parse(body(["five_hour": drift]))
            #expect(result.status == .badResponse)
            #expect(result.detail == "five_hour / seven_day 형식이 바뀌었습니다")
            #expect(result.limits == UsageLimits())
        }
    }

    @Test func notJsonIsBadResponse() {
        #expect(UsageResponseParser.parse(Data("<html>login</html>".utf8)).status == .badResponse)
        #expect(UsageResponseParser.parse(Data()).status == .badResponse)
    }

    @Test func mapsHttpStatuses() {
        func map(_ status: Int, error: String? = nil) -> FetchResult {
            UsageResponseParser.map(HTTPResponse(status: status, body: Data("x".utf8), errorText: error))
        }
        #expect(map(401).status == .tokenExpired)
        #expect(map(403).detail == "인증이 만료되었습니다 (HTTP 403)")
        #expect(map(429).status == .rateLimited)
        #expect(map(503).status == .serverError)
        #expect(map(503).detail == "서버 오류 (HTTP 503)")
        #expect(map(0, error: "오프라인").detail == "오프라인")
        #expect(map(0).status == .networkError)
        #expect(map(0).detail == "네트워크 연결 없음")
        #expect(map(200, error: "끊김").status == .networkError)
        #expect(map(404).status == .badResponse)
        #expect(map(404).rawBody == Data("x".utf8))
        #expect(UsageResponseParser.map(HTTPResponse(status: 200, body: body(["five_hour": ["utilization": 1]]))).status == .ok)
    }
}
```

- [ ] **Step 2: 테스트가 실패하는지 확인한다**

Run: `cd macos && swift test --filter "CredentialParserTests|UsageResponseParserTests"`
Expected: 컴파일 실패 — `cannot find 'CredentialParser' in scope`.

- [ ] **Step 3: 두 파서를 만든다**

`macos/Sources/TokenViewerCore/Service/CredentialParser.swift`:

```swift
import Foundation

/// Reads Claude Code's OAuth credential (v0.1.0 spec 3.2). Only the access token, expiry and plan are read;
/// the refresh token is never touched so the app cannot disturb Claude Code's login.
public enum CredentialParser {
    private static let planNames = ["max": "Max", "pro": "Pro", "team": "Team", "enterprise": "Enterprise"]

    /// Keychain first; the JSON file is what Claude Code writes when the Keychain is unavailable.
    public static func resolve(keychain: SecretLookup, readFile: () -> Data?, now: Date) -> CredentialResult {
        switch keychain {
        case .found(let data):
            return parse(data, now: now)
        case .denied:
            return CredentialResult(status: .accessDenied)
        case .notFound:
            guard let data = readFile() else {
                return CredentialResult(status: .notFound)
            }
            return parse(data, now: now)
        }
    }

    public static func parse(_ json: Data, now: Date) -> CredentialResult {
        guard let root = (try? JSONSerialization.jsonObject(with: json)) as? [String: Any],
              let oauth = root["claudeAiOauth"] as? [String: Any],
              let token = oauth["accessToken"] as? String, !token.isEmpty else {
            return CredentialResult(status: .malformed)
        }
        let credential = Credential(accessToken: token,
                                    expiresAt: JSONValues.date(oauth["expiresAt"]),
                                    subscriptionType: oauth["subscriptionType"] as? String ?? "",
                                    rateLimitTier: oauth["rateLimitTier"] as? String ?? "")
        let isExpired = credential.expiresAt.map { $0 <= now } ?? false
        return CredentialResult(status: isExpired ? .expired : .ok, credential: credential)
    }

    /// `max` + `default_claude_max_5x` → `Max 5x`; unknown plans give an empty label so the badge hides.
    public static func planLabel(subscriptionType: String, rateLimitTier: String) -> String {
        guard let name = planNames[subscriptionType.lowercased()] else {
            return ""
        }
        if name == "Max", let match = rateLimitTier.lowercased().firstMatch(of: #/(\d+)x/#) {
            return "Max \(match.1)x"
        }
        return name
    }
}
```

`macos/Sources/TokenViewerCore/Service/UsageResponseParser.swift`:

```swift
import Foundation

/// Turns `/api/oauth/usage` answers into a FetchResult (v0.1.0 spec 3.1, 7). The API is undocumented,
/// so the parser accepts known variants and reports anything else as format drift instead of guessing.
public enum UsageResponseParser {
    private static let fiveHourKey = "five_hour"
    private static let sevenDayKey = "seven_day"
    private static let utilizationKey = "utilization"
    private static let usedPercentageKey = "used_percentage"

    private struct FormatDrift: Error {}

    public static func map(_ response: HTTPResponse) -> FetchResult {
        let status = response.status
        if status == 200 && response.errorText == nil {
            return parse(response.body)
        }
        switch status {
        case 401, 403:
            return FetchResult(status: .tokenExpired, detail: "인증이 만료되었습니다 (HTTP \(status))", httpStatus: status)
        case 429:
            return FetchResult(status: .rateLimited, detail: "요청이 너무 많습니다 (HTTP 429)", httpStatus: status)
        case 500...:
            return FetchResult(status: .serverError, detail: "서버 오류 (HTTP \(status))", httpStatus: status)
        case 0, 200:
            let detail = response.errorText.flatMap { $0.isEmpty ? nil : $0 } ?? "네트워크 연결 없음"
            return FetchResult(status: .networkError, detail: detail, httpStatus: status)
        default:
            return FetchResult(status: .badResponse, detail: "예상하지 못한 응답 (HTTP \(status))", httpStatus: status, rawBody: response.body)
        }
    }

    public static func parse(_ body: Data) -> FetchResult {
        guard let root = (try? JSONSerialization.jsonObject(with: body)) as? [String: Any],
              root[fiveHourKey] != nil || root[sevenDayKey] != nil else {
            return FetchResult(status: .badResponse, detail: "five_hour / seven_day 필드가 없습니다", httpStatus: 200, rawBody: body)
        }
        do {
            let limits = UsageLimits(fiveHour: try window(root[fiveHourKey]), sevenDay: try window(root[sevenDayKey]))
            return FetchResult(status: .ok, limits: limits, httpStatus: 200)
        } catch {
            return FetchResult(status: .badResponse, detail: "five_hour / seven_day 형식이 바뀌었습니다", httpStatus: 200, rawBody: body)
        }
    }

    private static func window(_ value: Any?) throws -> LimitWindow? {
        guard let value, !(value is NSNull) else {
            return nil
        }
        guard let object = value as? [String: Any],
              object[utilizationKey] != nil || object[usedPercentageKey] != nil else {
            throw FormatDrift()
        }
        guard let percent = JSONValues.number(object[utilizationKey]) ?? JSONValues.number(object[usedPercentageKey]) else {
            // A percent key that is null leaves the meter empty; any other type is format drift.
            if (object[utilizationKey] ?? object[usedPercentageKey]) is NSNull {
                return nil
            }
            throw FormatDrift()
        }
        return LimitWindow(percent: min(max(percent, 0), 100), resetsAt: JSONValues.date(object["resets_at"]))
    }
}
```

- [ ] **Step 4: 테스트가 통과하는지 확인한다**

Run: `cd macos && swift test --filter "CredentialParserTests|UsageResponseParserTests"`
Expected: 16 tests passed.

- [ ] **Step 5: 커밋**

```bash
git add macos/Sources/TokenViewerCore/Service macos/Tests/TokenViewerCoreTests
git commit -m "feat: read Claude Code credentials and parse the usage API in Swift

Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>"
```

---

### Task 9: RefreshPolicy, UsageStatus, StatusText

**Files:**
- Create: `macos/Sources/TokenViewerCore/Service/{RefreshPolicy,UsageStatus,StatusText}.swift`
- Create: `macos/Tests/TokenViewerCoreTests/{RefreshPolicyTests,UsageStatusTests,StatusTextTests}.swift`

**Interfaces:**
- Consumes: `FetchStatus`, `FetchResult`, `UsageLimits`, `Formatters` (Task 3)
- Produces:
  - `struct RefreshDecision { var delay: TimeInterval; var autoRetry: Bool }`, `enum RefreshPolicy { static let maxBackoff: TimeInterval = 1800; static func next(after: FetchStatus, baseInterval: TimeInterval, previousDelay: TimeInterval) -> RefreshDecision }`
  - `enum LimitDisplayState { empty, normal, stale }`, `struct UsageStatus { limits: UsageLimits?; planLabel; lastStatus; lastDetail; lastAttemptAt; lastSuccessAt; mutating func apply(_:now:); func displayState(now:) -> LimitDisplayState; func isRefreshDueOnOpen(now:) -> Bool; var needsRetryButton: Bool }`
  - `enum StatusText { static func notice(_ status: UsageStatus, now: Date) -> String; struct Footer { text; isFailure }; static func footer(_ status: UsageStatus, now: Date) -> Footer }`

- [ ] **Step 1: 실패하는 테스트를 쓴다**

`macos/Tests/TokenViewerCoreTests/RefreshPolicyTests.swift`:

```swift
import Testing
@testable import TokenViewerCore

@Suite struct RefreshPolicyTests {
    @Test func okUsesBaseInterval() {
        #expect(RefreshPolicy.next(after: .ok, baseInterval: 180, previousDelay: 999) == RefreshDecision(delay: 180, autoRetry: true))
    }

    @Test func rateLimitDoublesDelay() {
        #expect(RefreshPolicy.next(after: .rateLimited, baseInterval: 180, previousDelay: 0).delay == 360)
        #expect(RefreshPolicy.next(after: .rateLimited, baseInterval: 180, previousDelay: 360).delay == 720)
    }

    @Test func rateLimitIsCapped() {
        #expect(RefreshPolicy.next(after: .rateLimited, baseInterval: 180, previousDelay: 1500).delay == 1800)
        #expect(RefreshPolicy.next(after: .rateLimited, baseInterval: 600, previousDelay: 1800).delay == 1800)
    }

    @Test func keychainDeniedStopsAutoRetry() {
        #expect(!RefreshPolicy.next(after: .keychainDenied, baseInterval: 180, previousDelay: 0).autoRetry)
    }

    @Test func otherFailuresKeepBaseInterval() {
        for status: FetchStatus in [.networkError, .serverError, .tokenExpired, .badResponse, .notLoggedIn] {
            #expect(RefreshPolicy.next(after: status, baseInterval: 180, previousDelay: 720) == RefreshDecision(delay: 180, autoRetry: true))
        }
    }
}
```

`macos/Tests/TokenViewerCoreTests/UsageStatusTests.swift`:

```swift
import Foundation
import Testing
@testable import TokenViewerCore

@Suite struct UsageStatusTests {
    private let now = TestSupport.date("2026-10-05T12:00:00Z")
    private let limits = UsageLimits(fiveHour: LimitWindow(percent: 42, resetsAt: nil), sevenDay: LimitWindow(percent: 18, resetsAt: nil))

    private func status(after results: [(FetchResult, TimeInterval)]) -> UsageStatus {
        var status = UsageStatus()
        for (result, offset) in results {
            status.apply(result, now: now.addingTimeInterval(offset))
        }
        return status
    }

    @Test func emptyBeforeFirstFetch() {
        let status = UsageStatus()
        #expect(status.displayState(now: now) == .empty)
        #expect(status.isRefreshDueOnOpen(now: now))
        #expect(!status.needsRetryButton)
    }

    @Test func okStoresLimitsAndPlan() {
        let status = status(after: [(FetchResult(status: .ok, limits: limits, planLabel: "Max 5x"), 0)])
        #expect(status.limits == limits)
        #expect(status.planLabel == "Max 5x")
        #expect(status.lastSuccessAt == now)
        #expect(status.displayState(now: now) == .normal)
    }

    @Test func failureKeepsValuesThenTurnsStale() {
        let status = status(after: [(FetchResult(status: .ok, limits: limits), 0), (FetchResult(status: .networkError, detail: "오프라인"), 60)])
        #expect(status.limits == limits)
        #expect(status.lastStatus == .networkError)
        #expect(status.displayState(now: now.addingTimeInterval(60)) == .normal)
        #expect(status.displayState(now: now.addingTimeInterval(16 * 60)) == .stale)
    }

    @Test func staleAfterSleepEvenIfLastStatusOk() {
        let status = status(after: [(FetchResult(status: .ok, limits: limits), 0)])
        #expect(status.displayState(now: now.addingTimeInterval(20 * 60)) == .stale)
    }

    @Test func keychainDeniedClearsLimits() {
        let status = status(after: [(FetchResult(status: .ok, limits: limits, planLabel: "Max 5x"), 0), (FetchResult(status: .keychainDenied), 60)])
        #expect(status.limits == nil)
        #expect(status.planLabel == "")
        #expect(status.displayState(now: now.addingTimeInterval(60)) == .empty)
        #expect(status.needsRetryButton)
    }

    @Test func loggedOutClearsPlanLabel() {
        let status = status(after: [(FetchResult(status: .ok, planLabel: "Pro"), 0), (FetchResult(status: .notLoggedIn), 60)])
        #expect(status.planLabel == "")
        #expect(status.needsRetryButton)
    }

    @Test func badResponseClearsLimits() {
        let status = status(after: [(FetchResult(status: .ok, limits: limits), 0), (FetchResult(status: .badResponse), 60)])
        #expect(status.limits == nil)
    }

    @Test func expiredTokenKeepsPlanLabel() {
        let status = status(after: [(FetchResult(status: .tokenExpired, planLabel: "Max 5x"), 0)])
        #expect(status.planLabel == "Max 5x")
    }

    @Test func refreshDueOnOpenAfter30Seconds() {
        let status = status(after: [(FetchResult(status: .ok, limits: limits), 0)])
        #expect(!status.isRefreshDueOnOpen(now: now.addingTimeInterval(10)))
        #expect(status.isRefreshDueOnOpen(now: now.addingTimeInterval(31)))
    }

    @Test func noOpenRefreshAfterKeychainDenied() {
        let status = status(after: [(FetchResult(status: .keychainDenied), 0)])
        #expect(!status.isRefreshDueOnOpen(now: now.addingTimeInterval(3600)))
    }
}
```

`macos/Tests/TokenViewerCoreTests/StatusTextTests.swift`:

```swift
import Foundation
import Testing
@testable import TokenViewerCore

@Suite struct StatusTextTests {
    private let now = TestSupport.date("2026-10-05T12:00:00Z")

    private func status(_ results: (FetchResult, TimeInterval)...) -> UsageStatus {
        var status = UsageStatus()
        for (result, offset) in results {
            status.apply(result, now: now.addingTimeInterval(offset))
        }
        return status
    }

    @Test func loadingBeforeFirstAttempt() {
        #expect(StatusText.notice(UsageStatus(), now: now) == "한도를 불러오는 중…")
        #expect(StatusText.footer(UsageStatus(), now: now) == StatusText.Footer(text: "한도 정보 없음", isFailure: false))
    }

    @Test func okHasNoNotice() {
        #expect(StatusText.notice(status((FetchResult(status: .ok), 0)), now: now) == "")
    }

    @Test func noticeExplainsEachFailure() {
        #expect(StatusText.notice(status((FetchResult(status: .notLoggedIn), 0)), now: now)
                == "Claude Code 로그인 정보를 찾지 못했습니다.\n터미널에서 claude 실행 후 /login 하세요.")
        #expect(StatusText.notice(status((FetchResult(status: .keychainDenied), 0)), now: now)
                == "Keychain 접근이 거부되었습니다.\n'다시 확인'을 누르면 허용 창이 다시 뜹니다.")
        #expect(StatusText.notice(status((FetchResult(status: .tokenExpired), 0)), now: now)
                == "로그인 토큰이 만료되었습니다. Claude Code 를 실행하면 갱신됩니다.")
        #expect(StatusText.notice(status((FetchResult(status: .badResponse), 0)), now: now)
                == "사용량 API 응답 형식이 바뀌었습니다.\n~/Library/Logs/TokenViewer 를 확인하세요.")
    }

    @Test func noticeShowsAgeOfLastValue() {
        let failing = FetchResult(status: .serverError, detail: "서버 오류 (HTTP 503)")
        #expect(StatusText.notice(status((FetchResult(status: .ok), 0), (failing, 18 * 60)), now: now.addingTimeInterval(18 * 60))
                == "18분 전 값입니다. 갱신 실패: 서버 오류 (HTTP 503)")
        #expect(StatusText.notice(status((failing, 0)), now: now) == "갱신 실패: 서버 오류 (HTTP 503)")
    }

    @Test func footerShowsAgeOrFailure() {
        let ok = status((FetchResult(status: .ok), 0))
        #expect(StatusText.footer(ok, now: now.addingTimeInterval(5 * 60)) == StatusText.Footer(text: "5분 전 업데이트", isFailure: false))
        let failing = status((FetchResult(status: .ok), 0), (FetchResult(status: .networkError), 60))
        #expect(StatusText.footer(failing, now: now.addingTimeInterval(6 * 60)) == StatusText.Footer(text: "갱신 실패 · 6분 전", isFailure: true))
        #expect(StatusText.footer(status((FetchResult(status: .networkError), 0)), now: now) == StatusText.Footer(text: "한도 정보 없음", isFailure: true))
    }
}
```

- [ ] **Step 2: 테스트가 실패하는지 확인한다**

Run: `cd macos && swift test --filter "RefreshPolicyTests|UsageStatusTests|StatusTextTests"`
Expected: 컴파일 실패 — `cannot find 'RefreshPolicy' in scope`.

- [ ] **Step 3: 세 파일을 만든다**

`macos/Sources/TokenViewerCore/Service/RefreshPolicy.swift`:

```swift
import Foundation

public struct RefreshDecision: Sendable, Equatable {
    public var delay: TimeInterval
    public var autoRetry: Bool

    public init(delay: TimeInterval, autoRetry: Bool) {
        self.delay = delay
        self.autoRetry = autoRetry
    }
}

/// When to call the usage API next (v0.1.0 spec 6, 7).
public enum RefreshPolicy {
    public static let maxBackoff: TimeInterval = 30 * 60

    public static func next(after status: FetchStatus, baseInterval: TimeInterval, previousDelay: TimeInterval) -> RefreshDecision {
        switch status {
        case .rateLimited:
            return RefreshDecision(delay: min(max(previousDelay, baseInterval) * 2, maxBackoff), autoRetry: true)
        case .keychainDenied:
            // Retrying would pop the Keychain prompt every few minutes; wait for the user to ask.
            return RefreshDecision(delay: baseInterval, autoRetry: false)
        default:
            return RefreshDecision(delay: baseInterval, autoRetry: true)
        }
    }
}
```

`macos/Sources/TokenViewerCore/Service/UsageStatus.swift`:

```swift
import Foundation

public enum LimitDisplayState: Sendable, Equatable {
    case empty
    case normal
    case stale
}

/// The latest usage-limit state the menu bar and the dropdown show (Qt `SystemStatus`).
public struct UsageStatus: Sendable, Equatable {
    public static let staleAfter: TimeInterval = 15 * 60
    public static let refreshOnOpenAfter: TimeInterval = 30

    public private(set) var limits: UsageLimits?
    public private(set) var planLabel = ""
    public private(set) var lastStatus: FetchStatus = .ok
    public private(set) var lastDetail = ""
    public private(set) var lastAttemptAt: Date?
    public private(set) var lastSuccessAt: Date?

    public init() {}

    public mutating func apply(_ result: FetchResult, now: Date) {
        lastAttemptAt = now
        lastStatus = result.status
        lastDetail = result.detail
        switch result.status {
        case .notLoggedIn, .keychainDenied:
            // The account is unknown, so the old plan badge no longer applies.
            planLabel = ""
        default:
            if !result.planLabel.isEmpty {
                planLabel = result.planLabel
            }
        }
        switch result.status {
        case .ok:
            limits = result.limits
            lastSuccessAt = now
        case .notLoggedIn, .keychainDenied, .badResponse:
            limits = nil
        case .tokenExpired, .rateLimited, .networkError, .serverError:
            // Keep the last values; displayState dims them once they are old.
            break
        }
    }

    public func displayState(now: Date) -> LimitDisplayState {
        guard limits != nil, let lastSuccessAt else {
            return .empty
        }
        // Age alone decides, so values also dim after the Mac wakes from sleep.
        return now.timeIntervalSince(lastSuccessAt) > Self.staleAfter ? .stale : .normal
    }

    public func isRefreshDueOnOpen(now: Date) -> Bool {
        // Only the '다시 확인' button retries after a Keychain denial.
        if lastStatus == .keychainDenied {
            return false
        }
        guard let lastAttemptAt else {
            return true
        }
        return now.timeIntervalSince(lastAttemptAt) > Self.refreshOnOpenAfter
    }

    /// The retry button only helps when the credential itself was missing or refused.
    public var needsRetryButton: Bool {
        lastAttemptAt != nil && (lastStatus == .notLoggedIn || lastStatus == .keychainDenied)
    }
}
```

`macos/Sources/TokenViewerCore/Service/StatusText.swift`:

```swift
import Foundation

/// The dropdown's notice box and footer line (v0.1.0 spec 2.2).
public enum StatusText {
    public struct Footer: Sendable, Equatable {
        public var text: String
        public var isFailure: Bool

        public init(text: String, isFailure: Bool) {
            self.text = text
            self.isFailure = isFailure
        }
    }

    public static func notice(_ status: UsageStatus, now: Date) -> String {
        guard status.lastAttemptAt != nil else {
            return "한도를 불러오는 중…"
        }
        switch status.lastStatus {
        case .ok:
            return ""
        case .notLoggedIn:
            return "Claude Code 로그인 정보를 찾지 못했습니다.\n터미널에서 claude 실행 후 /login 하세요."
        case .keychainDenied:
            return "Keychain 접근이 거부되었습니다.\n'다시 확인'을 누르면 허용 창이 다시 뜹니다."
        case .tokenExpired:
            return "로그인 토큰이 만료되었습니다. Claude Code 를 실행하면 갱신됩니다."
        case .badResponse:
            return "사용량 API 응답 형식이 바뀌었습니다.\n~/Library/Logs/TokenViewer 를 확인하세요."
        case .rateLimited, .networkError, .serverError:
            break
        }
        let failure = "갱신 실패: \(status.lastDetail)"
        guard let lastSuccessAt = status.lastSuccessAt else {
            return failure
        }
        return "\(Formatters.age(since: lastSuccessAt, now: now)) 값입니다. \(failure)"
    }

    public static func footer(_ status: UsageStatus, now: Date) -> Footer {
        let isFailing = status.lastAttemptAt != nil && status.lastStatus != .ok
        guard let lastSuccessAt = status.lastSuccessAt else {
            return Footer(text: "한도 정보 없음", isFailure: isFailing)
        }
        let age = Formatters.age(since: lastSuccessAt, now: now)
        return isFailing ? Footer(text: "갱신 실패 · \(age)", isFailure: true) : Footer(text: "\(age) 업데이트", isFailure: false)
    }
}
```

- [ ] **Step 4: 테스트가 통과하는지 확인한다**

Run: `cd macos && swift test --filter "RefreshPolicyTests|UsageStatusTests|StatusTextTests"`
Expected: 20 tests passed.

- [ ] **Step 5: 커밋**

```bash
git add macos/Sources/TokenViewerCore/Service macos/Tests/TokenViewerCoreTests
git commit -m "feat: add refresh policy, usage status and status text in Swift

Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>"
```

---

### Task 10: SettingsStore

**Files:**
- Create: `macos/Sources/TokenViewerCore/Service/SettingsStore.swift`, `macos/Tests/TokenViewerCoreTests/SettingsStoreTests.swift`

**Interfaces:**
- Consumes: `BreakdownDimension`, `BreakdownPeriod` (Task 3)
- Produces: `struct SettingsValues: Equatable { intervalMinutes, warnPercent, criticalPercent, launchAtLogin, dimension, period }`, `struct SettingsStore { init(defaults: UserDefaults = .standard); static allowedIntervals/minWarn/maxWarn/maxPercent/percentStep; var values: SettingsValues; func save(_ values: SettingsValues) }`

- [ ] **Step 1: 실패하는 테스트를 쓴다**

`macos/Tests/TokenViewerCoreTests/SettingsStoreTests.swift`:

```swift
import Foundation
import Testing
@testable import TokenViewerCore

@Suite struct SettingsStoreTests {
    /// Runs `body` against an empty, throwaway defaults domain.
    private func withDefaults(_ body: (UserDefaults, SettingsStore) -> Void) {
        let name = "TokenViewerTests-\(UUID().uuidString)"
        let defaults = UserDefaults(suiteName: name)!
        defer { defaults.removePersistentDomain(forName: name) }
        body(defaults, SettingsStore(defaults: defaults))
    }

    @Test func defaultsWhenEmpty() {
        withDefaults { _, store in
            #expect(store.values == SettingsValues(intervalMinutes: 3, warnPercent: 70, criticalPercent: 90,
                                                   launchAtLogin: true, dimension: .project, period: .fiveHourWindow))
        }
    }

    @Test func rejectsUnknownInterval() {
        withDefaults { defaults, store in
            defaults.set(7, forKey: "refresh.intervalMinutes")
            #expect(store.values.intervalMinutes == 3)
            defaults.set(10, forKey: "refresh.intervalMinutes")
            #expect(store.values.intervalMinutes == 10)
        }
    }

    @Test func rejectsInvalidWarnPercent() {
        withDefaults { defaults, store in
            for (stored, expected) in [(52, 70), (45, 70), (100, 70), (85, 85)] {
                defaults.set(stored, forKey: "alert.warnPercent")
                #expect(store.values.warnPercent == expected)
            }
        }
    }

    @Test func keepsCriticalAboveWarn() {
        withDefaults { defaults, store in
            defaults.set(90, forKey: "alert.warnPercent")
            defaults.set(85, forKey: "alert.criticalPercent")
            #expect(store.values.criticalPercent == 95)
            defaults.set(95, forKey: "alert.warnPercent")
            defaults.removeObject(forKey: "alert.criticalPercent")
            #expect(store.values.criticalPercent == 100)
            defaults.set(60, forKey: "alert.warnPercent")
            defaults.set(100, forKey: "alert.criticalPercent")
            #expect(store.values.criticalPercent == 100)
            defaults.set(70, forKey: "alert.warnPercent")
            defaults.set(92, forKey: "alert.criticalPercent")
            #expect(store.values.criticalPercent == 90)
        }
    }

    @Test func roundTripsAllValues() {
        withDefaults { defaults, store in
            let values = SettingsValues(intervalMinutes: 5, warnPercent: 60, criticalPercent: 80,
                                        launchAtLogin: false, dimension: .session, period: .thirtyDays)
            store.save(values)
            #expect(store.values == values)
            #expect(defaults.integer(forKey: "refresh.intervalMinutes") == 5)
            #expect(defaults.integer(forKey: "popup.dimension") == 2)
            #expect(defaults.object(forKey: "app.launchAtLogin") as? Bool == false)
        }
    }

    @Test func readsValuesQtStoredAsStrings() {
        withDefaults { defaults, store in
            defaults.set("85", forKey: "alert.warnPercent")
            defaults.set("false", forKey: "app.launchAtLogin")
            #expect(store.values.warnPercent == 85)
            #expect(!store.values.launchAtLogin)
        }
    }

    @Test func ignoresUnknownEnumValues() {
        withDefaults { defaults, store in
            defaults.set(7, forKey: "popup.dimension")
            defaults.set(-1, forKey: "popup.period")
            #expect(store.values.dimension == .project)
            #expect(store.values.period == .fiveHourWindow)
        }
    }
}
```

- [ ] **Step 2: 테스트가 실패하는지 확인한다**

Run: `cd macos && swift test --filter SettingsStoreTests`
Expected: 컴파일 실패 — `cannot find 'SettingsStore' in scope`.

- [ ] **Step 3: SettingsStore 를 만든다**

`macos/Sources/TokenViewerCore/Service/SettingsStore.swift`:

```swift
import Foundation

public struct SettingsValues: Sendable, Equatable {
    public var intervalMinutes: Int
    public var warnPercent: Int
    public var criticalPercent: Int
    public var launchAtLogin: Bool
    public var dimension: BreakdownDimension
    public var period: BreakdownPeriod

    public init(intervalMinutes: Int, warnPercent: Int, criticalPercent: Int, launchAtLogin: Bool,
                dimension: BreakdownDimension, period: BreakdownPeriod) {
        self.intervalMinutes = intervalMinutes
        self.warnPercent = warnPercent
        self.criticalPercent = criticalPercent
        self.launchAtLogin = launchAtLogin
        self.dimension = dimension
        self.period = period
    }
}

/// UserDefaults with the same keys the Qt build's QSettings wrote, so existing choices carry over (spec 4.3).
public struct SettingsStore {
    public static let allowedIntervals = [1, 3, 5, 10]
    public static let minWarn = 50
    public static let maxWarn = 95
    public static let maxPercent = 100
    public static let percentStep = 5
    private static let defaultInterval = 3
    private static let defaultWarn = 70
    private static let defaultCritical = 90

    private enum Key {
        static let interval = "refresh.intervalMinutes"
        static let warn = "alert.warnPercent"
        static let critical = "alert.criticalPercent"
        static let launchAtLogin = "app.launchAtLogin"
        static let dimension = "popup.dimension"
        static let period = "popup.period"
    }

    private let defaults: UserDefaults

    public init(defaults: UserDefaults = .standard) {
        self.defaults = defaults
    }

    /// The stored values, with anything out of range replaced by its default.
    public var values: SettingsValues {
        let interval = int(Key.interval) ?? Self.defaultInterval
        let warn = validWarn(int(Key.warn))
        return SettingsValues(
            intervalMinutes: Self.allowedIntervals.contains(interval) ? interval : Self.defaultInterval,
            warnPercent: warn,
            criticalPercent: validCritical(int(Key.critical), warn: warn),
            launchAtLogin: bool(Key.launchAtLogin) ?? true,
            dimension: int(Key.dimension).flatMap(BreakdownDimension.init(rawValue:)) ?? .project,
            period: int(Key.period).flatMap(BreakdownPeriod.init(rawValue:)) ?? .fiveHourWindow)
    }

    public func save(_ values: SettingsValues) {
        defaults.set(values.intervalMinutes, forKey: Key.interval)
        defaults.set(values.warnPercent, forKey: Key.warn)
        defaults.set(values.criticalPercent, forKey: Key.critical)
        defaults.set(values.launchAtLogin, forKey: Key.launchAtLogin)
        defaults.set(values.dimension.rawValue, forKey: Key.dimension)
        defaults.set(values.period.rawValue, forKey: Key.period)
    }

    private func validWarn(_ value: Int?) -> Int {
        guard let value, value >= Self.minWarn, value <= Self.maxWarn, value % Self.percentStep == 0 else {
            return Self.defaultWarn
        }
        return value
    }

    private func validCritical(_ value: Int?, warn: Int) -> Int {
        if let value, value > warn, value <= Self.maxPercent, value % Self.percentStep == 0 {
            return value
        }
        return Self.defaultCritical > warn ? Self.defaultCritical : min(warn + Self.percentStep, Self.maxPercent)
    }

    // QSettings may have written numbers and booleans as strings; accept both.
    private func int(_ key: String) -> Int? {
        switch defaults.object(forKey: key) {
        case let number as NSNumber:
            return number.intValue
        case let text as String:
            return Int(text)
        default:
            return nil
        }
    }

    private func bool(_ key: String) -> Bool? {
        switch defaults.object(forKey: key) {
        case let number as NSNumber:
            return number.boolValue
        case let text as String:
            return ["true": true, "false": false][text.lowercased()]
        default:
            return nil
        }
    }
}
```

- [ ] **Step 4: 테스트가 통과하는지 확인한다**

Run: `cd macos && swift test --filter SettingsStoreTests`
Expected: 7 tests passed.

- [ ] **Step 5: 커밋**

```bash
git add macos/Sources/TokenViewerCore/Service/SettingsStore.swift macos/Tests/TokenViewerCoreTests/SettingsStoreTests.swift
git commit -m "feat: store settings under the Qt build's keys

Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>"
```

---

### Task 11: WakeSignal, UsageFetcher

**Files:**
- Create: `macos/Sources/TokenViewerCore/Worker/WakeSignal.swift`, `macos/Sources/TokenViewerCore/Worker/UsageFetcher.swift`
- Create: `macos/Tests/TokenViewerCoreTests/WakeSignalTests.swift`, `macos/Tests/TokenViewerCoreTests/UsageFetcherTests.swift`

**Interfaces:**
- Consumes: `CredentialResult`, `CredentialParser.planLabel`, `UsageResponseParser.map`, `RefreshPolicy`, `FetchResult`, `HTTPResponse`
- Produces:
  - `actor WakeSignal { func wait(timeout: TimeInterval?) async; func wake() }`
  - `struct UsageFetchEnvironment { loadCredential: @Sendable () async -> CredentialResult; requestUsage: @Sendable (String) async -> HTTPResponse }`
  - `actor UsageFetcher { init(environment:interval:onResult:); func start(); func stop() async; func requestRefresh() async; func setInterval(_ seconds: TimeInterval); func fetchOnce() async -> FetchResult (internal) }`

- [ ] **Step 1: 실패하는 테스트를 쓴다**

`macos/Tests/TokenViewerCoreTests/WakeSignalTests.swift`:

```swift
import Foundation
import Testing
@testable import TokenViewerCore

@Suite struct WakeSignalTests {
    @Test func timesOut() async {
        let signal = WakeSignal()
        let start = Date()
        await signal.wait(timeout: 0.05)
        #expect(Date().timeIntervalSince(start) >= 0.04)
    }

    @Test func wakeEndsWaitEarly() async {
        let signal = WakeSignal()
        let start = Date()
        Task {
            try? await Task.sleep(nanoseconds: 50_000_000)
            await signal.wake()
        }
        await signal.wait(timeout: 10)
        #expect(Date().timeIntervalSince(start) < 5)
    }

    @Test func wakeBeforeWaitIsKept() async {
        let signal = WakeSignal()
        await signal.wake()
        let start = Date()
        await signal.wait(timeout: 10)
        #expect(Date().timeIntervalSince(start) < 1)
    }

    @Test func waitWithoutTimeoutNeedsWake() async {
        let signal = WakeSignal()
        let finished = Recorder<Bool>()
        Task {
            await signal.wait(timeout: nil)
            await finished.append(true)
        }
        try? await Task.sleep(nanoseconds: 150_000_000)
        #expect(await finished.values.isEmpty)
        await signal.wake()
        #expect(await waitUntil { await !finished.values.isEmpty })
    }
}
```

`macos/Tests/TokenViewerCoreTests/UsageFetcherTests.swift`:

```swift
import Foundation
import Testing
@testable import TokenViewerCore

@Suite struct UsageFetcherTests {
    static let okBody = Data(#"{"five_hour": {"utilization": 42}, "seven_day": {"utilization": 18}}"#.utf8)
    static let validCredential = CredentialResult(status: .ok, credential: Credential(
        accessToken: "token", expiresAt: nil, subscriptionType: "max", rateLimitTier: "default_claude_max_5x"))

    private func environment(
        credential: @escaping @Sendable () async -> CredentialResult = { UsageFetcherTests.validCredential },
        response: @escaping @Sendable (String) async -> HTTPResponse = { _ in HTTPResponse(status: 200, body: UsageFetcherTests.okBody) }
    ) -> UsageFetchEnvironment {
        UsageFetchEnvironment(loadCredential: credential, requestUsage: response)
    }

    @Test func fetchesImmediatelyThenOnInterval() async {
        let results = Recorder<FetchResult>()
        let fetcher = UsageFetcher(environment: environment(), interval: 0.1, onResult: { await results.append($0) })
        await fetcher.start()
        #expect(await waitUntil { await results.values.count >= 3 })
        await fetcher.stop()
        #expect(await results.values.first?.status == .ok)
        #expect(await results.values.first?.planLabel == "Max 5x")
    }

    @Test func refreshRequestFetchesNow() async {
        let results = Recorder<FetchResult>()
        let fetcher = UsageFetcher(environment: environment(), interval: 60, onResult: { await results.append($0) })
        await fetcher.start()
        #expect(await waitUntil { await results.values.count == 1 })
        await fetcher.requestRefresh()
        #expect(await waitUntil { await results.values.count == 2 })
        await fetcher.stop()
    }

    @Test func refreshWhileCredentialPendingDoesNotStack() async {
        let gate = Gate()
        let lookups = Recorder<Int>()
        let results = Recorder<FetchResult>()
        let fetcher = UsageFetcher(
            environment: environment(credential: {
                await lookups.append(1)
                await gate.pass()
                return UsageFetcherTests.validCredential
            }),
            interval: 60,
            onResult: { await results.append($0) })
        await fetcher.start()
        #expect(await waitUntil { await gate.isWaiting })
        await fetcher.requestRefresh()
        await fetcher.requestRefresh()
        await gate.open()
        #expect(await waitUntil { await results.values.count == 1 })
        try? await Task.sleep(nanoseconds: 300_000_000)
        #expect(await lookups.values.count == 1)
        #expect(await results.values.count == 1)
        await fetcher.stop()
    }

    @Test func keychainDeniedStopsAutoRetry() async {
        let results = Recorder<FetchResult>()
        let fetcher = UsageFetcher(environment: environment(credential: { CredentialResult(status: .accessDenied) }),
                                   interval: 0.05, onResult: { await results.append($0) })
        await fetcher.start()
        #expect(await waitUntil { await results.values.count == 1 })
        try? await Task.sleep(nanoseconds: 300_000_000)
        #expect(await results.values.count == 1)
        #expect(await results.values.first?.status == .keychainDenied)
        await fetcher.requestRefresh()
        #expect(await waitUntil { await results.values.count == 2 })
        await fetcher.stop()
    }

    @Test func rateLimitBacksOff() async {
        let results = Recorder<FetchResult>()
        let fetcher = UsageFetcher(environment: environment(response: { _ in HTTPResponse(status: 429) }),
                                   interval: 0.05, onResult: { await results.append($0) })
        await fetcher.start()
        try? await Task.sleep(nanoseconds: 500_000_000)
        await fetcher.stop()
        // Fetches at 0, 0.1 and 0.3 s; without backoff a 0.05 s interval would give about ten.
        let count = await results.values.count
        #expect(count >= 2 && count <= 4)
    }

    @Test func mapsCredentialProblems() async {
        let requests = Recorder<String>()
        func fetch(_ credential: CredentialResult) async -> FetchResult {
            let fetcher = UsageFetcher(environment: environment(credential: { credential }, response: { token in
                await requests.append(token)
                return HTTPResponse(status: 200, body: UsageFetcherTests.okBody)
            }), interval: 60, onResult: { _ in })
            return await fetcher.fetchOnce()
        }
        #expect(await fetch(CredentialResult(status: .notFound)).status == .notLoggedIn)
        #expect(await fetch(CredentialResult(status: .malformed)).status == .notLoggedIn)
        #expect(await fetch(CredentialResult(status: .accessDenied)).status == .keychainDenied)
        var expired = UsageFetcherTests.validCredential
        expired.status = .expired
        let expiredResult = await fetch(expired)
        #expect(expiredResult.status == .tokenExpired)
        #expect(expiredResult.planLabel == "Max 5x")
        #expect(await requests.values.isEmpty)
    }

    @Test func passesTokenAndPlanToResult() async {
        let requests = Recorder<String>()
        let fetcher = UsageFetcher(environment: environment(response: { token in
            await requests.append(token)
            return HTTPResponse(status: 200, body: UsageFetcherTests.okBody)
        }), interval: 60, onResult: { _ in })
        let result = await fetcher.fetchOnce()
        #expect(await requests.values == ["token"])
        #expect(result.planLabel == "Max 5x")
        #expect(result.limits.fiveHour?.percent == 42)
    }
}
```

- [ ] **Step 2: 테스트가 실패하는지 확인한다**

Run: `cd macos && swift test --filter "WakeSignalTests|UsageFetcherTests"`
Expected: 컴파일 실패 — `cannot find 'WakeSignal' in scope`.

- [ ] **Step 3: WakeSignal 과 UsageFetcher 를 만든다**

`macos/Sources/TokenViewerCore/Worker/WakeSignal.swift`:

```swift
import Foundation

/// Lets a worker loop sleep until a timeout or an early wake-up, whichever comes first.
/// A wake-up sent while nobody waits is kept and ends the next wait at once.
public actor WakeSignal {
    private var waiter: CheckedContinuation<Void, Never>?
    private var timer: Task<Void, Never>?
    private var generation = 0
    private var isPending = false

    public init() {}

    /// Waits for `wake()` or the timeout. A nil timeout waits for `wake()` only.
    public func wait(timeout: TimeInterval?) async {
        if isPending {
            isPending = false
            return
        }
        generation += 1
        let current = generation
        if let timeout {
            timer = Task { [weak self] in
                try? await Task.sleep(nanoseconds: UInt64(max(timeout, 0) * 1_000_000_000))
                await self?.timeOut(generation: current)
            }
        }
        await withCheckedContinuation { waiter = $0 }
    }

    public func wake() {
        if waiter == nil {
            isPending = true
        } else {
            resume()
        }
    }

    private func timeOut(generation timedOut: Int) {
        // A cancelled or older timer must not end a newer wait.
        guard timedOut == generation, waiter != nil else {
            return
        }
        resume()
    }

    private func resume() {
        timer?.cancel()
        timer = nil
        let continuation = waiter
        waiter = nil
        continuation?.resume()
    }
}
```

`macos/Sources/TokenViewerCore/Worker/UsageFetcher.swift`:

```swift
import Foundation

/// The system calls UsageFetcher needs; the app passes the Keychain and URLSession, tests pass fakes.
public struct UsageFetchEnvironment: Sendable {
    public var loadCredential: @Sendable () async -> CredentialResult
    public var requestUsage: @Sendable (_ accessToken: String) async -> HTTPResponse

    public init(loadCredential: @escaping @Sendable () async -> CredentialResult,
                requestUsage: @escaping @Sendable (_ accessToken: String) async -> HTTPResponse) {
        self.loadCredential = loadCredential
        self.requestUsage = requestUsage
    }
}

/// Polls the usage API on its own schedule and hands each result to `onResult` (spec 4.1).
public actor UsageFetcher {
    private let environment: UsageFetchEnvironment
    private let onResult: @Sendable (FetchResult) async -> Void
    private let signal = WakeSignal()
    private var interval: TimeInterval
    private var loop: Task<Void, Never>?
    private var isFetching = false

    public init(environment: UsageFetchEnvironment, interval: TimeInterval, onResult: @escaping @Sendable (FetchResult) async -> Void) {
        self.environment = environment
        self.interval = interval
        self.onResult = onResult
    }

    public func start() {
        guard loop == nil else {
            return
        }
        loop = Task { await self.run() }
    }

    public func stop() async {
        loop?.cancel()
        loop = nil
        await signal.wake()
    }

    /// A request made while a fetch is running is answered by that fetch, so prompts and calls never stack up.
    public func requestRefresh() async {
        guard !isFetching else {
            return
        }
        await signal.wake()
    }

    /// Takes effect from the next wait.
    public func setInterval(_ seconds: TimeInterval) {
        interval = seconds
    }

    func fetchOnce() async -> FetchResult {
        let credential = await environment.loadCredential()
        let planLabel = credential.credential.map {
            CredentialParser.planLabel(subscriptionType: $0.subscriptionType, rateLimitTier: $0.rateLimitTier)
        } ?? ""
        switch credential.status {
        case .notFound, .malformed:
            return FetchResult(status: .notLoggedIn, detail: "Claude Code 로그인 정보를 찾지 못했습니다")
        case .accessDenied:
            return FetchResult(status: .keychainDenied, detail: "Keychain 접근이 거부되었습니다")
        case .expired:
            return FetchResult(status: .tokenExpired, planLabel: planLabel, detail: "로그인 토큰이 만료되었습니다")
        case .ok:
            break
        }
        guard let accessToken = credential.credential?.accessToken else {
            return FetchResult(status: .notLoggedIn, detail: "Claude Code 로그인 정보를 찾지 못했습니다")
        }
        var result = UsageResponseParser.map(await environment.requestUsage(accessToken))
        result.planLabel = planLabel
        return result
    }

    private func run() async {
        var backoff: TimeInterval = 0
        while !Task.isCancelled {
            isFetching = true
            let result = await fetchOnce()
            isFetching = false
            let decision = RefreshPolicy.next(after: result.status, baseInterval: interval, previousDelay: backoff)
            backoff = decision.delay
            await onResult(result)
            if Task.isCancelled {
                break
            }
            // Outside of a rate-limit backoff, always use the latest interval from the settings.
            let delay = result.status == .rateLimited ? backoff : interval
            await signal.wait(timeout: decision.autoRetry ? delay : nil)
        }
    }
}
```

- [ ] **Step 4: 테스트가 통과하는지 확인한다**

Run: `cd macos && swift test --filter "WakeSignalTests|UsageFetcherTests"`
Expected: 11 tests passed.

- [ ] **Step 5: 커밋**

```bash
git add macos/Sources/TokenViewerCore/Worker macos/Tests/TokenViewerCoreTests
git commit -m "feat: poll the usage API from an actor with backoff

Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>"
```

---

### Task 12: LogScanner

**Files:**
- Create: `macos/Sources/TokenViewerCore/Worker/LogScanner.swift`, `macos/Tests/TokenViewerCoreTests/LogScannerTests.swift`

**Interfaces:**
- Consumes: `LogStore`, `TokenAggregator`, `PriceTable`, `WakeSignal`, `Breakdown*`
- Produces: `struct BreakdownQuery: Equatable { dimension; period; fiveHourResetsAt: Date? }`, `actor LogScanner { init(root: URL, prices: PriceTable?, interval: TimeInterval, query: BreakdownQuery, onUpdate: @escaping @Sendable (Breakdown) async -> Void); func start(); func stop() async; func requestRescan() async; func setQuery(_ query: BreakdownQuery, revision: Int) async }`

- [ ] **Step 1: 실패하는 테스트를 쓴다**

`macos/Tests/TokenViewerCoreTests/LogScannerTests.swift`:

```swift
import Foundation
import Testing
@testable import TokenViewerCore

@Suite struct LogScannerTests {
    private let recent = TestSupport.isoString(Date().addingTimeInterval(-3600))

    private func makeScanner(root: URL, updates: Recorder<Breakdown>) throws -> LogScanner {
        LogScanner(root: root, prices: try TestSupport.prices(), interval: 60,
                   query: BreakdownQuery(dimension: .project, period: .thirtyDays),
                   onUpdate: { await updates.append($0) })
    }

    @Test func publishesOnStartAndRescan() async throws {
        let dir = try TemporaryDirectory()
        try dir.write("-w-app/s1.jsonl", [LogLines.assistant(id: "a", cwd: "/w/app", timestamp: recent)])
        let updates = Recorder<Breakdown>()
        let scanner = try makeScanner(root: dir.url, updates: updates)
        await scanner.start()
        #expect(await waitUntil { await updates.values.last?.rows.map(\.label) == ["app"] })
        try dir.write("-w-web/s2.jsonl", [LogLines.assistant(id: "b", cwd: "/w/web", timestamp: recent)])
        await scanner.requestRescan()
        #expect(await waitUntil { await updates.values.last?.rows.count == 2 })
        await scanner.stop()
    }

    @Test func setQueryRegroupsRightAway() async throws {
        let dir = try TemporaryDirectory()
        try dir.write("-w-app/s1.jsonl", [LogLines.assistant(id: "a", cwd: "/w/app", timestamp: recent)])
        let updates = Recorder<Breakdown>()
        let scanner = try makeScanner(root: dir.url, updates: updates)
        await scanner.start()
        #expect(await waitUntil { await !updates.values.isEmpty })
        await scanner.setQuery(BreakdownQuery(dimension: .model, period: .thirtyDays), revision: 1)
        #expect(await updates.values.last?.rows.map(\.label) == ["Opus 5.5"])
        await scanner.stop()
    }

    @Test func ignoresOlderQueryRevisions() async throws {
        let dir = try TemporaryDirectory()
        try dir.write("-w-app/s1.jsonl", [LogLines.assistant(id: "a", cwd: "/w/app", timestamp: recent)])
        let updates = Recorder<Breakdown>()
        let scanner = try makeScanner(root: dir.url, updates: updates)
        await scanner.start()
        #expect(await waitUntil { await !updates.values.isEmpty })
        await scanner.setQuery(BreakdownQuery(dimension: .model, period: .thirtyDays), revision: 2)
        await scanner.setQuery(BreakdownQuery(dimension: .project, period: .thirtyDays), revision: 1)
        #expect(await updates.values.last?.rows.map(\.label) == ["Opus 5.5"])
        await scanner.stop()
    }

    @Test func emptyRootPublishesEmptyBreakdown() async throws {
        let dir = try TemporaryDirectory()
        let updates = Recorder<Breakdown>()
        let scanner = try makeScanner(root: dir.url.appendingPathComponent("missing"), updates: updates)
        await scanner.start()
        #expect(await waitUntil { await updates.values.last == Breakdown() })
        await scanner.stop()
    }
}
```

- [ ] **Step 2: 테스트가 실패하는지 확인한다**

Run: `cd macos && swift test --filter LogScannerTests`
Expected: 컴파일 실패 — `cannot find 'LogScanner' in scope`.

- [ ] **Step 3: LogScanner 를 만든다**

`macos/Sources/TokenViewerCore/Worker/LogScanner.swift`:

```swift
import Foundation

public struct BreakdownQuery: Sendable, Equatable {
    public var dimension: BreakdownDimension
    public var period: BreakdownPeriod
    public var fiveHourResetsAt: Date?

    public init(dimension: BreakdownDimension, period: BreakdownPeriod, fiveHourResetsAt: Date? = nil) {
        self.dimension = dimension
        self.period = period
        self.fiveHourResetsAt = fiveHourResetsAt
    }
}

/// Scans `~/.claude/projects` off the main thread and publishes the breakdown for the current tab and period (spec 4.2).
public actor LogScanner {
    private let root: URL
    private let prices: PriceTable?
    private let interval: TimeInterval
    private let onUpdate: @Sendable (Breakdown) async -> Void
    private let store = LogStore()
    private let signal = WakeSignal()
    private var query: BreakdownQuery
    private var queryRevision = 0
    private var loop: Task<Void, Never>?

    public init(root: URL, prices: PriceTable?, interval: TimeInterval, query: BreakdownQuery,
                onUpdate: @escaping @Sendable (Breakdown) async -> Void) {
        self.root = root
        self.prices = prices
        self.interval = interval
        self.query = query
        self.onUpdate = onUpdate
    }

    public func start() {
        guard loop == nil else {
            return
        }
        loop = Task { await self.run() }
    }

    public func stop() async {
        loop?.cancel()
        loop = nil
        await signal.wake()
    }

    public func requestRescan() async {
        await signal.wake()
    }

    /// Re-groups right away. Queries come from separate tasks and can arrive out of order, so older revisions are dropped.
    public func setQuery(_ query: BreakdownQuery, revision: Int) async {
        guard revision > queryRevision else {
            return
        }
        self.query = query
        queryRevision = revision
        await publish()
    }

    private func run() async {
        while !Task.isCancelled {
            store.scan(root: root, now: Date(), shouldContinue: { !Task.isCancelled })
            // Periods move with the clock, so publish every pass even when no file changed.
            await publish()
            await signal.wait(timeout: interval)
        }
    }

    private func publish() async {
        let now = Date()
        let start = TokenAggregator.periodStart(query.period, now: now, fiveHourResetsAt: query.fiveHourResetsAt)
        let breakdown = TokenAggregator.aggregate(store.snapshot, prices: prices, dimension: query.dimension, from: start)
        await onUpdate(breakdown)
    }
}
```

- [ ] **Step 4: 테스트가 통과하는지 확인한다**

Run: `cd macos && swift test --filter LogScannerTests`
Expected: 4 tests passed.

- [ ] **Step 5: 커밋**

```bash
git add macos/Sources/TokenViewerCore/Worker/LogScanner.swift macos/Tests/TokenViewerCoreTests/LogScannerTests.swift
git commit -m "feat: scan session logs from an actor and publish breakdowns

Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>"
```

---

### Task 13: Palette, StatusIconRenderer

**Files:**
- Create: `macos/Sources/TokenViewerCore/Render/Palette.swift`, `macos/Sources/TokenViewerCore/Render/StatusIconRenderer.swift`
- Create: `macos/Tests/TokenViewerCoreTests/StatusIconRendererTests.swift`

**Interfaces:**
- Consumes: `UsageStatus`, `LimitDisplayState` (Task 9)
- Produces:
  - `struct RGBA { red, green, blue, alpha; init(_:_:_:_:); init(hex:) (internal); var cgColor }`, `enum BarLevel { normal, warning, critical }`, `enum MenuBarAppearance { light, dark }`, `enum Palette { ink(_:), level(_:_:), meter(_:_:) }`
  - `struct StatusIconState: Equatable { fiveHourPercent: Double?; sevenDayPercent: Double?; isStale; warnPercent; criticalPercent; init(usage:warnPercent:criticalPercent:now:) }`
  - `struct StatusIconImage { image: CGImage; size: CGSize; isTemplate: Bool }`
  - `enum StatusIconRenderer { level(percent:warn:critical:), isTemplate(_:), label(_:), render(_:appearance:scale:) -> StatusIconImage? }`

- [ ] **Step 1: 실패하는 테스트를 쓴다**

`macos/Tests/TokenViewerCoreTests/StatusIconRendererTests.swift`:

```swift
import CoreGraphics
import Foundation
import Testing
@testable import TokenViewerCore

@Suite struct StatusIconRendererTests {
    private let now = TestSupport.date("2026-10-05T12:00:00Z")

    private struct Pixel {
        var red: Int
        var green: Int
        var blue: Int
        var alpha: Int

        func isClose(to color: UInt32, tolerance: Int = 4) -> Bool {
            abs(red - Int((color >> 16) & 0xff)) <= tolerance
                && abs(green - Int((color >> 8) & 0xff)) <= tolerance
                && abs(blue - Int(color & 0xff)) <= tolerance
        }
    }

    /// Reads the pixel at a point position (origin top-left) from the rendered icon.
    private func pixel(_ icon: StatusIconImage, x: CGFloat, y: CGFloat, scale: CGFloat = 2) -> Pixel {
        let image = icon.image
        var bytes = [UInt8](repeating: 0, count: image.width * image.height * 4)
        bytes.withUnsafeMutableBytes { buffer in
            let context = CGContext(data: buffer.baseAddress, width: image.width, height: image.height, bitsPerComponent: 8,
                                    bytesPerRow: image.width * 4, space: CGColorSpace(name: CGColorSpace.sRGB)!,
                                    bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!
            context.draw(image, in: CGRect(x: 0, y: 0, width: image.width, height: image.height))
        }
        let offset = (Int(y * scale) * image.width + Int(x * scale)) * 4
        return Pixel(red: Int(bytes[offset]), green: Int(bytes[offset + 1]), blue: Int(bytes[offset + 2]), alpha: Int(bytes[offset + 3]))
    }

    private func render(_ state: StatusIconState, _ appearance: MenuBarAppearance = .light, scale: CGFloat = 2) throws -> StatusIconImage {
        try #require(StatusIconRenderer.render(state, appearance: appearance, scale: scale))
    }

    @Test func classifiesLevels() {
        #expect(StatusIconRenderer.level(percent: 69.9, warn: 70, critical: 90) == .normal)
        #expect(StatusIconRenderer.level(percent: 70, warn: 70, critical: 90) == .warning)
        #expect(StatusIconRenderer.level(percent: 89.9, warn: 70, critical: 90) == .warning)
        #expect(StatusIconRenderer.level(percent: 90, warn: 70, critical: 90) == .critical)
    }

    @Test func isTemplateOnlyWhenBothNormal() {
        #expect(StatusIconRenderer.isTemplate(StatusIconState(fiveHourPercent: 10, sevenDayPercent: 20)))
        #expect(!StatusIconRenderer.isTemplate(StatusIconState(fiveHourPercent: 75, sevenDayPercent: 20)))
        #expect(!StatusIconRenderer.isTemplate(StatusIconState(fiveHourPercent: 10, sevenDayPercent: 95)))
        #expect(StatusIconRenderer.isTemplate(StatusIconState()))
    }

    @Test func formatsLabel() {
        #expect(StatusIconRenderer.label(StatusIconState(fiveHourPercent: 41.6)) == "42%")
        #expect(StatusIconRenderer.label(StatusIconState(fiveHourPercent: 120)) == "100%")
        #expect(StatusIconRenderer.label(StatusIconState(sevenDayPercent: 50)) == "—")
    }

    @Test func fillsFiveHourBarProportionally() throws {
        let icon = try render(StatusIconState(fiveHourPercent: 50, sevenDayPercent: 0))
        #expect(pixel(icon, x: 4, y: 6).alpha > 200)
        #expect(pixel(icon, x: 15, y: 6).alpha < 40)
        #expect(icon.isTemplate)
    }

    @Test func usesLevelColorWhenWarning() throws {
        #expect(pixel(try render(StatusIconState(fiveHourPercent: 75), .light), x: 4, y: 6).isClose(to: 0xc98500))
        #expect(pixel(try render(StatusIconState(fiveHourPercent: 75), .dark), x: 4, y: 6).isClose(to: 0xfab219))
        #expect(pixel(try render(StatusIconState(fiveHourPercent: 95), .dark), x: 4, y: 6).isClose(to: 0xff6b6b))
    }

    @Test func colorsSevenDayBarIndependently() throws {
        let icon = try render(StatusIconState(fiveHourPercent: 10, sevenDayPercent: 95), .light)
        #expect(pixel(icon, x: 4, y: 12).isClose(to: 0xd03b3b))
        let fiveHour = pixel(icon, x: 1, y: 6)
        #expect(fiveHour.isClose(to: 0x000000, tolerance: 30))
        #expect(fiveHour.alpha > 200)
        #expect(!icon.isTemplate)
    }

    @Test func dimsStaleState() throws {
        let alpha = pixel(try render(StatusIconState(fiveHourPercent: 50, isStale: true)), x: 4, y: 6).alpha
        #expect((118...137).contains(alpha))
    }

    @Test func emptyStateHasNoFill() throws {
        let icon = try render(StatusIconState())
        #expect(pixel(icon, x: 4, y: 6).alpha < 40)
        #expect(icon.isTemplate)
    }

    @Test func scalesWithBackingFactor() throws {
        let double = try render(StatusIconState(fiveHourPercent: 50), scale: 2)
        #expect(double.size.height == 18)
        #expect(double.image.height == 36)
        #expect(double.image.width == Int((double.size.width * 2).rounded()))
        #expect(try render(StatusIconState(fiveHourPercent: 50), scale: 3).image.height == 54)
    }

    @Test func buildsStateFromUsage() {
        var usage = UsageStatus()
        #expect(StatusIconState(usage: usage, warnPercent: 70, criticalPercent: 90, now: now) == StatusIconState())
        usage.apply(FetchResult(status: .ok, limits: UsageLimits(fiveHour: LimitWindow(percent: 42, resetsAt: nil))), now: now)
        #expect(StatusIconState(usage: usage, warnPercent: 60, criticalPercent: 80, now: now)
                == StatusIconState(fiveHourPercent: 42, warnPercent: 60, criticalPercent: 80))
        #expect(StatusIconState(usage: usage, warnPercent: 70, criticalPercent: 90, now: now.addingTimeInterval(16 * 60)).isStale)
    }

    @Test func paletteMatchesSpec() {
        #expect(Palette.meter(.normal, .light) == RGBA(hex: 0x2a78d6))
        #expect(Palette.meter(.normal, .dark) == RGBA(hex: 0x3987e5))
        #expect(Palette.meter(.critical, .light) == RGBA(hex: 0xd03b3b))
        #expect(Palette.level(.normal, .dark) == RGBA(1, 1, 1))
        #expect(Palette.level(.warning, .light) == RGBA(hex: 0xc98500))
    }
}
```

- [ ] **Step 2: 테스트가 실패하는지 확인한다**

Run: `cd macos && swift test --filter StatusIconRendererTests`
Expected: 컴파일 실패 — `cannot find type 'StatusIconImage' in scope`.

- [ ] **Step 3: Palette 와 StatusIconRenderer 를 만든다**

`macos/Sources/TokenViewerCore/Render/Palette.swift`:

```swift
import CoreGraphics

public struct RGBA: Sendable, Equatable {
    public var red: Double
    public var green: Double
    public var blue: Double
    public var alpha: Double

    public init(_ red: Double, _ green: Double, _ blue: Double, _ alpha: Double = 1) {
        self.red = red
        self.green = green
        self.blue = blue
        self.alpha = alpha
    }

    init(hex: UInt32) {
        self.init(Double((hex >> 16) & 0xff) / 255, Double((hex >> 8) & 0xff) / 255, Double(hex & 0xff) / 255)
    }

    public var cgColor: CGColor { CGColor(srgbRed: red, green: green, blue: blue, alpha: alpha) }

    func withAlpha(_ alpha: Double) -> RGBA {
        RGBA(red, green, blue, alpha)
    }
}

public enum BarLevel: Sendable, Equatable {
    case normal
    case warning
    case critical
}

public enum MenuBarAppearance: Sendable, Equatable {
    case light
    case dark
}

/// Colors from v0.1.0 spec 2.1 and 2.2.
public enum Palette {
    public static func ink(_ appearance: MenuBarAppearance) -> RGBA {
        appearance == .dark ? RGBA(1, 1, 1) : RGBA(0, 0, 0)
    }

    public static func level(_ level: BarLevel, _ appearance: MenuBarAppearance) -> RGBA {
        let isDark = appearance == .dark
        switch level {
        case .warning:
            return RGBA(hex: isDark ? 0xfab219 : 0xc98500)
        case .critical:
            return RGBA(hex: isDark ? 0xff6b6b : 0xd03b3b)
        case .normal:
            return ink(appearance)
        }
    }

    /// The dropdown meters are blue rather than ink while nothing is wrong.
    public static func meter(_ level: BarLevel, _ appearance: MenuBarAppearance) -> RGBA {
        level == .normal ? RGBA(hex: appearance == .dark ? 0x3987e5 : 0x2a78d6) : self.level(level, appearance)
    }
}
```

`macos/Sources/TokenViewerCore/Render/StatusIconRenderer.swift`:

```swift
import CoreGraphics
import CoreText
import Foundation

public struct StatusIconState: Sendable, Equatable {
    public var fiveHourPercent: Double?
    public var sevenDayPercent: Double?
    public var isStale: Bool
    public var warnPercent: Int
    public var criticalPercent: Int

    public init(fiveHourPercent: Double? = nil, sevenDayPercent: Double? = nil, isStale: Bool = false,
                warnPercent: Int = 70, criticalPercent: Int = 90) {
        self.fiveHourPercent = fiveHourPercent
        self.sevenDayPercent = sevenDayPercent
        self.isStale = isStale
        self.warnPercent = warnPercent
        self.criticalPercent = criticalPercent
    }

    /// What the menu bar shows for the current usage state.
    public init(usage: UsageStatus, warnPercent: Int, criticalPercent: Int, now: Date) {
        self.init(warnPercent: warnPercent, criticalPercent: criticalPercent)
        let display = usage.displayState(now: now)
        guard display != .empty, let limits = usage.limits else {
            return
        }
        fiveHourPercent = limits.fiveHour?.percent
        sevenDayPercent = limits.sevenDay?.percent
        isStale = display == .stale
    }
}

public struct StatusIconImage {
    public let image: CGImage
    /// Size in points; the image itself is `size × scale` pixels.
    public let size: CGSize
    public let isTemplate: Bool
}

/// Draws the menu bar item: a 5-hour bar over a weekly bar, then the 5-hour % (v0.1.0 spec 2.1).
public enum StatusIconRenderer {
    public static let height: CGFloat = 18
    public static let barWidth: CGFloat = 22
    public static let barHeight: CGFloat = 4
    public static let fiveHourBarTop: CGFloat = 4
    public static let sevenDayBarTop: CGFloat = 10
    public static let textGap: CGFloat = 5
    public static let fontSize: CGFloat = 13
    public static let dimmedOpacity: CGFloat = 0.5
    private static let minVisibleFill: CGFloat = 2
    private static let outlineAlpha = 0.45
    private static let outlineRadius: CGFloat = 1.5
    private static let fillRadius: CGFloat = 2
    private static let emptyLabel = "—"

    public static func level(percent: Double, warn: Int, critical: Int) -> BarLevel {
        if percent >= Double(critical) {
            return .critical
        }
        if percent >= Double(warn) {
            return .warning
        }
        return .normal
    }

    /// Only an all-normal icon is a template image, which macOS tints for the menu bar itself.
    public static func isTemplate(_ state: StatusIconState) -> Bool {
        levels(state).five == .normal && levels(state).seven == .normal
    }

    public static func label(_ state: StatusIconState) -> String {
        guard let percent = state.fiveHourPercent else {
            return emptyLabel
        }
        return "\(Int(clamp(percent).rounded()))%"
    }

    public static func render(_ state: StatusIconState, appearance: MenuBarAppearance, scale: CGFloat) -> StatusIconImage? {
        let isTemplate = isTemplate(state)
        // A template image only carries alpha and macOS picks the color, so draw it in plain black.
        let ink = Palette.ink(isTemplate ? .light : appearance)
        let (fiveLevel, sevenLevel) = levels(state)
        let fiveColor = fiveLevel == .normal ? ink : Palette.level(fiveLevel, appearance)
        let sevenColor = sevenLevel == .normal ? ink : Palette.level(sevenLevel, appearance)

        let font = CTFontCreateUIFontForLanguage(.system, fontSize, nil) ?? CTFontCreateWithName("Helvetica" as CFString, fontSize, nil)
        let attributes: [NSAttributedString.Key: Any] = [
            NSAttributedString.Key(kCTFontAttributeName as String): font,
            NSAttributedString.Key(kCTForegroundColorAttributeName as String): fiveColor.cgColor,
        ]
        let line = CTLineCreateWithAttributedString(NSAttributedString(string: label(state), attributes: attributes))
        var ascent: CGFloat = 0
        var descent: CGFloat = 0
        var leading: CGFloat = 0
        let textWidth = CGFloat(CTLineGetTypographicBounds(line, &ascent, &descent, &leading))
        let width = ceil(barWidth + textGap + textWidth + 1)

        let pixelWidth = Int((width * scale).rounded())
        let pixelHeight = Int((height * scale).rounded())
        guard let colorSpace = CGColorSpace(name: CGColorSpace.sRGB),
              let context = CGContext(data: nil, width: pixelWidth, height: pixelHeight, bitsPerComponent: 8, bytesPerRow: 0,
                                      space: colorSpace, bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue) else {
            return nil
        }
        // Draw in points with the origin at the top-left, like the Qt painter did.
        context.translateBy(x: 0, y: CGFloat(pixelHeight))
        context.scaleBy(x: scale, y: -scale)
        let isEmpty = state.fiveHourPercent == nil && state.sevenDayPercent == nil
        context.setAlpha(state.isStale || isEmpty ? dimmedOpacity : 1)

        let outline = ink.withAlpha(outlineAlpha)
        drawBar(context, top: fiveHourBarTop, percent: state.fiveHourPercent, fill: fiveColor, outline: outline)
        drawBar(context, top: sevenDayBarTop, percent: state.sevenDayPercent, fill: sevenColor, outline: outline)

        // Core Text draws upward from the baseline, so flip back around it.
        context.saveGState()
        context.translateBy(x: barWidth + textGap, y: (height - (ascent + descent)) / 2 + ascent)
        context.scaleBy(x: 1, y: -1)
        context.textMatrix = .identity
        context.textPosition = .zero
        CTLineDraw(line, context)
        context.restoreGState()

        guard let image = context.makeImage() else {
            return nil
        }
        return StatusIconImage(image: image, size: CGSize(width: width, height: height), isTemplate: isTemplate)
    }

    private static func levels(_ state: StatusIconState) -> (five: BarLevel, seven: BarLevel) {
        let five = state.fiveHourPercent.map { level(percent: $0, warn: state.warnPercent, critical: state.criticalPercent) } ?? .normal
        let seven = state.sevenDayPercent.map { level(percent: $0, warn: state.warnPercent, critical: state.criticalPercent) } ?? .normal
        return (five, seven)
    }

    private static func drawBar(_ context: CGContext, top: CGFloat, percent: Double?, fill: RGBA, outline: RGBA) {
        context.setStrokeColor(outline.cgColor)
        context.setLineWidth(1)
        context.addPath(roundedPath(CGRect(x: 0.5, y: top + 0.5, width: barWidth - 1, height: barHeight - 1), radius: outlineRadius))
        context.strokePath()

        guard let percent else {
            return
        }
        let fillWidth = barWidth * CGFloat(clamp(percent)) / 100
        guard fillWidth > 0 else {
            return
        }
        context.setFillColor(fill.cgColor)
        context.addPath(roundedPath(CGRect(x: 0, y: top, width: max(fillWidth, minVisibleFill), height: barHeight), radius: fillRadius))
        context.fillPath()
    }

    private static func roundedPath(_ rect: CGRect, radius: CGFloat) -> CGPath {
        // CGPath rejects corners larger than half the rect, which a 2 pt sliver would hit.
        let corner = min(radius, rect.width / 2, rect.height / 2)
        return CGPath(roundedRect: rect, cornerWidth: corner, cornerHeight: corner, transform: nil)
    }

    private static func clamp(_ percent: Double) -> Double {
        min(max(percent, 0), 100)
    }
}
```

- [ ] **Step 4: 테스트가 통과하는지 확인한다**

Run: `cd macos && swift test --filter StatusIconRendererTests`
Expected: 11 tests passed.

- [ ] **Step 5: 전체 테스트를 돌리고 커밋한다**

Run: `cd macos && swift test`
Expected: 모든 테스트 통과.

```bash
git add macos/Sources/TokenViewerCore/Render macos/Tests/TokenViewerCoreTests/StatusIconRendererTests.swift
git commit -m "feat: draw the menu bar icon with Core Graphics

Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>"
```

---

### Task 14: 시스템 어댑터 (Keychain, URLSession, 로그인 항목, 오류 로그)

단위 테스트가 없다 (실제 Keychain·네트워크·로그인 항목을 부르면 안 된다). 빌드와 Core 의존성 검사로 확인하고, Task 15 에서 실제 실행으로 확인한다.

**Files:**
- Create: `macos/Sources/TokenViewer/App/AppInfo.swift`, `macos/Sources/TokenViewer/System/{KeychainReader,UsageClient,LoginItem,ErrorLog}.swift`

**Interfaces:**
- Consumes: `AppConstants`, `CredentialParser.resolve`, `SecretLookup`, `HTTPResponse`, `FetchResult`
- Produces: `enum AppInfo { version; repositoryURL: URL?; pricesURL: URL? }`, `enum KeychainReader { static func loadCredential(now: Date) async -> CredentialResult }`, `enum UsageClient { static func request(accessToken: String) async -> HTTPResponse }`, `enum LoginItem { static func apply(enabled: Bool); static func unregister() }`, `enum ErrorLog { static func writeBadResponse(_ result: FetchResult) }`

- [ ] **Step 1: 네 어댑터와 AppInfo 를 만든다**

`macos/Sources/TokenViewer/App/AppInfo.swift`:

```swift
import Foundation

enum AppInfo {
    static var version: String {
        Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String ?? "dev"
    }

    /// Set by build-app.sh from the git remote; nil in builds without one.
    static var repositoryURL: URL? {
        guard let repository = Bundle.main.object(forInfoDictionaryKey: "TVRepository") as? String, !repository.isEmpty else {
            return nil
        }
        return URL(string: "https://github.com/\(repository)")
    }

    static var pricesURL: URL? {
        Bundle.main.url(forResource: "prices", withExtension: "json")
    }
}
```

`macos/Sources/TokenViewer/System/KeychainReader.swift`:

```swift
import Foundation
import Security
import TokenViewerCore

/// Reads Claude Code's credential (v0.1.0 spec 3.2): Keychain first, then `~/.claude/.credentials.json`.
enum KeychainReader {
    // SecItemCopyMatching blocks while the macOS "allow access" prompt is open; keep that off the Swift concurrency pool.
    private static let queue = DispatchQueue(label: "com.tokenviewer.keychain")

    static func loadCredential(now: Date) async -> CredentialResult {
        let lookup = await withCheckedContinuation { continuation in
            queue.async {
                continuation.resume(returning: copySecret(service: AppConstants.keychainService))
            }
        }
        let fileURL = FileManager.default.homeDirectoryForCurrentUser.appendingPathComponent(".claude/.credentials.json")
        return CredentialParser.resolve(keychain: lookup, readFile: { try? Data(contentsOf: fileURL) }, now: now)
    }

    private static func copySecret(service: String) -> SecretLookup {
        let query: [String: Any] = [
            kSecClass as String: kSecClassGenericPassword,
            kSecAttrService as String: service,
            kSecReturnData as String: true,
            kSecMatchLimit as String: kSecMatchLimitOne,
        ]
        var result: CFTypeRef?
        switch SecItemCopyMatching(query as CFDictionary, &result) {
        case errSecSuccess:
            guard let data = result as? Data else {
                return .notFound
            }
            return .found(data)
        case errSecItemNotFound:
            return .notFound
        default:
            return .denied
        }
    }
}
```

`macos/Sources/TokenViewer/System/UsageClient.swift`:

```swift
import Foundation
import TokenViewerCore

/// GET /api/oauth/usage (v0.1.0 spec 3.1). The token lives only in this request.
enum UsageClient {
    // Ephemeral: no cookies, no cache, nothing written to disk.
    private static let session = URLSession(configuration: .ephemeral)

    static func request(accessToken: String) async -> HTTPResponse {
        var request = URLRequest(url: AppConstants.usageURL, timeoutInterval: AppConstants.requestTimeout)
        request.setValue("Bearer \(accessToken)", forHTTPHeaderField: "Authorization")
        request.setValue(AppConstants.usageBetaHeader, forHTTPHeaderField: "anthropic-beta")
        request.setValue("application/json", forHTTPHeaderField: "Accept")
        request.setValue("TokenViewer/\(AppInfo.version)", forHTTPHeaderField: "User-Agent")
        do {
            let (data, response) = try await session.data(for: request)
            return HTTPResponse(status: (response as? HTTPURLResponse)?.statusCode ?? 0, body: data)
        } catch let error as URLError where error.code == .timedOut {
            return HTTPResponse(status: 0, errorText: "응답 시간 초과")
        } catch {
            return HTTPResponse(status: 0, errorText: error.localizedDescription)
        }
    }
}
```

`macos/Sources/TokenViewer/System/LoginItem.swift`:

```swift
import Foundation
import os
import ServiceManagement
import TokenViewerCore

/// Launch at login through SMAppService, so System Settings lists the app by name and removal leaves nothing behind.
enum LoginItem {
    private static let log = Logger(subsystem: AppConstants.bundleIdentifier, category: "LoginItem")

    /// Matches the registration to `enabled`.
    static func apply(enabled: Bool) {
        let service = SMAppService.mainApp
        do {
            if enabled {
                guard isInstalled, service.status == .notRegistered || service.status == .notFound else {
                    return
                }
                try service.register()
            } else if service.status == .enabled || service.status == .requiresApproval {
                try service.unregister()
            }
        } catch {
            log.error("launch-at-login update failed: \(error.localizedDescription, privacy: .public)")
        }
    }

    /// Only an installed app registers; a build in macos/dist or `swift run` would leave a stale entry once rebuilt or moved.
    private static var isInstalled: Bool {
        let path = Bundle.main.bundleURL.path
        let userApplications = FileManager.default.homeDirectoryForCurrentUser.appendingPathComponent("Applications").path
        return Bundle.main.bundleURL.pathExtension == "app" && (path.hasPrefix("/Applications/") || path.hasPrefix(userApplications + "/"))
    }

    /// Used by `install.sh --uninstall` through the `--unregister-login-item` argument.
    static func unregister() {
        try? SMAppService.mainApp.unregister()
    }
}
```

`macos/Sources/TokenViewer/System/ErrorLog.swift`:

```swift
import Foundation
import TokenViewerCore

enum ErrorLog {
    private static let directory = FileManager.default.homeDirectoryForCurrentUser.appendingPathComponent("Library/Logs/TokenViewer")

    /// Keeps the last format-drift response for diagnosis. Only the response body is written; the request and its token never are.
    static func writeBadResponse(_ result: FetchResult) {
        try? FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
        var data = Data("\(ISO8601DateFormatter().string(from: Date())) HTTP \(result.httpStatus) \(result.detail)\n".utf8)
        data.append(result.rawBody)
        try? data.write(to: directory.appendingPathComponent("usage-response.log"), options: .atomic)
    }
}
```

- [ ] **Step 2: 빌드와 Core 의존성을 확인한다**

Run: `cd macos && swift build 2>&1 | tail -3 && grep -rnE "^import (AppKit|SwiftUI|Security|ServiceManagement)" Sources/TokenViewerCore || echo "core imports ok"`
Expected: `Build complete!` 와 `core imports ok`.

- [ ] **Step 3: 커밋**

```bash
git add macos/Sources/TokenViewer
git commit -m "feat: add Keychain, usage API, login item and error log adapters

Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>"
```

---

### Task 15: AppState, AppController, 메뉴바 아이콘

**Files:**
- Create: `macos/Sources/TokenViewer/App/{AppState,AppController,AppDelegate}.swift`, `macos/Sources/TokenViewer/UI/StatusItemController.swift`
- Modify: `macos/Sources/TokenViewer/main.swift` (전체 교체)

**Interfaces:**
- Consumes: Task 3–14 의 모든 공개 타입
- Produces: `@MainActor @Observable final class AppState { var settings: SettingsValues; private(set) usage, breakdown, now; apply(_: FetchResult); apply(_: Breakdown); tick(); displayState; iconState; breakdownQuery }`, `@MainActor final class AppController { let state: AppState; start(); refreshNow() }`, `@MainActor final class StatusItemController: NSObject { var onClick; var anchorFrame: NSRect?; update(_:) }`

- [ ] **Step 1: 상태·컨트롤러·아이콘·진입점을 만든다**

`macos/Sources/TokenViewer/App/AppState.swift`:

```swift
import Foundation
import Observation
import TokenViewerCore

/// Everything the menu bar item, the dropdown and the settings window show. Views bind to `settings` directly;
/// AppController saves and applies each change.
@MainActor
@Observable
final class AppState {
    var settings: SettingsValues
    private(set) var usage = UsageStatus()
    private(set) var breakdown = Breakdown()
    /// Advanced by a clock so ages and staleness update without new data.
    private(set) var now = Date()

    init(settings: SettingsValues) {
        self.settings = settings
    }

    func apply(_ result: FetchResult) {
        now = Date()
        usage.apply(result, now: now)
    }

    func apply(_ breakdown: Breakdown) {
        self.breakdown = breakdown
    }

    func tick() {
        now = Date()
    }

    var displayState: LimitDisplayState { usage.displayState(now: now) }

    var iconState: StatusIconState {
        StatusIconState(usage: usage, warnPercent: settings.warnPercent, criticalPercent: settings.criticalPercent, now: now)
    }

    var breakdownQuery: BreakdownQuery {
        BreakdownQuery(dimension: settings.dimension, period: settings.period, fiveHourResetsAt: usage.limits?.fiveHour?.resetsAt)
    }
}
```

`macos/Sources/TokenViewer/UI/StatusItemController.swift`:

```swift
import AppKit
import TokenViewerCore

/// The menu bar item: draws the bars and the 5-hour % and reports clicks (v0.1.0 spec 2.1).
@MainActor
final class StatusItemController: NSObject {
    var onClick: @MainActor () -> Void = {}

    private struct DrawKey: Equatable {
        var state: StatusIconState
        var appearance: MenuBarAppearance
        var scale: CGFloat
    }

    private let item = NSStatusBar.system.statusItem(withLength: NSStatusItem.variableLength)
    private var iconState = StatusIconState()
    private var drawnKey: DrawKey?
    private var appearanceObservation: NSKeyValueObservation?

    override init() {
        super.init()
        guard let button = item.button else {
            return
        }
        button.target = self
        button.action = #selector(handleClick)
        appearanceObservation = button.observe(\.effectiveAppearance) { [weak self] _, _ in
            MainActor.assumeIsolated { self?.redraw() }
        }
        redraw()
    }

    /// The item's frame in screen coordinates, for placing the dropdown under it.
    var anchorFrame: NSRect? { item.button?.window?.frame }

    func update(_ state: StatusIconState) {
        iconState = state
        redraw()
    }

    @objc private func handleClick() {
        // An LSUIElement app is never active on its own; activate it so the dropdown gets focus and closes on outside clicks.
        NSApp.activate()
        onClick()
    }

    /// Redraws only when the state, the menu bar appearance or the screen scale changed.
    private func redraw() {
        guard let button = item.button else {
            return
        }
        let isDark = button.effectiveAppearance.bestMatch(from: [.aqua, .darkAqua]) == .darkAqua
        let scale = max(button.window?.backingScaleFactor ?? NSScreen.main?.backingScaleFactor ?? 2, 2)
        let key = DrawKey(state: iconState, appearance: isDark ? .dark : .light, scale: scale)
        guard key != drawnKey, let icon = StatusIconRenderer.render(key.state, appearance: key.appearance, scale: scale) else {
            return
        }
        let image = NSImage(cgImage: icon.image, size: icon.size)
        image.isTemplate = icon.isTemplate
        button.image = image
        drawnKey = key
    }
}
```

`macos/Sources/TokenViewer/App/AppController.swift`:

```swift
import AppKit
import Observation
import TokenViewerCore

/// Owns the workers, the settings and the menu bar item, and connects them (spec 4).
@MainActor
final class AppController {
    let state: AppState

    private let store: SettingsStore
    private let statusItem = StatusItemController()
    private var appliedSettings: SettingsValues
    private var fetcher: UsageFetcher?
    private var scanner: LogScanner?
    private var queryRevision = 0
    private var clockTimer: Timer?
    private var wakeObserver: NSObjectProtocol?

    init(store: SettingsStore = SettingsStore()) {
        self.store = store
        state = AppState(settings: store.values)
        appliedSettings = store.values
    }

    func start() {
        let prices = AppInfo.pricesURL.flatMap { try? Data(contentsOf: $0) }.flatMap { PriceTable(json: $0) }
        let fetcher = UsageFetcher(
            environment: UsageFetchEnvironment(
                loadCredential: { await KeychainReader.loadCredential(now: Date()) },
                requestUsage: { await UsageClient.request(accessToken: $0) }),
            interval: Self.seconds(minutes: state.settings.intervalMinutes),
            onResult: { [weak self] result in await self?.receive(result) })
        let scanner = LogScanner(
            root: FileManager.default.homeDirectoryForCurrentUser.appendingPathComponent(".claude/projects"),
            prices: prices,
            interval: AppConstants.logScanInterval,
            query: state.breakdownQuery,
            onUpdate: { [weak self] breakdown in await self?.receive(breakdown) })
        self.fetcher = fetcher
        self.scanner = scanner

        statusItem.onClick = { [weak self] in self?.handleStatusItemClick() }
        statusItem.update(state.iconState)
        LoginItem.apply(enabled: state.settings.launchAtLogin)
        observeSettings()
        startClock()
        Task {
            await fetcher.start()
            await scanner.start()
        }
    }

    /// The ↻ and '다시 확인' buttons: fetch limits and rescan logs now.
    func refreshNow() {
        Task { [fetcher, scanner] in
            await fetcher?.requestRefresh()
            await scanner?.requestRescan()
        }
    }

    private func handleStatusItemClick() {
        refreshNow()
    }

    private func receive(_ result: FetchResult) {
        let resetsBefore = state.usage.limits?.fiveHour?.resetsAt
        state.apply(result)
        if result.status == .badResponse {
            ErrorLog.writeBadResponse(result)
        }
        // The 5-hour period starts from the API's reset time.
        if state.usage.limits?.fiveHour?.resetsAt != resetsBefore {
            pushQuery()
        }
        statusItem.update(state.iconState)
    }

    private func receive(_ breakdown: Breakdown) {
        state.apply(breakdown)
    }

    private func pushQuery() {
        queryRevision += 1
        let query = state.breakdownQuery
        let revision = queryRevision
        Task { [scanner] in await scanner?.setQuery(query, revision: revision) }
    }

    private func observeSettings() {
        withObservationTracking {
            _ = state.settings
        } onChange: { [weak self] in
            Task { @MainActor in self?.settingsDidChange() }
        }
    }

    /// Saves what the views changed, corrects it (red must stay above orange) and applies each part that moved.
    private func settingsDidChange() {
        let previous = appliedSettings
        store.save(state.settings)
        let saved = store.values
        if state.settings != saved {
            state.settings = saved
        }
        appliedSettings = saved

        if saved.intervalMinutes != previous.intervalMinutes {
            let seconds = Self.seconds(minutes: saved.intervalMinutes)
            Task { [fetcher] in await fetcher?.setInterval(seconds) }
        }
        if saved.launchAtLogin != previous.launchAtLogin {
            LoginItem.apply(enabled: saved.launchAtLogin)
        }
        if saved.dimension != previous.dimension || saved.period != previous.period {
            pushQuery()
        }
        statusItem.update(state.iconState)
        observeSettings()
    }

    private func startClock() {
        clockTimer = Timer.scheduledTimer(withTimeInterval: AppConstants.clockTickInterval, repeats: true) { [weak self] _ in
            MainActor.assumeIsolated { self?.tick() }
        }
        wakeObserver = NSWorkspace.shared.notificationCenter.addObserver(
            forName: NSWorkspace.didWakeNotification, object: nil, queue: .main
        ) { [weak self] _ in
            MainActor.assumeIsolated { self?.scheduleWakeRefresh() }
        }
    }

    /// Staleness and "n분 전" depend on the clock, so re-check them even when nothing new arrives.
    private func tick() {
        state.tick()
        statusItem.update(state.iconState)
    }

    private func scheduleWakeRefresh() {
        // The delay lets the network come back before refreshing.
        Task { [weak self] in
            try? await Task.sleep(nanoseconds: UInt64(AppConstants.wakeRefreshDelay * 1_000_000_000))
            self?.refreshNow()
        }
    }

    private static func seconds(minutes: Int) -> TimeInterval {
        TimeInterval(minutes * 60)
    }
}
```

`macos/Sources/TokenViewer/App/AppDelegate.swift`:

```swift
import AppKit

@MainActor
final class AppDelegate: NSObject, NSApplicationDelegate {
    private var controller: AppController?

    func applicationDidFinishLaunching(_ notification: Notification) {
        let controller = AppController()
        self.controller = controller
        controller.start()
    }
}
```

`macos/Sources/TokenViewer/main.swift`:

```swift
import AppKit

if CommandLine.arguments.contains("--unregister-login-item") {
    LoginItem.unregister()
    exit(0)
}

let application = NSApplication.shared
let delegate = AppDelegate()
application.delegate = delegate
application.setActivationPolicy(.accessory)
application.run()
```

- [ ] **Step 2: 빌드한다**

Run: `cd macos && swift build 2>&1 | grep -E "error|warning|Compiling|Build complete" | tail -5`
Expected: `Build complete!`, 경고 없음.

- [ ] **Step 3: 실제로 실행해 아이콘이 값을 보이는지 확인한다**

이전 Qt 개발 빌드의 로그인 항목을 지운다 (스펙 8절). 그대로 두면 로그인 때 아이콘이 두 개 뜬다.

```bash
launchctl bootout "gui/$(id -u)/com.tokenviewer.TokenViewer" 2>/dev/null || true
rm -f ~/Library/LaunchAgents/com.tokenviewer.TokenViewer.plist
pkill -x TokenViewer || true
macos/scripts/build-app.sh --debug && open macos/dist/TokenViewer.app
```

macOS 가 Keychain `Claude Code-credentials` 접근을 물으면 "항상 허용"을 누른다. 10초 뒤:

```bash
screencapture -x -R 0,0,1800,40 "$TMPDIR/tv-menubar.png"
```

캡처를 열어 막대 2줄과 `NN%` 가 보이는지 확인한다 (허용 창이 아직이면 `—`). `pkill -x TokenViewer` 로 끈다.

- [ ] **Step 4: 커밋**

```bash
git add macos/Sources/TokenViewer
git commit -m "feat: wire the Swift app with the menu bar icon

Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>"
```

---

### Task 16: 드롭다운과 설정 창

**Files:**
- Create: `macos/Sources/TokenViewer/UI/{PopupPanel,PopupController,PopupView,LimitMeterView,BreakdownListView,SettingsView,SettingsWindowController,RGBA+Color}.swift`
- Modify: `macos/Sources/TokenViewer/App/AppController.swift` (속성 2개, `handleStatusItemClick`, `openSettings`, `quit`)

**Interfaces:**
- Consumes: `AppController.state`, `AppController.refreshNow()`, `StatusItemController.anchorFrame`, `StatusText`, `Formatters`, `Palette`, `StatusIconRenderer.level`, `SettingsStore` 상수
- Produces: `AppController.openSettings()`, `AppController.quit()`

- [ ] **Step 1: 패널과 뷰를 만든다**

`macos/Sources/TokenViewer/UI/RGBA+Color.swift`:

```swift
import SwiftUI
import TokenViewerCore

extension RGBA {
    var color: Color { Color(.sRGB, red: red, green: green, blue: blue, opacity: alpha) }
}
```

`macos/Sources/TokenViewer/UI/PopupPanel.swift`:

```swift
import AppKit

/// A borderless, see-through panel; the SwiftUI content draws the rounded background itself.
final class PopupPanel: NSPanel {
    var onCancel: () -> Void = {}

    init(contentView: NSView) {
        super.init(contentRect: NSRect(x: 0, y: 0, width: 300, height: 300), styleMask: [.borderless], backing: .buffered, defer: true)
        level = .popUpMenu
        collectionBehavior = [.canJoinAllSpaces, .fullScreenAuxiliary]
        isReleasedWhenClosed = false
        hidesOnDeactivate = false
        backgroundColor = .clear
        isOpaque = false
        hasShadow = true
        self.contentView = contentView
    }

    // Borderless windows refuse key status by default; the pickers and buttons need it.
    override var canBecomeKey: Bool { true }

    override func cancelOperation(_ sender: Any?) {
        onCancel()
    }
}
```

`macos/Sources/TokenViewer/UI/PopupController.swift`:

```swift
import AppKit
import SwiftUI

/// Shows the dropdown right below the menu bar item and closes it on an outside click (v0.1.0 spec 2.2).
@MainActor
final class PopupController: NSObject, NSWindowDelegate {
    private static let gap: CGFloat = 4
    private static let screenMargin: CGFloat = 8
    private static let reopenGuard: TimeInterval = 0.3

    private let panel: PopupPanel
    private let hostingView: NSView
    private var anchorTop: CGFloat?
    private var closedAt: Date?
    private var outsideClickMonitor: Any?

    init<Content: View>(content: Content) {
        let hostingView = NSHostingView(rootView: content)
        self.hostingView = hostingView
        panel = PopupPanel(contentView: hostingView)
        super.init()
        panel.delegate = self
        panel.onCancel = { [weak self] in self?.close() }
    }

    var isShown: Bool { panel.isVisible }

    /// The click that closes the panel from outside can also land on the menu bar item; that click must not reopen it.
    var wasJustClosed: Bool { closedAt.map { Date().timeIntervalSince($0) < Self.reopenGuard } ?? false }

    func show(below anchor: NSRect?) {
        hostingView.layoutSubtreeIfNeeded()
        let size = hostingView.fittingSize
        let screen = anchor.flatMap { frame in
            NSScreen.screens.first { $0.frame.contains(NSPoint(x: frame.midX, y: frame.midY)) }
        } ?? NSScreen.main
        let visible = screen?.visibleFrame ?? NSRect(x: 0, y: 0, width: 1440, height: 900)
        let minX = visible.minX + Self.screenMargin
        let maxX = max(minX, visible.maxX - size.width - Self.screenMargin)
        let wantedX = anchor.map { $0.midX - size.width / 2 } ?? maxX
        let top = (anchor?.minY ?? visible.maxY) - Self.gap
        anchorTop = top
        panel.setFrame(NSRect(x: min(max(wantedX, minX), maxX), y: top - size.height, width: size.width, height: size.height), display: true)
        panel.makeKeyAndOrderFront(nil)
        startOutsideClickMonitor()
    }

    func close() {
        guard panel.isVisible else {
            return
        }
        panel.orderOut(nil)
        stopOutsideClickMonitor()
        closedAt = Date()
    }

    func windowDidResignKey(_ notification: Notification) {
        close()
    }

    func windowDidResize(_ notification: Notification) {
        // Rows arrive after the panel opens; keep its top edge under the menu bar item while it grows.
        guard let anchorTop, panel.frame.maxY != anchorTop else {
            return
        }
        panel.setFrameOrigin(NSPoint(x: panel.frame.minX, y: anchorTop - panel.frame.height))
        panel.invalidateShadow()
    }

    private func startOutsideClickMonitor() {
        guard outsideClickMonitor == nil else {
            return
        }
        // Clicks in other apps never reach this app's windows, so watch them globally.
        outsideClickMonitor = NSEvent.addGlobalMonitorForEvents(matching: [.leftMouseDown, .rightMouseDown]) { [weak self] _ in
            MainActor.assumeIsolated { self?.close() }
        }
    }

    private func stopOutsideClickMonitor() {
        if let outsideClickMonitor {
            NSEvent.removeMonitor(outsideClickMonitor)
        }
        outsideClickMonitor = nil
    }
}
```

`macos/Sources/TokenViewer/UI/LimitMeterView.swift`:

```swift
import SwiftUI

/// One limit meter: name and %, a 6 pt bar, and the reset time (v0.1.0 spec 2.2).
struct LimitMeterView: View {
    let name: String
    let value: String
    let percent: Double
    let subtitle: String
    let fill: Color
    let isDimmed: Bool

    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            HStack {
                Text(name)
                    .font(.system(size: 12))
                    .foregroundStyle(.secondary)
                Spacer()
                Text(value)
                    .font(.system(size: 15, weight: .bold))
                    .foregroundStyle(isDimmed ? Color.secondary : Color.primary)
            }
            GeometryReader { proxy in
                ZStack(alignment: .leading) {
                    Capsule().fill(Color.primary.opacity(0.12))
                    if percent > 0 {
                        Capsule()
                            .fill(fill.opacity(isDimmed ? 0.45 : 1))
                            .frame(width: max(proxy.size.width * min(percent, 100) / 100, 6))
                    }
                }
            }
            .frame(height: 6)
            Text(subtitle)
                .font(.system(size: 11))
                .foregroundStyle(.tertiary)
        }
        .frame(maxWidth: .infinity)
    }
}
```

`macos/Sources/TokenViewer/UI/BreakdownListView.swift`:

```swift
import SwiftUI
import TokenViewerCore

/// The top five groups plus "기타 N개", each with a bar relative to the first row (v0.1.0 spec 2.2).
struct BreakdownListView: View {
    let breakdown: Breakdown
    let fill: Color

    var body: some View {
        let rows = breakdown.displayRows
        let maxCost = breakdown.rows.first?.costUsd ?? 0
        VStack(spacing: 2) {
            if rows.isEmpty {
                Text("이 기간에 기록이 없습니다")
                    .font(.system(size: 12))
                    .foregroundStyle(.secondary)
                    .frame(maxWidth: .infinity)
                    .padding(.vertical, 8)
            }
            ForEach(Array(rows.enumerated()), id: \.offset) { _, row in
                BreakdownRowView(row: row, ratio: maxCost > 0 ? min(row.costUsd / maxCost, 1) : 0, fill: fill)
            }
        }
    }
}

private struct BreakdownRowView: View {
    let row: BreakdownRow
    let ratio: Double
    let fill: Color

    var body: some View {
        VStack(spacing: 3) {
            HStack(spacing: 8) {
                Text(row.label)
                    .font(.system(size: 12))
                    .lineLimit(1)
                    .truncationMode(.middle)
                Spacer(minLength: 0)
                Text(Formatters.amount(row))
                    .font(.system(size: 12))
                    .foregroundStyle(.secondary)
            }
            GeometryReader { proxy in
                ZStack(alignment: .leading) {
                    Capsule().fill(Color.primary.opacity(0.12))
                    if ratio > 0 {
                        Capsule().fill(fill).frame(width: max(proxy.size.width * ratio, 4))
                    }
                }
            }
            .frame(height: 4)
        }
        .frame(height: 28)
        .contentShape(Rectangle())
        .help(Formatters.tooltip(row))
    }
}
```

`macos/Sources/TokenViewer/UI/PopupView.swift`:

```swift
import SwiftUI
import TokenViewerCore

/// The dropdown (v0.1.0 spec 2.2): header, limits, notice, where the tokens went, footer.
struct PopupView: View {
    let controller: AppController
    @Environment(\.colorScheme) private var colorScheme

    private var state: AppState { controller.state }
    private var appearance: MenuBarAppearance { colorScheme == .dark ? .dark : .light }

    var body: some View {
        @Bindable var bindable = controller.state
        VStack(alignment: .leading, spacing: 8) {
            header
            if state.displayState != .empty {
                meters
            }
            notice
            if state.usage.needsRetryButton {
                Button("다시 확인") { controller.refreshNow() }
            }
            Divider()
            Picker("기준", selection: $bindable.settings.dimension) {
                ForEach(BreakdownDimension.allCases, id: \.self) { Text($0.title).tag($0) }
            }
            .pickerStyle(.segmented)
            .labelsHidden()
            BreakdownListView(breakdown: state.breakdown, fill: Palette.meter(.normal, appearance).color)
            Picker("기간", selection: $bindable.settings.period) {
                ForEach(BreakdownPeriod.allCases, id: \.self) { Text($0.title).tag($0) }
            }
            .pickerStyle(.menu)
            .labelsHidden()
            .fixedSize()
            Divider()
            footer
        }
        .padding(EdgeInsets(top: 11, leading: 14, bottom: 10, trailing: 14))
        .frame(width: 300)
        .background(RoundedRectangle(cornerRadius: 12).fill(Color(nsColor: .windowBackgroundColor)))
        .overlay(RoundedRectangle(cornerRadius: 12).strokeBorder(Color.primary.opacity(0.12), lineWidth: 1))
    }

    private var header: some View {
        HStack(spacing: 6) {
            Text("Claude 사용량")
                .font(.system(size: 13, weight: .bold))
            if !state.usage.planLabel.isEmpty {
                Text(state.usage.planLabel)
                    .font(.system(size: 11))
                    .padding(.horizontal, 5)
                    .overlay(RoundedRectangle(cornerRadius: 5).strokeBorder(Color.secondary.opacity(0.5)))
            }
            Spacer()
        }
    }

    private var meters: some View {
        let limits = state.usage.limits
        let isDimmed = state.displayState == .stale
        return HStack(spacing: 14) {
            meter("5시간", limits?.fiveHour, subtitle: Formatters.resetCountdown(limits?.fiveHour?.resetsAt, now: state.now), isDimmed: isDimmed)
            meter("주간", limits?.sevenDay, subtitle: Formatters.resetDay(limits?.sevenDay?.resetsAt, now: state.now), isDimmed: isDimmed)
        }
    }

    private func meter(_ name: String, _ window: LimitWindow?, subtitle: String, isDimmed: Bool) -> LimitMeterView {
        guard let window else {
            return LimitMeterView(name: name, value: "—", percent: 0, subtitle: "정보 없음",
                                  fill: Palette.meter(.normal, appearance).color, isDimmed: isDimmed)
        }
        let level = StatusIconRenderer.level(percent: window.percent, warn: state.settings.warnPercent, critical: state.settings.criticalPercent)
        return LimitMeterView(name: name, value: Formatters.percent(window.percent), percent: window.percent, subtitle: subtitle,
                              fill: Palette.meter(level, appearance).color, isDimmed: isDimmed)
    }

    @ViewBuilder private var notice: some View {
        let text = StatusText.notice(state.usage, now: state.now)
        if !text.isEmpty {
            Text(text)
                .font(.system(size: 12))
                .fixedSize(horizontal: false, vertical: true)
                .frame(maxWidth: .infinity, alignment: .leading)
                .padding(8)
                .background(RoundedRectangle(cornerRadius: 8).fill(Color.primary.opacity(0.08)))
        }
    }

    private var footer: some View {
        let footer = StatusText.footer(state.usage, now: state.now)
        return HStack(spacing: 4) {
            Text(footer.text)
                .font(.system(size: 11))
                .foregroundStyle(footer.isFailure ? Palette.level(.critical, appearance).color : Color.secondary)
            Spacer()
            Button("↻") { controller.refreshNow() }
                .help("새로고침")
            Button("⚙") { controller.openSettings() }
                .help("설정")
            Button("종료") { controller.quit() }
        }
        .buttonStyle(.borderless)
    }
}
```

`macos/Sources/TokenViewer/UI/SettingsView.swift`:

```swift
import SwiftUI
import TokenViewerCore

/// The settings window (v0.1.0 spec 2.3). Changes go straight into AppState; AppController saves and applies them.
struct SettingsView: View {
    let controller: AppController

    var body: some View {
        @Bindable var state = controller.state
        let step = SettingsStore.percentStep
        Form {
            Picker("한도 갱신 주기", selection: $state.settings.intervalMinutes) {
                ForEach(SettingsStore.allowedIntervals, id: \.self) { Text("\($0)분").tag($0) }
            }
            Text("드롭다운을 열 때 30초가 지났으면 바로 갱신합니다.")
                .font(.system(size: 11))
                .foregroundStyle(.secondary)
            Picker("주황 경고", selection: $state.settings.warnPercent) {
                ForEach(Array(stride(from: SettingsStore.minWarn, through: SettingsStore.maxWarn, by: step)), id: \.self) {
                    Text("\($0)%").tag($0)
                }
            }
            Picker("빨강 경고", selection: $state.settings.criticalPercent) {
                ForEach(Array(stride(from: state.settings.warnPercent + step, through: SettingsStore.maxPercent, by: step)), id: \.self) {
                    Text("\($0)%").tag($0)
                }
            }
            Toggle("로그인 시 자동 실행", isOn: $state.settings.launchAtLogin)
            HStack {
                Text("v\(AppInfo.version)")
                    .foregroundStyle(.secondary)
                Spacer()
                if let url = AppInfo.repositoryURL {
                    Link("GitHub", destination: url)
                }
            }
        }
        .formStyle(.grouped)
        .frame(width: 380)
        .fixedSize(horizontal: false, vertical: true)
    }
}
```

`macos/Sources/TokenViewer/UI/SettingsWindowController.swift`:

```swift
import AppKit
import SwiftUI

@MainActor
final class SettingsWindowController {
    private var window: NSWindow?

    func show(controller: AppController) {
        let window = self.window ?? makeWindow(controller: controller)
        self.window = window
        if !window.isVisible {
            window.center()
        }
        NSApp.activate()
        window.makeKeyAndOrderFront(nil)
    }

    private func makeWindow(controller: AppController) -> NSWindow {
        let window = NSWindow(contentViewController: NSHostingController(rootView: SettingsView(controller: controller)))
        window.title = "TokenViewer 설정"
        window.styleMask = [.titled, .closable]
        window.isReleasedWhenClosed = false
        return window
    }
}
```

- [ ] **Step 2: AppController 에 드롭다운과 설정 창을 연결한다**

`macos/Sources/TokenViewer/App/AppController.swift` 에서 `private let statusItem = StatusItemController()` 아래에 두 줄을 넣는다:

```swift
    private lazy var popup = PopupController(content: PopupView(controller: self))
    private let settingsWindow = SettingsWindowController()
```

`handleStatusItemClick()` 전체를 바꾼다:

```swift
    private func handleStatusItemClick() {
        if popup.isShown {
            popup.close()
            return
        }
        if popup.wasJustClosed {
            return
        }
        state.tick()
        if state.usage.isRefreshDueOnOpen(now: state.now) {
            Task { [fetcher] in await fetcher?.requestRefresh() }
        }
        Task { [scanner] in await scanner?.requestRescan() }
        popup.show(below: statusItem.anchorFrame)
    }
```

`refreshNow()` 아래에 넣는다:

```swift
    func openSettings() {
        popup.close()
        settingsWindow.show(controller: self)
    }

    func quit() {
        NSApp.terminate(nil)
    }
```

- [ ] **Step 3: 빌드한다**

Run: `cd macos && swift build 2>&1 | grep -E "error|warning|Build complete" | tail -5`
Expected: `Build complete!`, 경고 없음.

- [ ] **Step 4: 드롭다운과 설정 창을 실제로 확인한다**

```bash
pkill -x TokenViewer || true
macos/scripts/build-app.sh --debug && open macos/dist/TokenViewer.app
```

손으로 확인한다 (사람이 해야 하는 클릭이다. 자동 실행 중이면 마지막 보고에 "수동 확인 필요"로 넘긴다):
1. 메뉴바 아이콘 클릭 → 아이콘 바로 아래에 드롭다운. 미터 2개, 목록, 기간, 바닥 줄이 보인다.
2. 다시 클릭 → 닫힘. 바깥 클릭·다른 앱 클릭·Esc → 닫힘.
3. 탭(프로젝트/모델/세션)·기간을 바꾸면 목록이 바뀐다. 앱을 다시 켜도 고른 탭·기간이 남는다.
4. 줄에 마우스를 올리면 입력/출력/캐시 토큰 툴팁.
5. ⚙ → 설정 창. 주황을 90 으로 바꾸면 빨강 목록이 95 부터 시작하고 값이 95 가 된다.
6. 로그인 시 자동 실행을 끄고 켜면 시스템 설정 → 일반 → 로그인 항목에서 TokenViewer 가 빠지고 다시 생긴다.
7. 종료 → 메뉴바에서 사라진다.

자동으로 확인할 수 있는 것: `defaults read com.tokenviewer.TokenViewer` 에 `popup.dimension`, `popup.period` 가 있다.

- [ ] **Step 5: 커밋**

```bash
git add macos/Sources/TokenViewer
git commit -m "feat: add the dropdown and settings window in SwiftUI

Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>"
```

---

### Task 17: 설치 스크립트와 CI

**Files:**
- Create: `macos/scripts/install.sh`, `.github/workflows/macos.yml`

**Interfaces:**
- Consumes: `build-app.sh` 결과물 (`dist/TokenViewer.app`, `TokenViewer-macos.zip`, `.sha256`), 앱 인자 `--unregister-login-item`
- Produces: `install.sh [--from-source|--uninstall|--help]`, 환경 변수 `TV_REPO`, `TV_RELEASE_BASE`

- [ ] **Step 1: install.sh 를 쓴다**

`macos/scripts/install.sh`:

```bash
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
```

```bash
chmod +x macos/scripts/install.sh
```

- [ ] **Step 2: 스크립트 문법과 세 가지 방식을 확인한다**

이 PC 의 설정을 지우는 `--uninstall` 을 돌리기 전에 설정을 백업한다.

```bash
bash -n macos/scripts/install.sh && bash -n macos/scripts/build-app.sh && echo syntax ok
defaults export com.tokenviewer.TokenViewer "$TMPDIR/tv-defaults.plist" 2>/dev/null || true

macos/scripts/install.sh --from-source
pgrep -x TokenViewer && ls -d /Applications/TokenViewer.app ~/Applications/TokenViewer.app 2>/dev/null

macos/scripts/install.sh --uninstall
ls -d /Applications/TokenViewer.app ~/Applications/TokenViewer.app 2>/dev/null || echo "removed"
pgrep -x TokenViewer || echo "not running"

TV_RELEASE_BASE="file://$PWD/macos/dist" bash < macos/scripts/install.sh
pgrep -x TokenViewer

[[ -f "$TMPDIR/tv-defaults.plist" ]] && defaults import com.tokenviewer.TokenViewer "$TMPDIR/tv-defaults.plist"
```

Expected: `syntax ok`; `--from-source` 뒤 PID 와 설치 경로; `--uninstall` 뒤 `removed`, `not running`; 파이프 설치(`bash <`) 뒤 PID.

- [ ] **Step 3: CI 워크플로를 쓴다**

`.github/workflows/macos.yml`:

```yaml
name: macOS

on:
  push:
    branches: [main]
    # Path filters are not evaluated for tag pushes, so every v* tag builds a release.
    tags: ['v*']
    paths:
      - 'macos/**'
      - 'shared/**'
      - '.github/workflows/macos.yml'
  pull_request:
    paths:
      - 'macos/**'
      - 'shared/**'
      - '.github/workflows/macos.yml'

jobs:
  test:
    runs-on: macos-15
    steps:
      - uses: actions/checkout@v4
      - name: Test
        working-directory: macos
        run: swift test

  release:
    if: startsWith(github.ref, 'refs/tags/v')
    needs: test
    runs-on: macos-15
    permissions:
      contents: write
    steps:
      - uses: actions/checkout@v4
      - name: Build app
        working-directory: macos
        env:
          TV_VERSION: ${{ github.ref_name }}
          TV_REPO: ${{ github.repository }}
        run: ./scripts/build-app.sh
      - name: Prepare install script
        run: sed 's#^RELEASE_REPO=.*#RELEASE_REPO="${{ github.repository }}"#' macos/scripts/install.sh > macos/dist/install.sh
      - name: Publish release
        env:
          GH_TOKEN: ${{ github.token }}
        run: |
          gh release view "$GITHUB_REF_NAME" >/dev/null 2>&1 || gh release create "$GITHUB_REF_NAME" --title "$GITHUB_REF_NAME" --generate-notes
          gh release upload "$GITHUB_REF_NAME" macos/dist/TokenViewer-macos.zip macos/dist/TokenViewer-macos.zip.sha256 macos/dist/install.sh --clobber
```

- [ ] **Step 4: 릴리스용 sed 가 한 줄만 바꾸는지 확인한다**

Run: `sed 's#^RELEASE_REPO=.*#RELEASE_REPO="me/TokenViewer"#' macos/scripts/install.sh | grep -n 'RELEASE_REPO'`
Expected: `RELEASE_REPO="me/TokenViewer"` 는 할당 줄 하나뿐이고, 나머지 줄은 `$RELEASE_REPO` 참조 그대로.

- [ ] **Step 5: 커밋**

```bash
git add macos/scripts/install.sh .github/workflows/macos.yml
git commit -m "feat: add the install script and macOS CI workflow

Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>"
```

---

### Task 18: README, LICENSE, 마지막 확인

**Files:**
- Create: `LICENSE`
- Modify: `README.md` (전체 교체)

- [ ] **Step 1: LICENSE 를 쓴다**

`LICENSE`:

```text
MIT License

Copyright (c) 2026 jy.seong

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

- [ ] **Step 2: README 를 다시 쓴다**

`README.md`:

````markdown
# TokenViewer

Claude Code 요금제 한도(5시간·주간)를 메뉴바에 늘 보여 주는 앱입니다.
클릭하면 리셋 시각과, 로컬 세션 로그로 계산한 프로젝트·모델·세션별 API 환산 비용을 보여 줍니다.

| 플랫폼 | 상태 | 위치 |
| --- | --- | --- |
| macOS 14 이상 (Apple Silicon, Intel) | 사용 가능. Swift 네이티브 앱, 약 1–2MB | `macos/` |
| Windows, Ubuntu | 준비 중 (Qt) | 저장소 최상위 |

## macOS

### 설치

```bash
curl -fsSL https://github.com/<owner>/TokenViewer/releases/latest/download/install.sh | bash
```

최신 릴리스를 내려받아 SHA-256 을 확인하고 `/Applications` (쓸 수 없으면 `~/Applications`) 에 설치한 뒤 실행합니다.
sudo 는 쓰지 않습니다. 같은 명령으로 업데이트합니다.

소스에서 설치 (Xcode 16 이상 필요):

```bash
git clone https://github.com/<owner>/TokenViewer.git
TokenViewer/macos/scripts/install.sh --from-source
```

제거 (앱, 로그인 항목, 설정, 로그):

```bash
curl -fsSL https://github.com/<owner>/TokenViewer/releases/latest/download/install.sh | bash -s -- --uninstall
```

### 처음 실행할 때

- macOS 가 Keychain 의 `Claude Code-credentials` 접근을 묻습니다. "항상 허용"을 누르세요.
- 릴리스 앱은 Apple Developer ID 가 아닌 ad-hoc 서명입니다. 그래서 **업데이트할 때마다 Keychain 허용 창이 한 번 더 뜹니다.**
  curl 로 설치하면 Gatekeeper 경고 없이 실행됩니다. 브라우저로 zip 을 받으면 시스템 설정 → 개인정보 보호 및 보안 → "그래도 열기"가 필요합니다.
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
scripts/build-app.sh             # universal 릴리스 빌드 + zip + sha256
```

`TV_SIGN_IDENTITY="Developer ID Application: …"` 와 `TV_NOTARY_PROFILE=<notarytool 프로필>` 을 주면 서명·공증까지 합니다.
`v*` 태그를 올리면 GitHub Actions 가 릴리스에 zip, sha256, install.sh 를 올립니다.

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
````

- [ ] **Step 3: 전체 확인**

```bash
cd macos && swift test 2>&1 | tail -3 && cd ..
macos/scripts/build-app.sh | tail -2
grep -rnE "^import (AppKit|SwiftUI|Security|ServiceManagement)" macos/Sources/TokenViewerCore || echo "core imports ok"
cmake --build build && ctest --test-dir build --output-on-failure | tail -3
```

Expected: Swift 테스트 전부 통과; `TokenViewer.app ...: N KB [x86_64 arm64]` 에서 N ≤ 3072; `core imports ok`; Qt 테스트 전부 통과.

- [ ] **Step 4: 설치된 앱을 최신으로 바꾼다**

```bash
macos/scripts/install.sh --from-source
```

- [ ] **Step 5: 커밋**

```bash
git add LICENSE README.md
git commit -m "docs: rewrite README for the macOS app and add the MIT license

Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>"
```

---

## 수동 확인 목록 (사람이 할 일)

자동 실행으로 확인할 수 없는 것들이다. 마지막 보고에 그대로 옮긴다.

1. 밝은/어두운 메뉴바에서 아이콘 (템플릿일 때 메뉴바 색을 따르는지, 주황·빨강).
2. 아이콘 클릭으로 열고 닫기, 바깥 클릭·다른 앱 클릭·Esc 로 닫기, 전체 화면 앱 위에서 열기.
3. 다중 모니터에서 드롭다운 위치.
4. 메뉴바 % 가 Claude Code `/usage` 의 5시간 % 와 같은지.
5. Keychain 허용 창 ("항상 허용" 후 다시 묻지 않는지).
6. 로그인 항목 (/Applications 에 설치한 앱으로): 재로그인 후 자동 실행, 설정에서 끄면 시스템 설정 → 로그인 항목에서 빠지는지.
7. README 의 `<owner>` 를 실제 GitHub 계정으로 바꾸기 (저장소를 만든 뒤).
