# 메뉴바 스파이크 결과 (Task 2)

- 날짜: 2026-10-05
- 환경: macOS 26.5.2, MacBook Pro 내장 Liquid Retina XDR (Retina, 2x), Anaconda Qt 5.15.2
- 방식: `QSystemTrayIcon` + `TrayIconPainter` 가 그린 가로로 긴 이미지 (`QIcon::setIsMask` 로 템플릿)

## 결과

| 항목 | 결과 |
| --- | --- |
| 1. 막대·숫자가 잘리지 않고 보인다 | **실패** — 잘리지는 않지만 아이콘 전체가 약 절반 크기로 그려진다. "42%" 글자 높이가 시계 글자의 절반 정도라 읽을 수 없다 (사용자 확인, 화면 캡처로 재확인) |
| 2–5 | 1번 실패로 판정에 영향 없음 (확인하지 않음) |

## 판정

**Task 3 진행** — `TrayIcon` 만 `NSStatusItem` (Objective-C++) 구현으로 바꾼다. Qt 5.15 의 macOS 트레이 아이콘은 Retina 픽스맵을 메뉴바 높이 기준으로 다시 줄여 넣어, 이미지의 포인트 크기를 직접 지정할 방법이 없다. `NSStatusItem` 은 `NSImage` 의 포인트 크기(픽셀 ÷ 배율)를 그대로 쓴다.

## Task 3 재확인 (NSStatusItem)

- 같은 환경에서 `TV_NATIVE_TRAY=ON` 빌드로 다시 확인했다.
- 화면 캡처: 막대 2줄과 "78%" 가 시계 글자와 같은 높이로 보이고, 주황 상태에서 색칠된 막대·숫자가 정상이다.
- 사용자 확인: 크기·색·클릭으로 열고 닫기·다른 창을 누르면 닫히기 모두 정상.
- 판정: **NSStatusItem 구현 채택** (`TV_NATIVE_TRAY` 기본 ON).

## 최종 확인 (Task 15)

- 메뉴바 아이콘 크기·색·클릭·바깥 클릭으로 닫기: Task 3 재확인에서 통과 (어두운 메뉴바, 사용자 확인).
- Keychain 허용 창: 첫 실행 때 표시, "항상 허용" 후 정상 조회 (Task 13, 사용자 확인).
- 메뉴바 % 와 Claude Code `/usage` 일치, 드롭다운 미터·배지·탭·기간·툴팁: 통과 (Task 13, 사용자 확인).
- 설정 창, 오픈소스 라이선스, Qt 정보, 경고 임계치 즉시 반영: 통과 (Task 14, 사용자 확인).
- 로그인 시 자동 실행: 기본값(켬)으로 `~/Library/LaunchAgents/com.tokenviewer.TokenViewer.plist` 가 개발 빌드 경로로 생성됨을 확인. 사용자가 설정 창에서 끌 예정.
- 다중 모니터: 확인하지 않음 (외장 모니터 없음).
- Release 빌드: 경고 0, 테스트 16/16 (최종 리뷰 수정 후 Debug 17/17).
