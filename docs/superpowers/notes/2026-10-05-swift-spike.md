# Swift 스파이크 결과 (Task 2)

- 날짜: 2026-10-05
- 환경: macOS 26.5.2, Xcode 26.4.1, Swift 6.3.1, ad-hoc 서명 (hardened runtime)

| 항목 | 결과 |
| --- | --- |
| universal 빌드 (`swift build --arch arm64 --arch x86_64`) | **통과** — `lipo -archs` → `x86_64 arm64`, 뼈대 앱 148 KB |
| `SMAppService.mainApp.register()` — /Applications | **통과** — `before: 3` (notFound) → `register: ok` → `after: 1` (enabled) → `unregistered: 0`. "Login Item Added" 알림이 뜬다 |
| `SMAppService.mainApp.register()` — ~/Applications | **통과** — `before: 0` → `register: ok` → `after: 1` → `unregistered: 0` |
| `NSPanel` 이 메뉴바 아이템 아래에 뜸 | **판정 못 함** — 확인할 때 화면이 Mission Control 상태라 메뉴바가 캡처되지 않았다. `CGWindowListCopyWindowInfo` 로 본 패널 위치는 `x=-132, y=1019` (화면 밖)로, 아이템 창 프레임이 `(0, -33)` 근처로 보고됐다 |

## 판정

- 로그인 항목: **SMAppService 채택.** ad-hoc 서명으로도 두 위치 모두 등록된다.
- 드롭다운 위치: Qt 버전에서 같은 방식(`button.window.frame` 기준)이 동작했으므로 그대로 간다.
  다만 아이템 프레임이 화면 밖으로 보고되는 경우가 있으므로, `PopupController` 는 앵커가 어느 화면에도 없으면 앵커가 없는 것처럼 주 화면 오른쪽 위에 띄운다 (Task 16 ruling).
- 바깥 클릭·다른 앱 전환 때 닫힘, 전체 화면 앱 위 표시는 클릭이 필요해 수동 확인으로 넘긴다.
