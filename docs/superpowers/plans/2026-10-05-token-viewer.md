# TokenViewer Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** macOS 메뉴바에 Claude Code 요금제 한도(5시간·주간 %)를 보여 주고, 클릭하면 한도 상세와 로컬 로그 기반의 프로젝트·모델·세션별 API 환산 비용을 보여 주는 Qt5 앱을 만든다.

**Architecture:** `Application` 이 모든 모듈을 `std::unique_ptr` 로 소유한다. 한도는 `UsageFetchThread` 가 Keychain 토큰으로 `/api/oauth/usage` 를 불러 오고, 비용은 `LogScanThread` 가 `~/.claude/projects/**/*.jsonl` 을 증분으로 읽어 만든다. 결과는 `SystemStatus` 에 모이고 `Observers` signal 로 `TrayIcon`·`UsagePopup` 에 알린다. 파싱·집계·가격·아이콘 그리기는 순수 함수로 분리해 Qt Test 로 검증한다.

**Tech Stack:** C++17, Qt 5.15.2 (Anaconda, Core/Gui/Widgets/Network/Test), CMake ≥ 3.21 + Ninja, Security.framework + CoreFoundation (C API), 필요 시 AppKit (Objective-C++).

**Spec:** `docs/superpowers/specs/2026-10-05-token-viewer-design.md`

**범위 밖:** 배포 패키지(공식 Qt 빌드, `macdeployqt`, 코드 서명)는 이 계획에 없다. 개발 PC 의 Anaconda Qt 로 빌드·실행한다.

## Global Constraints

- Qt 5.15 모듈은 Core, Gui, Widgets, Network, Test 만 쓴다. Qt Charts 등 GPL 전용 모듈 금지. Qt 는 공유 라이브러리로 링크한다 (CMake 가 검사).
- 빌드 구성 명령 (한 번): `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DQt5_DIR=/opt/homebrew/anaconda3/lib/cmake/Qt5`
- 빌드: `cmake --build build` / 테스트: `ctest --test-dir build --output-on-failure -R <TestName>`
- 테스트는 `QT_QPA_PLATFORM=offscreen` 으로 돈다 (tests/CMakeLists.txt 가 설정). 실제 API·Keychain 은 테스트에서 부르지 않는다.
- 이름 규칙은 `docs/CppConvention.md`: 함수·클래스 PascalCase, 함수는 동사로 시작, 멤버 `m_` + 타입 접두어, 매개변수는 접두어 없음, enum 은 `E` 접두어 + 대문자 값. 클래스 안 순서는 생성자/소멸자 → public 함수 → Getter/Setter → protected → private 함수 → 멤버.
- Qt 타입 접두어: `QString` str, `QStringList`/`QList` lst, `QByteArray` ba, `QVector` vec, `QHash` hash, `QSet` set, `QDateTime` dt, `QFileInfo` fi, `QDir` dir, `QFile` file, `QJsonObject` obj, `QJsonValue` jv, `QJsonDocument` doc, `QImage` img, `QColor` clr, `QFont` font, `QRect`/`QRectF` rc, `QPalette` pal, `QTimer`/`QElapsedTimer` timer. 주입받은 소유하지 않는 포인터는 `m_kp`, Qt 부모가 소유하는 자식 위젯 포인터는 `m_p`.
- 스레드는 `docs/Qt.md` 와 qt-worker-thread-pattern 규칙: `XxxThreadWorker` + `XxxThread`, `moveToThread`, PIMPL, `std::atomic<bool>` 정지 플래그, Stop → quit → wait → terminate 순서, 100ms 단위 대기. 이 프로젝트는 소유자 메서드를 PascalCase(`Start`, `Stop`)로 쓰고, 오류는 결과 구조체로 전달하므로 `ErrorOccurred` signal 은 두지 않는다.
- UI 버튼 클릭은 `docs/Qt.md` 2.2.2 처럼 `installEventFilter` + `eventFilter` 한 곳에서 처리한다 (콤보박스·체크박스는 signal).
- 매직 넘버 금지: 파일 상단 `constexpr` 상수로.
- 기본값: 한도 갱신 3분 (1·3·5·10), 주황 70%, 빨강 90%, 로그인 시 자동 실행 켬, stale 15분, 드롭다운 열 때 마지막 시도 30초 넘으면 갱신, 로그 재검사 60초, 429 이면 주기 2배씩 최대 30분, HTTP 타임아웃 10초.
- 색: 평소 미터 파랑 `#2a78d6`(라이트)/`#3987e5`(다크), 주황 `#c98500`/`#fab219`, 빨강 `#d03b3b`/`#ff6b6b`.
- 번들 ID `com.tokenviewer.TokenViewer`, `LSUIElement=true`.
- 액세스 토큰은 로그·파일·설정에 쓰지 않는다. refreshToken 은 읽지도 쓰지도 않는다.
- 커밋 메시지 끝에 `Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>` 를 붙인다 (`git commit -m "<제목>" -m "Co-Authored-By: ..."`).

## Review Focus

1. **로그 폴더가 없음** (`~/.claude/projects` 가 아직 없는 새 설치) → 앱이 죽지 않고 목록에 "이 기간에 기록이 없습니다". Task 8 `TestMissingRootIsEmpty`, Task 12 `TestListsBreakdownRowsAndEmptyState`.
2. **Claude Code 가 줄을 쓰는 도중** (마지막 줄에 줄바꿈 없음) → 그 줄을 잃거나 두 번 세지 않는다. Task 8 `TestWaitsForPartialLine`.
3. **"오늘" 경계** (UTC 타임스탬프, 로컬 자정) → 한국 시간 자정 기준. Task 7 `TestCalculatesTodayFromLocalMidnight`.
4. **API 가 범위 밖 % 나 null 창을 돌려줌** → 0–100 으로 자르고, null 창은 그 미터만 비운다. Task 9 `TestClampsOutOfRange`, `TestNullWindowLeavesMeterEmpty`.
5. **맥이 잠자기에서 깨어남** (마지막 성공이 오래됐지만 마지막 상태는 OK) → 15분 넘으면 흐리게. Task 11 `TestStaleAfterSleepEvenIfLastStatusOk`.

## File Map

| 파일 | 책임 | Task |
| --- | --- | --- |
| `CMakeLists.txt`, `tests/CMakeLists.txt`, `.gitignore` | 빌드·테스트 등록 | 1 (이후 계속 추가) |
| `src/UI/TrayIconPainter.{h,cpp}` | 아이콘 이미지 그리기 (순수) | 1 |
| `src/UI/TrayIcon.h`, `src/UI/TrayIcon.cpp` | `QSystemTrayIcon` 구현 | 2 |
| `src/UI/TrayIconMac.mm` | `NSStatusItem` 구현 (스파이크 실패 시) | 3 |
| `resources/Info.plist.in` | 번들 정보 | 2 |
| `docs/superpowers/notes/*.md` | 스파이크 결과 기록 | 2, 4 |
| `tests/fixtures/usage_response.json` | 실제 응답 형태의 가짜 값 | 4 |
| `src/Model/TokenRecord.h` | `TokenRecord`, `LogSnapshot` | 5 |
| `src/Analysis/PriceTable.{h,cpp}`, `resources/prices.json` | 가격표·비용 계산 | 5 |
| `tests/TestHelpers.h` | 테스트 공용 함수 | 5, 8 |
| `src/Analysis/LogParser.{h,cpp}` | jsonl 한 줄 해석 | 6 |
| `src/Model/Breakdown.h`, `src/Analysis/TokenAggregator.{h,cpp}` | 기간·기준별 순위 | 7 |
| `src/Analysis/LogStore.{h,cpp}` | 파일별 증분 읽기·중복 제거 | 8 |
| `src/Thread/LogScanThread.{h,cpp}` | 로그 스레드 | 8 |
| `src/Model/UsageLimits.h` | 한도·로그인 정보·조회 결과 구조체 | 9 |
| `src/Service/CredentialStore.{h,cpp}` | Keychain/파일에서 토큰 읽기 | 9 |
| `src/Service/UsageApiClient.{h,cpp}` | `/api/oauth/usage` 호출·해석 | 9 |
| `src/Service/RefreshPolicy.{h,cpp}` | 다음 조회 시점 결정 (순수) | 10 |
| `src/Thread/UsageFetchThread.{h,cpp}` | 한도 스레드 | 10 |
| `src/Core/Settings.{h,cpp}`, `src/Core/SystemStatus.{h,cpp}`, `src/Core/Observers.h` | 설정·상태·signal | 11 |
| `src/UI/Formatters.{h,cpp}`, `src/UI/LimitMeterWidget.{h,cpp}`, `src/UI/BreakdownListWidget.{h,cpp}`, `src/UI/UsagePopup.{h,cpp}` | 드롭다운 | 12 |
| `src/Core/Application.{h,cpp}`, `src/main.cpp`, `resources/TokenViewer.qrc` | 조립·실행 | 13 |
| `src/Service/LoginItem.{h,cpp}`, `src/UI/SettingsDialog.{h,cpp}`, `src/UI/LicenseNoticesDialog.{h,cpp}`, `resources/THIRD_PARTY_NOTICES.md` | 설정 창·자동 실행·고지 | 14 |
| `README.md` | 빌드·실행 안내 | 15 |

---

### Task 1: 프로젝트 골격 + TrayIconPainter

**Files:**
- Create: `.gitignore`, `CMakeLists.txt`, `tests/CMakeLists.txt`
- Create: `src/UI/TrayIconPainter.h`, `src/UI/TrayIconPainter.cpp`
- Test: `tests/TestTrayIconPainter.cpp`

**Interfaces:**
- Consumes: 없음
- Produces:
  - `enum class EBarLevel { NORMAL, WARNING, CRITICAL }`, `enum class EMenuBarAppearance { LIGHT, DARK }`
  - `struct TrayIconState { bool m_bFiveHourValid; double m_dFiveHourPercent; bool m_bSevenDayValid; double m_dSevenDayPercent; bool m_bStale; int m_iWarnPercent = 70; int m_iCriticalPercent = 90; }` + `operator==`, `operator!=`
  - `struct TrayIconImage { QImage m_imgIcon; bool m_bTemplate; }`
  - `TrayIconPainter::ClassifyLevel(double, int, int) -> EBarLevel`, `IsTemplate(const TrayIconState&) -> bool`, `FormatLabel(const TrayIconState&) -> QString`, `GetInkColor(EMenuBarAppearance) -> QColor`, `GetLevelColor(EBarLevel, EMenuBarAppearance) -> QColor`, `Render(const TrayIconState&, EMenuBarAppearance, qreal dpr) -> TrayIconImage`
  - CMake 타깃 `TokenViewerCore` (정적 라이브러리, include 루트 `src/`), 테스트 등록 함수 `tv_add_test(<Name>)`

- [ ] **Step 1: 저장소 초기화와 기존 문서 커밋**

`.gitignore`:

```gitignore
build/
build-*/
.superpowers/
.DS_Store
*.user
compile_commands.json
```

```bash
git init
git add .gitignore README.md docs
git commit -m "chore: add project docs and design spec" -m "Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>"
```

- [ ] **Step 2: 빌드 파일 작성**

`CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.21)
project(TokenViewer VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Qt5 5.15 REQUIRED COMPONENTS Core Gui Widgets Network Test)

# LGPLv3: Qt must stay a shared library (docs/LicenseCompliance.md).
foreach(_module Core Gui Widgets Network)
    get_target_property(_type Qt5::${_module} TYPE)
    if(NOT _type STREQUAL "SHARED_LIBRARY")
        message(FATAL_ERROR "Qt5::${_module} must be linked as a shared library (LGPLv3)")
    endif()
endforeach()

set(TV_CORE_SOURCES
    src/UI/TrayIconPainter.cpp
)

add_library(TokenViewerCore STATIC ${TV_CORE_SOURCES})
target_include_directories(TokenViewerCore PUBLIC src)
target_link_libraries(TokenViewerCore PUBLIC Qt5::Core Qt5::Gui Qt5::Widgets Qt5::Network)
target_compile_definitions(TokenViewerCore PUBLIC TV_VERSION="${PROJECT_VERSION}")

enable_testing()
add_subdirectory(tests)
```

`tests/CMakeLists.txt`:

```cmake
function(tv_add_test name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE TokenViewerCore Qt5::Test)
    target_compile_definitions(${name} PRIVATE TV_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endfunction()

tv_add_test(TestTrayIconPainter)
```

- [ ] **Step 3: 실패하는 테스트 작성**

`tests/TestTrayIconPainter.cpp`:

```cpp
#include "UI/TrayIconPainter.h"

#include <QtTest>

namespace
{
TrayIconState MakeState(double fiveHourPercent, double sevenDayPercent)
{
    TrayIconState state;
    state.m_bFiveHourValid = true;
    state.m_dFiveHourPercent = fiveHourPercent;
    state.m_bSevenDayValid = true;
    state.m_dSevenDayPercent = sevenDayPercent;
    return state;
}
}

class TestTrayIconPainter : public QObject
{
    Q_OBJECT

private slots:
    void TestClassifyLevelThresholds();
    void TestIsTemplateOnlyWhenBothNormal();
    void TestFormatLabelRoundsAndClamps();
    void TestRenderFillsFiveHourBarProportionally();
    void TestRenderUsesLevelColorWhenWarning();
    void TestRenderColorsSevenDayBarIndependently();
    void TestRenderDimsStaleState();
    void TestRenderEmptyStateHasNoFill();
    void TestRenderScalesWithDevicePixelRatio();
};

void TestTrayIconPainter::TestClassifyLevelThresholds()
{
    QCOMPARE(TrayIconPainter::ClassifyLevel(69.9, 70, 90), EBarLevel::NORMAL);
    QCOMPARE(TrayIconPainter::ClassifyLevel(70.0, 70, 90), EBarLevel::WARNING);
    QCOMPARE(TrayIconPainter::ClassifyLevel(89.9, 70, 90), EBarLevel::WARNING);
    QCOMPARE(TrayIconPainter::ClassifyLevel(90.0, 70, 90), EBarLevel::CRITICAL);
    QCOMPARE(TrayIconPainter::ClassifyLevel(100.0, 70, 90), EBarLevel::CRITICAL);
}

void TestTrayIconPainter::TestIsTemplateOnlyWhenBothNormal()
{
    QVERIFY(TrayIconPainter::IsTemplate(MakeState(42.0, 18.0)));
    QVERIFY(!TrayIconPainter::IsTemplate(MakeState(78.0, 18.0)));
    QVERIFY(!TrayIconPainter::IsTemplate(MakeState(12.0, 93.0)));
    QVERIFY(TrayIconPainter::IsTemplate(TrayIconState()));
}

void TestTrayIconPainter::TestFormatLabelRoundsAndClamps()
{
    QCOMPARE(TrayIconPainter::FormatLabel(MakeState(42.4, 0.0)), QStringLiteral("42%"));
    QCOMPARE(TrayIconPainter::FormatLabel(MakeState(99.6, 0.0)), QStringLiteral("100%"));
    QCOMPARE(TrayIconPainter::FormatLabel(MakeState(130.0, 0.0)), QStringLiteral("100%"));
    QCOMPARE(TrayIconPainter::FormatLabel(TrayIconState()), QStringLiteral("—"));
}

void TestTrayIconPainter::TestRenderFillsFiveHourBarProportionally()
{
    const TrayIconImage image = TrayIconPainter::Render(MakeState(42.0, 18.0), EMenuBarAppearance::LIGHT, 1.0);
    QVERIFY(image.m_bTemplate);
    const QRgb rgbInside = image.m_imgIcon.pixel(4, 6);
    QCOMPARE(qAlpha(rgbInside), 255);
    QCOMPARE(QColor(rgbInside), QColor(Qt::black));
    // 42% of the 22pt bar ends near x=9, so x=16 is empty bar interior.
    QCOMPARE(qAlpha(image.m_imgIcon.pixel(16, 6)), 0);
}

void TestTrayIconPainter::TestRenderUsesLevelColorWhenWarning()
{
    const TrayIconImage image = TrayIconPainter::Render(MakeState(78.0, 31.0), EMenuBarAppearance::DARK, 1.0);
    QVERIFY(!image.m_bTemplate);
    QCOMPARE(QColor(image.m_imgIcon.pixel(4, 6)), QColor(0xfa, 0xb2, 0x19));
    QCOMPARE(QColor(image.m_imgIcon.pixel(4, 12)), QColor(Qt::white));
}

void TestTrayIconPainter::TestRenderColorsSevenDayBarIndependently()
{
    const TrayIconImage image = TrayIconPainter::Render(MakeState(12.0, 93.0), EMenuBarAppearance::LIGHT, 1.0);
    QVERIFY(!image.m_bTemplate);
    QCOMPARE(QColor(image.m_imgIcon.pixel(4, 12)), QColor(0xd0, 0x3b, 0x3b));
    // 12% of the 22pt bar ends near x=3.
    QCOMPARE(qAlpha(image.m_imgIcon.pixel(8, 6)), 0);
}

void TestTrayIconPainter::TestRenderDimsStaleState()
{
    TrayIconState state = MakeState(42.0, 18.0);
    state.m_bStale = true;
    const TrayIconImage image = TrayIconPainter::Render(state, EMenuBarAppearance::LIGHT, 1.0);
    const int iAlpha = qAlpha(image.m_imgIcon.pixel(4, 6));
    QVERIFY2(iAlpha > 115 && iAlpha < 140, qPrintable(QString::number(iAlpha)));
}

void TestTrayIconPainter::TestRenderEmptyStateHasNoFill()
{
    const TrayIconImage image = TrayIconPainter::Render(TrayIconState(), EMenuBarAppearance::LIGHT, 1.0);
    QVERIFY(image.m_bTemplate);
    QCOMPARE(qAlpha(image.m_imgIcon.pixel(4, 6)), 0);
    QCOMPARE(qAlpha(image.m_imgIcon.pixel(4, 12)), 0);
}

void TestTrayIconPainter::TestRenderScalesWithDevicePixelRatio()
{
    const TrayIconImage image = TrayIconPainter::Render(MakeState(42.0, 18.0), EMenuBarAppearance::LIGHT, 2.0);
    QCOMPARE(image.m_imgIcon.height(), 36);
    QCOMPARE(image.m_imgIcon.devicePixelRatio(), 2.0);
    QVERIFY(image.m_imgIcon.width() >= qRound((TrayIconPainter::kBarWidth + TrayIconPainter::kTextGap) * 2.0));
}

QTEST_MAIN(TestTrayIconPainter)
#include "TestTrayIconPainter.moc"
```

- [ ] **Step 4: 빌드해서 실패 확인**

Run: `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DQt5_DIR=/opt/homebrew/anaconda3/lib/cmake/Qt5 && cmake --build build`
Expected: FAIL — CMake 가 `src/UI/TrayIconPainter.cpp` 를 찾지 못한다 (또는 `UI/TrayIconPainter.h` file not found).

- [ ] **Step 5: 구현**

`src/UI/TrayIconPainter.h`:

```cpp
#pragma once

#include <QColor>
#include <QImage>
#include <QString>

class QPainter;

enum class EBarLevel
{
    NORMAL,
    WARNING,
    CRITICAL,
};

enum class EMenuBarAppearance
{
    LIGHT,
    DARK,
};

struct TrayIconState
{
    bool m_bFiveHourValid = false;
    double m_dFiveHourPercent = 0.0;
    bool m_bSevenDayValid = false;
    double m_dSevenDayPercent = 0.0;
    bool m_bStale = false;
    int m_iWarnPercent = 70;
    int m_iCriticalPercent = 90;
};

bool operator==(const TrayIconState& lhs, const TrayIconState& rhs);
bool operator!=(const TrayIconState& lhs, const TrayIconState& rhs);

struct TrayIconImage
{
    QImage m_imgIcon;
    bool m_bTemplate = true;
};

class TrayIconPainter
{
public:
    static constexpr double kHeight = 18.0;
    static constexpr double kBarWidth = 22.0;
    static constexpr double kBarHeight = 4.0;
    static constexpr double kFiveHourBarTop = 4.0;
    static constexpr double kSevenDayBarTop = 10.0;
    static constexpr double kTextGap = 5.0;
    static constexpr int kFontPixelSize = 13;
    static constexpr double kDimmedOpacity = 0.5;

public:
    static EBarLevel ClassifyLevel(double percent, int warnPercent, int criticalPercent);
    static bool IsTemplate(const TrayIconState& state);
    static QString FormatLabel(const TrayIconState& state);
    static QColor GetInkColor(EMenuBarAppearance appearance);
    static QColor GetLevelColor(EBarLevel level, EMenuBarAppearance appearance);
    static TrayIconImage Render(const TrayIconState& state, EMenuBarAppearance appearance, qreal devicePixelRatio);

private:
    static double ClampPercent(double percent);
    static void DrawBar(QPainter& painter, double top, bool valid, double percent, const QColor& fillColor, const QColor& outlineColor);
};
```

`src/UI/TrayIconPainter.cpp`:

```cpp
#include "UI/TrayIconPainter.h"

#include <QFont>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QPainter>

#include <algorithm>
#include <cmath>

namespace
{
constexpr double kMinVisibleFill = 2.0;
constexpr double kOutlineAlpha = 0.45;
constexpr double kOutlineRadius = 1.5;
constexpr double kFillRadius = 2.0;
const QString kEmptyLabel = QStringLiteral("—");
}

bool operator==(const TrayIconState& lhs, const TrayIconState& rhs)
{
    return lhs.m_bFiveHourValid == rhs.m_bFiveHourValid
        && lhs.m_dFiveHourPercent == rhs.m_dFiveHourPercent
        && lhs.m_bSevenDayValid == rhs.m_bSevenDayValid
        && lhs.m_dSevenDayPercent == rhs.m_dSevenDayPercent
        && lhs.m_bStale == rhs.m_bStale
        && lhs.m_iWarnPercent == rhs.m_iWarnPercent
        && lhs.m_iCriticalPercent == rhs.m_iCriticalPercent;
}

bool operator!=(const TrayIconState& lhs, const TrayIconState& rhs)
{
    return !(lhs == rhs);
}

EBarLevel TrayIconPainter::ClassifyLevel(double percent, int warnPercent, int criticalPercent)
{
    if (percent >= criticalPercent)
    {
        return EBarLevel::CRITICAL;
    }
    if (percent >= warnPercent)
    {
        return EBarLevel::WARNING;
    }
    return EBarLevel::NORMAL;
}

bool TrayIconPainter::IsTemplate(const TrayIconState& state)
{
    const bool bFiveNormal = !state.m_bFiveHourValid
        || ClassifyLevel(state.m_dFiveHourPercent, state.m_iWarnPercent, state.m_iCriticalPercent) == EBarLevel::NORMAL;
    const bool bSevenNormal = !state.m_bSevenDayValid
        || ClassifyLevel(state.m_dSevenDayPercent, state.m_iWarnPercent, state.m_iCriticalPercent) == EBarLevel::NORMAL;
    return bFiveNormal && bSevenNormal;
}

QString TrayIconPainter::FormatLabel(const TrayIconState& state)
{
    if (!state.m_bFiveHourValid)
    {
        return kEmptyLabel;
    }
    return QString::number(qRound(ClampPercent(state.m_dFiveHourPercent))) + QLatin1Char('%');
}

QColor TrayIconPainter::GetInkColor(EMenuBarAppearance appearance)
{
    return appearance == EMenuBarAppearance::DARK ? QColor(Qt::white) : QColor(Qt::black);
}

QColor TrayIconPainter::GetLevelColor(EBarLevel level, EMenuBarAppearance appearance)
{
    const bool bDark = appearance == EMenuBarAppearance::DARK;
    switch (level)
    {
    case EBarLevel::WARNING:
        return bDark ? QColor(0xfa, 0xb2, 0x19) : QColor(0xc9, 0x85, 0x00);
    case EBarLevel::CRITICAL:
        return bDark ? QColor(0xff, 0x6b, 0x6b) : QColor(0xd0, 0x3b, 0x3b);
    case EBarLevel::NORMAL:
        break;
    }
    return GetInkColor(appearance);
}

TrayIconImage TrayIconPainter::Render(const TrayIconState& state, EMenuBarAppearance appearance, qreal devicePixelRatio)
{
    TrayIconImage result;
    result.m_bTemplate = IsTemplate(state);

    QFont font = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
    font.setPixelSize(kFontPixelSize);
    const QString strLabel = FormatLabel(state);
    const double dTextWidth = QFontMetricsF(font).horizontalAdvance(strLabel);
    const double dWidth = std::ceil(kBarWidth + kTextGap + dTextWidth + 1.0);

    QImage imgIcon(qRound(dWidth * devicePixelRatio), qRound(kHeight * devicePixelRatio), QImage::Format_ARGB32_Premultiplied);
    imgIcon.setDevicePixelRatio(devicePixelRatio);
    imgIcon.fill(Qt::transparent);

    // A template image only carries alpha and macOS picks the color, so draw it in plain black.
    const EMenuBarAppearance eInkAppearance = result.m_bTemplate ? EMenuBarAppearance::LIGHT : appearance;
    const QColor clrInk = GetInkColor(eInkAppearance);
    QColor clrOutline = clrInk;
    clrOutline.setAlphaF(kOutlineAlpha);

    const EBarLevel eFiveLevel = state.m_bFiveHourValid
        ? ClassifyLevel(state.m_dFiveHourPercent, state.m_iWarnPercent, state.m_iCriticalPercent)
        : EBarLevel::NORMAL;
    const EBarLevel eSevenLevel = state.m_bSevenDayValid
        ? ClassifyLevel(state.m_dSevenDayPercent, state.m_iWarnPercent, state.m_iCriticalPercent)
        : EBarLevel::NORMAL;
    const QColor clrFive = eFiveLevel == EBarLevel::NORMAL ? clrInk : GetLevelColor(eFiveLevel, appearance);
    const QColor clrSeven = eSevenLevel == EBarLevel::NORMAL ? clrInk : GetLevelColor(eSevenLevel, appearance);
    const bool bEmpty = !state.m_bFiveHourValid && !state.m_bSevenDayValid;

    QPainter painter(&imgIcon);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    painter.setOpacity((state.m_bStale || bEmpty) ? kDimmedOpacity : 1.0);

    DrawBar(painter, kFiveHourBarTop, state.m_bFiveHourValid, state.m_dFiveHourPercent, clrFive, clrOutline);
    DrawBar(painter, kSevenDayBarTop, state.m_bSevenDayValid, state.m_dSevenDayPercent, clrSeven, clrOutline);

    painter.setFont(font);
    painter.setPen(clrFive);
    painter.drawText(QRectF(kBarWidth + kTextGap, 0.0, dTextWidth + 1.0, kHeight), Qt::AlignLeft | Qt::AlignVCenter, strLabel);
    painter.end();

    result.m_imgIcon = imgIcon;
    return result;
}

double TrayIconPainter::ClampPercent(double percent)
{
    return std::clamp(percent, 0.0, 100.0);
}

void TrayIconPainter::DrawBar(QPainter& painter, double top, bool valid, double percent, const QColor& fillColor, const QColor& outlineColor)
{
    painter.setPen(QPen(outlineColor, 1.0));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(QRectF(0.5, top + 0.5, kBarWidth - 1.0, kBarHeight - 1.0), kOutlineRadius, kOutlineRadius);

    if (!valid)
    {
        return;
    }
    double dFillWidth = kBarWidth * ClampPercent(percent) / 100.0;
    if (dFillWidth <= 0.0)
    {
        return;
    }
    dFillWidth = std::max(dFillWidth, kMinVisibleFill);
    painter.setPen(Qt::NoPen);
    painter.setBrush(fillColor);
    painter.drawRoundedRect(QRectF(0.0, top, dFillWidth, kBarHeight), kFillRadius, kFillRadius);
}
```

- [ ] **Step 6: 테스트 통과 확인**

Run: `cmake --build build && ctest --test-dir build --output-on-failure -R TestTrayIconPainter`
Expected: PASS (9 tests)

- [ ] **Step 7: Commit**

```bash
git add CMakeLists.txt tests/CMakeLists.txt tests/TestTrayIconPainter.cpp src/UI/TrayIconPainter.h src/UI/TrayIconPainter.cpp
git commit -m "feat: add tray icon painter with level colors" -m "Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>"
```

---

### Task 2: TrayIcon (Qt) + 메뉴바 스파이크

이 Task 의 결과물은 **판정**이다. Qt 5.15.2 의 `QSystemTrayIcon` 으로 스펙 2.1 을 만족하는지 사용자의 눈으로 확인한다.

**Files:**
- Create: `src/UI/TrayIcon.h`, `src/UI/TrayIcon.cpp`, `src/main.cpp` (스파이크용, Task 13 에서 교체), `resources/Info.plist.in`
- Create: `docs/superpowers/notes/2026-10-05-tray-spike.md`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: Task 1 `TrayIconState`, `TrayIconPainter::Render`, `EMenuBarAppearance`
- Produces:
  - `class TrayIcon : public QObject` — `explicit TrayIcon(QObject* parent = nullptr)`, `void Show()`, `void SetState(const TrayIconState& state)`, `QRect GetAnchorGeometry() const` (전역 좌표, 팝업 위치 기준), signal `void Clicked()`
  - CMake 타깃 `TokenViewer` (MACOSX_BUNDLE)

- [ ] **Step 1: TrayIcon 헤더**

`src/UI/TrayIcon.h` (Task 3 의 Objective-C++ 구현도 같은 헤더를 쓴다):

```cpp
#pragma once

#include "UI/TrayIconPainter.h"

#include <QObject>
#include <QRect>

#include <memory>

class TrayIcon : public QObject
{
    Q_OBJECT

public:
    explicit TrayIcon(QObject* parent = nullptr);
    ~TrayIcon();

public:
    void Show();
    void SetState(const TrayIconState& state);

public:
    QRect GetAnchorGeometry() const;

signals:
    void Clicked();

private:
    class TrayIconImpl;
    std::unique_ptr<TrayIconImpl> m_pimpl;
};
```

- [ ] **Step 2: Qt 구현**

`src/UI/TrayIcon.cpp`:

```cpp
#include "UI/TrayIcon.h"

#include <QGuiApplication>
#include <QIcon>
#include <QPalette>
#include <QPixmap>
#include <QSystemTrayIcon>

namespace
{
constexpr int kDarkTextLightness = 128;
}

class TrayIcon::TrayIconImpl
{
public:
    QSystemTrayIcon m_trayIcon;
};

TrayIcon::TrayIcon(QObject* parent)
    : QObject(parent)
    , m_pimpl(std::make_unique<TrayIconImpl>())
{
    connect(&m_pimpl->m_trayIcon, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason)
    {
        if (reason == QSystemTrayIcon::Trigger)
        {
            emit Clicked();
        }
    });
    SetState(TrayIconState());
}

TrayIcon::~TrayIcon() = default;

void TrayIcon::Show()
{
    m_pimpl->m_trayIcon.show();
}

void TrayIcon::SetState(const TrayIconState& state)
{
    // Qt cannot see the menu bar's own appearance; the app palette is the closest guess.
    const bool bDark = QGuiApplication::palette().color(QPalette::WindowText).lightness() > kDarkTextLightness;
    const EMenuBarAppearance eAppearance = bDark ? EMenuBarAppearance::DARK : EMenuBarAppearance::LIGHT;
    const qreal dDevicePixelRatio = qGuiApp->devicePixelRatio();
    const TrayIconImage image = TrayIconPainter::Render(state, eAppearance, dDevicePixelRatio);

    QPixmap pixmap = QPixmap::fromImage(image.m_imgIcon);
    pixmap.setDevicePixelRatio(dDevicePixelRatio);
    QIcon icon(pixmap);
    icon.setIsMask(image.m_bTemplate);
    m_pimpl->m_trayIcon.setIcon(icon);
}

QRect TrayIcon::GetAnchorGeometry() const
{
    return m_pimpl->m_trayIcon.geometry();
}
```

- [ ] **Step 3: 번들 정보와 스파이크 앱**

`resources/Info.plist.in`:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleDevelopmentRegion</key>
    <string>ko</string>
    <key>CFBundleExecutable</key>
    <string>${MACOSX_BUNDLE_EXECUTABLE_NAME}</string>
    <key>CFBundleIdentifier</key>
    <string>${MACOSX_BUNDLE_GUI_IDENTIFIER}</string>
    <key>CFBundleInfoDictionaryVersion</key>
    <string>6.0</string>
    <key>CFBundleName</key>
    <string>${MACOSX_BUNDLE_BUNDLE_NAME}</string>
    <key>CFBundlePackageType</key>
    <string>APPL</string>
    <key>CFBundleShortVersionString</key>
    <string>${MACOSX_BUNDLE_SHORT_VERSION_STRING}</string>
    <key>CFBundleVersion</key>
    <string>${MACOSX_BUNDLE_BUNDLE_VERSION}</string>
    <key>LSMinimumSystemVersion</key>
    <string>11.0</string>
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

(`NSAppSleepDisabled` 는 스펙에 없지만 App Nap 이 갱신 타이머를 늦추지 않게 넣는다.)

`src/main.cpp` (스파이크 전용):

```cpp
#include "UI/TrayIcon.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QEvent>
#include <QLabel>
#include <QTimer>
#include <QVector>

namespace
{
constexpr int kStateCycleMs = 3000;
constexpr int kPopupGap = 4;
constexpr int kReopenGuardMs = 300;
constexpr int kPopupWidth = 300;
constexpr int kPopupHeight = 120;
constexpr int kPopupMargin = 16;

TrayIconState MakeSpikeState(bool valid, double fiveHourPercent, double sevenDayPercent, bool stale)
{
    TrayIconState state;
    state.m_bFiveHourValid = valid;
    state.m_dFiveHourPercent = fiveHourPercent;
    state.m_bSevenDayValid = valid;
    state.m_dSevenDayPercent = sevenDayPercent;
    state.m_bStale = stale;
    return state;
}

class HideWatcher : public QObject
{
public:
    explicit HideWatcher(QElapsedTimer* timer)
        : m_kpTimer(timer)
    {
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (event->type() == QEvent::Hide)
        {
            m_kpTimer->start();
        }
        return QObject::eventFilter(watched, event);
    }

private:
    QElapsedTimer* m_kpTimer;
};
}

// Spike: cycles through every tray icon state so it can be checked by eye on macOS 26.
int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setQuitOnLastWindowClosed(false);

    TrayIcon trayIcon;
    QLabel popup(QStringLiteral("TokenViewer spike popup\n바깥을 누르면 닫혀야 합니다"), nullptr, Qt::Popup);
    popup.setMargin(kPopupMargin);
    popup.resize(kPopupWidth, kPopupHeight);

    QElapsedTimer timerSinceHide;
    HideWatcher hideWatcher(&timerSinceHide);
    popup.installEventFilter(&hideWatcher);

    QObject::connect(&trayIcon, &TrayIcon::Clicked, [&]()
    {
        if (popup.isVisible())
        {
            popup.hide();
            return;
        }
        // The click that closed the popup also reaches the icon; do not reopen right away.
        if (timerSinceHide.isValid() && timerSinceHide.elapsed() < kReopenGuardMs)
        {
            return;
        }
        const QRect rcAnchor = trayIcon.GetAnchorGeometry();
        popup.move(rcAnchor.center().x() - popup.width() / 2, rcAnchor.bottom() + kPopupGap);
        popup.show();
        popup.activateWindow();
    });

    const QVector<TrayIconState> vecStates = {
        MakeSpikeState(false, 0.0, 0.0, false),
        MakeSpikeState(true, 42.0, 18.0, false),
        MakeSpikeState(true, 78.0, 31.0, false),
        MakeSpikeState(true, 96.0, 40.0, false),
        MakeSpikeState(true, 12.0, 93.0, false),
        MakeSpikeState(true, 42.0, 18.0, true),
    };
    int iStateIndex = 0;
    QTimer timerCycle;
    QObject::connect(&timerCycle, &QTimer::timeout, [&]()
    {
        iStateIndex = (iStateIndex + 1) % vecStates.size();
        trayIcon.SetState(vecStates[iStateIndex]);
    });

    trayIcon.SetState(vecStates[iStateIndex]);
    trayIcon.Show();
    timerCycle.start(kStateCycleMs);
    return app.exec();
}
```

- [ ] **Step 4: CMake 에 추가**

`CMakeLists.txt` 의 `set(TV_CORE_SOURCES ...)` 블록 바로 아래(아직 `add_library` 앞)에:

```cmake
list(APPEND TV_CORE_SOURCES src/UI/TrayIcon.h src/UI/TrayIcon.cpp)
```

`target_compile_definitions(TokenViewerCore ...)` 줄 아래에:

```cmake
add_executable(TokenViewer MACOSX_BUNDLE src/main.cpp)
set_target_properties(TokenViewer PROPERTIES
    MACOSX_BUNDLE_INFO_PLIST ${PROJECT_SOURCE_DIR}/resources/Info.plist.in
    MACOSX_BUNDLE_GUI_IDENTIFIER com.tokenviewer.TokenViewer
    MACOSX_BUNDLE_BUNDLE_NAME TokenViewer
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION})
target_link_libraries(TokenViewer PRIVATE TokenViewerCore)
```

- [ ] **Step 5: 빌드와 기존 테스트**

Run: `cmake --build build && ctest --test-dir build --output-on-failure`
Expected: 빌드 성공, `TestTrayIconPainter` PASS

- [ ] **Step 6: 실행해서 사용자와 함께 확인**

Run: `open build/TokenViewer.app`

아이콘이 3초마다 `—` → 42% → 78% → 96% → 주간 93% → 흐린 42% 로 바뀐다. **사용자에게** 아래 항목을 밝은 배경화면과 어두운 배경화면(시스템 설정 > 배경화면) 양쪽에서 봐 달라고 요청한다.

1. 막대 2줄과 숫자가 잘리지 않고 다 보인다.
2. 평소(42%) 아이콘이 다른 메뉴바 아이콘과 같은 색이다 (템플릿).
3. 주황/빨강 상태에서 색칠되지 않은 막대·외곽선·숫자가 배경에서 잘 보인다.
4. 아이콘을 누르면 팝업이 아이콘 바로 아래에 열리고, 다시 누르면 닫힌다.
5. 팝업이 열린 채 다른 앱 창을 누르면 팝업이 닫힌다.

확인이 끝나면 `pkill -x TokenViewer` 로 끈다.

- [ ] **Step 7: 판정 기록과 커밋**

`docs/superpowers/notes/2026-10-05-tray-spike.md` 에 항목별 결과(통과/실패와 사용자가 본 현상)와 판정을 적는다.

- 5개 모두 통과 → **Qt 유지**, Task 3 건너뜀.
- 하나라도 실패 → **Task 3 진행** (Objective-C++ `TrayIcon`).

```bash
git add CMakeLists.txt resources/Info.plist.in src/UI/TrayIcon.h src/UI/TrayIcon.cpp src/main.cpp docs/superpowers/notes/2026-10-05-tray-spike.md
git commit -m "feat: add Qt tray icon and menu bar spike" -m "Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>"
```

---

### Task 3 (조건부): NSStatusItem TrayIcon

**Task 2 판정이 "Qt 유지"면 이 Task 를 건너뛰고 체크박스 옆에 "건너뜀"이라고 적는다.**

**Files:**
- Create: `src/UI/TrayIconMac.mm`
- Modify: `CMakeLists.txt`, `docs/superpowers/notes/2026-10-05-tray-spike.md`

**Interfaces:**
- Consumes: Task 2 `src/UI/TrayIcon.h` (그대로), Task 1 `TrayIconPainter`
- Produces: 같은 `TrayIcon` 인터페이스. 메뉴바 밝기를 `NSStatusBarButton.effectiveAppearance` 로 알아내고 바뀌면 다시 그린다. CMake 옵션 `TV_NATIVE_TRAY` (기본 ON).

- [ ] **Step 1: CMake 전환**

`project(...)` 줄 아래에:

```cmake
option(TV_NATIVE_TRAY "Use the Objective-C++ NSStatusItem tray icon" ON)
```

Task 2 에서 넣은 `list(APPEND TV_CORE_SOURCES src/UI/TrayIcon.h src/UI/TrayIcon.cpp)` 줄을 다음으로 바꾼다:

```cmake
if(TV_NATIVE_TRAY)
    enable_language(OBJCXX)
    list(APPEND TV_CORE_SOURCES src/UI/TrayIcon.h src/UI/TrayIconMac.mm)
    set_source_files_properties(src/UI/TrayIconMac.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")
else()
    list(APPEND TV_CORE_SOURCES src/UI/TrayIcon.h src/UI/TrayIcon.cpp)
endif()
```

`target_link_libraries(TokenViewerCore PUBLIC ...)` 줄 아래에:

```cmake
if(TV_NATIVE_TRAY)
    target_link_libraries(TokenViewerCore PUBLIC "-framework AppKit")
endif()
```

- [ ] **Step 2: Objective-C++ 구현**

`src/UI/TrayIconMac.mm`:

```objc
#import <AppKit/AppKit.h>

#include "UI/TrayIcon.h"

#include <algorithm>
#include <functional>

@interface TVStatusItemTarget : NSObject
{
@public
    std::function<void()> m_funcClicked;
    std::function<void()> m_funcAppearanceChanged;
}
- (void)onClick:(id)sender;
@end

@implementation TVStatusItemTarget
- (void)onClick:(id)sender
{
    (void)sender;
    // An LSUIElement app is never active on its own; activate it so the popup gets focus and closes on outside clicks.
    if (@available(macOS 14.0, *))
    {
        [NSApp activate];
    }
    else
    {
        [NSApp activateIgnoringOtherApps:YES];
    }
    if (m_funcClicked)
    {
        m_funcClicked();
    }
}

- (void)observeValueForKeyPath:(NSString*)keyPath ofObject:(id)object change:(NSDictionary*)change context:(void*)context
{
    (void)keyPath;
    (void)object;
    (void)change;
    (void)context;
    if (m_funcAppearanceChanged)
    {
        m_funcAppearanceChanged();
    }
}
@end

namespace
{
constexpr CGFloat kMinBackingScale = 2.0;
NSString* const kAppearanceKeyPath = @"effectiveAppearance";

NSImage* CreateNSImage(const QImage& image)
{
    const QImage imgArgb = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    CGColorSpaceRef colorSpace = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGContextRef context = CGBitmapContextCreate(const_cast<uchar*>(imgArgb.constBits()), imgArgb.width(), imgArgb.height(), 8,
        imgArgb.bytesPerLine(), colorSpace, kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Host);
    CGImageRef cgImage = CGBitmapContextCreateImage(context);
    const qreal dDevicePixelRatio = image.devicePixelRatio();
    NSImage* nsImage = [[NSImage alloc] initWithCGImage:cgImage
                                                   size:NSMakeSize(image.width() / dDevicePixelRatio, image.height() / dDevicePixelRatio)];
    CGImageRelease(cgImage);
    CGContextRelease(context);
    CGColorSpaceRelease(colorSpace);
    return nsImage;
}
}

class TrayIcon::TrayIconImpl
{
public:
    NSStatusItem* m_pStatusItem = nil;
    TVStatusItemTarget* m_pTarget = nil;
    TrayIconState m_state;

    EMenuBarAppearance DetectAppearance() const
    {
        NSAppearance* pAppearance = m_pStatusItem.button.effectiveAppearance;
        NSAppearanceName strName = [pAppearance bestMatchFromAppearancesWithNames:@[ NSAppearanceNameAqua, NSAppearanceNameDarkAqua ]];
        return [strName isEqualToString:NSAppearanceNameDarkAqua] ? EMenuBarAppearance::DARK : EMenuBarAppearance::LIGHT;
    }

    void Redraw()
    {
        NSWindow* pWindow = m_pStatusItem.button.window;
        const CGFloat dScale = pWindow ? pWindow.backingScaleFactor : NSScreen.mainScreen.backingScaleFactor;
        const TrayIconImage image = TrayIconPainter::Render(m_state, DetectAppearance(), std::max(dScale, kMinBackingScale));
        NSImage* pImage = CreateNSImage(image.m_imgIcon);
        // `template` is a C++ keyword, so use the setter instead of dot syntax.
        [pImage setTemplate:image.m_bTemplate ? YES : NO];
        m_pStatusItem.button.image = pImage;
    }
};

TrayIcon::TrayIcon(QObject* parent)
    : QObject(parent)
    , m_pimpl(std::make_unique<TrayIconImpl>())
{
    TrayIconImpl* pImpl = m_pimpl.get();
    pImpl->m_pStatusItem = [[NSStatusBar systemStatusBar] statusItemWithLength:NSVariableStatusItemLength];
    pImpl->m_pTarget = [[TVStatusItemTarget alloc] init];
    pImpl->m_pTarget->m_funcClicked = [this]() { emit Clicked(); };
    pImpl->m_pTarget->m_funcAppearanceChanged = [pImpl]() { pImpl->Redraw(); };

    NSStatusBarButton* pButton = pImpl->m_pStatusItem.button;
    pButton.target = pImpl->m_pTarget;
    pButton.action = @selector(onClick:);
    [pButton addObserver:pImpl->m_pTarget forKeyPath:kAppearanceKeyPath options:NSKeyValueObservingOptionNew context:nullptr];
    pImpl->m_pStatusItem.visible = NO;
    pImpl->Redraw();
}

TrayIcon::~TrayIcon()
{
    [m_pimpl->m_pStatusItem.button removeObserver:m_pimpl->m_pTarget forKeyPath:kAppearanceKeyPath];
    [[NSStatusBar systemStatusBar] removeStatusItem:m_pimpl->m_pStatusItem];
}

void TrayIcon::Show()
{
    m_pimpl->m_pStatusItem.visible = YES;
}

void TrayIcon::SetState(const TrayIconState& state)
{
    m_pimpl->m_state = state;
    m_pimpl->Redraw();
}

QRect TrayIcon::GetAnchorGeometry() const
{
    NSWindow* pWindow = m_pimpl->m_pStatusItem.button.window;
    if (pWindow == nil)
    {
        return QRect();
    }
    // Cocoa measures from the bottom-left of the primary screen; Qt from its top-left.
    const NSRect rcFrame = pWindow.frame;
    const CGFloat dPrimaryHeight = NSScreen.screens.firstObject.frame.size.height;
    return QRect(qRound(rcFrame.origin.x), qRound(dPrimaryHeight - rcFrame.origin.y - rcFrame.size.height),
        qRound(rcFrame.size.width), qRound(rcFrame.size.height));
}
```

- [ ] **Step 3: 빌드와 테스트**

Run: `cmake -S . -B build -DTV_NATIVE_TRAY=ON && cmake --build build && ctest --test-dir build --output-on-failure`
Expected: 빌드 성공, `TestTrayIconPainter` PASS

- [ ] **Step 4: 사용자와 다시 확인**

`open build/TokenViewer.app` 후 Task 2 Step 6 의 5개 항목을 다시 본다. 이번에는 배경화면을 밝게/어둡게 바꾸면 주황·빨강 상태의 나머지 요소 색도 따라 바뀌어야 한다. `pkill -x TokenViewer` 로 끈다. 결과를 `docs/superpowers/notes/2026-10-05-tray-spike.md` 에 덧붙인다.

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt src/UI/TrayIconMac.mm docs/superpowers/notes/2026-10-05-tray-spike.md
git commit -m "feat: switch tray icon to NSStatusItem" -m "Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>"
```

---

### Task 4: 사용량 API·로그인 정보 확인 (스파이크)

이 Task 의 결과물은 **실제 응답 형태의 기록**과 그 형태를 따른 fixture 다. Task 9 의 파서는 아래 가정으로 쓰여 있다:
`five_hour.utilization` (0–100 숫자) 또는 `used_percentage`, `resets_at` (ISO 8601 문자열 또는 epoch), credentials 는 `claudeAiOauth.{accessToken, expiresAt(ms), subscriptionType, rateLimitTier}`.

**Files:**
- Create: `docs/superpowers/notes/2026-10-05-usage-api-probe.md`, `tests/fixtures/usage_response.json`
- Modify: `docs/superpowers/specs/2026-10-05-token-viewer-design.md` (3.1, 3.2 의 "미검증" 표시)

**Interfaces:**
- Consumes: 없음
- Produces: `tests/fixtures/usage_response.json` (Task 9 `TestParsesRecordedResponseShape` 가 읽는다)

- [ ] **Step 1: 사용자 동의 받기**

사용자에게 묻는다: "Keychain 에서 Claude Code 로그인 정보를 한 번 읽어 `/api/oauth/usage` 를 호출합니다. macOS 가 `security` 의 Keychain 접근 허용을 물을 수 있습니다. 토큰은 화면에 출력하지 않고, 응답(사용률 %, 리셋 시각)과 credentials 의 키 이름만 봅니다. 진행할까요?" 거절하면 이 Task 를 멈추고 Task 9 는 가정대로 진행한다고 기록한다.

- [ ] **Step 2: 확인 스크립트 작성 (커밋하지 않음)**

임시 파일로 만든다:

```bash
PROBE=$(mktemp -t tv_probe).py
cat > "$PROBE" <<'EOF'
import json
import sys
import urllib.error
import urllib.request


def shape(value):
    if isinstance(value, dict):
        return {key: shape(item) for key, item in value.items()}
    if isinstance(value, list):
        return [shape(value[0])] if value else []
    return type(value).__name__


credential = json.loads(sys.stdin.read())
print("== credential shape (types only) ==")
print(json.dumps(shape(credential), indent=2))
oauth = credential.get("claudeAiOauth", {})
for key in ("subscriptionType", "rateLimitTier", "expiresAt"):
    print(f"{key} = {oauth.get(key)!r}")

request = urllib.request.Request(
    "https://api.anthropic.com/api/oauth/usage",
    headers={
        "Authorization": "Bearer " + oauth["accessToken"],
        "anthropic-beta": "oauth-2025-04-20",
        "Accept": "application/json",
        "User-Agent": "TokenViewer-probe/0.1",
    },
)
print("== /api/oauth/usage ==")
try:
    with urllib.request.urlopen(request, timeout=10) as response:
        print("HTTP", response.status)
        print(json.dumps(json.loads(response.read()), indent=2))
except urllib.error.HTTPError as error:
    print("HTTP", error.code)
    print(error.read().decode("utf-8", "replace")[:2000])
EOF
echo "$PROBE"
```

- [ ] **Step 3: 실행**

토큰이 명령줄 인자로 드러나지 않게 파이프로 넘긴다:

```bash
security find-generic-password -s "Claude Code-credentials" -w | python3 "$PROBE"
```

Keychain 항목이 없다고 나오면: `python3 "$PROBE" < ~/.claude/.credentials.json`
HTTP 401 이면 사용자에게 Claude Code 를 한 번 실행해 달라고 한 뒤 다시 실행한다.
끝나면 `rm "$PROBE"`.

- [ ] **Step 4: 결과 기록**

`docs/superpowers/notes/2026-10-05-usage-api-probe.md` 에 적는다:
- credentials 의 키 구조(타입만), `subscriptionType`·`rateLimitTier` 값, `expiresAt` 단위(ms/s)
- 응답의 최상위 키 목록과 `five_hour`·`seven_day` 안의 키·타입, `resets_at` 형식 예, % 의 범위(0–100 인지 0–1 인지)
- 위 가정과 다른 점

- [ ] **Step 5: fixture 작성**

`tests/fixtures/usage_response.json` 은 **실제 응답과 키·타입·형식이 같고 값만 바꾼** JSON 이다. 실제 응답이 가정과 같다면 다음과 같다:

```json
{
  "five_hour": {
    "utilization": 42.0,
    "resets_at": "2026-10-05T07:40:00.123456+00:00"
  },
  "seven_day": {
    "utilization": 18.0,
    "resets_at": "2026-10-08T00:00:00.000000+00:00"
  }
}
```

실제 응답에 다른 키(예: `seven_day_opus`, `extra_usage`)가 있으면 같은 이름·타입으로 넣는다. 계정 ID 같은 식별자가 있으면 `"redacted"` 로 바꾼다.

- [ ] **Step 6: 가정과 다르면 계획을 고친다**

% 가 0–1 이거나 키 이름·`resets_at` 형식·credentials 구조가 다르면, **Task 9 의 `ParseWindow`/`ParseTimestamp`/`ParseCredentialJson`/`FormatPlanLabel` 코드와 테스트 값을 실제 형태로 고친 뒤** Task 9 를 시작한다. 고친 내용을 노트에 적는다.

- [ ] **Step 7: 스펙 갱신과 커밋**

스펙 3.1·3.2 의 "(**미검증**…)" 을 "(2026-10-05 실제 응답으로 확인, `docs/superpowers/notes/2026-10-05-usage-api-probe.md`)" 로 바꾼다.

```bash
git add docs/superpowers/notes/2026-10-05-usage-api-probe.md tests/fixtures/usage_response.json docs/superpowers/specs/2026-10-05-token-viewer-design.md
git commit -m "docs: record usage API and credential shapes" -m "Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>"
```

---

### Task 5: TokenRecord + PriceTable

**Files:**
- Create: `src/Model/TokenRecord.h`, `src/Analysis/PriceTable.h`, `src/Analysis/PriceTable.cpp`, `resources/prices.json`, `tests/TestHelpers.h`
- Modify: `CMakeLists.txt`, `tests/CMakeLists.txt`
- Test: `tests/TestPriceTable.cpp`

**Interfaces:**
- Consumes: 없음
- Produces:
  - `struct TokenRecord { QString m_strKey; QDateTime m_dtTimestamp; QString m_strSessionId; QString m_strProjectPath; QString m_strModel; bool m_bFast; qint64 m_llInput, m_llOutput, m_llCacheWrite5m, m_llCacheWrite1h, m_llCacheRead; }`
  - `struct LogSnapshot { QVector<TokenRecord> m_vecRecords; QHash<QString, QString> m_hashSessionTitles; }` + `Q_DECLARE_METATYPE(LogSnapshot)`
  - `enum class EPriceMatch { EXACT, FAMILY, NONE }`, `struct ModelPrice`, `struct PriceQuote { EPriceMatch m_eMatch; ModelPrice m_price; }`, `struct CostResult { double m_dUsd; bool m_bApproximate; bool m_bUnpriced; }`
  - `PriceTable::LoadFromJson(const QByteArray&) -> bool`, `FindPrice(const QString&) const -> PriceQuote`, `CalculateCost(const TokenRecord&) const -> CostResult`, `GetAsOf() const -> QString`, `GetModelCount() const -> int`
  - `tests/TestHelpers.h`: `QByteArray ReadSourceFile(const QString& relativePath)`

- [ ] **Step 1: 테스트 공용 헤더와 실패하는 테스트**

`tests/TestHelpers.h`:

```cpp
#pragma once

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>

inline QByteArray ReadSourceFile(const QString& relativePath)
{
    QFile file(QString::fromUtf8(TV_SOURCE_DIR) + QLatin1Char('/') + relativePath);
    if (!file.open(QIODevice::ReadOnly))
    {
        return QByteArray();
    }
    return file.readAll();
}
```

`tests/TestPriceTable.cpp`:

```cpp
#include "Analysis/PriceTable.h"

#include "TestHelpers.h"

#include <QtTest>

namespace
{
TokenRecord MakeRecord(const QString& model, bool fast)
{
    TokenRecord record;
    record.m_strModel = model;
    record.m_bFast = fast;
    record.m_llInput = 1000000;
    record.m_llOutput = 100000;
    record.m_llCacheWrite5m = 200000;
    record.m_llCacheWrite1h = 50000;
    record.m_llCacheRead = 2000000;
    return record;
}
}

class TestPriceTable : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void TestLoadsBundledPrices();
    void TestFindsExactModel();
    void TestFindsDatedModel();
    void TestFallsBackToFamily();
    void TestUnknownFamilyIsUnpriced();
    void TestCalculatesCost();
    void TestDoublesFastOpus();
    void TestIgnoresFastWithoutMultiplier();
    void TestRejectsBrokenJson();

private:
    PriceTable m_priceTable;
};

void TestPriceTable::init()
{
    QVERIFY(m_priceTable.LoadFromJson(ReadSourceFile(QStringLiteral("resources/prices.json"))));
}

void TestPriceTable::TestLoadsBundledPrices()
{
    QCOMPARE(m_priceTable.GetModelCount(), 7);
    QCOMPARE(m_priceTable.GetAsOf(), QStringLiteral("2026-09-25"));
}

void TestPriceTable::TestFindsExactModel()
{
    const PriceQuote quote = m_priceTable.FindPrice(QStringLiteral("claude-opus-5-5"));
    QCOMPARE(quote.m_eMatch, EPriceMatch::EXACT);
    QCOMPARE(quote.m_price.m_dInput, 4.0);
    QCOMPARE(quote.m_price.m_dCacheRead, 0.2);
}

void TestPriceTable::TestFindsDatedModel()
{
    const PriceQuote quote = m_priceTable.FindPrice(QStringLiteral("claude-haiku-4-5-20251001"));
    QCOMPARE(quote.m_eMatch, EPriceMatch::EXACT);
    QCOMPARE(quote.m_price.m_dOutput, 5.0);
}

void TestPriceTable::TestFallsBackToFamily()
{
    const PriceQuote quoteNew = m_priceTable.FindPrice(QStringLiteral("claude-opus-6"));
    QCOMPARE(quoteNew.m_eMatch, EPriceMatch::FAMILY);
    QCOMPARE(quoteNew.m_price.m_strId, QStringLiteral("claude-opus-5-5"));

    // "claude-opus-5" is a prefix of this id, but the rest is not a date suffix.
    const PriceQuote quotePoint = m_priceTable.FindPrice(QStringLiteral("claude-opus-5-7"));
    QCOMPARE(quotePoint.m_eMatch, EPriceMatch::FAMILY);
}

void TestPriceTable::TestUnknownFamilyIsUnpriced()
{
    QCOMPARE(m_priceTable.FindPrice(QStringLiteral("<synthetic>")).m_eMatch, EPriceMatch::NONE);
    const CostResult cost = m_priceTable.CalculateCost(MakeRecord(QStringLiteral("gpt-oss"), false));
    QVERIFY(cost.m_bUnpriced);
    QCOMPARE(cost.m_dUsd, 0.0);
}

void TestPriceTable::TestCalculatesCost()
{
    // Opus 5.5: 4 + 2 + 1 + 0.4 + 0.4 dollars for the token mix in MakeRecord.
    const CostResult cost = m_priceTable.CalculateCost(MakeRecord(QStringLiteral("claude-opus-5-5"), false));
    QVERIFY(qFuzzyCompare(cost.m_dUsd, 7.8));
    QVERIFY(!cost.m_bApproximate);
    QVERIFY(!cost.m_bUnpriced);

    const CostResult costFamily = m_priceTable.CalculateCost(MakeRecord(QStringLiteral("claude-opus-6"), false));
    QVERIFY(costFamily.m_bApproximate);
}

void TestPriceTable::TestDoublesFastOpus()
{
    const CostResult cost = m_priceTable.CalculateCost(MakeRecord(QStringLiteral("claude-opus-5-5"), true));
    QVERIFY(qFuzzyCompare(cost.m_dUsd, 15.6));
}

void TestPriceTable::TestIgnoresFastWithoutMultiplier()
{
    // Sonnet 5.5: 2 + 1 + 0.5 + 0.2 + 0.4 dollars, fast or not.
    const CostResult cost = m_priceTable.CalculateCost(MakeRecord(QStringLiteral("claude-sonnet-5-5"), true));
    QVERIFY(qFuzzyCompare(cost.m_dUsd, 4.1));
}

void TestPriceTable::TestRejectsBrokenJson()
{
    PriceTable table;
    QVERIFY(!table.LoadFromJson(QByteArrayLiteral("{")));
    QVERIFY(!table.LoadFromJson(QByteArrayLiteral("{\"models\":[]}")));
    QCOMPARE(table.GetModelCount(), 0);
}

QTEST_GUILESS_MAIN(TestPriceTable)
#include "TestPriceTable.moc"
```

`tests/CMakeLists.txt` 끝에 `tv_add_test(TestPriceTable)` 추가.

- [ ] **Step 2: 빌드해서 실패 확인**

Run: `cmake --build build`
Expected: FAIL — `Analysis/PriceTable.h` file not found

- [ ] **Step 3: 구현**

`src/Model/TokenRecord.h`:

```cpp
#pragma once

#include <QDateTime>
#include <QHash>
#include <QMetaType>
#include <QString>
#include <QVector>

struct TokenRecord
{
    QString m_strKey;
    QDateTime m_dtTimestamp;
    QString m_strSessionId;
    QString m_strProjectPath;
    QString m_strModel;
    bool m_bFast = false;
    qint64 m_llInput = 0;
    qint64 m_llOutput = 0;
    qint64 m_llCacheWrite5m = 0;
    qint64 m_llCacheWrite1h = 0;
    qint64 m_llCacheRead = 0;
};

struct LogSnapshot
{
    QVector<TokenRecord> m_vecRecords;
    QHash<QString, QString> m_hashSessionTitles;
};

Q_DECLARE_METATYPE(LogSnapshot)
```

`resources/prices.json`:

```json
{
  "asOf": "2026-09-25",
  "source": "claude-api skill model table (USD per 1M tokens)",
  "models": [
    { "id": "claude-fable-5-1", "family": "fable", "input": 10, "output": 50, "cacheWrite5m": 12.5, "cacheWrite1h": 20, "cacheRead": 0.25, "fastMultiplier": 1 },
    { "id": "claude-fable-5", "family": "fable", "input": 10, "output": 50, "cacheWrite5m": 12.5, "cacheWrite1h": 20, "cacheRead": 1.0, "fastMultiplier": 1 },
    { "id": "claude-opus-5-5", "family": "opus", "input": 4, "output": 20, "cacheWrite5m": 5, "cacheWrite1h": 8, "cacheRead": 0.2, "fastMultiplier": 2 },
    { "id": "claude-opus-5", "family": "opus", "input": 5, "output": 25, "cacheWrite5m": 6.25, "cacheWrite1h": 10, "cacheRead": 0.5, "fastMultiplier": 2 },
    { "id": "claude-sonnet-5-5", "family": "sonnet", "input": 2, "output": 10, "cacheWrite5m": 2.5, "cacheWrite1h": 4, "cacheRead": 0.2, "fastMultiplier": 1 },
    { "id": "claude-sonnet-5", "family": "sonnet", "input": 2, "output": 10, "cacheWrite5m": 2.5, "cacheWrite1h": 4, "cacheRead": 0.2, "fastMultiplier": 1 },
    { "id": "claude-haiku-4-5", "family": "haiku", "input": 1, "output": 5, "cacheWrite5m": 1.25, "cacheWrite1h": 2, "cacheRead": 0.1, "fastMultiplier": 1 }
  ]
}
```

(같은 계열의 첫 행이 계열 대체 가격이다. 그래서 계열마다 최신 모델을 먼저 둔다. Opus 5 fast 도 2배라서 `fastMultiplier` 2.)

`src/Analysis/PriceTable.h`:

```cpp
#pragma once

#include "Model/TokenRecord.h"

#include <QByteArray>
#include <QString>
#include <QVector>

struct ModelPrice
{
    QString m_strId;
    QString m_strFamily;
    double m_dInput = 0.0;
    double m_dOutput = 0.0;
    double m_dCacheWrite5m = 0.0;
    double m_dCacheWrite1h = 0.0;
    double m_dCacheRead = 0.0;
    double m_dFastMultiplier = 1.0;
};

enum class EPriceMatch
{
    EXACT,
    FAMILY,
    NONE,
};

struct PriceQuote
{
    EPriceMatch m_eMatch = EPriceMatch::NONE;
    ModelPrice m_price;
};

struct CostResult
{
    double m_dUsd = 0.0;
    bool m_bApproximate = false;
    bool m_bUnpriced = false;
};

class PriceTable
{
public:
    PriceTable();
    ~PriceTable();

public:
    bool LoadFromJson(const QByteArray& json);
    PriceQuote FindPrice(const QString& modelId) const;
    CostResult CalculateCost(const TokenRecord& record) const;

public:
    QString GetAsOf() const;
    int GetModelCount() const;

private:
    QVector<ModelPrice> m_vecModels;
    QString m_strAsOf;
};
```

`src/Analysis/PriceTable.cpp`:

```cpp
#include "Analysis/PriceTable.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QRegularExpression>
#include <QStringList>

namespace
{
constexpr double kTokensPerPriceUnit = 1000000.0;
}

PriceTable::PriceTable()
    : m_vecModels()
    , m_strAsOf()
{
}

PriceTable::~PriceTable() = default;

bool PriceTable::LoadFromJson(const QByteArray& json)
{
    const QJsonDocument doc = QJsonDocument::fromJson(json);
    if (!doc.isObject())
    {
        return false;
    }
    const QJsonObject objRoot = doc.object();
    QVector<ModelPrice> vecModels;
    const QJsonArray arrModels = objRoot.value(QStringLiteral("models")).toArray();
    for (const QJsonValue& jvModel : arrModels)
    {
        const QJsonObject objModel = jvModel.toObject();
        ModelPrice price;
        price.m_strId = objModel.value(QStringLiteral("id")).toString();
        price.m_strFamily = objModel.value(QStringLiteral("family")).toString();
        price.m_dInput = objModel.value(QStringLiteral("input")).toDouble();
        price.m_dOutput = objModel.value(QStringLiteral("output")).toDouble();
        price.m_dCacheWrite5m = objModel.value(QStringLiteral("cacheWrite5m")).toDouble();
        price.m_dCacheWrite1h = objModel.value(QStringLiteral("cacheWrite1h")).toDouble();
        price.m_dCacheRead = objModel.value(QStringLiteral("cacheRead")).toDouble();
        price.m_dFastMultiplier = objModel.value(QStringLiteral("fastMultiplier")).toDouble(1.0);
        if (!price.m_strId.isEmpty())
        {
            vecModels.append(price);
        }
    }
    if (vecModels.isEmpty())
    {
        return false;
    }
    m_vecModels = vecModels;
    m_strAsOf = objRoot.value(QStringLiteral("asOf")).toString();
    return true;
}

PriceQuote PriceTable::FindPrice(const QString& modelId) const
{
    static const QRegularExpression reDateSuffix(QStringLiteral("^-\\d{8}$"));
    PriceQuote quote;
    for (const ModelPrice& price : m_vecModels)
    {
        const bool bSame = modelId == price.m_strId;
        const bool bDated = modelId.startsWith(price.m_strId) && reDateSuffix.match(modelId.mid(price.m_strId.size())).hasMatch();
        if (bSame || bDated)
        {
            quote.m_eMatch = EPriceMatch::EXACT;
            quote.m_price = price;
            return quote;
        }
    }

    const QStringList lstParts = modelId.split(QLatin1Char('-'));
    for (const ModelPrice& price : m_vecModels)
    {
        if (!price.m_strFamily.isEmpty() && lstParts.contains(price.m_strFamily))
        {
            quote.m_eMatch = EPriceMatch::FAMILY;
            quote.m_price = price;
            return quote;
        }
    }
    return quote;
}

CostResult PriceTable::CalculateCost(const TokenRecord& record) const
{
    CostResult result;
    const PriceQuote quote = FindPrice(record.m_strModel);
    if (quote.m_eMatch == EPriceMatch::NONE)
    {
        result.m_bUnpriced = true;
        return result;
    }

    const ModelPrice& price = quote.m_price;
    double dUsd = (record.m_llInput * price.m_dInput
        + record.m_llOutput * price.m_dOutput
        + record.m_llCacheWrite5m * price.m_dCacheWrite5m
        + record.m_llCacheWrite1h * price.m_dCacheWrite1h
        + record.m_llCacheRead * price.m_dCacheRead) / kTokensPerPriceUnit;
    if (record.m_bFast)
    {
        dUsd *= price.m_dFastMultiplier;
    }
    result.m_dUsd = dUsd;
    result.m_bApproximate = quote.m_eMatch == EPriceMatch::FAMILY;
    return result;
}

QString PriceTable::GetAsOf() const
{
    return m_strAsOf;
}

int PriceTable::GetModelCount() const
{
    return m_vecModels.size();
}
```

`CMakeLists.txt` 의 `set(TV_CORE_SOURCES ...)` 안에 `src/Analysis/PriceTable.cpp` 를 추가한다.

- [ ] **Step 4: 테스트 통과 확인**

Run: `cmake --build build && ctest --test-dir build --output-on-failure -R TestPriceTable`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt tests/CMakeLists.txt tests/TestHelpers.h tests/TestPriceTable.cpp src/Model/TokenRecord.h src/Analysis/PriceTable.h src/Analysis/PriceTable.cpp resources/prices.json
git commit -m "feat: add price table and token record model" -m "Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>"
```

---

### Task 6: LogParser

**Files:**
- Create: `src/Analysis/LogParser.h`, `src/Analysis/LogParser.cpp`
- Modify: `CMakeLists.txt`, `tests/CMakeLists.txt`
- Test: `tests/TestLogParser.cpp`

**Interfaces:**
- Consumes: Task 5 `TokenRecord`
- Produces:
  - `enum class ELineKind { IGNORED, USAGE, TITLE }`
  - `struct ParsedLine { ELineKind m_eKind; TokenRecord m_record; QString m_strSessionId; QString m_strTitle; }`
  - `LogParser::ParseLine(const QByteArray& line, const QString& fallbackSessionId) -> ParsedLine`
  - `LogParser::FindSessionIdFromPath(const QString& filePath) -> QString`
  - 중복 제거 키: `message.id + "|" + requestId` (requestId 가 없으면 `message.id`, message.id 도 없으면 `uuid`)

- [ ] **Step 1: 실패하는 테스트**

`tests/TestLogParser.cpp`:

```cpp
#include "Analysis/LogParser.h"

#include <QtTest>

namespace
{
const QByteArray kAssistantLine = QByteArrayLiteral(R"({"parentUuid":"p1","isSidechain":false,"message":{"model":"claude-opus-5-5","id":"msg_A","type":"message","role":"assistant","content":[],"usage":{"input_tokens":2,"cache_creation_input_tokens":18829,"cache_read_input_tokens":20738,"output_tokens":558,"cache_creation":{"ephemeral_1h_input_tokens":18000,"ephemeral_5m_input_tokens":829},"speed":"standard"}},"requestId":"req_A","type":"assistant","uuid":"u1","timestamp":"2026-09-27T00:10:21.996Z","cwd":"/Users/me/Desktop/Games/CaptureGame","sessionId":"sess-1","version":"2.1.282"})");
}

class TestLogParser : public QObject
{
    Q_OBJECT

private slots:
    void TestParsesAssistantUsage();
    void TestFallsBackToTotalCacheCreation();
    void TestMarksFastSpeed();
    void TestIgnoresSyntheticModel();
    void TestIgnoresNonAssistantLines();
    void TestIgnoresBrokenJson();
    void TestParsesAiTitle();
    void TestUsesFallbackSessionId();
    void TestKeyWithoutRequestId();
    void TestFindsSessionIdFromPath();
};

void TestLogParser::TestParsesAssistantUsage()
{
    const ParsedLine parsed = LogParser::ParseLine(kAssistantLine, QStringLiteral("unused"));
    QCOMPARE(parsed.m_eKind, ELineKind::USAGE);
    const TokenRecord& record = parsed.m_record;
    QCOMPARE(record.m_strKey, QStringLiteral("msg_A|req_A"));
    QCOMPARE(record.m_dtTimestamp, QDateTime(QDate(2026, 9, 27), QTime(0, 10, 21, 996), Qt::UTC));
    QCOMPARE(record.m_strSessionId, QStringLiteral("sess-1"));
    QCOMPARE(record.m_strProjectPath, QStringLiteral("/Users/me/Desktop/Games/CaptureGame"));
    QCOMPARE(record.m_strModel, QStringLiteral("claude-opus-5-5"));
    QCOMPARE(record.m_llInput, qint64(2));
    QCOMPARE(record.m_llOutput, qint64(558));
    QCOMPARE(record.m_llCacheWrite5m, qint64(829));
    QCOMPARE(record.m_llCacheWrite1h, qint64(18000));
    QCOMPARE(record.m_llCacheRead, qint64(20738));
    QVERIFY(!record.m_bFast);
}

void TestLogParser::TestFallsBackToTotalCacheCreation()
{
    const QByteArray baLine = QByteArrayLiteral(R"({"type":"assistant","sessionId":"s","timestamp":"2026-09-27T00:00:00.000Z","requestId":"r","message":{"id":"m","model":"claude-sonnet-5-5","usage":{"input_tokens":1,"output_tokens":2,"cache_creation_input_tokens":500,"cache_read_input_tokens":3}}})");
    const ParsedLine parsed = LogParser::ParseLine(baLine, QString());
    QCOMPARE(parsed.m_eKind, ELineKind::USAGE);
    QCOMPARE(parsed.m_record.m_llCacheWrite5m, qint64(500));
    QCOMPARE(parsed.m_record.m_llCacheWrite1h, qint64(0));
}

void TestLogParser::TestMarksFastSpeed()
{
    QByteArray baLine = kAssistantLine;
    baLine.replace("\"speed\":\"standard\"", "\"speed\":\"fast\"");
    QVERIFY(LogParser::ParseLine(baLine, QString()).m_record.m_bFast);
}

void TestLogParser::TestIgnoresSyntheticModel()
{
    QByteArray baLine = kAssistantLine;
    baLine.replace("\"model\":\"claude-opus-5-5\"", "\"model\":\"<synthetic>\"");
    QCOMPARE(LogParser::ParseLine(baLine, QString()).m_eKind, ELineKind::IGNORED);
}

void TestLogParser::TestIgnoresNonAssistantLines()
{
    const QByteArray baLine = QByteArrayLiteral(R"({"type":"user","message":{"role":"user","content":"hi"},"toolUseResult":{"usage":{"output_tokens":5}},"timestamp":"2026-09-27T00:00:00.000Z","sessionId":"s"})");
    QCOMPARE(LogParser::ParseLine(baLine, QString()).m_eKind, ELineKind::IGNORED);
}

void TestLogParser::TestIgnoresBrokenJson()
{
    QCOMPARE(LogParser::ParseLine(kAssistantLine.left(120), QString()).m_eKind, ELineKind::IGNORED);
}

void TestLogParser::TestParsesAiTitle()
{
    const QByteArray baLine = QByteArrayLiteral(R"({"type":"ai-title","aiTitle":"BundleFusion 논문 정리","sessionId":"sess-1"})");
    const ParsedLine parsed = LogParser::ParseLine(baLine, QString());
    QCOMPARE(parsed.m_eKind, ELineKind::TITLE);
    QCOMPARE(parsed.m_strSessionId, QStringLiteral("sess-1"));
    QCOMPARE(parsed.m_strTitle, QStringLiteral("BundleFusion 논문 정리"));
}

void TestLogParser::TestUsesFallbackSessionId()
{
    QByteArray baLine = kAssistantLine;
    baLine.replace(",\"sessionId\":\"sess-1\"", "");
    const ParsedLine parsed = LogParser::ParseLine(baLine, QStringLiteral("from-path"));
    QCOMPARE(parsed.m_record.m_strSessionId, QStringLiteral("from-path"));
}

void TestLogParser::TestKeyWithoutRequestId()
{
    QByteArray baLine = kAssistantLine;
    baLine.replace("\"requestId\":\"req_A\",", "");
    QCOMPARE(LogParser::ParseLine(baLine, QString()).m_record.m_strKey, QStringLiteral("msg_A"));
}

void TestLogParser::TestFindsSessionIdFromPath()
{
    QCOMPARE(LogParser::FindSessionIdFromPath(QStringLiteral("/h/.claude/projects/-Users-me-A/abc-123.jsonl")), QStringLiteral("abc-123"));
    QCOMPARE(LogParser::FindSessionIdFromPath(QStringLiteral("/h/.claude/projects/-Users-me-A/abc-123/subagents/agent-x.jsonl")), QStringLiteral("abc-123"));
}

QTEST_GUILESS_MAIN(TestLogParser)
#include "TestLogParser.moc"
```

`tests/CMakeLists.txt` 끝에 `tv_add_test(TestLogParser)` 추가.

- [ ] **Step 2: 빌드해서 실패 확인**

Run: `cmake --build build`
Expected: FAIL — `Analysis/LogParser.h` file not found

- [ ] **Step 3: 구현**

`src/Analysis/LogParser.h`:

```cpp
#pragma once

#include "Model/TokenRecord.h"

#include <QByteArray>
#include <QString>

enum class ELineKind
{
    IGNORED,
    USAGE,
    TITLE,
};

struct ParsedLine
{
    ELineKind m_eKind = ELineKind::IGNORED;
    TokenRecord m_record;
    QString m_strSessionId;
    QString m_strTitle;
};

class LogParser
{
public:
    static ParsedLine ParseLine(const QByteArray& line, const QString& fallbackSessionId);
    static QString FindSessionIdFromPath(const QString& filePath);
};
```

`src/Analysis/LogParser.cpp`:

```cpp
#include "Analysis/LogParser.h"

#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>

namespace
{
const QString kSyntheticModel = QStringLiteral("<synthetic>");
const QString kSubagentDirName = QStringLiteral("subagents");

qint64 ReadCount(const QJsonObject& object, const QString& key)
{
    return static_cast<qint64>(object.value(key).toDouble());
}
}

ParsedLine LogParser::ParseLine(const QByteArray& line, const QString& fallbackSessionId)
{
    ParsedLine parsed;
    // Most lines carry neither field; skip them before paying for a JSON parse.
    if (!line.contains("\"usage\"") && !line.contains("\"ai-title\""))
    {
        return parsed;
    }

    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(line, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject())
    {
        return parsed;
    }

    const QJsonObject objLine = doc.object();
    const QString strType = objLine.value(QStringLiteral("type")).toString();
    QString strSessionId = objLine.value(QStringLiteral("sessionId")).toString();
    if (strSessionId.isEmpty())
    {
        strSessionId = fallbackSessionId;
    }

    if (strType == QStringLiteral("ai-title"))
    {
        const QString strTitle = objLine.value(QStringLiteral("aiTitle")).toString().trimmed();
        if (!strTitle.isEmpty() && !strSessionId.isEmpty())
        {
            parsed.m_eKind = ELineKind::TITLE;
            parsed.m_strSessionId = strSessionId;
            parsed.m_strTitle = strTitle;
        }
        return parsed;
    }
    if (strType != QStringLiteral("assistant"))
    {
        return parsed;
    }

    const QJsonObject objMessage = objLine.value(QStringLiteral("message")).toObject();
    const QJsonObject objUsage = objMessage.value(QStringLiteral("usage")).toObject();
    const QString strModel = objMessage.value(QStringLiteral("model")).toString();
    if (objUsage.isEmpty() || strModel.isEmpty() || strModel == kSyntheticModel)
    {
        return parsed;
    }

    const QDateTime dtTimestamp = QDateTime::fromString(objLine.value(QStringLiteral("timestamp")).toString(), Qt::ISODateWithMs);
    if (!dtTimestamp.isValid())
    {
        return parsed;
    }

    // Streaming writes the same response 2-5 times; message id + request id identifies it.
    QString strKey = objMessage.value(QStringLiteral("id")).toString();
    if (strKey.isEmpty())
    {
        strKey = objLine.value(QStringLiteral("uuid")).toString();
    }
    if (strKey.isEmpty())
    {
        return parsed;
    }
    const QString strRequestId = objLine.value(QStringLiteral("requestId")).toString();
    if (!strRequestId.isEmpty())
    {
        strKey += QLatin1Char('|') + strRequestId;
    }

    TokenRecord& record = parsed.m_record;
    record.m_strKey = strKey;
    record.m_dtTimestamp = dtTimestamp.toUTC();
    record.m_strSessionId = strSessionId;
    record.m_strProjectPath = objLine.value(QStringLiteral("cwd")).toString();
    record.m_strModel = strModel;
    record.m_bFast = objUsage.value(QStringLiteral("speed")).toString() == QStringLiteral("fast");
    record.m_llInput = ReadCount(objUsage, QStringLiteral("input_tokens"));
    record.m_llOutput = ReadCount(objUsage, QStringLiteral("output_tokens"));
    record.m_llCacheRead = ReadCount(objUsage, QStringLiteral("cache_read_input_tokens"));

    const QJsonObject objCacheCreation = objUsage.value(QStringLiteral("cache_creation")).toObject();
    const bool bHasSplit = objCacheCreation.contains(QStringLiteral("ephemeral_5m_input_tokens"))
        || objCacheCreation.contains(QStringLiteral("ephemeral_1h_input_tokens"));
    if (bHasSplit)
    {
        record.m_llCacheWrite5m = ReadCount(objCacheCreation, QStringLiteral("ephemeral_5m_input_tokens"));
        record.m_llCacheWrite1h = ReadCount(objCacheCreation, QStringLiteral("ephemeral_1h_input_tokens"));
    }
    else
    {
        record.m_llCacheWrite5m = ReadCount(objUsage, QStringLiteral("cache_creation_input_tokens"));
    }

    parsed.m_eKind = ELineKind::USAGE;
    return parsed;
}

QString LogParser::FindSessionIdFromPath(const QString& filePath)
{
    const QFileInfo fiFile(filePath);
    const QFileInfo fiParent(fiFile.path());
    if (fiParent.fileName() == kSubagentDirName)
    {
        return QFileInfo(fiParent.path()).fileName();
    }
    return fiFile.completeBaseName();
}
```

`CMakeLists.txt` 의 `set(TV_CORE_SOURCES ...)` 안에 `src/Analysis/LogParser.cpp` 추가.

- [ ] **Step 4: 테스트 통과 확인**

Run: `cmake --build build && ctest --test-dir build --output-on-failure -R TestLogParser`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt tests/CMakeLists.txt tests/TestLogParser.cpp src/Analysis/LogParser.h src/Analysis/LogParser.cpp
git commit -m "feat: parse Claude Code session log lines" -m "Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>"
```

---

### Task 7: Breakdown 모델 + TokenAggregator

**Files:**
- Create: `src/Model/Breakdown.h`, `src/Analysis/TokenAggregator.h`, `src/Analysis/TokenAggregator.cpp`
- Modify: `CMakeLists.txt`, `tests/CMakeLists.txt`
- Test: `tests/TestTokenAggregator.cpp`

**Interfaces:**
- Consumes: Task 5 `TokenRecord`, `LogSnapshot`, `PriceTable::CalculateCost`
- Produces:
  - `enum class EBreakdownDimension { PROJECT = 0, MODEL = 1, SESSION = 2 }`, `enum class EBreakdownPeriod { FIVE_HOUR_WINDOW = 0, TODAY = 1, SEVEN_DAYS = 2, THIRTY_DAYS = 3 }`
  - `struct BreakdownRow { QString m_strLabel; QString m_strDetail; double m_dCostUsd; bool m_bApproximate; bool m_bUnpriced; qint64 m_llInput, m_llOutput, m_llCacheWrite, m_llCacheRead; }`
  - `struct Breakdown { QVector<BreakdownRow> m_vecRows; int m_iOtherCount; BreakdownRow m_otherRow; double m_dTotalUsd; }`
  - `struct AggregateQuery { EBreakdownDimension m_eDimension; QDateTime m_dtFrom; int m_iTopCount = 5; }`
  - `TokenAggregator::Aggregate(const LogSnapshot&, const PriceTable&, const AggregateQuery&) -> Breakdown`
  - `TokenAggregator::CalculatePeriodStart(EBreakdownPeriod, const QDateTime& now, const QDateTime& fiveHourResetsAt) -> QDateTime` (UTC)
  - `TokenAggregator::FormatModelName(const QString&) -> QString`

- [ ] **Step 1: 실패하는 테스트**

`tests/TestTokenAggregator.cpp`:

```cpp
#include "Analysis/TokenAggregator.h"

#include "TestHelpers.h"

#include <QtTest>

#include <ctime>

namespace
{
TokenRecord MakeRecord(const QString& key, const QString& timestamp, const QString& sessionId,
    const QString& projectPath, const QString& model, qint64 output)
{
    TokenRecord record;
    record.m_strKey = key;
    record.m_dtTimestamp = QDateTime::fromString(timestamp, Qt::ISODate);
    record.m_strSessionId = sessionId;
    record.m_strProjectPath = projectPath;
    record.m_strModel = model;
    record.m_llOutput = output;
    return record;
}

AggregateQuery MakeQuery(EBreakdownDimension dimension)
{
    AggregateQuery query;
    query.m_eDimension = dimension;
    return query;
}
}

class TestTokenAggregator : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void TestGroupsByProjectAndSortsByCost();
    void TestDisambiguatesSameProjectNames();
    void TestGroupsByModelWithDisplayNames();
    void TestSessionUsesTitleOrFallback();
    void TestFiltersByPeriodStart();
    void TestFoldsBeyondTopCountIntoOther();
    void TestMarksApproximateAndUnpriced();
    void TestCalculatesFiveHourWindowStart();
    void TestCalculatesTodayFromLocalMidnight();
    void TestFormatsModelNames();

private:
    PriceTable m_priceTable;
};

void TestTokenAggregator::initTestCase()
{
    qputenv("TZ", "Asia/Seoul");
    tzset();
    QVERIFY(m_priceTable.LoadFromJson(ReadSourceFile(QStringLiteral("resources/prices.json"))));
}

void TestTokenAggregator::TestGroupsByProjectAndSortsByCost()
{
    // Output-only records: Opus 5.5 is $20 per 1M output tokens, Sonnet 5.5 is $10.
    LogSnapshot snapshot;
    snapshot.m_vecRecords = {
        MakeRecord("k1", "2026-10-05T01:00:00Z", "s1", "/Users/me/A", "claude-opus-5-5", 100000),
        MakeRecord("k2", "2026-10-05T01:10:00Z", "s1", "/Users/me/A", "claude-sonnet-5-5", 100000),
        MakeRecord("k3", "2026-10-05T01:20:00Z", "s2", "/Users/me/B", "claude-opus-5-5", 300000),
    };
    const Breakdown breakdown = TokenAggregator::Aggregate(snapshot, m_priceTable, MakeQuery(EBreakdownDimension::PROJECT));
    QCOMPARE(breakdown.m_vecRows.size(), 2);
    QCOMPARE(breakdown.m_vecRows[0].m_strLabel, QStringLiteral("B"));
    QCOMPARE(breakdown.m_vecRows[0].m_strDetail, QStringLiteral("/Users/me/B"));
    QVERIFY(qFuzzyCompare(breakdown.m_vecRows[0].m_dCostUsd, 6.0));
    QCOMPARE(breakdown.m_vecRows[1].m_strLabel, QStringLiteral("A"));
    QVERIFY(qFuzzyCompare(breakdown.m_vecRows[1].m_dCostUsd, 3.0));
    QVERIFY(qFuzzyCompare(breakdown.m_dTotalUsd, 9.0));
    QCOMPARE(breakdown.m_iOtherCount, 0);
}

void TestTokenAggregator::TestDisambiguatesSameProjectNames()
{
    LogSnapshot snapshot;
    snapshot.m_vecRecords = {
        MakeRecord("k1", "2026-10-05T01:00:00Z", "s1", "/Users/me/x/App", "claude-opus-5-5", 200000),
        MakeRecord("k2", "2026-10-05T01:00:00Z", "s2", "/Users/me/y/App", "claude-opus-5-5", 100000),
    };
    const Breakdown breakdown = TokenAggregator::Aggregate(snapshot, m_priceTable, MakeQuery(EBreakdownDimension::PROJECT));
    QCOMPARE(breakdown.m_vecRows[0].m_strLabel, QStringLiteral("x/App"));
    QCOMPARE(breakdown.m_vecRows[1].m_strLabel, QStringLiteral("y/App"));
}

void TestTokenAggregator::TestGroupsByModelWithDisplayNames()
{
    LogSnapshot snapshot;
    snapshot.m_vecRecords = {
        MakeRecord("k1", "2026-10-05T01:00:00Z", "s1", "/Users/me/A", "claude-opus-5-5", 100000),
        MakeRecord("k2", "2026-10-05T01:00:00Z", "s1", "/Users/me/A", "claude-sonnet-5-5", 100000),
    };
    const Breakdown breakdown = TokenAggregator::Aggregate(snapshot, m_priceTable, MakeQuery(EBreakdownDimension::MODEL));
    QCOMPARE(breakdown.m_vecRows[0].m_strLabel, QStringLiteral("Opus 5.5"));
    QCOMPARE(breakdown.m_vecRows[0].m_strDetail, QStringLiteral("claude-opus-5-5"));
    QCOMPARE(breakdown.m_vecRows[1].m_strLabel, QStringLiteral("Sonnet 5.5"));
}

void TestTokenAggregator::TestSessionUsesTitleOrFallback()
{
    LogSnapshot snapshot;
    snapshot.m_vecRecords = {
        MakeRecord("k1", "2026-10-05T05:27:00Z", "s1", "/Users/me/A", "claude-opus-5-5", 100000),
        MakeRecord("k2", "2026-10-05T05:27:00Z", "s2", "/Users/me/A", "claude-opus-5-5", 200000),
    };
    snapshot.m_hashSessionTitles.insert(QStringLiteral("s1"), QStringLiteral("논문 정리"));
    const Breakdown breakdown = TokenAggregator::Aggregate(snapshot, m_priceTable, MakeQuery(EBreakdownDimension::SESSION));
    // 05:27 UTC is 14:27 in Seoul.
    QCOMPARE(breakdown.m_vecRows[0].m_strLabel, QStringLiteral("A · 10/5 14:27"));
    QCOMPARE(breakdown.m_vecRows[0].m_strDetail, QStringLiteral("s2"));
    QCOMPARE(breakdown.m_vecRows[1].m_strLabel, QStringLiteral("논문 정리"));
}

void TestTokenAggregator::TestFiltersByPeriodStart()
{
    LogSnapshot snapshot;
    snapshot.m_vecRecords = {
        MakeRecord("k1", "2026-10-04T23:59:59Z", "s1", "/Users/me/Old", "claude-opus-5-5", 100000),
        MakeRecord("k2", "2026-10-05T00:00:00Z", "s1", "/Users/me/New", "claude-opus-5-5", 100000),
    };
    AggregateQuery query = MakeQuery(EBreakdownDimension::PROJECT);
    query.m_dtFrom = QDateTime(QDate(2026, 10, 5), QTime(0, 0), Qt::UTC);
    const Breakdown breakdown = TokenAggregator::Aggregate(snapshot, m_priceTable, query);
    QCOMPARE(breakdown.m_vecRows.size(), 1);
    QCOMPARE(breakdown.m_vecRows[0].m_strLabel, QStringLiteral("New"));
}

void TestTokenAggregator::TestFoldsBeyondTopCountIntoOther()
{
    LogSnapshot snapshot;
    for (int iIndex = 1; iIndex <= 7; ++iIndex)
    {
        snapshot.m_vecRecords.append(MakeRecord(QStringLiteral("k%1").arg(iIndex), "2026-10-05T01:00:00Z", "s1",
            QStringLiteral("/Users/me/P%1").arg(iIndex), "claude-opus-5-5", (8 - iIndex) * 100000));
    }
    const Breakdown breakdown = TokenAggregator::Aggregate(snapshot, m_priceTable, MakeQuery(EBreakdownDimension::PROJECT));
    QCOMPARE(breakdown.m_vecRows.size(), 5);
    QCOMPARE(breakdown.m_vecRows[0].m_strLabel, QStringLiteral("P1"));
    QCOMPARE(breakdown.m_iOtherCount, 2);
    QCOMPARE(breakdown.m_otherRow.m_strLabel, QStringLiteral("기타 2개"));
    // P6 ($4) + P7 ($2)
    QVERIFY(qFuzzyCompare(breakdown.m_otherRow.m_dCostUsd, 6.0));
    QVERIFY(qFuzzyCompare(breakdown.m_dTotalUsd, 56.0));
}

void TestTokenAggregator::TestMarksApproximateAndUnpriced()
{
    LogSnapshot snapshot;
    snapshot.m_vecRecords = {
        MakeRecord("k1", "2026-10-05T01:00:00Z", "s1", "/Users/me/A", "claude-opus-6", 300000),
        MakeRecord("k2", "2026-10-05T01:00:00Z", "s1", "/Users/me/B", "gpt-x", 100000),
        MakeRecord("k3", "2026-10-05T01:00:00Z", "s1", "/Users/me/C", "claude-opus-5-5", 200000),
        MakeRecord("k4", "2026-10-05T01:00:00Z", "s1", "/Users/me/C", "gpt-x", 100000),
    };
    const Breakdown breakdown = TokenAggregator::Aggregate(snapshot, m_priceTable, MakeQuery(EBreakdownDimension::PROJECT));
    QCOMPARE(breakdown.m_vecRows.size(), 3);
    QCOMPARE(breakdown.m_vecRows[0].m_strLabel, QStringLiteral("A"));
    QVERIFY(breakdown.m_vecRows[0].m_bApproximate);
    QVERIFY(!breakdown.m_vecRows[0].m_bUnpriced);
    QCOMPARE(breakdown.m_vecRows[1].m_strLabel, QStringLiteral("C"));
    QVERIFY(breakdown.m_vecRows[1].m_bApproximate);
    QVERIFY(!breakdown.m_vecRows[1].m_bUnpriced);
    QCOMPARE(breakdown.m_vecRows[2].m_strLabel, QStringLiteral("B"));
    QVERIFY(breakdown.m_vecRows[2].m_bUnpriced);
}

void TestTokenAggregator::TestCalculatesFiveHourWindowStart()
{
    const QDateTime dtNow(QDate(2026, 10, 5), QTime(5, 0), Qt::UTC);
    const QDateTime dtResetsAt(QDate(2026, 10, 5), QTime(7, 40), Qt::UTC);
    QCOMPARE(TokenAggregator::CalculatePeriodStart(EBreakdownPeriod::FIVE_HOUR_WINDOW, dtNow, dtResetsAt),
        QDateTime(QDate(2026, 10, 5), QTime(2, 40), Qt::UTC));
    QCOMPARE(TokenAggregator::CalculatePeriodStart(EBreakdownPeriod::FIVE_HOUR_WINDOW, dtNow, QDateTime()),
        QDateTime(QDate(2026, 10, 5), QTime(0, 0), Qt::UTC));
    QCOMPARE(TokenAggregator::CalculatePeriodStart(EBreakdownPeriod::SEVEN_DAYS, dtNow, QDateTime()),
        QDateTime(QDate(2026, 9, 28), QTime(5, 0), Qt::UTC));
}

void TestTokenAggregator::TestCalculatesTodayFromLocalMidnight()
{
    // 00:30 UTC is 09:30 on 10/5 in Seoul, so "today" starts at 10/5 00:00 KST = 10/4 15:00 UTC.
    QCOMPARE(TokenAggregator::CalculatePeriodStart(EBreakdownPeriod::TODAY, QDateTime(QDate(2026, 10, 5), QTime(0, 30), Qt::UTC), QDateTime()),
        QDateTime(QDate(2026, 10, 4), QTime(15, 0), Qt::UTC));
    // 14:59 UTC is 23:59 on 10/4 in Seoul.
    QCOMPARE(TokenAggregator::CalculatePeriodStart(EBreakdownPeriod::TODAY, QDateTime(QDate(2026, 10, 4), QTime(14, 59), Qt::UTC), QDateTime()),
        QDateTime(QDate(2026, 10, 3), QTime(15, 0), Qt::UTC));
}

void TestTokenAggregator::TestFormatsModelNames()
{
    QCOMPARE(TokenAggregator::FormatModelName(QStringLiteral("claude-opus-5-5")), QStringLiteral("Opus 5.5"));
    QCOMPARE(TokenAggregator::FormatModelName(QStringLiteral("claude-haiku-4-5-20251001")), QStringLiteral("Haiku 4.5"));
    QCOMPARE(TokenAggregator::FormatModelName(QStringLiteral("claude-sonnet-5")), QStringLiteral("Sonnet 5"));
    QCOMPARE(TokenAggregator::FormatModelName(QStringLiteral("claude-fable-5-1")), QStringLiteral("Fable 5.1"));
    QCOMPARE(TokenAggregator::FormatModelName(QStringLiteral("gpt-x")), QStringLiteral("gpt-x"));
}

QTEST_GUILESS_MAIN(TestTokenAggregator)
#include "TestTokenAggregator.moc"
```

`tests/CMakeLists.txt` 끝에 `tv_add_test(TestTokenAggregator)` 추가.

- [ ] **Step 2: 빌드해서 실패 확인**

Run: `cmake --build build`
Expected: FAIL — `Analysis/TokenAggregator.h` file not found

- [ ] **Step 3: 구현**

`src/Model/Breakdown.h`:

```cpp
#pragma once

#include <QString>
#include <QVector>

enum class EBreakdownDimension
{
    PROJECT = 0,
    MODEL = 1,
    SESSION = 2,
};

enum class EBreakdownPeriod
{
    FIVE_HOUR_WINDOW = 0,
    TODAY = 1,
    SEVEN_DAYS = 2,
    THIRTY_DAYS = 3,
};

struct BreakdownRow
{
    QString m_strLabel;
    QString m_strDetail;
    double m_dCostUsd = 0.0;
    bool m_bApproximate = false;
    bool m_bUnpriced = false;
    qint64 m_llInput = 0;
    qint64 m_llOutput = 0;
    qint64 m_llCacheWrite = 0;
    qint64 m_llCacheRead = 0;
};

struct Breakdown
{
    QVector<BreakdownRow> m_vecRows;
    int m_iOtherCount = 0;
    BreakdownRow m_otherRow;
    double m_dTotalUsd = 0.0;
};
```

`src/Analysis/TokenAggregator.h`:

```cpp
#pragma once

#include "Analysis/PriceTable.h"
#include "Model/Breakdown.h"
#include "Model/TokenRecord.h"

#include <QDateTime>
#include <QHash>
#include <QString>
#include <QVector>

struct AggregateQuery
{
    EBreakdownDimension m_eDimension = EBreakdownDimension::PROJECT;
    QDateTime m_dtFrom;
    int m_iTopCount = 5;
};

class TokenAggregator
{
public:
    static Breakdown Aggregate(const LogSnapshot& snapshot, const PriceTable& priceTable, const AggregateQuery& query);
    static QDateTime CalculatePeriodStart(EBreakdownPeriod period, const QDateTime& now, const QDateTime& fiveHourResetsAt);
    static QString FormatModelName(const QString& modelId);

private:
    static QHash<QString, QString> BuildProjectLabels(const QVector<const TokenRecord*>& records);
};
```

`src/Analysis/TokenAggregator.cpp`:

```cpp
#include "Analysis/TokenAggregator.h"

#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>

namespace
{
constexpr qint64 kFiveHourSecs = 5 * 60 * 60;
constexpr int kSevenDays = 7;
constexpr int kThirtyDays = 30;
const QString kUnknownProject = QStringLiteral("(알 수 없음)");
const QString kSessionTimeFormat = QStringLiteral("M/d HH:mm");

struct GroupAccumulator
{
    BreakdownRow m_row;
    int m_iPricedCount = 0;
    int m_iUnpricedCount = 0;
    QDateTime m_dtFirstSeen;
    QString m_strProjectPath;
};

qint64 SumTokens(const BreakdownRow& row)
{
    return row.m_llInput + row.m_llOutput + row.m_llCacheWrite + row.m_llCacheRead;
}

bool IsRankedBefore(const BreakdownRow& lhs, const BreakdownRow& rhs)
{
    if (lhs.m_dCostUsd != rhs.m_dCostUsd)
    {
        return lhs.m_dCostUsd > rhs.m_dCostUsd;
    }
    const qint64 llLhs = SumTokens(lhs);
    const qint64 llRhs = SumTokens(rhs);
    if (llLhs != llRhs)
    {
        return llLhs > llRhs;
    }
    return lhs.m_strLabel < rhs.m_strLabel;
}

void AddRow(BreakdownRow& target, const BreakdownRow& source)
{
    target.m_dCostUsd += source.m_dCostUsd;
    target.m_bApproximate = target.m_bApproximate || source.m_bApproximate || source.m_bUnpriced;
    target.m_llInput += source.m_llInput;
    target.m_llOutput += source.m_llOutput;
    target.m_llCacheWrite += source.m_llCacheWrite;
    target.m_llCacheRead += source.m_llCacheRead;
}

QString LabelOrUnknown(const QString& label)
{
    return label.isEmpty() ? kUnknownProject : label;
}
}

Breakdown TokenAggregator::Aggregate(const LogSnapshot& snapshot, const PriceTable& priceTable, const AggregateQuery& query)
{
    QVector<const TokenRecord*> vecRecords;
    for (const TokenRecord& record : snapshot.m_vecRecords)
    {
        if (!query.m_dtFrom.isValid() || record.m_dtTimestamp >= query.m_dtFrom)
        {
            vecRecords.append(&record);
        }
    }
    const QHash<QString, QString> hashProjectLabels = BuildProjectLabels(vecRecords);

    QHash<QString, GroupAccumulator> hashGroups;
    for (const TokenRecord* pRecord : vecRecords)
    {
        QString strGroupKey;
        switch (query.m_eDimension)
        {
        case EBreakdownDimension::PROJECT:
            strGroupKey = pRecord->m_strProjectPath;
            break;
        case EBreakdownDimension::MODEL:
            strGroupKey = pRecord->m_strModel;
            break;
        case EBreakdownDimension::SESSION:
            strGroupKey = pRecord->m_strSessionId;
            break;
        }

        GroupAccumulator& group = hashGroups[strGroupKey];
        const CostResult cost = priceTable.CalculateCost(*pRecord);
        group.m_row.m_dCostUsd += cost.m_dUsd;
        group.m_row.m_bApproximate = group.m_row.m_bApproximate || cost.m_bApproximate;
        if (cost.m_bUnpriced)
        {
            ++group.m_iUnpricedCount;
        }
        else
        {
            ++group.m_iPricedCount;
        }
        group.m_row.m_llInput += pRecord->m_llInput;
        group.m_row.m_llOutput += pRecord->m_llOutput;
        group.m_row.m_llCacheWrite += pRecord->m_llCacheWrite5m + pRecord->m_llCacheWrite1h;
        group.m_row.m_llCacheRead += pRecord->m_llCacheRead;
        if (!group.m_dtFirstSeen.isValid() || pRecord->m_dtTimestamp < group.m_dtFirstSeen)
        {
            group.m_dtFirstSeen = pRecord->m_dtTimestamp;
            group.m_strProjectPath = pRecord->m_strProjectPath;
        }
    }

    QVector<BreakdownRow> vecRows;
    for (auto it = hashGroups.begin(); it != hashGroups.end(); ++it)
    {
        GroupAccumulator& group = it.value();
        BreakdownRow& row = group.m_row;
        row.m_strDetail = it.key();
        row.m_bUnpriced = group.m_iPricedCount == 0;
        row.m_bApproximate = row.m_bApproximate || (group.m_iPricedCount > 0 && group.m_iUnpricedCount > 0);
        switch (query.m_eDimension)
        {
        case EBreakdownDimension::PROJECT:
            row.m_strLabel = LabelOrUnknown(hashProjectLabels.value(it.key()));
            break;
        case EBreakdownDimension::MODEL:
            row.m_strLabel = FormatModelName(it.key());
            break;
        case EBreakdownDimension::SESSION:
        {
            const QString strTitle = snapshot.m_hashSessionTitles.value(it.key());
            row.m_strLabel = !strTitle.isEmpty()
                ? strTitle
                : LabelOrUnknown(hashProjectLabels.value(group.m_strProjectPath)) + QStringLiteral(" · ")
                    + group.m_dtFirstSeen.toLocalTime().toString(kSessionTimeFormat);
            break;
        }
        }
        vecRows.append(row);
    }
    std::sort(vecRows.begin(), vecRows.end(), IsRankedBefore);

    Breakdown breakdown;
    for (int iIndex = 0; iIndex < vecRows.size(); ++iIndex)
    {
        breakdown.m_dTotalUsd += vecRows[iIndex].m_dCostUsd;
        if (iIndex < query.m_iTopCount)
        {
            breakdown.m_vecRows.append(vecRows[iIndex]);
        }
        else
        {
            AddRow(breakdown.m_otherRow, vecRows[iIndex]);
            ++breakdown.m_iOtherCount;
        }
    }
    if (breakdown.m_iOtherCount > 0)
    {
        breakdown.m_otherRow.m_strLabel = QStringLiteral("기타 %1개").arg(breakdown.m_iOtherCount);
    }
    return breakdown;
}

QDateTime TokenAggregator::CalculatePeriodStart(EBreakdownPeriod period, const QDateTime& now, const QDateTime& fiveHourResetsAt)
{
    switch (period)
    {
    case EBreakdownPeriod::FIVE_HOUR_WINDOW:
        return fiveHourResetsAt.isValid() ? fiveHourResetsAt.addSecs(-kFiveHourSecs).toUTC() : now.addSecs(-kFiveHourSecs).toUTC();
    case EBreakdownPeriod::TODAY:
        return QDateTime(now.toLocalTime().date(), QTime(0, 0), Qt::LocalTime).toUTC();
    case EBreakdownPeriod::SEVEN_DAYS:
        return now.addDays(-kSevenDays).toUTC();
    case EBreakdownPeriod::THIRTY_DAYS:
        break;
    }
    return now.addDays(-kThirtyDays).toUTC();
}

QString TokenAggregator::FormatModelName(const QString& modelId)
{
    static const QRegularExpression reModel(QStringLiteral("^claude-([a-z]+)((?:-\\d{1,2})+)(?:-\\d{8})?$"));
    const QRegularExpressionMatch match = reModel.match(modelId);
    if (!match.hasMatch())
    {
        return modelId;
    }
    QString strFamily = match.captured(1);
    strFamily[0] = strFamily[0].toUpper();
    QString strVersion = match.captured(2).mid(1);
    strVersion.replace(QLatin1Char('-'), QLatin1Char('.'));
    return strFamily + QLatin1Char(' ') + strVersion;
}

QHash<QString, QString> TokenAggregator::BuildProjectLabels(const QVector<const TokenRecord*>& records)
{
    QHash<QString, QSet<QString>> hashPathsByName;
    for (const TokenRecord* pRecord : records)
    {
        hashPathsByName[QFileInfo(pRecord->m_strProjectPath).fileName()].insert(pRecord->m_strProjectPath);
    }

    QHash<QString, QString> hashLabels;
    for (auto it = hashPathsByName.cbegin(); it != hashPathsByName.cend(); ++it)
    {
        for (const QString& strPath : it.value())
        {
            if (it.value().size() == 1)
            {
                hashLabels.insert(strPath, it.key());
                continue;
            }
            // Two projects share a folder name: prefix the parent folder to tell them apart.
            const QFileInfo fiPath(strPath);
            hashLabels.insert(strPath, QFileInfo(fiPath.path()).fileName() + QLatin1Char('/') + fiPath.fileName());
        }
    }
    return hashLabels;
}
```

`CMakeLists.txt` 의 `set(TV_CORE_SOURCES ...)` 안에 `src/Analysis/TokenAggregator.cpp` 추가.

- [ ] **Step 4: 테스트 통과 확인**

Run: `cmake --build build && ctest --test-dir build --output-on-failure -R TestTokenAggregator`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt tests/CMakeLists.txt tests/TestTokenAggregator.cpp src/Model/Breakdown.h src/Analysis/TokenAggregator.h src/Analysis/TokenAggregator.cpp
git commit -m "feat: aggregate token cost by project, model and session" -m "Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>"
```

---

### Task 8: LogStore + LogScanThread

**Files:**
- Create: `src/Analysis/LogStore.h`, `src/Analysis/LogStore.cpp`, `src/Thread/LogScanThread.h`, `src/Thread/LogScanThread.cpp`
- Modify: `CMakeLists.txt`, `tests/CMakeLists.txt`, `tests/TestHelpers.h`
- Test: `tests/TestLogStore.cpp`, `tests/TestLogScanThread.cpp`

**Interfaces:**
- Consumes: Task 6 `LogParser::ParseLine`, `LogParser::FindSessionIdFromPath`, `ELineKind`; Task 5 `LogSnapshot`
- Produces:
  - `LogStore::ScanDirectory(const QString& rootPath, const QDateTime& now, const std::function<bool()>& isAlive = {}) -> bool` (바뀐 게 있으면 true), `CreateSnapshot() const -> LogSnapshot`, `GetRecordCount() const -> int`
  - `class LogScanThread : public QObject` — `LogScanThread(const QString& rootPath, int scanIntervalMs)`, `void Start()`, `void Stop()`, `void RequestRescan()`, signals `void SnapshotReady(const LogSnapshot& snapshot)`, `void Finished(bool success)`
  - `tests/TestHelpers.h`: `bool AppendText(const QString& filePath, const QByteArray& text)`, `QByteArray MakeUsageLine(const QString& messageId, const QString& sessionId, const QString& timestamp)`

- [ ] **Step 1: 테스트 공용 함수 추가**

`tests/TestHelpers.h` 끝에 추가:

```cpp
inline bool AppendText(const QString& filePath, const QByteArray& text)
{
    QDir().mkpath(QFileInfo(filePath).path());
    QFile file(filePath);
    if (!file.open(QIODevice::Append))
    {
        return false;
    }
    return file.write(text) == text.size();
}

inline QByteArray MakeUsageLine(const QString& messageId, const QString& sessionId, const QString& timestamp)
{
    const QString strLine = QStringLiteral(R"({"type":"assistant","sessionId":"%1","cwd":"/Users/me/Proj","timestamp":"%2","requestId":"req_%3","message":{"id":"%3","model":"claude-opus-5-5","usage":{"input_tokens":1,"output_tokens":10,"cache_creation_input_tokens":0,"cache_read_input_tokens":0}}})");
    return strLine.arg(sessionId, timestamp, messageId).toUtf8() + '\n';
}
```

- [ ] **Step 2: 실패하는 LogStore 테스트**

`tests/TestLogStore.cpp`:

```cpp
#include "Analysis/LogStore.h"

#include "TestHelpers.h"

#include <QTemporaryDir>
#include <QtTest>

namespace
{
const QDateTime kNow(QDate(2026, 10, 5), QTime(12, 0), Qt::UTC);
const QByteArray kTitleLine = QByteArrayLiteral(R"({"type":"ai-title","aiTitle":"Title A","sessionId":"sessA"})") + '\n';

bool SetModifiedTime(const QString& filePath, const QDateTime& time)
{
    QFile file(filePath);
    return file.open(QIODevice::ReadWrite) && file.setFileTime(time, QFileDevice::FileModificationTime);
}
}

class TestLogStore : public QObject
{
    Q_OBJECT

private slots:
    void TestReadsMainAndSubagentFiles();
    void TestAppendsOnlyNewLines();
    void TestWaitsForPartialLine();
    void TestDropsDeletedFile();
    void TestRereadsTruncatedFile();
    void TestSkipsFilesOlderThanRetention();
    void TestPrunesOldRecords();
    void TestMissingRootIsEmpty();
    void TestStopsWhenNotAlive();
};

void TestLogStore::TestReadsMainAndSubagentFiles()
{
    QTemporaryDir dirRoot;
    const QString strMain = dirRoot.filePath(QStringLiteral("proj/sessA.jsonl"));
    const QString strAgent = dirRoot.filePath(QStringLiteral("proj/sessA/subagents/agent-1.jsonl"));
    const QByteArray baFirst = MakeUsageLine("msg_1", "sessA", "2026-10-05T10:00:00.000Z");
    // The same response is written twice, as streaming does.
    QVERIFY(AppendText(strMain, baFirst + baFirst + MakeUsageLine("msg_2", "sessA", "2026-10-05T10:01:00.000Z") + kTitleLine));
    QVERIFY(AppendText(strAgent, MakeUsageLine("msg_3", "sessA", "2026-10-05T10:02:00.000Z")));

    LogStore store;
    QVERIFY(store.ScanDirectory(dirRoot.path(), kNow));
    QCOMPARE(store.GetRecordCount(), 3);
    QCOMPARE(store.CreateSnapshot().m_hashSessionTitles.value(QStringLiteral("sessA")), QStringLiteral("Title A"));
    QVERIFY(!store.ScanDirectory(dirRoot.path(), kNow));
}

void TestLogStore::TestAppendsOnlyNewLines()
{
    QTemporaryDir dirRoot;
    const QString strMain = dirRoot.filePath(QStringLiteral("proj/sessA.jsonl"));
    QVERIFY(AppendText(strMain, MakeUsageLine("msg_1", "sessA", "2026-10-05T10:00:00.000Z")));
    LogStore store;
    QVERIFY(store.ScanDirectory(dirRoot.path(), kNow));

    QVERIFY(AppendText(strMain, MakeUsageLine("msg_4", "sessA", "2026-10-05T10:04:00.000Z")));
    QVERIFY(store.ScanDirectory(dirRoot.path(), kNow));
    QCOMPARE(store.GetRecordCount(), 2);
}

void TestLogStore::TestWaitsForPartialLine()
{
    QTemporaryDir dirRoot;
    const QString strMain = dirRoot.filePath(QStringLiteral("proj/sessA.jsonl"));
    QVERIFY(AppendText(strMain, MakeUsageLine("msg_1", "sessA", "2026-10-05T10:00:00.000Z")));
    LogStore store;
    QVERIFY(store.ScanDirectory(dirRoot.path(), kNow));

    const QByteArray baLine = MakeUsageLine("msg_5", "sessA", "2026-10-05T10:05:00.000Z");
    QVERIFY(AppendText(strMain, baLine.left(40)));
    QVERIFY(!store.ScanDirectory(dirRoot.path(), kNow));
    QCOMPARE(store.GetRecordCount(), 1);

    QVERIFY(AppendText(strMain, baLine.mid(40)));
    QVERIFY(store.ScanDirectory(dirRoot.path(), kNow));
    QCOMPARE(store.GetRecordCount(), 2);
}

void TestLogStore::TestDropsDeletedFile()
{
    QTemporaryDir dirRoot;
    const QString strMain = dirRoot.filePath(QStringLiteral("proj/sessA.jsonl"));
    QVERIFY(AppendText(strMain, MakeUsageLine("msg_1", "sessA", "2026-10-05T10:00:00.000Z")));
    LogStore store;
    QVERIFY(store.ScanDirectory(dirRoot.path(), kNow));

    QVERIFY(QFile::remove(strMain));
    QVERIFY(store.ScanDirectory(dirRoot.path(), kNow));
    QCOMPARE(store.GetRecordCount(), 0);
}

void TestLogStore::TestRereadsTruncatedFile()
{
    QTemporaryDir dirRoot;
    const QString strMain = dirRoot.filePath(QStringLiteral("proj/sessA.jsonl"));
    QVERIFY(AppendText(strMain, MakeUsageLine("msg_1", "sessA", "2026-10-05T10:00:00.000Z")
        + MakeUsageLine("msg_2", "sessA", "2026-10-05T10:01:00.000Z")));
    LogStore store;
    QVERIFY(store.ScanDirectory(dirRoot.path(), kNow));

    QFile file(strMain);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(MakeUsageLine("msg_9", "sessA", "2026-10-05T10:09:00.000Z"));
    file.close();

    QVERIFY(store.ScanDirectory(dirRoot.path(), kNow));
    const LogSnapshot snapshot = store.CreateSnapshot();
    QCOMPARE(snapshot.m_vecRecords.size(), 1);
    QCOMPARE(snapshot.m_vecRecords.first().m_strKey, QStringLiteral("msg_9|req_msg_9"));
}

void TestLogStore::TestSkipsFilesOlderThanRetention()
{
    QTemporaryDir dirRoot;
    const QString strMain = dirRoot.filePath(QStringLiteral("proj/old.jsonl"));
    QVERIFY(AppendText(strMain, MakeUsageLine("msg_1", "old", "2026-08-20T10:00:00.000Z")));
    QVERIFY(SetModifiedTime(strMain, QDateTime(QDate(2026, 8, 20), QTime(10, 0), Qt::UTC)));

    LogStore store;
    QVERIFY(!store.ScanDirectory(dirRoot.path(), kNow));
    QCOMPARE(store.GetRecordCount(), 0);
}

void TestLogStore::TestPrunesOldRecords()
{
    QTemporaryDir dirRoot;
    QVERIFY(AppendText(dirRoot.filePath(QStringLiteral("proj/sessA.jsonl")), MakeUsageLine("msg_1", "sessA", "2026-08-20T10:00:00.000Z")));
    LogStore store;
    store.ScanDirectory(dirRoot.path(), kNow);
    QCOMPARE(store.GetRecordCount(), 0);
}

void TestLogStore::TestMissingRootIsEmpty()
{
    LogStore store;
    QVERIFY(!store.ScanDirectory(QStringLiteral("/nonexistent/tokenviewer/projects"), kNow));
    QCOMPARE(store.GetRecordCount(), 0);
}

void TestLogStore::TestStopsWhenNotAlive()
{
    QTemporaryDir dirRoot;
    QVERIFY(AppendText(dirRoot.filePath(QStringLiteral("proj/sessA.jsonl")), MakeUsageLine("msg_1", "sessA", "2026-10-05T10:00:00.000Z")));
    LogStore store;
    store.ScanDirectory(dirRoot.path(), kNow, []() { return false; });
    QCOMPARE(store.GetRecordCount(), 0);
}

QTEST_GUILESS_MAIN(TestLogStore)
#include "TestLogStore.moc"
```

`tests/CMakeLists.txt` 끝에 `tv_add_test(TestLogStore)` 추가.

- [ ] **Step 3: 빌드해서 실패 확인**

Run: `cmake --build build`
Expected: FAIL — `Analysis/LogStore.h` file not found

- [ ] **Step 4: LogStore 구현**

`src/Analysis/LogStore.h`:

```cpp
#pragma once

#include "Model/TokenRecord.h"

#include <QDateTime>
#include <QHash>
#include <QSet>
#include <QString>

#include <functional>

class LogStore
{
public:
    static constexpr qint64 kChunkBytes = 4 * 1024 * 1024;
    static constexpr int kRetentionDays = 31;

public:
    LogStore();
    ~LogStore();

public:
    bool ScanDirectory(const QString& rootPath, const QDateTime& now, const std::function<bool()>& isAlive = {});
    LogSnapshot CreateSnapshot() const;

public:
    int GetRecordCount() const;

private:
    struct FileCursor
    {
        qint64 m_llOffset = 0;
        QSet<QString> m_setKeys;
    };

private:
    bool ReadFile(const QString& filePath, FileCursor& cursor);
    void ForgetFile(const QString& filePath);
    bool PruneOldRecords(const QDateTime& now);

private:
    QHash<QString, FileCursor> m_hashCursors;
    QHash<QString, TokenRecord> m_hashRecords;
    QHash<QString, QString> m_hashSessionTitles;
};
```

`src/Analysis/LogStore.cpp`:

```cpp
#include "Analysis/LogStore.h"

#include "Analysis/LogParser.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QStringList>

namespace
{
const QString kLogFilePattern = QStringLiteral("*.jsonl");
}

LogStore::LogStore()
    : m_hashCursors()
    , m_hashRecords()
    , m_hashSessionTitles()
{
}

LogStore::~LogStore() = default;

bool LogStore::ScanDirectory(const QString& rootPath, const QDateTime& now, const std::function<bool()>& isAlive)
{
    bool bChanged = false;
    const QDateTime dtCutoff = now.addDays(-kRetentionDays);
    QSet<QString> setSeen;

    QDirIterator itFile(rootPath, QStringList{ kLogFilePattern }, QDir::Files, QDirIterator::Subdirectories);
    while (itFile.hasNext())
    {
        if (isAlive && !isAlive())
        {
            return bChanged;
        }
        const QString strPath = itFile.next();
        const QFileInfo fiFile = itFile.fileInfo();
        setSeen.insert(strPath);

        auto itCursor = m_hashCursors.find(strPath);
        if (itCursor == m_hashCursors.end())
        {
            // Claude Code deletes transcripts after 30 days; skip anything older on first sight.
            if (fiFile.lastModified() < dtCutoff)
            {
                continue;
            }
            itCursor = m_hashCursors.insert(strPath, FileCursor());
        }

        FileCursor& cursor = itCursor.value();
        if (fiFile.size() < cursor.m_llOffset)
        {
            // Truncated or replaced: drop what this file contributed and read it again.
            for (const QString& strKey : cursor.m_setKeys)
            {
                m_hashRecords.remove(strKey);
            }
            cursor = FileCursor();
            bChanged = true;
        }
        if (fiFile.size() > cursor.m_llOffset)
        {
            bChanged = ReadFile(strPath, cursor) || bChanged;
        }
    }

    const QStringList lstTracked = m_hashCursors.keys();
    for (const QString& strPath : lstTracked)
    {
        if (!setSeen.contains(strPath))
        {
            ForgetFile(strPath);
            bChanged = true;
        }
    }

    return PruneOldRecords(now) || bChanged;
}

LogSnapshot LogStore::CreateSnapshot() const
{
    LogSnapshot snapshot;
    snapshot.m_vecRecords.reserve(m_hashRecords.size());
    for (const TokenRecord& record : m_hashRecords)
    {
        snapshot.m_vecRecords.append(record);
    }
    snapshot.m_hashSessionTitles = m_hashSessionTitles;
    return snapshot;
}

int LogStore::GetRecordCount() const
{
    return m_hashRecords.size();
}

bool LogStore::ReadFile(const QString& filePath, FileCursor& cursor)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly) || !file.seek(cursor.m_llOffset))
    {
        return false;
    }

    const QString strFallbackSessionId = LogParser::FindSessionIdFromPath(filePath);
    bool bChanged = false;
    QByteArray baPending;
    while (true)
    {
        const QByteArray baChunk = file.read(kChunkBytes);
        if (baChunk.isEmpty())
        {
            break;
        }
        baPending += baChunk;

        int iLineStart = 0;
        int iNewline = baPending.indexOf('\n');
        while (iNewline >= 0)
        {
            const ParsedLine parsed = LogParser::ParseLine(baPending.mid(iLineStart, iNewline - iLineStart), strFallbackSessionId);
            if (parsed.m_eKind == ELineKind::USAGE)
            {
                // Later copies of the same response replace earlier ones.
                m_hashRecords.insert(parsed.m_record.m_strKey, parsed.m_record);
                cursor.m_setKeys.insert(parsed.m_record.m_strKey);
                bChanged = true;
            }
            else if (parsed.m_eKind == ELineKind::TITLE)
            {
                m_hashSessionTitles.insert(parsed.m_strSessionId, parsed.m_strTitle);
                bChanged = true;
            }
            cursor.m_llOffset += (iNewline - iLineStart) + 1;
            iLineStart = iNewline + 1;
            iNewline = baPending.indexOf('\n', iLineStart);
        }
        // Keep the unfinished last line; the offset stays before it until Claude Code finishes writing it.
        baPending = baPending.mid(iLineStart);
    }
    return bChanged;
}

void LogStore::ForgetFile(const QString& filePath)
{
    const FileCursor cursor = m_hashCursors.take(filePath);
    for (const QString& strKey : cursor.m_setKeys)
    {
        m_hashRecords.remove(strKey);
    }
}

bool LogStore::PruneOldRecords(const QDateTime& now)
{
    const QDateTime dtCutoff = now.addDays(-kRetentionDays);
    bool bChanged = false;
    for (auto it = m_hashRecords.begin(); it != m_hashRecords.end();)
    {
        if (it.value().m_dtTimestamp < dtCutoff)
        {
            it = m_hashRecords.erase(it);
            bChanged = true;
        }
        else
        {
            ++it;
        }
    }
    return bChanged;
}
```

`CMakeLists.txt` 의 `set(TV_CORE_SOURCES ...)` 안에 `src/Analysis/LogStore.cpp` 추가.

- [ ] **Step 5: LogStore 테스트 통과 확인**

Run: `cmake --build build && ctest --test-dir build --output-on-failure -R TestLogStore`
Expected: PASS

- [ ] **Step 6: 실패하는 LogScanThread 테스트**

`tests/TestLogScanThread.cpp`:

```cpp
#include "Thread/LogScanThread.h"

#include "TestHelpers.h"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

namespace
{
constexpr int kLongIntervalMs = 60000;
constexpr int kSignalWaitMs = 5000;

QString CurrentTimestamp()
{
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
}
}

class TestLogScanThread : public QObject
{
    Q_OBJECT

private slots:
    void TestEmitsSnapshotAndRescansOnRequest();
};

void TestLogScanThread::TestEmitsSnapshotAndRescansOnRequest()
{
    QTemporaryDir dirRoot;
    const QString strFile = dirRoot.filePath(QStringLiteral("proj/sessA.jsonl"));
    QVERIFY(AppendText(strFile, MakeUsageLine("msg_1", "sessA", CurrentTimestamp())));

    LogScanThread thread(dirRoot.path(), kLongIntervalMs);
    QSignalSpy spy(&thread, &LogScanThread::SnapshotReady);
    thread.Start();
    QVERIFY(spy.wait(kSignalWaitMs));
    QCOMPARE(spy.last().at(0).value<LogSnapshot>().m_vecRecords.size(), 1);

    QVERIFY(AppendText(strFile, MakeUsageLine("msg_2", "sessA", CurrentTimestamp())));
    thread.RequestRescan();
    QVERIFY(spy.wait(kSignalWaitMs));
    QCOMPARE(spy.last().at(0).value<LogSnapshot>().m_vecRecords.size(), 2);

    thread.Stop();
}

QTEST_GUILESS_MAIN(TestLogScanThread)
#include "TestLogScanThread.moc"
```

`tests/CMakeLists.txt` 끝에 `tv_add_test(TestLogScanThread)` 추가.

Run: `cmake --build build`
Expected: FAIL — `Thread/LogScanThread.h` file not found

- [ ] **Step 7: LogScanThread 구현**

`src/Thread/LogScanThread.h`:

```cpp
#pragma once

#include "Model/TokenRecord.h"

#include <QObject>
#include <QString>

#include <memory>

class LogScanThreadWorker : public QObject
{
    Q_OBJECT

public:
    explicit LogScanThreadWorker(QObject* parent = nullptr);
    ~LogScanThreadWorker();

public:
    void SetContext(const QString& rootPath, int scanIntervalMs);
    void Stop();
    void RequestRescan();

public slots:
    void DoWork();

signals:
    void SnapshotReady(const LogSnapshot& snapshot);
    void WorkFinished(bool success);

private:
    void Run();
    bool IsThreadAlive() const;

private:
    class LogScanThreadWorkerImpl;
    std::unique_ptr<LogScanThreadWorkerImpl> m_pimpl;
};

class LogScanThread : public QObject
{
    Q_OBJECT

public:
    LogScanThread(const QString& rootPath, int scanIntervalMs);
    ~LogScanThread();

public:
    void Start();
    void Stop();
    void RequestRescan();

signals:
    void SnapshotReady(const LogSnapshot& snapshot);
    void Finished(bool success);

private:
    class LogScanThreadImpl;
    std::unique_ptr<LogScanThreadImpl> m_pimpl;
};
```

`src/Thread/LogScanThread.cpp`:

```cpp
#include "Thread/LogScanThread.h"

#include "Analysis/LogStore.h"

#include <QDateTime>
#include <QMetaObject>
#include <QThread>

#include <atomic>
#include <functional>

namespace
{
constexpr int kStopWaitMs = 4000;
constexpr int kTickMs = 100;
}

////////////////////////////////////////////////////////////////////////////////
// Worker
////////////////////////////////////////////////////////////////////////////////
class LogScanThreadWorker::LogScanThreadWorkerImpl
{
public:
    std::atomic<bool> m_bDoRunThread{ false };
    std::atomic<bool> m_bRescanRequested{ false };
    QString m_strRootPath;
    int m_iScanIntervalMs = 0;
};

LogScanThreadWorker::LogScanThreadWorker(QObject* parent)
    : QObject(parent)
    , m_pimpl(std::make_unique<LogScanThreadWorkerImpl>())
{
}

LogScanThreadWorker::~LogScanThreadWorker() = default;

void LogScanThreadWorker::SetContext(const QString& rootPath, int scanIntervalMs)
{
    m_pimpl->m_strRootPath = rootPath;
    m_pimpl->m_iScanIntervalMs = scanIntervalMs;
}

void LogScanThreadWorker::Stop()
{
    m_pimpl->m_bDoRunThread = false;
}

void LogScanThreadWorker::RequestRescan()
{
    m_pimpl->m_bRescanRequested = true;
}

void LogScanThreadWorker::Run()
{
    m_pimpl->m_bDoRunThread = true;
}

bool LogScanThreadWorker::IsThreadAlive() const
{
    return m_pimpl->m_bDoRunThread;
}

void LogScanThreadWorker::DoWork()
{
    Run();
    LogStore store;
    const std::function<bool()> funcIsAlive = [this]() { return IsThreadAlive(); };
    while (IsThreadAlive())
    {
        m_pimpl->m_bRescanRequested = false;
        if (store.ScanDirectory(m_pimpl->m_strRootPath, QDateTime::currentDateTimeUtc(), funcIsAlive))
        {
            emit SnapshotReady(store.CreateSnapshot());
        }
        for (int iWaitedMs = 0; iWaitedMs < m_pimpl->m_iScanIntervalMs && IsThreadAlive() && !m_pimpl->m_bRescanRequested; iWaitedMs += kTickMs)
        {
            QThread::msleep(kTickMs);
        }
    }
    emit WorkFinished(true);
}

////////////////////////////////////////////////////////////////////////////////
// Thread
////////////////////////////////////////////////////////////////////////////////
class LogScanThread::LogScanThreadImpl
{
public:
    QThread m_workerThread;
    std::unique_ptr<LogScanThreadWorker> m_upWorker;
    QString m_strRootPath;
    int m_iScanIntervalMs = 0;

    ~LogScanThreadImpl()
    {
        StopWorker();
    }

    void ConnectWorker(LogScanThread* owner)
    {
        QObject::connect(m_upWorker.get(), &LogScanThreadWorker::SnapshotReady, owner, &LogScanThread::SnapshotReady);
        QObject::connect(m_upWorker.get(), &LogScanThreadWorker::WorkFinished, owner, &LogScanThread::Finished);
    }

    void StopWorker()
    {
        if (m_upWorker)
        {
            m_upWorker->Stop();
        }
        if (m_workerThread.isRunning())
        {
            m_workerThread.quit();
            if (!m_workerThread.wait(kStopWaitMs))
            {
                m_workerThread.terminate();
                m_workerThread.wait();
            }
        }
        m_upWorker.reset();
    }
};

LogScanThread::LogScanThread(const QString& rootPath, int scanIntervalMs)
    : QObject(nullptr)
    , m_pimpl(std::make_unique<LogScanThreadImpl>())
{
    qRegisterMetaType<LogSnapshot>("LogSnapshot");
    m_pimpl->m_strRootPath = rootPath;
    m_pimpl->m_iScanIntervalMs = scanIntervalMs;
}

LogScanThread::~LogScanThread() = default;

void LogScanThread::Start()
{
    if (!m_pimpl->m_upWorker)
    {
        m_pimpl->m_upWorker = std::make_unique<LogScanThreadWorker>();
        m_pimpl->m_upWorker->SetContext(m_pimpl->m_strRootPath, m_pimpl->m_iScanIntervalMs);
        m_pimpl->ConnectWorker(this);
        m_pimpl->m_upWorker->moveToThread(&m_pimpl->m_workerThread);
    }
    if (!m_pimpl->m_workerThread.isRunning())
    {
        m_pimpl->m_workerThread.start();
    }
    QMetaObject::invokeMethod(m_pimpl->m_upWorker.get(), "DoWork", Qt::QueuedConnection);
}

void LogScanThread::Stop()
{
    m_pimpl->StopWorker();
}

void LogScanThread::RequestRescan()
{
    if (m_pimpl->m_upWorker)
    {
        m_pimpl->m_upWorker->RequestRescan();
    }
}
```

`CMakeLists.txt` 의 `set(TV_CORE_SOURCES ...)` 안에 `src/Thread/LogScanThread.h` 와 `src/Thread/LogScanThread.cpp` 추가.

- [ ] **Step 8: 테스트 통과 확인**

Run: `cmake --build build && ctest --test-dir build --output-on-failure -R "TestLogStore|TestLogScanThread"`
Expected: PASS

- [ ] **Step 9: Commit**

```bash
git add CMakeLists.txt tests/CMakeLists.txt tests/TestHelpers.h tests/TestLogStore.cpp tests/TestLogScanThread.cpp src/Analysis/LogStore.h src/Analysis/LogStore.cpp src/Thread/LogScanThread.h src/Thread/LogScanThread.cpp
git commit -m "feat: scan session logs incrementally on a worker thread" -m "Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>"
```

---

### Task 9: CredentialStore + UsageApiClient

Task 4 에서 실제 형태가 가정과 달랐다면, 이 Task 의 파서와 테스트 값은 Task 4 Step 6 에서 이미 고쳐 둔 상태여야 한다.

**Files:**
- Create: `src/Model/UsageLimits.h`, `src/Service/CredentialStore.h`, `src/Service/CredentialStore.cpp`, `src/Service/UsageApiClient.h`, `src/Service/UsageApiClient.cpp`
- Modify: `CMakeLists.txt`, `tests/CMakeLists.txt`
- Test: `tests/TestCredentialStore.cpp`, `tests/TestUsageApiClient.cpp`

**Interfaces:**
- Consumes: Task 4 `tests/fixtures/usage_response.json`, Task 5 `ReadSourceFile`
- Produces:
  - `struct LimitWindow { bool m_bValid; double m_dPercent; QDateTime m_dtResetsAt; }`, `struct UsageLimits { LimitWindow m_fiveHour; LimitWindow m_sevenDay; }`
  - `struct OAuthCredential { QString m_strAccessToken; QDateTime m_dtExpiresAt; QString m_strSubscriptionType; QString m_strRateLimitTier; }`
  - `enum class EFetchStatus { OK, NOT_LOGGED_IN, KEYCHAIN_DENIED, TOKEN_EXPIRED, RATE_LIMITED, NETWORK_ERROR, SERVER_ERROR, BAD_RESPONSE }`
  - `struct FetchResult { EFetchStatus m_eStatus; UsageLimits m_limits; QString m_strPlanLabel; QString m_strDetail; int m_iHttpStatus; QByteArray m_baRawBody; }` + `Q_DECLARE_METATYPE(FetchResult)`
  - `enum class ECredentialStatus { OK, NOT_FOUND, ACCESS_DENIED, MALFORMED, EXPIRED }`, `struct CredentialResult { ECredentialStatus m_eStatus; OAuthCredential m_credential; }`
  - `CredentialStore::Load(const QDateTime& now) -> CredentialResult`, `ParseCredentialJson(const QByteArray&, const QDateTime& now) -> CredentialResult`, `FormatPlanLabel(const QString& subscriptionType, const QString& rateLimitTier) -> QString`
  - `UsageApiClient::FetchWithStoredCredential(const QDateTime& now) -> FetchResult` (블로킹, 워커 스레드에서만 호출), `MapHttpResult(int httpStatus, bool networkError, const QString& errorText, const QByteArray& body) -> FetchResult`, `ParseUsageResponse(const QByteArray&) -> FetchResult`

- [ ] **Step 1: 실패하는 CredentialStore 테스트**

`tests/TestCredentialStore.cpp`:

```cpp
#include "Service/CredentialStore.h"

#include <QtTest>

namespace
{
const QDateTime kNow(QDate(2026, 10, 5), QTime(12, 0), Qt::UTC);

QByteArray MakeCredentialJson(const QString& expiresAt)
{
    return QStringLiteral(R"({"claudeAiOauth":{"accessToken":"sk-test","refreshToken":"rt-test","expiresAt":%1,"scopes":["user:inference"],"subscriptionType":"max","rateLimitTier":"default_claude_max_5x"}})")
        .arg(expiresAt).toUtf8();
}
}

class TestCredentialStore : public QObject
{
    Q_OBJECT

private slots:
    void TestParsesTokenAndPlan();
    void TestReportsExpiredToken();
    void TestAcceptsEpochSeconds();
    void TestMissingTokenIsMalformed();
    void TestNotJsonIsMalformed();
    void TestFormatsPlanLabel();
};

void TestCredentialStore::TestParsesTokenAndPlan()
{
    const qint64 llExpiresMs = kNow.addSecs(3600).toMSecsSinceEpoch();
    const CredentialResult result = CredentialStore::ParseCredentialJson(MakeCredentialJson(QString::number(llExpiresMs)), kNow);
    QCOMPARE(result.m_eStatus, ECredentialStatus::OK);
    QCOMPARE(result.m_credential.m_strAccessToken, QStringLiteral("sk-test"));
    QCOMPARE(result.m_credential.m_dtExpiresAt, QDateTime::fromMSecsSinceEpoch(llExpiresMs, Qt::UTC));
    QCOMPARE(result.m_credential.m_strSubscriptionType, QStringLiteral("max"));
    QCOMPARE(result.m_credential.m_strRateLimitTier, QStringLiteral("default_claude_max_5x"));
}

void TestCredentialStore::TestReportsExpiredToken()
{
    const qint64 llExpiresMs = kNow.addSecs(-1).toMSecsSinceEpoch();
    const CredentialResult result = CredentialStore::ParseCredentialJson(MakeCredentialJson(QString::number(llExpiresMs)), kNow);
    QCOMPARE(result.m_eStatus, ECredentialStatus::EXPIRED);
    QCOMPARE(result.m_credential.m_strSubscriptionType, QStringLiteral("max"));
}

void TestCredentialStore::TestAcceptsEpochSeconds()
{
    const qint64 llExpiresSecs = kNow.addSecs(600).toSecsSinceEpoch();
    const CredentialResult result = CredentialStore::ParseCredentialJson(MakeCredentialJson(QString::number(llExpiresSecs)), kNow);
    QCOMPARE(result.m_eStatus, ECredentialStatus::OK);
    QCOMPARE(result.m_credential.m_dtExpiresAt, QDateTime::fromSecsSinceEpoch(llExpiresSecs, Qt::UTC));
}

void TestCredentialStore::TestMissingTokenIsMalformed()
{
    const CredentialResult result = CredentialStore::ParseCredentialJson(QByteArrayLiteral(R"({"claudeAiOauth":{"subscriptionType":"max"}})"), kNow);
    QCOMPARE(result.m_eStatus, ECredentialStatus::MALFORMED);
}

void TestCredentialStore::TestNotJsonIsMalformed()
{
    QCOMPARE(CredentialStore::ParseCredentialJson(QByteArrayLiteral("not json"), kNow).m_eStatus, ECredentialStatus::MALFORMED);
}

void TestCredentialStore::TestFormatsPlanLabel()
{
    QCOMPARE(CredentialStore::FormatPlanLabel(QStringLiteral("max"), QStringLiteral("default_claude_max_5x")), QStringLiteral("Max 5x"));
    QCOMPARE(CredentialStore::FormatPlanLabel(QStringLiteral("max"), QStringLiteral("default_claude_max_20x")), QStringLiteral("Max 20x"));
    QCOMPARE(CredentialStore::FormatPlanLabel(QStringLiteral("max"), QString()), QStringLiteral("Max"));
    QCOMPARE(CredentialStore::FormatPlanLabel(QStringLiteral("pro"), QString()), QStringLiteral("Pro"));
    QCOMPARE(CredentialStore::FormatPlanLabel(QString(), QString()), QString());
}

QTEST_GUILESS_MAIN(TestCredentialStore)
#include "TestCredentialStore.moc"
```

- [ ] **Step 2: 실패하는 UsageApiClient 테스트**

`tests/TestUsageApiClient.cpp`:

```cpp
#include "Service/UsageApiClient.h"

#include "TestHelpers.h"

#include <QtTest>

class TestUsageApiClient : public QObject
{
    Q_OBJECT

private slots:
    void TestParsesUtilizationAndIsoReset();
    void TestAcceptsUsedPercentageAndEpoch();
    void TestClampsOutOfRange();
    void TestNullWindowLeavesMeterEmpty();
    void TestMissingWindowsIsBadResponse();
    void TestNotJsonIsBadResponse();
    void TestMapsHttpStatuses();
    void TestParsesRecordedResponseShape();
};

void TestUsageApiClient::TestParsesUtilizationAndIsoReset()
{
    const FetchResult result = UsageApiClient::ParseUsageResponse(QByteArrayLiteral(
        R"({"five_hour":{"utilization":42.0,"resets_at":"2026-10-05T07:40:00.123456+00:00"},"seven_day":{"utilization":18,"resets_at":"2026-10-08T00:00:00Z"}})"));
    QCOMPARE(result.m_eStatus, EFetchStatus::OK);
    QVERIFY(result.m_limits.m_fiveHour.m_bValid);
    QCOMPARE(result.m_limits.m_fiveHour.m_dPercent, 42.0);
    QCOMPARE(result.m_limits.m_fiveHour.m_dtResetsAt, QDateTime(QDate(2026, 10, 5), QTime(7, 40, 0, 123), Qt::UTC));
    QCOMPARE(result.m_limits.m_sevenDay.m_dPercent, 18.0);
    QCOMPARE(result.m_limits.m_sevenDay.m_dtResetsAt, QDateTime(QDate(2026, 10, 8), QTime(0, 0), Qt::UTC));
}

void TestUsageApiClient::TestAcceptsUsedPercentageAndEpoch()
{
    const FetchResult result = UsageApiClient::ParseUsageResponse(QByteArrayLiteral(
        R"({"five_hour":{"used_percentage":55,"resets_at":1791160800},"seven_day":{"used_percentage":20.5,"resets_at":1791500000000}})"));
    QCOMPARE(result.m_eStatus, EFetchStatus::OK);
    QCOMPARE(result.m_limits.m_fiveHour.m_dPercent, 55.0);
    QCOMPARE(result.m_limits.m_fiveHour.m_dtResetsAt, QDateTime::fromSecsSinceEpoch(1791160800, Qt::UTC));
    QCOMPARE(result.m_limits.m_sevenDay.m_dtResetsAt, QDateTime::fromMSecsSinceEpoch(1791500000000, Qt::UTC));
}

void TestUsageApiClient::TestClampsOutOfRange()
{
    const FetchResult result = UsageApiClient::ParseUsageResponse(QByteArrayLiteral(
        R"({"five_hour":{"utilization":130},"seven_day":{"utilization":-5}})"));
    QCOMPARE(result.m_limits.m_fiveHour.m_dPercent, 100.0);
    QCOMPARE(result.m_limits.m_sevenDay.m_dPercent, 0.0);
    QVERIFY(!result.m_limits.m_fiveHour.m_dtResetsAt.isValid());
}

void TestUsageApiClient::TestNullWindowLeavesMeterEmpty()
{
    const FetchResult result = UsageApiClient::ParseUsageResponse(QByteArrayLiteral(
        R"({"five_hour":null,"seven_day":{"utilization":18,"resets_at":null}})"));
    QCOMPARE(result.m_eStatus, EFetchStatus::OK);
    QVERIFY(!result.m_limits.m_fiveHour.m_bValid);
    QVERIFY(result.m_limits.m_sevenDay.m_bValid);
    QVERIFY(!result.m_limits.m_sevenDay.m_dtResetsAt.isValid());
}

void TestUsageApiClient::TestMissingWindowsIsBadResponse()
{
    const FetchResult result = UsageApiClient::ParseUsageResponse(QByteArrayLiteral(R"({"other":1})"));
    QCOMPARE(result.m_eStatus, EFetchStatus::BAD_RESPONSE);
    QCOMPARE(result.m_baRawBody, QByteArrayLiteral(R"({"other":1})"));
}

void TestUsageApiClient::TestNotJsonIsBadResponse()
{
    QCOMPARE(UsageApiClient::ParseUsageResponse(QByteArrayLiteral("<html>")).m_eStatus, EFetchStatus::BAD_RESPONSE);
}

void TestUsageApiClient::TestMapsHttpStatuses()
{
    QCOMPARE(UsageApiClient::MapHttpResult(401, true, QString(), QByteArray()).m_eStatus, EFetchStatus::TOKEN_EXPIRED);
    QCOMPARE(UsageApiClient::MapHttpResult(403, true, QString(), QByteArray()).m_eStatus, EFetchStatus::TOKEN_EXPIRED);
    QCOMPARE(UsageApiClient::MapHttpResult(429, true, QString(), QByteArray()).m_eStatus, EFetchStatus::RATE_LIMITED);
    QCOMPARE(UsageApiClient::MapHttpResult(503, true, QString(), QByteArray()).m_eStatus, EFetchStatus::SERVER_ERROR);
    QCOMPARE(UsageApiClient::MapHttpResult(404, true, QString(), QByteArray()).m_eStatus, EFetchStatus::BAD_RESPONSE);

    const FetchResult resultNetwork = UsageApiClient::MapHttpResult(0, true, QStringLiteral("Host not found"), QByteArray());
    QCOMPARE(resultNetwork.m_eStatus, EFetchStatus::NETWORK_ERROR);
    QVERIFY(resultNetwork.m_strDetail.contains(QStringLiteral("Host not found")));

    const FetchResult resultOk = UsageApiClient::MapHttpResult(200, false, QString(), QByteArrayLiteral(R"({"five_hour":{"utilization":1}})"));
    QCOMPARE(resultOk.m_eStatus, EFetchStatus::OK);
}

void TestUsageApiClient::TestParsesRecordedResponseShape()
{
    const FetchResult result = UsageApiClient::ParseUsageResponse(ReadSourceFile(QStringLiteral("tests/fixtures/usage_response.json")));
    QCOMPARE(result.m_eStatus, EFetchStatus::OK);
    QVERIFY(result.m_limits.m_fiveHour.m_bValid);
    QVERIFY(result.m_limits.m_fiveHour.m_dtResetsAt.isValid());
    QVERIFY(result.m_limits.m_sevenDay.m_bValid);
    QVERIFY(result.m_limits.m_sevenDay.m_dtResetsAt.isValid());
}

QTEST_GUILESS_MAIN(TestUsageApiClient)
#include "TestUsageApiClient.moc"
```

`tests/CMakeLists.txt` 끝에 `tv_add_test(TestCredentialStore)`, `tv_add_test(TestUsageApiClient)` 추가.

- [ ] **Step 3: 빌드해서 실패 확인**

Run: `cmake --build build`
Expected: FAIL — `Service/CredentialStore.h` file not found

- [ ] **Step 4: 구현**

`src/Model/UsageLimits.h`:

```cpp
#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QMetaType>
#include <QString>

struct LimitWindow
{
    bool m_bValid = false;
    double m_dPercent = 0.0;
    QDateTime m_dtResetsAt;
};

struct UsageLimits
{
    LimitWindow m_fiveHour;
    LimitWindow m_sevenDay;
};

struct OAuthCredential
{
    QString m_strAccessToken;
    QDateTime m_dtExpiresAt;
    QString m_strSubscriptionType;
    QString m_strRateLimitTier;
};

enum class EFetchStatus
{
    OK,
    NOT_LOGGED_IN,
    KEYCHAIN_DENIED,
    TOKEN_EXPIRED,
    RATE_LIMITED,
    NETWORK_ERROR,
    SERVER_ERROR,
    BAD_RESPONSE,
};

struct FetchResult
{
    EFetchStatus m_eStatus = EFetchStatus::NETWORK_ERROR;
    UsageLimits m_limits;
    QString m_strPlanLabel;
    QString m_strDetail;
    int m_iHttpStatus = 0;
    QByteArray m_baRawBody;
};

Q_DECLARE_METATYPE(FetchResult)
```

`src/Service/CredentialStore.h`:

```cpp
#pragma once

#include "Model/UsageLimits.h"

#include <QByteArray>
#include <QDateTime>
#include <QString>

enum class ECredentialStatus
{
    OK,
    NOT_FOUND,
    ACCESS_DENIED,
    MALFORMED,
    EXPIRED,
};

struct CredentialResult
{
    ECredentialStatus m_eStatus = ECredentialStatus::NOT_FOUND;
    OAuthCredential m_credential;
};

class CredentialStore
{
public:
    static constexpr const char* kKeychainService = "Claude Code-credentials";

public:
    static CredentialResult Load(const QDateTime& now);
    static CredentialResult ParseCredentialJson(const QByteArray& json, const QDateTime& now);
    static QString FormatPlanLabel(const QString& subscriptionType, const QString& rateLimitTier);

private:
    static ECredentialStatus ReadKeychain(QByteArray* secret);
    static QDateTime ParseEpoch(double value);
};
```

`src/Service/CredentialStore.cpp`:

```cpp
#include "Service/CredentialStore.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QRegularExpression>

#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>

namespace
{
constexpr double kMillisecondEpochThreshold = 1e12;
constexpr double kMillisecondsPerSecond = 1000.0;
const QString kCredentialsFile = QStringLiteral("/.claude/.credentials.json");
}

CredentialResult CredentialStore::Load(const QDateTime& now)
{
    QByteArray baSecret;
    const ECredentialStatus eKeychainStatus = ReadKeychain(&baSecret);
    if (eKeychainStatus == ECredentialStatus::OK)
    {
        const CredentialResult result = ParseCredentialJson(baSecret, now);
        baSecret.fill('\0');
        return result;
    }
    if (eKeychainStatus == ECredentialStatus::ACCESS_DENIED)
    {
        CredentialResult result;
        result.m_eStatus = ECredentialStatus::ACCESS_DENIED;
        return result;
    }

    // Claude Code falls back to this file when the Keychain is unavailable.
    QFile file(QDir::homePath() + kCredentialsFile);
    if (!file.open(QIODevice::ReadOnly))
    {
        CredentialResult result;
        result.m_eStatus = ECredentialStatus::NOT_FOUND;
        return result;
    }
    QByteArray baFile = file.readAll();
    const CredentialResult result = ParseCredentialJson(baFile, now);
    baFile.fill('\0');
    return result;
}

CredentialResult CredentialStore::ParseCredentialJson(const QByteArray& json, const QDateTime& now)
{
    CredentialResult result;
    result.m_eStatus = ECredentialStatus::MALFORMED;
    const QJsonDocument doc = QJsonDocument::fromJson(json);
    if (!doc.isObject())
    {
        return result;
    }
    // Only these fields are read; the refresh token is never touched.
    const QJsonObject objOauth = doc.object().value(QStringLiteral("claudeAiOauth")).toObject();
    const QString strToken = objOauth.value(QStringLiteral("accessToken")).toString();
    if (strToken.isEmpty())
    {
        return result;
    }

    OAuthCredential& credential = result.m_credential;
    credential.m_strAccessToken = strToken;
    credential.m_strSubscriptionType = objOauth.value(QStringLiteral("subscriptionType")).toString();
    credential.m_strRateLimitTier = objOauth.value(QStringLiteral("rateLimitTier")).toString();
    const QJsonValue jvExpiresAt = objOauth.value(QStringLiteral("expiresAt"));
    if (jvExpiresAt.isDouble())
    {
        credential.m_dtExpiresAt = ParseEpoch(jvExpiresAt.toDouble());
    }
    else if (jvExpiresAt.isString())
    {
        credential.m_dtExpiresAt = QDateTime::fromString(jvExpiresAt.toString(), Qt::ISODateWithMs).toUTC();
    }

    const bool bExpired = credential.m_dtExpiresAt.isValid() && credential.m_dtExpiresAt <= now;
    result.m_eStatus = bExpired ? ECredentialStatus::EXPIRED : ECredentialStatus::OK;
    return result;
}

QString CredentialStore::FormatPlanLabel(const QString& subscriptionType, const QString& rateLimitTier)
{
    static const QRegularExpression reMultiplier(QStringLiteral("(\\d+)x"));
    const QString strType = subscriptionType.toLower();
    QString strName;
    if (strType == QStringLiteral("max"))
    {
        strName = QStringLiteral("Max");
    }
    else if (strType == QStringLiteral("pro"))
    {
        strName = QStringLiteral("Pro");
    }
    else if (strType == QStringLiteral("team"))
    {
        strName = QStringLiteral("Team");
    }
    else if (strType == QStringLiteral("enterprise"))
    {
        strName = QStringLiteral("Enterprise");
    }
    else
    {
        return QString();
    }

    const QRegularExpressionMatch match = reMultiplier.match(rateLimitTier.toLower());
    if (strName == QStringLiteral("Max") && match.hasMatch())
    {
        return strName + QLatin1Char(' ') + match.captured(1) + QLatin1Char('x');
    }
    return strName;
}

ECredentialStatus CredentialStore::ReadKeychain(QByteArray* secret)
{
    CFStringRef cfService = CFStringCreateWithCString(kCFAllocatorDefault, kKeychainService, kCFStringEncodingUTF8);
    const void* arrKeys[] = { kSecClass, kSecAttrService, kSecReturnData, kSecMatchLimit };
    const void* arrValues[] = { kSecClassGenericPassword, cfService, kCFBooleanTrue, kSecMatchLimitOne };
    CFDictionaryRef cfQuery = CFDictionaryCreate(kCFAllocatorDefault, arrKeys, arrValues, 4,
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);

    // The first call may show the macOS "allow access" prompt; this blocks until the user answers.
    CFTypeRef cfResult = nullptr;
    const OSStatus iStatus = SecItemCopyMatching(cfQuery, &cfResult);
    CFRelease(cfQuery);
    CFRelease(cfService);

    if (iStatus == errSecSuccess && cfResult != nullptr)
    {
        const CFDataRef cfData = static_cast<CFDataRef>(cfResult);
        *secret = QByteArray(reinterpret_cast<const char*>(CFDataGetBytePtr(cfData)), static_cast<int>(CFDataGetLength(cfData)));
        CFRelease(cfResult);
        return ECredentialStatus::OK;
    }
    if (cfResult != nullptr)
    {
        CFRelease(cfResult);
    }
    return iStatus == errSecItemNotFound ? ECredentialStatus::NOT_FOUND : ECredentialStatus::ACCESS_DENIED;
}

QDateTime CredentialStore::ParseEpoch(double value)
{
    const double dMilliseconds = value > kMillisecondEpochThreshold ? value : value * kMillisecondsPerSecond;
    return QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(dMilliseconds), Qt::UTC);
}
```

`src/Service/UsageApiClient.h`:

```cpp
#pragma once

#include "Model/UsageLimits.h"

#include <QByteArray>
#include <QDateTime>
#include <QJsonValue>
#include <QString>

class UsageApiClient
{
public:
    static constexpr const char* kUsageUrl = "https://api.anthropic.com/api/oauth/usage";
    static constexpr const char* kBetaHeader = "oauth-2025-04-20";
    static constexpr int kTimeoutMs = 10000;

public:
    static FetchResult FetchWithStoredCredential(const QDateTime& now);
    static FetchResult MapHttpResult(int httpStatus, bool networkError, const QString& errorText, const QByteArray& body);
    static FetchResult ParseUsageResponse(const QByteArray& body);

private:
    static FetchResult FetchBlocking(const QString& accessToken);
    static LimitWindow ParseWindow(const QJsonValue& value);
    static QDateTime ParseTimestamp(const QJsonValue& value);
};
```

`src/Service/UsageApiClient.cpp`:

```cpp
#include "Service/UsageApiClient.h"

#include "Service/CredentialStore.h"

#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QTimer>
#include <QUrl>

#include <algorithm>

namespace
{
constexpr int kHttpOk = 200;
constexpr int kHttpUnauthorized = 401;
constexpr int kHttpForbidden = 403;
constexpr int kHttpTooManyRequests = 429;
constexpr int kHttpServerErrorMin = 500;
constexpr double kMillisecondEpochThreshold = 1e12;
constexpr double kMillisecondsPerSecond = 1000.0;
const QString kFiveHourKey = QStringLiteral("five_hour");
const QString kSevenDayKey = QStringLiteral("seven_day");
}

FetchResult UsageApiClient::FetchWithStoredCredential(const QDateTime& now)
{
    const CredentialResult credential = CredentialStore::Load(now);
    const QString strPlanLabel = CredentialStore::FormatPlanLabel(credential.m_credential.m_strSubscriptionType, credential.m_credential.m_strRateLimitTier);
    FetchResult result;
    switch (credential.m_eStatus)
    {
    case ECredentialStatus::NOT_FOUND:
    case ECredentialStatus::MALFORMED:
        result.m_eStatus = EFetchStatus::NOT_LOGGED_IN;
        result.m_strDetail = QStringLiteral("Claude Code 로그인 정보를 찾지 못했습니다");
        return result;
    case ECredentialStatus::ACCESS_DENIED:
        result.m_eStatus = EFetchStatus::KEYCHAIN_DENIED;
        result.m_strDetail = QStringLiteral("Keychain 접근이 거부되었습니다");
        return result;
    case ECredentialStatus::EXPIRED:
        result.m_eStatus = EFetchStatus::TOKEN_EXPIRED;
        result.m_strDetail = QStringLiteral("로그인 토큰이 만료되었습니다");
        result.m_strPlanLabel = strPlanLabel;
        return result;
    case ECredentialStatus::OK:
        break;
    }

    result = FetchBlocking(credential.m_credential.m_strAccessToken);
    result.m_strPlanLabel = strPlanLabel;
    return result;
}

FetchResult UsageApiClient::MapHttpResult(int httpStatus, bool networkError, const QString& errorText, const QByteArray& body)
{
    if (httpStatus == kHttpOk)
    {
        return ParseUsageResponse(body);
    }

    FetchResult result;
    result.m_iHttpStatus = httpStatus;
    if (httpStatus == kHttpUnauthorized || httpStatus == kHttpForbidden)
    {
        result.m_eStatus = EFetchStatus::TOKEN_EXPIRED;
        result.m_strDetail = QStringLiteral("인증이 만료되었습니다 (HTTP %1)").arg(httpStatus);
    }
    else if (httpStatus == kHttpTooManyRequests)
    {
        result.m_eStatus = EFetchStatus::RATE_LIMITED;
        result.m_strDetail = QStringLiteral("요청이 너무 많습니다 (HTTP 429)");
    }
    else if (httpStatus >= kHttpServerErrorMin)
    {
        result.m_eStatus = EFetchStatus::SERVER_ERROR;
        result.m_strDetail = QStringLiteral("서버 오류 (HTTP %1)").arg(httpStatus);
    }
    else if (httpStatus == 0 || networkError)
    {
        result.m_eStatus = EFetchStatus::NETWORK_ERROR;
        result.m_strDetail = errorText.isEmpty() ? QStringLiteral("네트워크 연결 없음") : errorText;
    }
    else
    {
        result.m_eStatus = EFetchStatus::BAD_RESPONSE;
        result.m_strDetail = QStringLiteral("예상하지 못한 응답 (HTTP %1)").arg(httpStatus);
        result.m_baRawBody = body;
    }
    return result;
}

FetchResult UsageApiClient::ParseUsageResponse(const QByteArray& body)
{
    FetchResult result;
    result.m_iHttpStatus = kHttpOk;
    const QJsonDocument doc = QJsonDocument::fromJson(body);
    const QJsonObject objRoot = doc.object();
    if (!doc.isObject() || (!objRoot.contains(kFiveHourKey) && !objRoot.contains(kSevenDayKey)))
    {
        result.m_eStatus = EFetchStatus::BAD_RESPONSE;
        result.m_strDetail = QStringLiteral("five_hour / seven_day 필드가 없습니다");
        result.m_baRawBody = body;
        return result;
    }
    result.m_eStatus = EFetchStatus::OK;
    result.m_limits.m_fiveHour = ParseWindow(objRoot.value(kFiveHourKey));
    result.m_limits.m_sevenDay = ParseWindow(objRoot.value(kSevenDayKey));
    return result;
}

FetchResult UsageApiClient::FetchBlocking(const QString& accessToken)
{
    QNetworkAccessManager manager;
    QNetworkRequest request(QUrl(QString::fromLatin1(kUsageUrl)));
    request.setRawHeader("Authorization", QByteArrayLiteral("Bearer ") + accessToken.toUtf8());
    request.setRawHeader("anthropic-beta", kBetaHeader);
    request.setRawHeader("Accept", "application/json");
    request.setRawHeader("User-Agent", "TokenViewer/" TV_VERSION);

    QNetworkReply* pReply = manager.get(request);
    QEventLoop loop;
    QTimer timerTimeout;
    timerTimeout.setSingleShot(true);
    QObject::connect(pReply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timerTimeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    timerTimeout.start(kTimeoutMs);
    loop.exec();

    if (!pReply->isFinished())
    {
        pReply->abort();
        FetchResult result;
        result.m_eStatus = EFetchStatus::NETWORK_ERROR;
        result.m_strDetail = QStringLiteral("응답 시간 초과");
        return result;
    }
    const int iHttpStatus = pReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const bool bNetworkError = pReply->error() != QNetworkReply::NoError;
    // pReply is a child of manager and is deleted with it.
    return MapHttpResult(iHttpStatus, bNetworkError, pReply->errorString(), pReply->readAll());
}

LimitWindow UsageApiClient::ParseWindow(const QJsonValue& value)
{
    LimitWindow window;
    if (!value.isObject())
    {
        return window;
    }
    const QJsonObject objWindow = value.toObject();
    QJsonValue jvPercent = objWindow.value(QStringLiteral("utilization"));
    if (!jvPercent.isDouble())
    {
        jvPercent = objWindow.value(QStringLiteral("used_percentage"));
    }
    if (!jvPercent.isDouble())
    {
        return window;
    }
    window.m_bValid = true;
    window.m_dPercent = std::clamp(jvPercent.toDouble(), 0.0, 100.0);
    window.m_dtResetsAt = ParseTimestamp(objWindow.value(QStringLiteral("resets_at")));
    return window;
}

QDateTime UsageApiClient::ParseTimestamp(const QJsonValue& value)
{
    if (value.isDouble())
    {
        const double dValue = value.toDouble();
        const double dMilliseconds = dValue > kMillisecondEpochThreshold ? dValue : dValue * kMillisecondsPerSecond;
        return QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(dMilliseconds), Qt::UTC);
    }
    if (!value.isString())
    {
        return QDateTime();
    }
    QString strValue = value.toString();
    QDateTime dtValue = QDateTime::fromString(strValue, Qt::ISODateWithMs);
    if (!dtValue.isValid())
    {
        // Qt 5 reads at most milliseconds; drop longer fractions such as microseconds.
        static const QRegularExpression reFraction(QStringLiteral("\\.(\\d{3})\\d+"));
        strValue.replace(reFraction, QStringLiteral(".\\1"));
        dtValue = QDateTime::fromString(strValue, Qt::ISODateWithMs);
    }
    return dtValue.isValid() ? dtValue.toUTC() : QDateTime();
}
```

`CMakeLists.txt`:
- `set(TV_CORE_SOURCES ...)` 안에 `src/Service/CredentialStore.cpp`, `src/Service/UsageApiClient.cpp` 추가.
- `target_link_libraries(TokenViewerCore PUBLIC Qt5::Core ...)` 줄 아래에:

```cmake
target_link_libraries(TokenViewerCore PUBLIC "-framework Security" "-framework CoreFoundation")
```

- [ ] **Step 5: 테스트 통과 확인**

Run: `cmake --build build && ctest --test-dir build --output-on-failure -R "TestCredentialStore|TestUsageApiClient"`
Expected: PASS

- [ ] **Step 6: Commit**

```bash
git add CMakeLists.txt tests/CMakeLists.txt tests/TestCredentialStore.cpp tests/TestUsageApiClient.cpp src/Model/UsageLimits.h src/Service/CredentialStore.h src/Service/CredentialStore.cpp src/Service/UsageApiClient.h src/Service/UsageApiClient.cpp
git commit -m "feat: read Claude Code credentials and parse the usage API" -m "Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>"
```

---

### Task 10: RefreshPolicy + UsageFetchThread

**Files:**
- Create: `src/Service/RefreshPolicy.h`, `src/Service/RefreshPolicy.cpp`, `src/Thread/UsageFetchThread.h`, `src/Thread/UsageFetchThread.cpp`
- Modify: `CMakeLists.txt`, `tests/CMakeLists.txt`
- Test: `tests/TestRefreshPolicy.cpp`, `tests/TestUsageFetchThread.cpp`

**Interfaces:**
- Consumes: Task 9 `EFetchStatus`, `FetchResult`
- Produces:
  - `struct RefreshDecision { int m_iDelayMs; bool m_bAutoRetry; }`, `RefreshPolicy::DecideNext(EFetchStatus status, int baseIntervalMs, int previousDelayMs) -> RefreshDecision`, `RefreshPolicy::kMaxBackoffMs`
  - `using FetchFunction = std::function<FetchResult()>;`
  - `class UsageFetchThread : public QObject` — `UsageFetchThread(const FetchFunction& fetch, int intervalMs)`, `void Start()`, `void Stop()`, `void RequestRefresh()`, `void SetIntervalMs(int intervalMs)`, signals `void FetchCompleted(const FetchResult& result)`, `void Finished(bool success)`

- [ ] **Step 1: 실패하는 RefreshPolicy 테스트**

`tests/TestRefreshPolicy.cpp`:

```cpp
#include "Service/RefreshPolicy.h"

#include <QtTest>

namespace
{
constexpr int kBaseMs = 3 * 60 * 1000;
}

class TestRefreshPolicy : public QObject
{
    Q_OBJECT

private slots:
    void TestOkUsesBaseInterval();
    void TestRateLimitDoublesDelay();
    void TestRateLimitIsCapped();
    void TestKeychainDeniedStopsAutoRetry();
    void TestOtherFailuresKeepBaseInterval();
};

void TestRefreshPolicy::TestOkUsesBaseInterval()
{
    const RefreshDecision decision = RefreshPolicy::DecideNext(EFetchStatus::OK, kBaseMs, 4 * kBaseMs);
    QCOMPARE(decision.m_iDelayMs, kBaseMs);
    QVERIFY(decision.m_bAutoRetry);
}

void TestRefreshPolicy::TestRateLimitDoublesDelay()
{
    QCOMPARE(RefreshPolicy::DecideNext(EFetchStatus::RATE_LIMITED, kBaseMs, 0).m_iDelayMs, 2 * kBaseMs);
    QCOMPARE(RefreshPolicy::DecideNext(EFetchStatus::RATE_LIMITED, kBaseMs, 2 * kBaseMs).m_iDelayMs, 4 * kBaseMs);
}

void TestRefreshPolicy::TestRateLimitIsCapped()
{
    QCOMPARE(RefreshPolicy::DecideNext(EFetchStatus::RATE_LIMITED, kBaseMs, 25 * 60 * 1000).m_iDelayMs, RefreshPolicy::kMaxBackoffMs);
}

void TestRefreshPolicy::TestKeychainDeniedStopsAutoRetry()
{
    QVERIFY(!RefreshPolicy::DecideNext(EFetchStatus::KEYCHAIN_DENIED, kBaseMs, kBaseMs).m_bAutoRetry);
}

void TestRefreshPolicy::TestOtherFailuresKeepBaseInterval()
{
    for (const EFetchStatus eStatus : { EFetchStatus::NETWORK_ERROR, EFetchStatus::SERVER_ERROR, EFetchStatus::TOKEN_EXPIRED,
             EFetchStatus::NOT_LOGGED_IN, EFetchStatus::BAD_RESPONSE })
    {
        const RefreshDecision decision = RefreshPolicy::DecideNext(eStatus, kBaseMs, 4 * kBaseMs);
        QCOMPARE(decision.m_iDelayMs, kBaseMs);
        QVERIFY(decision.m_bAutoRetry);
    }
}

QTEST_GUILESS_MAIN(TestRefreshPolicy)
#include "TestRefreshPolicy.moc"
```

`tests/CMakeLists.txt` 끝에 `tv_add_test(TestRefreshPolicy)` 추가.

Run: `cmake --build build`
Expected: FAIL — `Service/RefreshPolicy.h` file not found

- [ ] **Step 2: RefreshPolicy 구현**

`src/Service/RefreshPolicy.h`:

```cpp
#pragma once

#include "Model/UsageLimits.h"

struct RefreshDecision
{
    int m_iDelayMs = 0;
    bool m_bAutoRetry = true;
};

class RefreshPolicy
{
public:
    static constexpr int kMaxBackoffMs = 30 * 60 * 1000;

public:
    static RefreshDecision DecideNext(EFetchStatus status, int baseIntervalMs, int previousDelayMs);
};
```

`src/Service/RefreshPolicy.cpp`:

```cpp
#include "Service/RefreshPolicy.h"

#include <algorithm>

RefreshDecision RefreshPolicy::DecideNext(EFetchStatus status, int baseIntervalMs, int previousDelayMs)
{
    RefreshDecision decision;
    decision.m_iDelayMs = baseIntervalMs;
    switch (status)
    {
    case EFetchStatus::RATE_LIMITED:
        decision.m_iDelayMs = std::min(std::max(previousDelayMs, baseIntervalMs) * 2, kMaxBackoffMs);
        break;
    case EFetchStatus::KEYCHAIN_DENIED:
        // Retrying would pop the Keychain prompt every few minutes; wait for the user to ask.
        decision.m_bAutoRetry = false;
        break;
    default:
        break;
    }
    return decision;
}
```

`CMakeLists.txt` 의 `set(TV_CORE_SOURCES ...)` 안에 `src/Service/RefreshPolicy.cpp` 추가.

Run: `cmake --build build && ctest --test-dir build --output-on-failure -R TestRefreshPolicy`
Expected: PASS

- [ ] **Step 3: 실패하는 UsageFetchThread 테스트**

`tests/TestUsageFetchThread.cpp`:

```cpp
#include "Thread/UsageFetchThread.h"

#include <QSignalSpy>
#include <QtTest>

#include <atomic>

namespace
{
constexpr int kShortIntervalMs = 300;
constexpr int kLongIntervalMs = 60000;
constexpr int kSignalWaitMs = 2000;
constexpr int kQuietWaitMs = 800;

FetchFunction MakeFetch(std::atomic<int>* calls, EFetchStatus status)
{
    return [calls, status]()
    {
        ++(*calls);
        FetchResult result;
        result.m_eStatus = status;
        return result;
    };
}
}

class TestUsageFetchThread : public QObject
{
    Q_OBJECT

private slots:
    void TestFetchesImmediatelyThenOnInterval();
    void TestKeychainDeniedStopsAutoRetry();
    void TestRefreshRequestFetchesNow();
};

void TestUsageFetchThread::TestFetchesImmediatelyThenOnInterval()
{
    std::atomic<int> iCalls{ 0 };
    UsageFetchThread thread(MakeFetch(&iCalls, EFetchStatus::OK), kShortIntervalMs);
    QSignalSpy spy(&thread, &UsageFetchThread::FetchCompleted);
    thread.Start();
    QVERIFY(spy.wait(kSignalWaitMs));
    QVERIFY(spy.wait(kSignalWaitMs));
    QVERIFY(iCalls.load() >= 2);
    thread.Stop();
}

void TestUsageFetchThread::TestKeychainDeniedStopsAutoRetry()
{
    std::atomic<int> iCalls{ 0 };
    UsageFetchThread thread(MakeFetch(&iCalls, EFetchStatus::KEYCHAIN_DENIED), kShortIntervalMs);
    QSignalSpy spy(&thread, &UsageFetchThread::FetchCompleted);
    thread.Start();
    QVERIFY(spy.wait(kSignalWaitMs));
    QTest::qWait(kQuietWaitMs);
    QCOMPARE(iCalls.load(), 1);

    thread.RequestRefresh();
    QVERIFY(spy.wait(kSignalWaitMs));
    QCOMPARE(iCalls.load(), 2);
    thread.Stop();
}

void TestUsageFetchThread::TestRefreshRequestFetchesNow()
{
    std::atomic<int> iCalls{ 0 };
    UsageFetchThread thread(MakeFetch(&iCalls, EFetchStatus::OK), kLongIntervalMs);
    QSignalSpy spy(&thread, &UsageFetchThread::FetchCompleted);
    thread.Start();
    QVERIFY(spy.wait(kSignalWaitMs));
    thread.RequestRefresh();
    QVERIFY(spy.wait(kSignalWaitMs));
    QCOMPARE(iCalls.load(), 2);
    QCOMPARE(spy.last().at(0).value<FetchResult>().m_eStatus, EFetchStatus::OK);
    thread.Stop();
}

QTEST_GUILESS_MAIN(TestUsageFetchThread)
#include "TestUsageFetchThread.moc"
```

`tests/CMakeLists.txt` 끝에 `tv_add_test(TestUsageFetchThread)` 추가.

Run: `cmake --build build`
Expected: FAIL — `Thread/UsageFetchThread.h` file not found

- [ ] **Step 4: UsageFetchThread 구현**

`src/Thread/UsageFetchThread.h`:

```cpp
#pragma once

#include "Model/UsageLimits.h"

#include <QObject>

#include <functional>
#include <memory>

using FetchFunction = std::function<FetchResult()>;

class UsageFetchThreadWorker : public QObject
{
    Q_OBJECT

public:
    explicit UsageFetchThreadWorker(QObject* parent = nullptr);
    ~UsageFetchThreadWorker();

public:
    void SetContext(const FetchFunction& fetch, int intervalMs);
    void SetIntervalMs(int intervalMs);
    void RequestRefresh();
    void Stop();

public slots:
    void DoWork();

signals:
    void FetchCompleted(const FetchResult& result);
    void WorkFinished(bool success);

private:
    void Run();
    bool IsThreadAlive() const;

private:
    class UsageFetchThreadWorkerImpl;
    std::unique_ptr<UsageFetchThreadWorkerImpl> m_pimpl;
};

class UsageFetchThread : public QObject
{
    Q_OBJECT

public:
    UsageFetchThread(const FetchFunction& fetch, int intervalMs);
    ~UsageFetchThread();

public:
    void Start();
    void Stop();
    void RequestRefresh();
    void SetIntervalMs(int intervalMs);

signals:
    void FetchCompleted(const FetchResult& result);
    void Finished(bool success);

private:
    class UsageFetchThreadImpl;
    std::unique_ptr<UsageFetchThreadImpl> m_pimpl;
};
```

`src/Thread/UsageFetchThread.cpp`:

```cpp
#include "Thread/UsageFetchThread.h"

#include "Service/RefreshPolicy.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEvent>
#include <QMetaObject>
#include <QThread>

#include <atomic>

namespace
{
constexpr int kStopWaitMs = 4000;
constexpr int kTickMs = 100;
}

////////////////////////////////////////////////////////////////////////////////
// Worker
////////////////////////////////////////////////////////////////////////////////
class UsageFetchThreadWorker::UsageFetchThreadWorkerImpl
{
public:
    std::atomic<bool> m_bDoRunThread{ false };
    std::atomic<bool> m_bRefreshRequested{ false };
    std::atomic<int> m_iIntervalMs{ 0 };
    FetchFunction m_funcFetch;
};

UsageFetchThreadWorker::UsageFetchThreadWorker(QObject* parent)
    : QObject(parent)
    , m_pimpl(std::make_unique<UsageFetchThreadWorkerImpl>())
{
}

UsageFetchThreadWorker::~UsageFetchThreadWorker() = default;

void UsageFetchThreadWorker::SetContext(const FetchFunction& fetch, int intervalMs)
{
    m_pimpl->m_funcFetch = fetch;
    m_pimpl->m_iIntervalMs = intervalMs;
}

void UsageFetchThreadWorker::SetIntervalMs(int intervalMs)
{
    m_pimpl->m_iIntervalMs = intervalMs;
}

void UsageFetchThreadWorker::RequestRefresh()
{
    m_pimpl->m_bRefreshRequested = true;
}

void UsageFetchThreadWorker::Stop()
{
    m_pimpl->m_bDoRunThread = false;
}

void UsageFetchThreadWorker::Run()
{
    m_pimpl->m_bDoRunThread = true;
}

bool UsageFetchThreadWorker::IsThreadAlive() const
{
    return m_pimpl->m_bDoRunThread;
}

void UsageFetchThreadWorker::DoWork()
{
    Run();
    QElapsedTimer timerSinceFetch;
    bool bFirstFetch = true;
    bool bAutoRetry = true;
    int iBackoffMs = 0;
    EFetchStatus eLastStatus = EFetchStatus::OK;
    while (IsThreadAlive())
    {
        const bool bManual = m_pimpl->m_bRefreshRequested.exchange(false);
        // Outside of a rate-limit backoff, always use the latest interval from the settings.
        const int iWaitMs = eLastStatus == EFetchStatus::RATE_LIMITED ? iBackoffMs : m_pimpl->m_iIntervalMs.load();
        const bool bDue = bAutoRetry && (bFirstFetch || timerSinceFetch.elapsed() >= iWaitMs);
        if (bManual || bDue)
        {
            const FetchResult result = m_pimpl->m_funcFetch();
            emit FetchCompleted(result);
            const RefreshDecision decision = RefreshPolicy::DecideNext(result.m_eStatus, m_pimpl->m_iIntervalMs.load(), iBackoffMs);
            iBackoffMs = decision.m_iDelayMs;
            bAutoRetry = decision.m_bAutoRetry;
            eLastStatus = result.m_eStatus;
            bFirstFetch = false;
            timerSinceFetch.restart();
        }
        // QNetworkAccessManager schedules deleteLater() on this thread, whose event loop DoWork blocks; flush them here.
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QThread::msleep(kTickMs);
    }
    emit WorkFinished(true);
}

////////////////////////////////////////////////////////////////////////////////
// Thread
////////////////////////////////////////////////////////////////////////////////
class UsageFetchThread::UsageFetchThreadImpl
{
public:
    QThread m_workerThread;
    std::unique_ptr<UsageFetchThreadWorker> m_upWorker;
    FetchFunction m_funcFetch;
    int m_iIntervalMs = 0;

    ~UsageFetchThreadImpl()
    {
        StopWorker();
    }

    void ConnectWorker(UsageFetchThread* owner)
    {
        QObject::connect(m_upWorker.get(), &UsageFetchThreadWorker::FetchCompleted, owner, &UsageFetchThread::FetchCompleted);
        QObject::connect(m_upWorker.get(), &UsageFetchThreadWorker::WorkFinished, owner, &UsageFetchThread::Finished);
    }

    void StopWorker()
    {
        if (m_upWorker)
        {
            m_upWorker->Stop();
        }
        if (m_workerThread.isRunning())
        {
            m_workerThread.quit();
            if (!m_workerThread.wait(kStopWaitMs))
            {
                m_workerThread.terminate();
                m_workerThread.wait();
            }
        }
        m_upWorker.reset();
    }
};

UsageFetchThread::UsageFetchThread(const FetchFunction& fetch, int intervalMs)
    : QObject(nullptr)
    , m_pimpl(std::make_unique<UsageFetchThreadImpl>())
{
    qRegisterMetaType<FetchResult>("FetchResult");
    m_pimpl->m_funcFetch = fetch;
    m_pimpl->m_iIntervalMs = intervalMs;
}

UsageFetchThread::~UsageFetchThread() = default;

void UsageFetchThread::Start()
{
    if (!m_pimpl->m_upWorker)
    {
        m_pimpl->m_upWorker = std::make_unique<UsageFetchThreadWorker>();
        m_pimpl->m_upWorker->SetContext(m_pimpl->m_funcFetch, m_pimpl->m_iIntervalMs);
        m_pimpl->ConnectWorker(this);
        m_pimpl->m_upWorker->moveToThread(&m_pimpl->m_workerThread);
    }
    if (!m_pimpl->m_workerThread.isRunning())
    {
        m_pimpl->m_workerThread.start();
    }
    QMetaObject::invokeMethod(m_pimpl->m_upWorker.get(), "DoWork", Qt::QueuedConnection);
}

void UsageFetchThread::Stop()
{
    m_pimpl->StopWorker();
}

void UsageFetchThread::RequestRefresh()
{
    if (m_pimpl->m_upWorker)
    {
        m_pimpl->m_upWorker->RequestRefresh();
    }
}

void UsageFetchThread::SetIntervalMs(int intervalMs)
{
    m_pimpl->m_iIntervalMs = intervalMs;
    if (m_pimpl->m_upWorker)
    {
        m_pimpl->m_upWorker->SetIntervalMs(intervalMs);
    }
}
```

`CMakeLists.txt` 의 `set(TV_CORE_SOURCES ...)` 안에 `src/Thread/UsageFetchThread.h`, `src/Thread/UsageFetchThread.cpp` 추가.

- [ ] **Step 5: 테스트 통과 확인**

Run: `cmake --build build && ctest --test-dir build --output-on-failure -R "TestRefreshPolicy|TestUsageFetchThread"`
Expected: PASS

- [ ] **Step 6: Commit**

```bash
git add CMakeLists.txt tests/CMakeLists.txt tests/TestRefreshPolicy.cpp tests/TestUsageFetchThread.cpp src/Service/RefreshPolicy.h src/Service/RefreshPolicy.cpp src/Thread/UsageFetchThread.h src/Thread/UsageFetchThread.cpp
git commit -m "feat: poll the usage API on a worker thread with backoff" -m "Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>"
```

---

### Task 11: Settings + SystemStatus + Observers

**Files:**
- Create: `src/Core/Settings.h`, `src/Core/Settings.cpp`, `src/Core/SystemStatus.h`, `src/Core/SystemStatus.cpp`, `src/Core/Observers.h`
- Modify: `CMakeLists.txt`, `tests/CMakeLists.txt`
- Test: `tests/TestSettings.cpp`, `tests/TestSystemStatus.cpp`

**Interfaces:**
- Consumes: Task 7 `EBreakdownDimension`, `EBreakdownPeriod`; Task 9 `FetchResult`, `UsageLimits`, `EFetchStatus`; Task 5 `LogSnapshot`
- Produces:
  - `class Settings` — `explicit Settings(QSettings* settings)` (소유하지 않음), `static QVector<int> GetAllowedIntervalMinutes()`, Get/Set: `RefreshIntervalMinutes`, `WarnPercent`, `CriticalPercent`, `LaunchAtLogin`, `Dimension`, `Period`; 상수 `kDefaultIntervalMinutes`, `kDefaultWarnPercent`, `kDefaultCriticalPercent`, `kMinWarnPercent`, `kMaxWarnPercent`, `kMaxPercent`, `kPercentStep`
  - `enum class ELimitDisplayState { EMPTY, NORMAL, STALE }`
  - `class SystemStatus` — `ApplyFetchResult(const FetchResult&, const QDateTime& now)`, `ApplyLogSnapshot(const LogSnapshot&)`, `GetLimitDisplayState(const QDateTime& now) const`, `IsRefreshDueOnOpen(const QDateTime& now) const`, `HasLimits()`, `GetLimits()`, `GetPlanLabel()`, `GetLastStatus()`, `GetLastDetail()`, `GetLastAttemptAt()`, `GetLastSuccessAt()`, `GetLogSnapshot()`
  - `class Observers : public QObject` — signals `LimitsChanged()`, `LogSnapshotChanged()`, `SettingsChanged()`, `RefreshRequested()`, `SettingsWindowRequested()`, `QuitRequested()`

- [ ] **Step 1: 실패하는 테스트**

`tests/TestSettings.cpp`:

```cpp
#include "Core/Settings.h"

#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>

class TestSettings : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();
    void TestDefaultsWhenEmpty();
    void TestRejectsUnknownInterval();
    void TestRejectsInvalidWarnPercent();
    void TestKeepsCriticalAboveWarn();
    void TestRoundTripsDimensionAndPeriod();

private:
    std::unique_ptr<QTemporaryDir> m_upDir;
    std::unique_ptr<QSettings> m_upStoredSettings;
    std::unique_ptr<Settings> m_upSettings;
};

void TestSettings::init()
{
    m_upDir = std::make_unique<QTemporaryDir>();
    m_upStoredSettings = std::make_unique<QSettings>(m_upDir->filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);
    m_upSettings = std::make_unique<Settings>(m_upStoredSettings.get());
}

void TestSettings::cleanup()
{
    m_upSettings.reset();
    m_upStoredSettings.reset();
    m_upDir.reset();
}

void TestSettings::TestDefaultsWhenEmpty()
{
    QCOMPARE(m_upSettings->GetRefreshIntervalMinutes(), 3);
    QCOMPARE(m_upSettings->GetWarnPercent(), 70);
    QCOMPARE(m_upSettings->GetCriticalPercent(), 90);
    QVERIFY(m_upSettings->GetLaunchAtLogin());
    QCOMPARE(m_upSettings->GetDimension(), EBreakdownDimension::PROJECT);
    QCOMPARE(m_upSettings->GetPeriod(), EBreakdownPeriod::FIVE_HOUR_WINDOW);
}

void TestSettings::TestRejectsUnknownInterval()
{
    m_upSettings->SetRefreshIntervalMinutes(10);
    QCOMPARE(m_upSettings->GetRefreshIntervalMinutes(), 10);
    m_upSettings->SetRefreshIntervalMinutes(7);
    QCOMPARE(m_upSettings->GetRefreshIntervalMinutes(), 3);
}

void TestSettings::TestRejectsInvalidWarnPercent()
{
    m_upSettings->SetWarnPercent(45);
    QCOMPARE(m_upSettings->GetWarnPercent(), 70);
    m_upSettings->SetWarnPercent(72);
    QCOMPARE(m_upSettings->GetWarnPercent(), 70);
    m_upSettings->SetWarnPercent(80);
    QCOMPARE(m_upSettings->GetWarnPercent(), 80);
}

void TestSettings::TestKeepsCriticalAboveWarn()
{
    m_upSettings->SetWarnPercent(90);
    m_upSettings->SetCriticalPercent(90);
    QCOMPARE(m_upSettings->GetCriticalPercent(), 95);
    m_upSettings->SetWarnPercent(95);
    m_upSettings->SetCriticalPercent(50);
    QCOMPARE(m_upSettings->GetCriticalPercent(), 100);
}

void TestSettings::TestRoundTripsDimensionAndPeriod()
{
    m_upSettings->SetDimension(EBreakdownDimension::SESSION);
    m_upSettings->SetPeriod(EBreakdownPeriod::SEVEN_DAYS);
    QCOMPARE(m_upSettings->GetDimension(), EBreakdownDimension::SESSION);
    QCOMPARE(m_upSettings->GetPeriod(), EBreakdownPeriod::SEVEN_DAYS);
    m_upStoredSettings->setValue(QStringLiteral("popup/dimension"), 9);
    QCOMPARE(m_upSettings->GetDimension(), EBreakdownDimension::PROJECT);
}

QTEST_GUILESS_MAIN(TestSettings)
#include "TestSettings.moc"
```

`tests/TestSystemStatus.cpp`:

```cpp
#include "Core/SystemStatus.h"

#include <QtTest>

namespace
{
const QDateTime kNow(QDate(2026, 10, 5), QTime(12, 0), Qt::UTC);

FetchResult MakeResult(EFetchStatus status)
{
    FetchResult result;
    result.m_eStatus = status;
    result.m_strDetail = QStringLiteral("detail");
    if (status == EFetchStatus::OK)
    {
        result.m_limits.m_fiveHour.m_bValid = true;
        result.m_limits.m_fiveHour.m_dPercent = 42.0;
        result.m_limits.m_sevenDay.m_bValid = true;
        result.m_limits.m_sevenDay.m_dPercent = 18.0;
        result.m_strPlanLabel = QStringLiteral("Max 5x");
    }
    return result;
}
}

class TestSystemStatus : public QObject
{
    Q_OBJECT

private slots:
    void TestEmptyBeforeFirstFetch();
    void TestOkStoresLimitsAndPlan();
    void TestFailureKeepsValuesThenTurnsStale();
    void TestStaleAfterSleepEvenIfLastStatusOk();
    void TestKeychainDeniedClearsLimits();
    void TestRefreshDueOnOpenAfter30Seconds();
};

void TestSystemStatus::TestEmptyBeforeFirstFetch()
{
    SystemStatus status;
    QCOMPARE(status.GetLimitDisplayState(kNow), ELimitDisplayState::EMPTY);
    QVERIFY(!status.GetLastAttemptAt().isValid());
    QVERIFY(status.IsRefreshDueOnOpen(kNow));
}

void TestSystemStatus::TestOkStoresLimitsAndPlan()
{
    SystemStatus status;
    status.ApplyFetchResult(MakeResult(EFetchStatus::OK), kNow);
    QCOMPARE(status.GetLimitDisplayState(kNow), ELimitDisplayState::NORMAL);
    QCOMPARE(status.GetLimits().m_fiveHour.m_dPercent, 42.0);
    QCOMPARE(status.GetPlanLabel(), QStringLiteral("Max 5x"));
    QCOMPARE(status.GetLastSuccessAt(), kNow);
}

void TestSystemStatus::TestFailureKeepsValuesThenTurnsStale()
{
    SystemStatus status;
    status.ApplyFetchResult(MakeResult(EFetchStatus::OK), kNow);
    status.ApplyFetchResult(MakeResult(EFetchStatus::NETWORK_ERROR), kNow.addSecs(5 * 60));
    QCOMPARE(status.GetLimitDisplayState(kNow.addSecs(5 * 60)), ELimitDisplayState::NORMAL);
    QCOMPARE(status.GetLimitDisplayState(kNow.addSecs(16 * 60)), ELimitDisplayState::STALE);
    QCOMPARE(status.GetLimits().m_fiveHour.m_dPercent, 42.0);
    QCOMPARE(status.GetLastStatus(), EFetchStatus::NETWORK_ERROR);
    QCOMPARE(status.GetPlanLabel(), QStringLiteral("Max 5x"));
}

void TestSystemStatus::TestStaleAfterSleepEvenIfLastStatusOk()
{
    SystemStatus status;
    status.ApplyFetchResult(MakeResult(EFetchStatus::OK), kNow);
    QCOMPARE(status.GetLimitDisplayState(kNow.addSecs(20 * 60)), ELimitDisplayState::STALE);
}

void TestSystemStatus::TestKeychainDeniedClearsLimits()
{
    SystemStatus status;
    status.ApplyFetchResult(MakeResult(EFetchStatus::OK), kNow);
    status.ApplyFetchResult(MakeResult(EFetchStatus::KEYCHAIN_DENIED), kNow.addSecs(60));
    QCOMPARE(status.GetLimitDisplayState(kNow.addSecs(60)), ELimitDisplayState::EMPTY);
    QVERIFY(!status.HasLimits());
}

void TestSystemStatus::TestRefreshDueOnOpenAfter30Seconds()
{
    SystemStatus status;
    status.ApplyFetchResult(MakeResult(EFetchStatus::OK), kNow);
    QVERIFY(!status.IsRefreshDueOnOpen(kNow.addSecs(10)));
    QVERIFY(status.IsRefreshDueOnOpen(kNow.addSecs(31)));
}

QTEST_GUILESS_MAIN(TestSystemStatus)
#include "TestSystemStatus.moc"
```

`tests/CMakeLists.txt` 끝에 `tv_add_test(TestSettings)`, `tv_add_test(TestSystemStatus)` 추가.

- [ ] **Step 2: 빌드해서 실패 확인**

Run: `cmake --build build`
Expected: FAIL — `Core/Settings.h` file not found

- [ ] **Step 3: 구현**

`src/Core/Settings.h`:

```cpp
#pragma once

#include "Model/Breakdown.h"

#include <QVector>

class QSettings;

class Settings
{
public:
    static constexpr int kDefaultIntervalMinutes = 3;
    static constexpr int kDefaultWarnPercent = 70;
    static constexpr int kDefaultCriticalPercent = 90;
    static constexpr int kMinWarnPercent = 50;
    static constexpr int kMaxWarnPercent = 95;
    static constexpr int kMaxPercent = 100;
    static constexpr int kPercentStep = 5;

public:
    explicit Settings(QSettings* settings);
    ~Settings();

public:
    static QVector<int> GetAllowedIntervalMinutes();

public:
    int GetRefreshIntervalMinutes() const;
    void SetRefreshIntervalMinutes(int minutes);
    int GetWarnPercent() const;
    void SetWarnPercent(int percent);
    int GetCriticalPercent() const;
    void SetCriticalPercent(int percent);
    bool GetLaunchAtLogin() const;
    void SetLaunchAtLogin(bool enabled);
    EBreakdownDimension GetDimension() const;
    void SetDimension(EBreakdownDimension dimension);
    EBreakdownPeriod GetPeriod() const;
    void SetPeriod(EBreakdownPeriod period);

private:
    QSettings* m_kpSettings;
};
```

`src/Core/Settings.cpp`:

```cpp
#include "Core/Settings.h"

#include <QSettings>
#include <QString>

#include <algorithm>

namespace
{
const QString kKeyInterval = QStringLiteral("refresh/intervalMinutes");
const QString kKeyWarn = QStringLiteral("alert/warnPercent");
const QString kKeyCritical = QStringLiteral("alert/criticalPercent");
const QString kKeyLaunchAtLogin = QStringLiteral("app/launchAtLogin");
const QString kKeyDimension = QStringLiteral("popup/dimension");
const QString kKeyPeriod = QStringLiteral("popup/period");
}

Settings::Settings(QSettings* settings)
    : m_kpSettings(settings)
{
}

Settings::~Settings() = default;

QVector<int> Settings::GetAllowedIntervalMinutes()
{
    return { 1, 3, 5, 10 };
}

int Settings::GetRefreshIntervalMinutes() const
{
    const int iValue = m_kpSettings->value(kKeyInterval, kDefaultIntervalMinutes).toInt();
    return GetAllowedIntervalMinutes().contains(iValue) ? iValue : kDefaultIntervalMinutes;
}

void Settings::SetRefreshIntervalMinutes(int minutes)
{
    m_kpSettings->setValue(kKeyInterval, minutes);
}

int Settings::GetWarnPercent() const
{
    const int iValue = m_kpSettings->value(kKeyWarn, kDefaultWarnPercent).toInt();
    const bool bValid = iValue >= kMinWarnPercent && iValue <= kMaxWarnPercent && iValue % kPercentStep == 0;
    return bValid ? iValue : kDefaultWarnPercent;
}

void Settings::SetWarnPercent(int percent)
{
    m_kpSettings->setValue(kKeyWarn, percent);
}

int Settings::GetCriticalPercent() const
{
    const int iWarn = GetWarnPercent();
    const int iValue = m_kpSettings->value(kKeyCritical, kDefaultCriticalPercent).toInt();
    if (iValue > iWarn && iValue <= kMaxPercent && iValue % kPercentStep == 0)
    {
        return iValue;
    }
    return kDefaultCriticalPercent > iWarn ? kDefaultCriticalPercent : std::min(iWarn + kPercentStep, kMaxPercent);
}

void Settings::SetCriticalPercent(int percent)
{
    m_kpSettings->setValue(kKeyCritical, percent);
}

bool Settings::GetLaunchAtLogin() const
{
    return m_kpSettings->value(kKeyLaunchAtLogin, true).toBool();
}

void Settings::SetLaunchAtLogin(bool enabled)
{
    m_kpSettings->setValue(kKeyLaunchAtLogin, enabled);
}

EBreakdownDimension Settings::GetDimension() const
{
    const int iValue = m_kpSettings->value(kKeyDimension, static_cast<int>(EBreakdownDimension::PROJECT)).toInt();
    if (iValue < static_cast<int>(EBreakdownDimension::PROJECT) || iValue > static_cast<int>(EBreakdownDimension::SESSION))
    {
        return EBreakdownDimension::PROJECT;
    }
    return static_cast<EBreakdownDimension>(iValue);
}

void Settings::SetDimension(EBreakdownDimension dimension)
{
    m_kpSettings->setValue(kKeyDimension, static_cast<int>(dimension));
}

EBreakdownPeriod Settings::GetPeriod() const
{
    const int iValue = m_kpSettings->value(kKeyPeriod, static_cast<int>(EBreakdownPeriod::FIVE_HOUR_WINDOW)).toInt();
    if (iValue < static_cast<int>(EBreakdownPeriod::FIVE_HOUR_WINDOW) || iValue > static_cast<int>(EBreakdownPeriod::THIRTY_DAYS))
    {
        return EBreakdownPeriod::FIVE_HOUR_WINDOW;
    }
    return static_cast<EBreakdownPeriod>(iValue);
}

void Settings::SetPeriod(EBreakdownPeriod period)
{
    m_kpSettings->setValue(kKeyPeriod, static_cast<int>(period));
}
```

`src/Core/SystemStatus.h`:

```cpp
#pragma once

#include "Model/TokenRecord.h"
#include "Model/UsageLimits.h"

#include <QDateTime>
#include <QString>

enum class ELimitDisplayState
{
    EMPTY,
    NORMAL,
    STALE,
};

class SystemStatus
{
public:
    static constexpr qint64 kStaleAfterSecs = 15 * 60;
    static constexpr qint64 kRefreshOnOpenAfterSecs = 30;

public:
    SystemStatus();
    ~SystemStatus();

public:
    void ApplyFetchResult(const FetchResult& result, const QDateTime& now);
    void ApplyLogSnapshot(const LogSnapshot& snapshot);
    ELimitDisplayState GetLimitDisplayState(const QDateTime& now) const;
    bool IsRefreshDueOnOpen(const QDateTime& now) const;

public:
    bool HasLimits() const;
    const UsageLimits& GetLimits() const;
    QString GetPlanLabel() const;
    EFetchStatus GetLastStatus() const;
    QString GetLastDetail() const;
    QDateTime GetLastAttemptAt() const;
    QDateTime GetLastSuccessAt() const;
    const LogSnapshot& GetLogSnapshot() const;

private:
    bool m_bHasLimits;
    UsageLimits m_limits;
    QString m_strPlanLabel;
    EFetchStatus m_eLastStatus;
    QString m_strLastDetail;
    QDateTime m_dtLastAttemptAt;
    QDateTime m_dtLastSuccessAt;
    LogSnapshot m_logSnapshot;
};
```

`src/Core/SystemStatus.cpp`:

```cpp
#include "Core/SystemStatus.h"

SystemStatus::SystemStatus()
    : m_bHasLimits(false)
    , m_limits()
    , m_strPlanLabel()
    , m_eLastStatus(EFetchStatus::OK)
    , m_strLastDetail()
    , m_dtLastAttemptAt()
    , m_dtLastSuccessAt()
    , m_logSnapshot()
{
}

SystemStatus::~SystemStatus() = default;

void SystemStatus::ApplyFetchResult(const FetchResult& result, const QDateTime& now)
{
    m_dtLastAttemptAt = now;
    m_eLastStatus = result.m_eStatus;
    m_strLastDetail = result.m_strDetail;
    if (!result.m_strPlanLabel.isEmpty())
    {
        m_strPlanLabel = result.m_strPlanLabel;
    }

    switch (result.m_eStatus)
    {
    case EFetchStatus::OK:
        m_bHasLimits = true;
        m_limits = result.m_limits;
        m_dtLastSuccessAt = now;
        break;
    case EFetchStatus::NOT_LOGGED_IN:
    case EFetchStatus::KEYCHAIN_DENIED:
    case EFetchStatus::BAD_RESPONSE:
        m_bHasLimits = false;
        m_limits = UsageLimits();
        break;
    case EFetchStatus::TOKEN_EXPIRED:
    case EFetchStatus::RATE_LIMITED:
    case EFetchStatus::NETWORK_ERROR:
    case EFetchStatus::SERVER_ERROR:
        // Keep the last values; GetLimitDisplayState dims them once they are old.
        break;
    }
}

void SystemStatus::ApplyLogSnapshot(const LogSnapshot& snapshot)
{
    m_logSnapshot = snapshot;
}

ELimitDisplayState SystemStatus::GetLimitDisplayState(const QDateTime& now) const
{
    if (!m_bHasLimits)
    {
        return ELimitDisplayState::EMPTY;
    }
    // Age alone decides, so values also dim after the Mac wakes from sleep.
    if (m_dtLastSuccessAt.secsTo(now) > kStaleAfterSecs)
    {
        return ELimitDisplayState::STALE;
    }
    return ELimitDisplayState::NORMAL;
}

bool SystemStatus::IsRefreshDueOnOpen(const QDateTime& now) const
{
    return !m_dtLastAttemptAt.isValid() || m_dtLastAttemptAt.secsTo(now) > kRefreshOnOpenAfterSecs;
}

bool SystemStatus::HasLimits() const
{
    return m_bHasLimits;
}

const UsageLimits& SystemStatus::GetLimits() const
{
    return m_limits;
}

QString SystemStatus::GetPlanLabel() const
{
    return m_strPlanLabel;
}

EFetchStatus SystemStatus::GetLastStatus() const
{
    return m_eLastStatus;
}

QString SystemStatus::GetLastDetail() const
{
    return m_strLastDetail;
}

QDateTime SystemStatus::GetLastAttemptAt() const
{
    return m_dtLastAttemptAt;
}

QDateTime SystemStatus::GetLastSuccessAt() const
{
    return m_dtLastSuccessAt;
}

const LogSnapshot& SystemStatus::GetLogSnapshot() const
{
    return m_logSnapshot;
}
```

`src/Core/Observers.h`:

```cpp
#pragma once

#include <QObject>

class Observers : public QObject
{
    Q_OBJECT

public:
    explicit Observers(QObject* parent = nullptr)
        : QObject(parent)
    {
    }

signals:
    void LimitsChanged();
    void LogSnapshotChanged();
    void SettingsChanged();
    void RefreshRequested();
    void SettingsWindowRequested();
    void QuitRequested();
};
```

`CMakeLists.txt` 의 `set(TV_CORE_SOURCES ...)` 안에 `src/Core/Settings.cpp`, `src/Core/SystemStatus.cpp`, `src/Core/Observers.h` 추가 (헤더만 있는 QObject 는 AUTOMOC 를 위해 목록에 넣는다).

- [ ] **Step 4: 테스트 통과 확인**

Run: `cmake --build build && ctest --test-dir build --output-on-failure -R "TestSettings|TestSystemStatus"`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt tests/CMakeLists.txt tests/TestSettings.cpp tests/TestSystemStatus.cpp src/Core/Settings.h src/Core/Settings.cpp src/Core/SystemStatus.h src/Core/SystemStatus.cpp src/Core/Observers.h
git commit -m "feat: add settings, system status and observers" -m "Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>"
```

---

### Task 12: 드롭다운 (Formatters + 미터 + 목록 + UsagePopup)

**Files:**
- Create: `src/UI/Formatters.h`, `src/UI/Formatters.cpp`, `src/UI/LimitMeterWidget.h`, `src/UI/LimitMeterWidget.cpp`, `src/UI/BreakdownListWidget.h`, `src/UI/BreakdownListWidget.cpp`, `src/UI/UsagePopup.h`, `src/UI/UsagePopup.cpp`
- Modify: `CMakeLists.txt`, `tests/CMakeLists.txt`
- Test: `tests/TestFormatters.cpp`, `tests/TestUsagePopup.cpp`

**Interfaces:**
- Consumes: Task 1 `TrayIconPainter::ClassifyLevel`, `GetLevelColor`, `EBarLevel`; Task 5 `PriceTable`; Task 7 `TokenAggregator`, `Breakdown`; Task 11 `Observers`, `SystemStatus`, `Settings`, `ELimitDisplayState`
- Produces:
  - `Formatters::FormatUsd(double, bool approximate)`, `FormatTokens(qint64)`, `FormatResetCountdown(const QDateTime& resetsAt, const QDateTime& now)`, `FormatResetDay(...)`, `FormatAge(const QDateTime& past, const QDateTime& now)`, `FormatPercent(double)` — 모두 `QString`
  - `LimitMeterWidget(const QString& name, QWidget* parent)`, `SetValue(const QString& valueText, double percent, const QString& subText, const QColor& fillColor, bool dimmed)`, `GetValueText()`, `GetSubText()`
  - `BreakdownListWidget::SetBreakdown(const Breakdown&, const QColor& fillColor)`, `GetRowCount() -> int`
  - `UsagePopup(Observers& observers, SystemStatus* systemStatus, Settings* settings, const PriceTable* priceTable, QWidget* parent = nullptr)`, `ShowBelow(const QRect& anchor)`, `Refresh()`, `WasJustHidden() const -> bool`
  - objectName: `planBadge`, `fiveHourMeter`, `sevenDayMeter`, `noticeLabel`, `retryButton`, `projectButton`, `modelButton`, `sessionButton`, `breakdownList`, `periodCombo`, `footerLabel`

- [ ] **Step 1: 실패하는 Formatters 테스트**

`tests/TestFormatters.cpp`:

```cpp
#include "UI/Formatters.h"

#include <QtTest>

#include <ctime>

namespace
{
QDateTime MakeUtc(int month, int day, int hour, int minute, int second = 0)
{
    return QDateTime(QDate(2026, month, day), QTime(hour, minute, second), Qt::UTC);
}
}

class TestFormatters : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void TestFormatsUsd();
    void TestFormatsTokens();
    void TestFormatsResetCountdown();
    void TestFormatsResetDay();
    void TestFormatsAge();
    void TestFormatsPercent();
};

void TestFormatters::initTestCase()
{
    qputenv("TZ", "Asia/Seoul");
    tzset();
}

void TestFormatters::TestFormatsUsd()
{
    QCOMPARE(Formatters::FormatUsd(12.4, false), QStringLiteral("$12.40"));
    QCOMPARE(Formatters::FormatUsd(0.004, false), QStringLiteral("<$0.01"));
    QCOMPARE(Formatters::FormatUsd(1234.5, true), QStringLiteral("≈$1,234.50"));
    QCOMPARE(Formatters::FormatUsd(0.0, false), QStringLiteral("$0.00"));
}

void TestFormatters::TestFormatsTokens()
{
    QCOMPARE(Formatters::FormatTokens(512), QStringLiteral("512"));
    QCOMPARE(Formatters::FormatTokens(48200), QStringLiteral("48.2K"));
    QCOMPARE(Formatters::FormatTokens(212000), QStringLiteral("212K"));
    QCOMPARE(Formatters::FormatTokens(1240000), QStringLiteral("1.24M"));
    QCOMPARE(Formatters::FormatTokens(18400000), QStringLiteral("18.4M"));
}

void TestFormatters::TestFormatsResetCountdown()
{
    const QDateTime dtNow = MakeUtc(10, 5, 2, 27);
    QCOMPARE(Formatters::FormatResetCountdown(MakeUtc(10, 5, 4, 40), dtNow), QStringLiteral("2:13 후 리셋"));
    QCOMPARE(Formatters::FormatResetCountdown(MakeUtc(10, 5, 3, 12), dtNow), QStringLiteral("45분 후 리셋"));
    QCOMPARE(Formatters::FormatResetCountdown(MakeUtc(10, 5, 2, 27, 30), dtNow), QStringLiteral("곧 리셋"));
    QCOMPARE(Formatters::FormatResetCountdown(QDateTime(), dtNow), QStringLiteral("리셋 시각 없음"));
}

void TestFormatters::TestFormatsResetDay()
{
    const QDateTime dtNow = MakeUtc(10, 5, 2, 27);
    // 10/8 00:00 UTC is Thursday 09:00 in Seoul.
    QCOMPARE(Formatters::FormatResetDay(MakeUtc(10, 8, 0, 0), dtNow), QStringLiteral("목 9:00 리셋"));
    QCOMPARE(Formatters::FormatResetDay(MakeUtc(10, 5, 4, 40), dtNow), QStringLiteral("2:13 후 리셋"));
}

void TestFormatters::TestFormatsAge()
{
    const QDateTime dtNow = MakeUtc(10, 5, 12, 0);
    QCOMPARE(Formatters::FormatAge(dtNow.addSecs(-30), dtNow), QStringLiteral("방금"));
    QCOMPARE(Formatters::FormatAge(dtNow.addSecs(-18 * 60), dtNow), QStringLiteral("18분 전"));
    QCOMPARE(Formatters::FormatAge(dtNow.addSecs(-125 * 60), dtNow), QStringLiteral("2시간 전"));
}

void TestFormatters::TestFormatsPercent()
{
    QCOMPARE(Formatters::FormatPercent(42.4), QStringLiteral("42%"));
    QCOMPARE(Formatters::FormatPercent(130.0), QStringLiteral("100%"));
}

QTEST_GUILESS_MAIN(TestFormatters)
#include "TestFormatters.moc"
```

`tests/CMakeLists.txt` 끝에 `tv_add_test(TestFormatters)` 추가.

Run: `cmake --build build`
Expected: FAIL — `UI/Formatters.h` file not found

- [ ] **Step 2: Formatters 구현**

`src/UI/Formatters.h`:

```cpp
#pragma once

#include <QDateTime>
#include <QString>

class Formatters
{
public:
    static QString FormatUsd(double usd, bool approximate);
    static QString FormatTokens(qint64 tokens);
    static QString FormatResetCountdown(const QDateTime& resetsAt, const QDateTime& now);
    static QString FormatResetDay(const QDateTime& resetsAt, const QDateTime& now);
    static QString FormatAge(const QDateTime& past, const QDateTime& now);
    static QString FormatPercent(double percent);
};
```

`src/UI/Formatters.cpp`:

```cpp
#include "UI/Formatters.h"

#include <QLocale>

#include <algorithm>

namespace
{
constexpr qint64 kSecsPerMinute = 60;
constexpr qint64 kSecsPerHour = 60 * 60;
constexpr qint64 kSecsPerDay = 24 * 60 * 60;
constexpr qint64 kThousand = 1000;
constexpr qint64 kHundredThousand = 100000;
constexpr qint64 kMillion = 1000000;
constexpr qint64 kTenMillion = 10000000;
constexpr double kSmallestCent = 0.005;
const QString kNoReset = QStringLiteral("리셋 시각 없음");
}

QString Formatters::FormatUsd(double usd, bool approximate)
{
    const QString strPrefix = approximate ? QStringLiteral("≈") : QString();
    if (usd > 0.0 && usd < kSmallestCent)
    {
        return strPrefix + QStringLiteral("<$0.01");
    }
    return strPrefix + QLatin1Char('$') + QLocale(QLocale::English).toString(usd, 'f', 2);
}

QString Formatters::FormatTokens(qint64 tokens)
{
    if (tokens < kThousand)
    {
        return QString::number(tokens);
    }
    if (tokens < kMillion)
    {
        return QString::number(static_cast<double>(tokens) / kThousand, 'f', tokens < kHundredThousand ? 1 : 0) + QLatin1Char('K');
    }
    return QString::number(static_cast<double>(tokens) / kMillion, 'f', tokens < kTenMillion ? 2 : 1) + QLatin1Char('M');
}

QString Formatters::FormatResetCountdown(const QDateTime& resetsAt, const QDateTime& now)
{
    if (!resetsAt.isValid())
    {
        return kNoReset;
    }
    const qint64 llSecs = now.secsTo(resetsAt);
    if (llSecs <= kSecsPerMinute)
    {
        return QStringLiteral("곧 리셋");
    }
    const qint64 llHours = llSecs / kSecsPerHour;
    const qint64 llMinutes = (llSecs % kSecsPerHour) / kSecsPerMinute;
    if (llHours > 0)
    {
        return QStringLiteral("%1:%2 후 리셋").arg(llHours).arg(llMinutes, 2, 10, QLatin1Char('0'));
    }
    return QStringLiteral("%1분 후 리셋").arg(llMinutes);
}

QString Formatters::FormatResetDay(const QDateTime& resetsAt, const QDateTime& now)
{
    if (!resetsAt.isValid())
    {
        return kNoReset;
    }
    if (now.secsTo(resetsAt) < kSecsPerDay)
    {
        return FormatResetCountdown(resetsAt, now);
    }
    return QLocale(QLocale::Korean).toString(resetsAt.toLocalTime(), QStringLiteral("ddd H:mm")) + QStringLiteral(" 리셋");
}

QString Formatters::FormatAge(const QDateTime& past, const QDateTime& now)
{
    if (!past.isValid())
    {
        return QString();
    }
    const qint64 llSecs = std::max<qint64>(0, past.secsTo(now));
    if (llSecs < kSecsPerMinute)
    {
        return QStringLiteral("방금");
    }
    if (llSecs < kSecsPerHour)
    {
        return QStringLiteral("%1분 전").arg(llSecs / kSecsPerMinute);
    }
    return QStringLiteral("%1시간 전").arg(llSecs / kSecsPerHour);
}

QString Formatters::FormatPercent(double percent)
{
    return QString::number(qRound(std::clamp(percent, 0.0, 100.0))) + QLatin1Char('%');
}
```

`CMakeLists.txt` 의 `set(TV_CORE_SOURCES ...)` 안에 `src/UI/Formatters.cpp` 추가.

Run: `cmake --build build && ctest --test-dir build --output-on-failure -R TestFormatters`
Expected: PASS

- [ ] **Step 3: 실패하는 UsagePopup 테스트**

`tests/TestUsagePopup.cpp`:

```cpp
#include "UI/UsagePopup.h"

#include "Analysis/PriceTable.h"
#include "Core/Observers.h"
#include "Core/Settings.h"
#include "Core/SystemStatus.h"
#include "TestHelpers.h"
#include "UI/BreakdownListWidget.h"
#include "UI/LimitMeterWidget.h"

#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>

namespace
{
TokenRecord MakeRecentRecord(const QString& key, const QString& projectPath)
{
    TokenRecord record;
    record.m_strKey = key;
    record.m_dtTimestamp = QDateTime::currentDateTimeUtc().addSecs(-3600);
    record.m_strSessionId = QStringLiteral("s1");
    record.m_strProjectPath = projectPath;
    record.m_strModel = QStringLiteral("claude-opus-5-5");
    record.m_llOutput = 100000;
    return record;
}
}

class TestUsagePopup : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();
    void TestShowsMetersWhenLimitsArrive();
    void TestShowsLoginNoticeWhenNotLoggedIn();
    void TestListsBreakdownRowsAndEmptyState();
    void TestSegmentClickChangesDimension();

private:
    std::unique_ptr<QTemporaryDir> m_upDir;
    std::unique_ptr<QSettings> m_upStoredSettings;
    std::unique_ptr<Settings> m_upSettings;
    std::unique_ptr<SystemStatus> m_upStatus;
    std::unique_ptr<Observers> m_upObservers;
    PriceTable m_priceTable;
};

void TestUsagePopup::init()
{
    m_upDir = std::make_unique<QTemporaryDir>();
    m_upStoredSettings = std::make_unique<QSettings>(m_upDir->filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);
    m_upSettings = std::make_unique<Settings>(m_upStoredSettings.get());
    m_upStatus = std::make_unique<SystemStatus>();
    m_upObservers = std::make_unique<Observers>();
    QVERIFY(m_priceTable.LoadFromJson(ReadSourceFile(QStringLiteral("resources/prices.json"))));
}

void TestUsagePopup::cleanup()
{
    m_upObservers.reset();
    m_upStatus.reset();
    m_upSettings.reset();
    m_upStoredSettings.reset();
    m_upDir.reset();
}

void TestUsagePopup::TestShowsMetersWhenLimitsArrive()
{
    const QDateTime dtNow = QDateTime::currentDateTimeUtc();
    FetchResult result;
    result.m_eStatus = EFetchStatus::OK;
    result.m_limits.m_fiveHour.m_bValid = true;
    result.m_limits.m_fiveHour.m_dPercent = 42.0;
    result.m_limits.m_fiveHour.m_dtResetsAt = dtNow.addSecs(2 * 3600 + 13 * 60 + 30);
    result.m_limits.m_sevenDay.m_bValid = true;
    result.m_limits.m_sevenDay.m_dPercent = 18.0;
    result.m_strPlanLabel = QStringLiteral("Max 5x");
    m_upStatus->ApplyFetchResult(result, dtNow);

    UsagePopup popup(*m_upObservers, m_upStatus.get(), m_upSettings.get(), &m_priceTable);
    popup.Refresh();

    LimitMeterWidget* pMeter = popup.findChild<LimitMeterWidget*>(QStringLiteral("fiveHourMeter"));
    QVERIFY(pMeter != nullptr);
    QCOMPARE(pMeter->GetValueText(), QStringLiteral("42%"));
    QCOMPARE(pMeter->GetSubText(), QStringLiteral("2:13 후 리셋"));
    QVERIFY(pMeter->isVisibleTo(&popup));
    QVERIFY(popup.findChild<QLabel*>(QStringLiteral("noticeLabel"))->isHidden());
    QCOMPARE(popup.findChild<QLabel*>(QStringLiteral("planBadge"))->text(), QStringLiteral("Max 5x"));
}

void TestUsagePopup::TestShowsLoginNoticeWhenNotLoggedIn()
{
    FetchResult result;
    result.m_eStatus = EFetchStatus::NOT_LOGGED_IN;
    m_upStatus->ApplyFetchResult(result, QDateTime::currentDateTimeUtc());

    UsagePopup popup(*m_upObservers, m_upStatus.get(), m_upSettings.get(), &m_priceTable);
    popup.Refresh();

    QLabel* pNotice = popup.findChild<QLabel*>(QStringLiteral("noticeLabel"));
    QVERIFY(!pNotice->isHidden());
    QVERIFY(pNotice->text().contains(QStringLiteral("/login")));
    QVERIFY(!popup.findChild<QPushButton*>(QStringLiteral("retryButton"))->isHidden());
    QVERIFY(!popup.findChild<LimitMeterWidget*>(QStringLiteral("fiveHourMeter"))->isVisibleTo(&popup));
}

void TestUsagePopup::TestListsBreakdownRowsAndEmptyState()
{
    UsagePopup popup(*m_upObservers, m_upStatus.get(), m_upSettings.get(), &m_priceTable);
    popup.Refresh();
    BreakdownListWidget* pList = popup.findChild<BreakdownListWidget*>(QStringLiteral("breakdownList"));
    QCOMPARE(pList->GetRowCount(), 0);

    LogSnapshot snapshot;
    snapshot.m_vecRecords = { MakeRecentRecord(QStringLiteral("k1"), QStringLiteral("/Users/me/A")),
        MakeRecentRecord(QStringLiteral("k2"), QStringLiteral("/Users/me/B")) };
    m_upStatus->ApplyLogSnapshot(snapshot);
    popup.Refresh();
    QCOMPARE(pList->GetRowCount(), 2);
}

void TestUsagePopup::TestSegmentClickChangesDimension()
{
    UsagePopup popup(*m_upObservers, m_upStatus.get(), m_upSettings.get(), &m_priceTable);
    QPushButton* pModelButton = popup.findChild<QPushButton*>(QStringLiteral("modelButton"));
    QTest::mouseClick(pModelButton, Qt::LeftButton);
    QCOMPARE(m_upSettings->GetDimension(), EBreakdownDimension::MODEL);
    QVERIFY(pModelButton->isChecked());
}

QTEST_MAIN(TestUsagePopup)
#include "TestUsagePopup.moc"
```

`tests/CMakeLists.txt` 끝에 `tv_add_test(TestUsagePopup)` 추가.

Run: `cmake --build build`
Expected: FAIL — `UI/UsagePopup.h` file not found

- [ ] **Step 4: LimitMeterWidget 구현**

`src/UI/LimitMeterWidget.h`:

```cpp
#pragma once

#include <QColor>
#include <QString>
#include <QWidget>

class LimitMeterWidget : public QWidget
{
    Q_OBJECT

public:
    explicit LimitMeterWidget(const QString& name, QWidget* parent = nullptr);
    ~LimitMeterWidget();

public:
    void SetValue(const QString& valueText, double percent, const QString& subText, const QColor& fillColor, bool dimmed);
    QSize sizeHint() const override;

public:
    QString GetValueText() const;
    QString GetSubText() const;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QString m_strName;
    QString m_strValueText;
    double m_dPercent;
    QString m_strSubText;
    QColor m_clrFill;
    bool m_bDimmed;
};
```

`src/UI/LimitMeterWidget.cpp`:

```cpp
#include "UI/LimitMeterWidget.h"

#include <QPainter>

#include <algorithm>

namespace
{
constexpr int kWidgetWidth = 128;
constexpr int kWidgetHeight = 50;
constexpr int kNamePixelSize = 12;
constexpr int kValuePixelSize = 15;
constexpr int kSubPixelSize = 11;
constexpr double kHeaderHeight = 20.0;
constexpr double kBarTop = 24.0;
constexpr double kBarHeight = 6.0;
constexpr double kBarRadius = 3.0;
constexpr double kSubGap = 4.0;
constexpr double kSubHeight = 16.0;
constexpr double kSecondaryAlpha = 0.7;
constexpr double kMutedAlpha = 0.5;
constexpr double kTrackAlpha = 0.12;
constexpr double kDimmedFillAlpha = 0.45;
}

LimitMeterWidget::LimitMeterWidget(const QString& name, QWidget* parent)
    : QWidget(parent)
    , m_strName(name)
    , m_strValueText(QStringLiteral("—"))
    , m_dPercent(0.0)
    , m_strSubText()
    , m_clrFill()
    , m_bDimmed(false)
{
    setMinimumHeight(kWidgetHeight);
}

LimitMeterWidget::~LimitMeterWidget() = default;

void LimitMeterWidget::SetValue(const QString& valueText, double percent, const QString& subText, const QColor& fillColor, bool dimmed)
{
    m_strValueText = valueText;
    m_dPercent = percent;
    m_strSubText = subText;
    m_clrFill = fillColor;
    m_bDimmed = dimmed;
    update();
}

QSize LimitMeterWidget::sizeHint() const
{
    return QSize(kWidgetWidth, kWidgetHeight);
}

QString LimitMeterWidget::GetValueText() const
{
    return m_strValueText;
}

QString LimitMeterWidget::GetSubText() const
{
    return m_strSubText;
}

void LimitMeterWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QColor clrText = palette().color(QPalette::WindowText);
    QColor clrSecondary = clrText;
    clrSecondary.setAlphaF(kSecondaryAlpha);
    QColor clrMuted = clrText;
    clrMuted.setAlphaF(kMutedAlpha);
    QColor clrTrack = clrText;
    clrTrack.setAlphaF(kTrackAlpha);
    const double dWidth = width();

    QFont fontName = font();
    fontName.setPixelSize(kNamePixelSize);
    painter.setFont(fontName);
    painter.setPen(clrSecondary);
    painter.drawText(QRectF(0.0, 0.0, dWidth, kHeaderHeight), Qt::AlignLeft | Qt::AlignVCenter, m_strName);

    QFont fontValue = font();
    fontValue.setPixelSize(kValuePixelSize);
    fontValue.setBold(true);
    painter.setFont(fontValue);
    painter.setPen(m_bDimmed ? clrSecondary : clrText);
    painter.drawText(QRectF(0.0, 0.0, dWidth, kHeaderHeight), Qt::AlignRight | Qt::AlignVCenter, m_strValueText);

    painter.setPen(Qt::NoPen);
    painter.setBrush(clrTrack);
    painter.drawRoundedRect(QRectF(0.0, kBarTop, dWidth, kBarHeight), kBarRadius, kBarRadius);
    const double dFillWidth = dWidth * std::clamp(m_dPercent, 0.0, 100.0) / 100.0;
    if (dFillWidth > 0.0)
    {
        QColor clrFill = m_clrFill;
        if (m_bDimmed)
        {
            clrFill.setAlphaF(kDimmedFillAlpha);
        }
        painter.setBrush(clrFill);
        painter.drawRoundedRect(QRectF(0.0, kBarTop, std::max(dFillWidth, kBarHeight), kBarHeight), kBarRadius, kBarRadius);
    }

    QFont fontSub = font();
    fontSub.setPixelSize(kSubPixelSize);
    painter.setFont(fontSub);
    painter.setPen(clrMuted);
    painter.drawText(QRectF(0.0, kBarTop + kBarHeight + kSubGap, dWidth, kSubHeight), Qt::AlignLeft | Qt::AlignVCenter, m_strSubText);
}
```

- [ ] **Step 5: BreakdownListWidget 구현**

`src/UI/BreakdownListWidget.h`:

```cpp
#pragma once

#include "Model/Breakdown.h"

#include <QColor>
#include <QList>
#include <QWidget>

class QLabel;
class QVBoxLayout;

class BreakdownListWidget : public QWidget
{
    Q_OBJECT

public:
    explicit BreakdownListWidget(QWidget* parent = nullptr);
    ~BreakdownListWidget();

public:
    void SetBreakdown(const Breakdown& breakdown, const QColor& fillColor);

public:
    int GetRowCount() const;

private:
    QVBoxLayout* m_pLayout;
    QLabel* m_pEmptyLabel;
    QList<QWidget*> m_lstRows;
};
```

`src/UI/BreakdownListWidget.cpp`:

```cpp
#include "UI/BreakdownListWidget.h"

#include "UI/Formatters.h"

#include <QFontMetricsF>
#include <QLabel>
#include <QPainter>
#include <QVBoxLayout>

#include <algorithm>

namespace
{
constexpr int kRowHeight = 28;
constexpr int kRowPixelSize = 12;
constexpr int kRowSpacing = 2;
constexpr double kRowBarHeight = 4.0;
constexpr double kRowBarRadius = 2.0;
constexpr double kRowBarBottomGap = 3.0;
constexpr double kTextBottomGap = 8.0;
constexpr double kAmountGap = 8.0;
constexpr double kSecondaryAlpha = 0.7;
constexpr double kTrackAlpha = 0.12;

QString FormatAmount(const BreakdownRow& row)
{
    if (row.m_bUnpriced)
    {
        const qint64 llTotal = row.m_llInput + row.m_llOutput + row.m_llCacheWrite + row.m_llCacheRead;
        return Formatters::FormatTokens(llTotal) + QStringLiteral(" 토큰");
    }
    return Formatters::FormatUsd(row.m_dCostUsd, row.m_bApproximate);
}

QString BuildToolTip(const BreakdownRow& row)
{
    return QStringLiteral("%1\n입력 %2 · 출력 %3\n캐시 쓰기 %4 · 캐시 읽기 %5")
        .arg(row.m_strDetail, Formatters::FormatTokens(row.m_llInput), Formatters::FormatTokens(row.m_llOutput),
            Formatters::FormatTokens(row.m_llCacheWrite), Formatters::FormatTokens(row.m_llCacheRead));
}

class BreakdownRowWidget : public QWidget
{
public:
    BreakdownRowWidget(const BreakdownRow& row, double ratio, const QColor& fillColor, QWidget* parent)
        : QWidget(parent)
        , m_strLabel(row.m_strLabel)
        , m_strAmount(FormatAmount(row))
        , m_dRatio(ratio)
        , m_clrFill(fillColor)
    {
        setFixedHeight(kRowHeight);
        setToolTip(BuildToolTip(row));
    }

protected:
    void paintEvent(QPaintEvent* event) override
    {
        Q_UNUSED(event);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const QColor clrText = palette().color(QPalette::WindowText);
        QColor clrSecondary = clrText;
        clrSecondary.setAlphaF(kSecondaryAlpha);
        QColor clrTrack = clrText;
        clrTrack.setAlphaF(kTrackAlpha);

        QFont fontRow = font();
        fontRow.setPixelSize(kRowPixelSize);
        painter.setFont(fontRow);
        const QFontMetricsF metrics(fontRow);
        const double dAmountWidth = metrics.horizontalAdvance(m_strAmount) + kAmountGap;
        const QRectF rcText(0.0, 0.0, width(), kRowHeight - kTextBottomGap);
        painter.setPen(clrText);
        painter.drawText(rcText.adjusted(0.0, 0.0, -dAmountWidth, 0.0), Qt::AlignLeft | Qt::AlignVCenter,
            metrics.elidedText(m_strLabel, Qt::ElideMiddle, width() - dAmountWidth));
        painter.setPen(clrSecondary);
        painter.drawText(rcText, Qt::AlignRight | Qt::AlignVCenter, m_strAmount);

        const double dBarTop = kRowHeight - kRowBarHeight - kRowBarBottomGap;
        painter.setPen(Qt::NoPen);
        painter.setBrush(clrTrack);
        painter.drawRoundedRect(QRectF(0.0, dBarTop, width(), kRowBarHeight), kRowBarRadius, kRowBarRadius);
        if (m_dRatio > 0.0)
        {
            painter.setBrush(m_clrFill);
            painter.drawRoundedRect(QRectF(0.0, dBarTop, std::max(width() * m_dRatio, kRowBarHeight), kRowBarHeight), kRowBarRadius, kRowBarRadius);
        }
    }

private:
    QString m_strLabel;
    QString m_strAmount;
    double m_dRatio;
    QColor m_clrFill;
};
}

BreakdownListWidget::BreakdownListWidget(QWidget* parent)
    : QWidget(parent)
    , m_pLayout(new QVBoxLayout(this))
    , m_pEmptyLabel(new QLabel(QStringLiteral("이 기간에 기록이 없습니다"), this))
    , m_lstRows()
{
    m_pLayout->setContentsMargins(0, 0, 0, 0);
    m_pLayout->setSpacing(kRowSpacing);
    m_pEmptyLabel->setAlignment(Qt::AlignCenter);
    m_pLayout->addWidget(m_pEmptyLabel);
}

BreakdownListWidget::~BreakdownListWidget() = default;

void BreakdownListWidget::SetBreakdown(const Breakdown& breakdown, const QColor& fillColor)
{
    for (QWidget* pRow : m_lstRows)
    {
        m_pLayout->removeWidget(pRow);
        pRow->hide();
        pRow->deleteLater();
    }
    m_lstRows.clear();

    QVector<BreakdownRow> vecRows = breakdown.m_vecRows;
    if (breakdown.m_iOtherCount > 0)
    {
        vecRows.append(breakdown.m_otherRow);
    }
    const double dMaxCost = breakdown.m_vecRows.isEmpty() ? 0.0 : breakdown.m_vecRows.first().m_dCostUsd;
    for (const BreakdownRow& row : vecRows)
    {
        const double dRatio = dMaxCost > 0.0 ? std::min(row.m_dCostUsd / dMaxCost, 1.0) : 0.0;
        QWidget* pRow = new BreakdownRowWidget(row, dRatio, fillColor, this);
        m_pLayout->addWidget(pRow);
        m_lstRows.append(pRow);
    }
    m_pEmptyLabel->setVisible(m_lstRows.isEmpty());
}

int BreakdownListWidget::GetRowCount() const
{
    return m_lstRows.size();
}
```

- [ ] **Step 6: UsagePopup 구현**

`src/UI/UsagePopup.h`:

```cpp
#pragma once

#include "Model/Breakdown.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QTimer>
#include <QWidget>

class BreakdownListWidget;
class LimitMeterWidget;
class Observers;
class PriceTable;
class QButtonGroup;
class QComboBox;
class QLabel;
class QPushButton;
class QToolButton;
class Settings;
class SystemStatus;

class UsagePopup : public QWidget
{
    Q_OBJECT

public:
    UsagePopup(Observers& observers, SystemStatus* systemStatus, Settings* settings, const PriceTable* priceTable, QWidget* parent = nullptr);
    ~UsagePopup();

public:
    void ShowBelow(const QRect& anchor);
    void Refresh();
    bool WasJustHidden() const;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    void BuildUi();
    QPushButton* CreateSegmentButton(const QString& text, const QString& objectName);
    void SelectDimension(EBreakdownDimension dimension);
    void RefreshLimits(const QDateTime& now);
    void RefreshBreakdown(const QDateTime& now);
    void RefreshFooter(const QDateTime& now);
    QString BuildNoticeText(const QDateTime& now) const;
    bool IsDarkAppearance() const;

private:
    Observers& m_observers;
    SystemStatus* m_kpSystemStatus;
    Settings* m_kpSettings;
    const PriceTable* m_kpPriceTable;
    QLabel* m_pPlanBadge;
    QWidget* m_pMetersRow;
    LimitMeterWidget* m_pFiveHourMeter;
    LimitMeterWidget* m_pSevenDayMeter;
    QLabel* m_pNoticeLabel;
    QPushButton* m_pRetryButton;
    QButtonGroup* m_pSegmentGroup;
    QPushButton* m_pProjectButton;
    QPushButton* m_pModelButton;
    QPushButton* m_pSessionButton;
    BreakdownListWidget* m_pBreakdownList;
    QComboBox* m_pPeriodCombo;
    QLabel* m_pFooterLabel;
    QToolButton* m_pRefreshButton;
    QToolButton* m_pSettingsButton;
    QToolButton* m_pQuitButton;
    QTimer m_timerAge;
    QElapsedTimer m_timerSinceHide;
};
```

`src/UI/UsagePopup.cpp`:

```cpp
#include "UI/UsagePopup.h"

#include "Analysis/PriceTable.h"
#include "Analysis/TokenAggregator.h"
#include "Core/Observers.h"
#include "Core/Settings.h"
#include "Core/SystemStatus.h"
#include "UI/BreakdownListWidget.h"
#include "UI/Formatters.h"
#include "UI/LimitMeterWidget.h"
#include "UI/TrayIconPainter.h"

#include <QButtonGroup>
#include <QComboBox>
#include <QEvent>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QScreen>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

namespace
{
constexpr int kPopupWidth = 300;
constexpr int kPopupGap = 4;
constexpr int kScreenMargin = 8;
constexpr int kReopenGuardMs = 300;
constexpr int kAgeRefreshMs = 30000;
constexpr int kMarginLeft = 14;
constexpr int kMarginTop = 11;
constexpr int kMarginRight = 14;
constexpr int kMarginBottom = 10;
constexpr int kSectionSpacing = 8;
constexpr int kMeterSpacing = 14;
constexpr int kSegmentSpacing = 2;
constexpr int kTitlePixelSize = 13;
constexpr int kSmallPixelSize = 11;
constexpr int kDarkWindowLightness = 128;
constexpr double kCornerRadius = 12.0;
constexpr double kBorderAlpha = 0.12;

const QColor kBlueLight(0x2a, 0x78, 0xd6);
const QColor kBlueDark(0x39, 0x87, 0xe5);
const QColor kFailRed(0xd0, 0x3b, 0x3b);

const char* const kPopupStyleSheet = R"(
QPushButton[segment="true"] { border: none; border-radius: 5px; padding: 3px 0px; font-size: 11px; background: transparent; }
QPushButton[segment="true"]:checked { background: rgba(127, 127, 127, 0.25); font-weight: 600; }
QToolButton { border: none; padding: 2px 4px; font-size: 12px; }
QComboBox { font-size: 11px; }
QLabel#planBadge { border: 1px solid rgba(127, 127, 127, 0.4); border-radius: 5px; padding: 0px 5px; font-size: 11px; }
QLabel#noticeLabel { background: rgba(127, 127, 127, 0.12); border-radius: 8px; padding: 8px; font-size: 12px; }
)";

QColor GetMeterColor(EBarLevel level, bool dark)
{
    if (level == EBarLevel::NORMAL)
    {
        return dark ? kBlueDark : kBlueLight;
    }
    return TrayIconPainter::GetLevelColor(level, dark ? EMenuBarAppearance::DARK : EMenuBarAppearance::LIGHT);
}

QFrame* CreateSeparator(QWidget* parent)
{
    QFrame* pSeparator = new QFrame(parent);
    pSeparator->setFrameShape(QFrame::HLine);
    pSeparator->setFrameShadow(QFrame::Plain);
    return pSeparator;
}
}

UsagePopup::UsagePopup(Observers& observers, SystemStatus* systemStatus, Settings* settings, const PriceTable* priceTable, QWidget* parent)
    : QWidget(parent, Qt::Popup | Qt::FramelessWindowHint)
    , m_observers(observers)
    , m_kpSystemStatus(systemStatus)
    , m_kpSettings(settings)
    , m_kpPriceTable(priceTable)
    , m_pPlanBadge(nullptr)
    , m_pMetersRow(nullptr)
    , m_pFiveHourMeter(nullptr)
    , m_pSevenDayMeter(nullptr)
    , m_pNoticeLabel(nullptr)
    , m_pRetryButton(nullptr)
    , m_pSegmentGroup(nullptr)
    , m_pProjectButton(nullptr)
    , m_pModelButton(nullptr)
    , m_pSessionButton(nullptr)
    , m_pBreakdownList(nullptr)
    , m_pPeriodCombo(nullptr)
    , m_pFooterLabel(nullptr)
    , m_pRefreshButton(nullptr)
    , m_pSettingsButton(nullptr)
    , m_pQuitButton(nullptr)
    , m_timerAge()
    , m_timerSinceHide()
{
    setAttribute(Qt::WA_TranslucentBackground, true);
    setFixedWidth(kPopupWidth);
    setStyleSheet(QString::fromUtf8(kPopupStyleSheet));
    BuildUi();

    connect(&m_observers, &Observers::LimitsChanged, this, [this]()
    {
        if (isVisible())
        {
            Refresh();
        }
    });
    connect(&m_observers, &Observers::LogSnapshotChanged, this, [this]()
    {
        if (isVisible())
        {
            Refresh();
        }
    });
    connect(&m_timerAge, &QTimer::timeout, this, &UsagePopup::Refresh);
    connect(m_pPeriodCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index)
    {
        m_kpSettings->SetPeriod(static_cast<EBreakdownPeriod>(index));
        RefreshBreakdown(QDateTime::currentDateTimeUtc());
    });
}

UsagePopup::~UsagePopup() = default;

void UsagePopup::ShowBelow(const QRect& anchor)
{
    adjustSize();
    QScreen* pScreen = QGuiApplication::screenAt(anchor.center());
    if (pScreen == nullptr)
    {
        pScreen = QGuiApplication::primaryScreen();
    }
    const QRect rcAvailable = pScreen->availableGeometry();
    const int iMinX = rcAvailable.left() + kScreenMargin;
    const int iMaxX = std::max(iMinX, rcAvailable.right() - width() - kScreenMargin);
    const int iWantedX = anchor.isValid() ? anchor.center().x() - width() / 2 : iMaxX;
    const int iY = anchor.isValid() ? anchor.bottom() + kPopupGap : rcAvailable.top() + kPopupGap;
    move(std::clamp(iWantedX, iMinX, iMaxX), iY);
    show();
    raise();
    activateWindow();
}

void UsagePopup::Refresh()
{
    const QDateTime dtNow = QDateTime::currentDateTimeUtc();
    RefreshLimits(dtNow);
    RefreshBreakdown(dtNow);
    RefreshFooter(dtNow);
}

bool UsagePopup::WasJustHidden() const
{
    return m_timerSinceHide.isValid() && m_timerSinceHide.elapsed() < kReopenGuardMs;
}

bool UsagePopup::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::MouseButtonRelease)
    {
        if (watched == m_pRefreshButton || watched == m_pRetryButton)
        {
            emit m_observers.RefreshRequested();
        }
        else if (watched == m_pSettingsButton)
        {
            hide();
            emit m_observers.SettingsWindowRequested();
        }
        else if (watched == m_pQuitButton)
        {
            emit m_observers.QuitRequested();
        }
        else if (watched == m_pProjectButton)
        {
            SelectDimension(EBreakdownDimension::PROJECT);
        }
        else if (watched == m_pModelButton)
        {
            SelectDimension(EBreakdownDimension::MODEL);
        }
        else if (watched == m_pSessionButton)
        {
            SelectDimension(EBreakdownDimension::SESSION);
        }
    }
    return QWidget::eventFilter(watched, event);
}

void UsagePopup::showEvent(QShowEvent* event)
{
    m_timerAge.start(kAgeRefreshMs);
    QWidget::showEvent(event);
}

void UsagePopup::hideEvent(QHideEvent* event)
{
    // Qt::Popup closes itself on an outside click; that same click may also reach the tray icon.
    m_timerSinceHide.start();
    m_timerAge.stop();
    QWidget::hideEvent(event);
}

void UsagePopup::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QColor clrBorder = palette().color(QPalette::WindowText);
    clrBorder.setAlphaF(kBorderAlpha);
    painter.setPen(QPen(clrBorder, 1.0));
    painter.setBrush(palette().color(QPalette::Window));
    painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), kCornerRadius, kCornerRadius);
}

void UsagePopup::BuildUi()
{
    QVBoxLayout* pRoot = new QVBoxLayout(this);
    pRoot->setContentsMargins(kMarginLeft, kMarginTop, kMarginRight, kMarginBottom);
    pRoot->setSpacing(kSectionSpacing);

    QHBoxLayout* pHeader = new QHBoxLayout();
    QLabel* pTitle = new QLabel(QStringLiteral("Claude 사용량"), this);
    QFont fontTitle = pTitle->font();
    fontTitle.setPixelSize(kTitlePixelSize);
    fontTitle.setBold(true);
    pTitle->setFont(fontTitle);
    m_pPlanBadge = new QLabel(this);
    m_pPlanBadge->setObjectName(QStringLiteral("planBadge"));
    pHeader->addWidget(pTitle);
    pHeader->addWidget(m_pPlanBadge);
    pHeader->addStretch(1);
    pRoot->addLayout(pHeader);

    m_pMetersRow = new QWidget(this);
    QHBoxLayout* pMeters = new QHBoxLayout(m_pMetersRow);
    pMeters->setContentsMargins(0, 0, 0, 0);
    pMeters->setSpacing(kMeterSpacing);
    m_pFiveHourMeter = new LimitMeterWidget(QStringLiteral("5시간"), m_pMetersRow);
    m_pFiveHourMeter->setObjectName(QStringLiteral("fiveHourMeter"));
    m_pSevenDayMeter = new LimitMeterWidget(QStringLiteral("주간"), m_pMetersRow);
    m_pSevenDayMeter->setObjectName(QStringLiteral("sevenDayMeter"));
    pMeters->addWidget(m_pFiveHourMeter, 1);
    pMeters->addWidget(m_pSevenDayMeter, 1);
    pRoot->addWidget(m_pMetersRow);

    m_pNoticeLabel = new QLabel(this);
    m_pNoticeLabel->setObjectName(QStringLiteral("noticeLabel"));
    m_pNoticeLabel->setWordWrap(true);
    pRoot->addWidget(m_pNoticeLabel);

    m_pRetryButton = new QPushButton(QStringLiteral("다시 확인"), this);
    m_pRetryButton->setObjectName(QStringLiteral("retryButton"));
    m_pRetryButton->installEventFilter(this);
    pRoot->addWidget(m_pRetryButton, 0, Qt::AlignLeft);

    pRoot->addWidget(CreateSeparator(this));

    QHBoxLayout* pSegments = new QHBoxLayout();
    pSegments->setSpacing(kSegmentSpacing);
    m_pSegmentGroup = new QButtonGroup(this);
    m_pSegmentGroup->setExclusive(true);
    m_pProjectButton = CreateSegmentButton(QStringLiteral("프로젝트"), QStringLiteral("projectButton"));
    m_pModelButton = CreateSegmentButton(QStringLiteral("모델"), QStringLiteral("modelButton"));
    m_pSessionButton = CreateSegmentButton(QStringLiteral("세션"), QStringLiteral("sessionButton"));
    pSegments->addWidget(m_pProjectButton);
    pSegments->addWidget(m_pModelButton);
    pSegments->addWidget(m_pSessionButton);
    pRoot->addLayout(pSegments);

    m_pBreakdownList = new BreakdownListWidget(this);
    m_pBreakdownList->setObjectName(QStringLiteral("breakdownList"));
    pRoot->addWidget(m_pBreakdownList);

    m_pPeriodCombo = new QComboBox(this);
    m_pPeriodCombo->setObjectName(QStringLiteral("periodCombo"));
    m_pPeriodCombo->addItems({ QStringLiteral("이번 5시간 창"), QStringLiteral("오늘"), QStringLiteral("7일"), QStringLiteral("30일") });
    m_pPeriodCombo->setCurrentIndex(static_cast<int>(m_kpSettings->GetPeriod()));
    pRoot->addWidget(m_pPeriodCombo, 0, Qt::AlignLeft);

    pRoot->addWidget(CreateSeparator(this));

    QHBoxLayout* pFooter = new QHBoxLayout();
    m_pFooterLabel = new QLabel(this);
    m_pFooterLabel->setObjectName(QStringLiteral("footerLabel"));
    QFont fontSmall = m_pFooterLabel->font();
    fontSmall.setPixelSize(kSmallPixelSize);
    m_pFooterLabel->setFont(fontSmall);
    m_pRefreshButton = new QToolButton(this);
    m_pRefreshButton->setText(QStringLiteral("↻"));
    m_pRefreshButton->setToolTip(QStringLiteral("새로고침"));
    m_pSettingsButton = new QToolButton(this);
    m_pSettingsButton->setText(QStringLiteral("⚙"));
    m_pSettingsButton->setToolTip(QStringLiteral("설정"));
    m_pQuitButton = new QToolButton(this);
    m_pQuitButton->setText(QStringLiteral("종료"));
    pFooter->addWidget(m_pFooterLabel);
    pFooter->addStretch(1);
    for (QToolButton* pButton : { m_pRefreshButton, m_pSettingsButton, m_pQuitButton })
    {
        pButton->installEventFilter(this);
        pFooter->addWidget(pButton);
    }
    pRoot->addLayout(pFooter);

    SelectDimension(m_kpSettings->GetDimension());
}

QPushButton* UsagePopup::CreateSegmentButton(const QString& text, const QString& objectName)
{
    QPushButton* pButton = new QPushButton(text, this);
    pButton->setObjectName(objectName);
    pButton->setCheckable(true);
    pButton->setProperty("segment", true);
    pButton->installEventFilter(this);
    m_pSegmentGroup->addButton(pButton);
    return pButton;
}

void UsagePopup::SelectDimension(EBreakdownDimension dimension)
{
    m_kpSettings->SetDimension(dimension);
    // The group is exclusive, so the button's own click handling after this keeps the same state.
    m_pProjectButton->setChecked(dimension == EBreakdownDimension::PROJECT);
    m_pModelButton->setChecked(dimension == EBreakdownDimension::MODEL);
    m_pSessionButton->setChecked(dimension == EBreakdownDimension::SESSION);
    RefreshBreakdown(QDateTime::currentDateTimeUtc());
}

void UsagePopup::RefreshLimits(const QDateTime& now)
{
    const QString strPlan = m_kpSystemStatus->GetPlanLabel();
    m_pPlanBadge->setText(strPlan);
    m_pPlanBadge->setVisible(!strPlan.isEmpty());

    const ELimitDisplayState eDisplay = m_kpSystemStatus->GetLimitDisplayState(now);
    m_pMetersRow->setVisible(eDisplay != ELimitDisplayState::EMPTY);
    if (eDisplay != ELimitDisplayState::EMPTY)
    {
        const bool bDark = IsDarkAppearance();
        const bool bDimmed = eDisplay == ELimitDisplayState::STALE;
        const int iWarn = m_kpSettings->GetWarnPercent();
        const int iCritical = m_kpSettings->GetCriticalPercent();
        const auto funcApply = [&](LimitMeterWidget* pMeter, const LimitWindow& window, const QString& subText)
        {
            if (!window.m_bValid)
            {
                pMeter->SetValue(QStringLiteral("—"), 0.0, QStringLiteral("정보 없음"), GetMeterColor(EBarLevel::NORMAL, bDark), bDimmed);
                return;
            }
            const EBarLevel eLevel = TrayIconPainter::ClassifyLevel(window.m_dPercent, iWarn, iCritical);
            pMeter->SetValue(Formatters::FormatPercent(window.m_dPercent), window.m_dPercent, subText, GetMeterColor(eLevel, bDark), bDimmed);
        };
        const UsageLimits& limits = m_kpSystemStatus->GetLimits();
        funcApply(m_pFiveHourMeter, limits.m_fiveHour, Formatters::FormatResetCountdown(limits.m_fiveHour.m_dtResetsAt, now));
        funcApply(m_pSevenDayMeter, limits.m_sevenDay, Formatters::FormatResetDay(limits.m_sevenDay.m_dtResetsAt, now));
    }

    const QString strNotice = BuildNoticeText(now);
    m_pNoticeLabel->setText(strNotice);
    m_pNoticeLabel->setVisible(!strNotice.isEmpty());
    const EFetchStatus eStatus = m_kpSystemStatus->GetLastStatus();
    const bool bAttempted = m_kpSystemStatus->GetLastAttemptAt().isValid();
    m_pRetryButton->setVisible(bAttempted && (eStatus == EFetchStatus::NOT_LOGGED_IN || eStatus == EFetchStatus::KEYCHAIN_DENIED));
}

void UsagePopup::RefreshBreakdown(const QDateTime& now)
{
    AggregateQuery query;
    query.m_eDimension = m_kpSettings->GetDimension();
    query.m_dtFrom = TokenAggregator::CalculatePeriodStart(m_kpSettings->GetPeriod(), now, m_kpSystemStatus->GetLimits().m_fiveHour.m_dtResetsAt);
    const Breakdown breakdown = TokenAggregator::Aggregate(m_kpSystemStatus->GetLogSnapshot(), *m_kpPriceTable, query);
    m_pBreakdownList->SetBreakdown(breakdown, GetMeterColor(EBarLevel::NORMAL, IsDarkAppearance()));
    adjustSize();
}

void UsagePopup::RefreshFooter(const QDateTime& now)
{
    const QDateTime dtSuccess = m_kpSystemStatus->GetLastSuccessAt();
    const bool bFailing = m_kpSystemStatus->GetLastAttemptAt().isValid() && m_kpSystemStatus->GetLastStatus() != EFetchStatus::OK;
    QPalette palFooter = palette();
    if (bFailing)
    {
        m_pFooterLabel->setText(dtSuccess.isValid()
            ? QStringLiteral("갱신 실패 · %1").arg(Formatters::FormatAge(dtSuccess, now))
            : QStringLiteral("한도 정보 없음"));
        palFooter.setColor(QPalette::WindowText, kFailRed);
    }
    else if (dtSuccess.isValid())
    {
        m_pFooterLabel->setText(QStringLiteral("%1 업데이트").arg(Formatters::FormatAge(dtSuccess, now)));
    }
    else
    {
        m_pFooterLabel->setText(QStringLiteral("한도 정보 없음"));
    }
    m_pFooterLabel->setPalette(palFooter);
}

QString UsagePopup::BuildNoticeText(const QDateTime& now) const
{
    if (!m_kpSystemStatus->GetLastAttemptAt().isValid())
    {
        return QStringLiteral("한도를 불러오는 중…");
    }
    switch (m_kpSystemStatus->GetLastStatus())
    {
    case EFetchStatus::OK:
        return QString();
    case EFetchStatus::NOT_LOGGED_IN:
        return QStringLiteral("Claude Code 로그인 정보를 찾지 못했습니다.\n터미널에서 claude 실행 후 /login 하세요.");
    case EFetchStatus::KEYCHAIN_DENIED:
        return QStringLiteral("Keychain 접근이 거부되었습니다.\n'다시 확인'을 누르면 허용 창이 다시 뜹니다.");
    case EFetchStatus::TOKEN_EXPIRED:
        return QStringLiteral("로그인 토큰이 만료되었습니다. Claude Code 를 실행하면 갱신됩니다.");
    case EFetchStatus::BAD_RESPONSE:
        return QStringLiteral("사용량 API 응답 형식이 바뀌었습니다.\n~/Library/Logs/TokenViewer 를 확인하세요.");
    case EFetchStatus::RATE_LIMITED:
    case EFetchStatus::NETWORK_ERROR:
    case EFetchStatus::SERVER_ERROR:
        break;
    }
    const QString strFailure = QStringLiteral("갱신 실패: %1").arg(m_kpSystemStatus->GetLastDetail());
    const QDateTime dtSuccess = m_kpSystemStatus->GetLastSuccessAt();
    if (!dtSuccess.isValid())
    {
        return strFailure;
    }
    return QStringLiteral("%1 값입니다. %2").arg(Formatters::FormatAge(dtSuccess, now), strFailure);
}

bool UsagePopup::IsDarkAppearance() const
{
    return palette().color(QPalette::Window).lightness() < kDarkWindowLightness;
}
```

`CMakeLists.txt` 의 `set(TV_CORE_SOURCES ...)` 안에 다음을 추가:

```cmake
    src/UI/LimitMeterWidget.h
    src/UI/LimitMeterWidget.cpp
    src/UI/BreakdownListWidget.h
    src/UI/BreakdownListWidget.cpp
    src/UI/UsagePopup.h
    src/UI/UsagePopup.cpp
```

- [ ] **Step 7: 테스트 통과 확인**

Run: `cmake --build build && ctest --test-dir build --output-on-failure -R "TestFormatters|TestUsagePopup"`
Expected: PASS

- [ ] **Step 8: Commit**

```bash
git add CMakeLists.txt tests/CMakeLists.txt tests/TestFormatters.cpp tests/TestUsagePopup.cpp src/UI/Formatters.h src/UI/Formatters.cpp src/UI/LimitMeterWidget.h src/UI/LimitMeterWidget.cpp src/UI/BreakdownListWidget.h src/UI/BreakdownListWidget.cpp src/UI/UsagePopup.h src/UI/UsagePopup.cpp
git commit -m "feat: add usage dropdown with limit meters and cost breakdown" -m "Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>"
```

---

### Task 13: Application 조립 + 실제 실행

**Files:**
- Create: `src/Core/Application.h`, `src/Core/Application.cpp`, `resources/TokenViewer.qrc`
- Modify: `src/main.cpp` (스파이크 코드를 교체), `CMakeLists.txt`

**Interfaces:**
- Consumes: 앞 Task 전부 — `TrayIcon`, `UsagePopup`, `UsageFetchThread`, `LogScanThread`, `UsageApiClient::FetchWithStoredCredential`, `SystemStatus`, `Settings`, `Observers`, `PriceTable`
- Produces: `class Application : public QObject` — `Application()`, `void Start()`. Task 14 가 `ShowSettingsDialog()` 와 로그인 항목 적용을 더한다.

통합 단계라 단위 테스트 대신 전체 테스트 + 실제 실행으로 확인한다.

- [ ] **Step 1: 리소스 파일**

`resources/TokenViewer.qrc`:

```xml
<RCC>
    <qresource prefix="/">
        <file>prices.json</file>
    </qresource>
</RCC>
```

- [ ] **Step 2: Application 작성**

`src/Core/Application.h`:

```cpp
#pragma once

#include "UI/TrayIconPainter.h"

#include <QObject>
#include <QTimer>

#include <memory>

class LogScanThread;
class Observers;
class PriceTable;
class QSettings;
class Settings;
class SystemStatus;
class TrayIcon;
class UsageFetchThread;
class UsagePopup;
struct FetchResult;

class Application : public QObject
{
    Q_OBJECT

public:
    Application();
    ~Application();

public:
    void Start();

private:
    void ConnectSignals();
    void HandleFetchResult(const FetchResult& result);
    void UpdateTrayIcon();
    void TogglePopup();
    void ApplySettings();
    TrayIconState BuildTrayIconState() const;
    void WriteBadResponseLog(const FetchResult& result) const;

private:
    std::unique_ptr<QSettings> m_upStoredSettings;
    std::unique_ptr<Settings> m_upSettings;
    std::unique_ptr<SystemStatus> m_upSystemStatus;
    std::unique_ptr<Observers> m_upObservers;
    std::unique_ptr<PriceTable> m_upPriceTable;
    std::unique_ptr<TrayIcon> m_upTrayIcon;
    std::unique_ptr<UsagePopup> m_upPopup;
    std::unique_ptr<UsageFetchThread> m_upFetchThread;
    std::unique_ptr<LogScanThread> m_upLogScanThread;
    QTimer m_timerStaleCheck;
    TrayIconState m_lastTrayState;
    bool m_bHasTrayState;
};
```

`src/Core/Application.cpp`:

```cpp
#include "Core/Application.h"

#include "Analysis/PriceTable.h"
#include "Core/Observers.h"
#include "Core/Settings.h"
#include "Core/SystemStatus.h"
#include "Service/UsageApiClient.h"
#include "Thread/LogScanThread.h"
#include "Thread/UsageFetchThread.h"
#include "UI/TrayIcon.h"
#include "UI/UsagePopup.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QSettings>

namespace
{
constexpr int kMsPerMinute = 60 * 1000;
constexpr int kLogScanIntervalMs = 60 * 1000;
constexpr int kStaleCheckMs = 60 * 1000;
const QString kPricesResource = QStringLiteral(":/prices.json");
const QString kProjectsDirectory = QStringLiteral("/.claude/projects");
const QString kLogDirectory = QStringLiteral("/Library/Logs/TokenViewer");
const QString kBadResponseLogName = QStringLiteral("/usage-response.log");
}

Application::Application()
    : QObject(nullptr)
    , m_upStoredSettings(std::make_unique<QSettings>())
    , m_upSettings(std::make_unique<Settings>(m_upStoredSettings.get()))
    , m_upSystemStatus(std::make_unique<SystemStatus>())
    , m_upObservers(std::make_unique<Observers>())
    , m_upPriceTable(std::make_unique<PriceTable>())
    , m_upTrayIcon(std::make_unique<TrayIcon>())
    , m_upPopup(std::make_unique<UsagePopup>(*m_upObservers, m_upSystemStatus.get(), m_upSettings.get(), m_upPriceTable.get()))
    , m_upFetchThread(std::make_unique<UsageFetchThread>(
          []() { return UsageApiClient::FetchWithStoredCredential(QDateTime::currentDateTimeUtc()); },
          m_upSettings->GetRefreshIntervalMinutes() * kMsPerMinute))
    , m_upLogScanThread(std::make_unique<LogScanThread>(QDir::homePath() + kProjectsDirectory, kLogScanIntervalMs))
    , m_timerStaleCheck()
    , m_lastTrayState()
    , m_bHasTrayState(false)
{
}

Application::~Application()
{
    // Stop the workers first; their queued signals target objects destroyed below.
    m_upFetchThread->Stop();
    m_upLogScanThread->Stop();
}

void Application::Start()
{
    QFile filePrices(kPricesResource);
    if (filePrices.open(QIODevice::ReadOnly))
    {
        m_upPriceTable->LoadFromJson(filePrices.readAll());
    }

    ConnectSignals();
    ApplySettings();
    m_upTrayIcon->Show();
    m_upFetchThread->Start();
    m_upLogScanThread->Start();
    m_timerStaleCheck.start(kStaleCheckMs);
}

void Application::ConnectSignals()
{
    connect(m_upFetchThread.get(), &UsageFetchThread::FetchCompleted, this, &Application::HandleFetchResult);
    connect(m_upLogScanThread.get(), &LogScanThread::SnapshotReady, this, [this](const LogSnapshot& snapshot)
    {
        m_upSystemStatus->ApplyLogSnapshot(snapshot);
        emit m_upObservers->LogSnapshotChanged();
    });
    connect(m_upTrayIcon.get(), &TrayIcon::Clicked, this, &Application::TogglePopup);
    connect(m_upObservers.get(), &Observers::LimitsChanged, this, &Application::UpdateTrayIcon);
    connect(m_upObservers.get(), &Observers::SettingsChanged, this, &Application::ApplySettings);
    connect(m_upObservers.get(), &Observers::RefreshRequested, this, [this]()
    {
        m_upFetchThread->RequestRefresh();
        m_upLogScanThread->RequestRescan();
    });
    connect(m_upObservers.get(), &Observers::QuitRequested, qApp, &QCoreApplication::quit);
    // Staleness depends on the clock, so re-check it even when nothing new arrives.
    connect(&m_timerStaleCheck, &QTimer::timeout, this, &Application::UpdateTrayIcon);
}

void Application::HandleFetchResult(const FetchResult& result)
{
    m_upSystemStatus->ApplyFetchResult(result, QDateTime::currentDateTimeUtc());
    if (result.m_eStatus == EFetchStatus::BAD_RESPONSE)
    {
        WriteBadResponseLog(result);
    }
    emit m_upObservers->LimitsChanged();
}

void Application::UpdateTrayIcon()
{
    const TrayIconState state = BuildTrayIconState();
    if (m_bHasTrayState && state == m_lastTrayState)
    {
        return;
    }
    m_upTrayIcon->SetState(state);
    m_lastTrayState = state;
    m_bHasTrayState = true;
}

void Application::TogglePopup()
{
    if (m_upPopup->isVisible())
    {
        m_upPopup->hide();
        return;
    }
    if (m_upPopup->WasJustHidden())
    {
        return;
    }
    if (m_upSystemStatus->IsRefreshDueOnOpen(QDateTime::currentDateTimeUtc()))
    {
        m_upFetchThread->RequestRefresh();
    }
    m_upLogScanThread->RequestRescan();
    m_upPopup->Refresh();
    m_upPopup->ShowBelow(m_upTrayIcon->GetAnchorGeometry());
}

void Application::ApplySettings()
{
    m_upFetchThread->SetIntervalMs(m_upSettings->GetRefreshIntervalMinutes() * kMsPerMinute);
    UpdateTrayIcon();
    if (m_upPopup->isVisible())
    {
        m_upPopup->Refresh();
    }
}

TrayIconState Application::BuildTrayIconState() const
{
    TrayIconState state;
    state.m_iWarnPercent = m_upSettings->GetWarnPercent();
    state.m_iCriticalPercent = m_upSettings->GetCriticalPercent();
    const ELimitDisplayState eDisplay = m_upSystemStatus->GetLimitDisplayState(QDateTime::currentDateTimeUtc());
    if (eDisplay == ELimitDisplayState::EMPTY)
    {
        return state;
    }
    const UsageLimits& limits = m_upSystemStatus->GetLimits();
    state.m_bFiveHourValid = limits.m_fiveHour.m_bValid;
    state.m_dFiveHourPercent = limits.m_fiveHour.m_dPercent;
    state.m_bSevenDayValid = limits.m_sevenDay.m_bValid;
    state.m_dSevenDayPercent = limits.m_sevenDay.m_dPercent;
    state.m_bStale = eDisplay == ELimitDisplayState::STALE;
    return state;
}

void Application::WriteBadResponseLog(const FetchResult& result) const
{
    // Only the response body is written; the request (and its token) never is.
    const QString strDirectory = QDir::homePath() + kLogDirectory;
    QDir().mkpath(strDirectory);
    QFile file(strDirectory + kBadResponseLogName);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        return;
    }
    file.write(QStringLiteral("%1 HTTP %2 %3\n")
        .arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODate))
        .arg(result.m_iHttpStatus)
        .arg(result.m_strDetail)
        .toUtf8());
    file.write(result.m_baRawBody);
}
```

- [ ] **Step 3: main 교체**

`src/main.cpp` 전체를 교체:

```cpp
#include "Core/Application.h"

#include <QApplication>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("TokenViewer"));
    QApplication::setOrganizationName(QStringLiteral("TokenViewer"));
    QApplication::setOrganizationDomain(QStringLiteral("tokenviewer.com"));
    QApplication::setApplicationVersion(QStringLiteral(TV_VERSION));
    QApplication::setQuitOnLastWindowClosed(false);

    Application application;
    application.Start();
    return app.exec();
}
```

- [ ] **Step 4: CMake 수정**

- `set(TV_CORE_SOURCES ...)` 안에 `src/Core/Application.h`, `src/Core/Application.cpp` 추가.
- `add_executable(TokenViewer MACOSX_BUNDLE src/main.cpp)` 를 `add_executable(TokenViewer MACOSX_BUNDLE src/main.cpp resources/TokenViewer.qrc)` 로 바꾼다.

- [ ] **Step 5: 빌드와 전체 테스트**

Run: `cmake --build build && ctest --test-dir build --output-on-failure`
Expected: 빌드 성공, 모든 테스트 PASS

- [ ] **Step 6: 실제 실행 확인 (사용자와 함께)**

Run: `open build/TokenViewer.app`

1. 처음 실행하면 macOS 가 TokenViewer 의 Keychain 접근 허용을 묻는다. 사용자에게 "항상 허용"을 눌러 달라고 한다.
2. 몇 초 안에 메뉴바 숫자가 Claude Code 의 `/usage` 의 5시간 % 와 같은지 사용자에게 확인을 요청한다.
3. 아이콘을 누르면 드롭다운이 열리고, 미터·리셋 시각·요금제 배지가 보이는지 확인한다.
4. 프로젝트/모델/세션 탭과 기간 콤보를 바꾸면 목록이 바뀌는지, 줄에 마우스를 올리면 토큰 툴팁이 뜨는지 확인한다.
5. Wi‑Fi 를 끄고 ↻ 를 누르면 "갱신 실패" 안내와 빨간 바닥 글씨가 나오는지 확인한다.
6. "종료"로 앱이 끝나는지 확인한다.

문제가 있으면 superpowers:systematic-debugging 으로 원인을 찾고 고친 뒤 다시 확인한다.

- [ ] **Step 7: Commit**

```bash
git add CMakeLists.txt resources/TokenViewer.qrc src/Core/Application.h src/Core/Application.cpp src/main.cpp
git commit -m "feat: assemble the menu bar app" -m "Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>"
```

---

### Task 14: 설정 창 + 로그인 시 자동 실행 + 오픈소스 고지

**Files:**
- Create: `src/Service/LoginItem.h`, `src/Service/LoginItem.cpp`, `src/UI/SettingsDialog.h`, `src/UI/SettingsDialog.cpp`, `src/UI/LicenseNoticesDialog.h`, `src/UI/LicenseNoticesDialog.cpp`, `resources/THIRD_PARTY_NOTICES.md`
- Modify: `src/Core/Application.h`, `src/Core/Application.cpp`, `resources/TokenViewer.qrc`, `CMakeLists.txt`, `tests/CMakeLists.txt`
- Test: `tests/TestLoginItem.cpp`, `tests/TestSettingsDialog.cpp`

**Interfaces:**
- Consumes: Task 11 `Settings`, `Observers`; Task 13 `Application`
- Produces:
  - `LoginItem::GetPlistPath() -> QString`, `BuildPlist(const QString& executablePath) -> QByteArray`, `IsBundledExecutable(const QString&) -> bool`, `Apply(bool enabled, const QString& executablePath) -> bool`
  - `SettingsDialog(Observers& observers, Settings* settings, QWidget* parent = nullptr)`, `void LoadFromSettings()`; objectName `intervalCombo`, `warnCombo`, `criticalCombo`, `launchCheck`
  - `LicenseNoticesDialog(QWidget* parent = nullptr)`

- [ ] **Step 1: 실패하는 테스트**

`tests/TestLoginItem.cpp`:

```cpp
#include "Service/LoginItem.h"

#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

namespace
{
const QString kBundledPath = QStringLiteral("/Applications/TokenViewer.app/Contents/MacOS/TokenViewer");
}

class TestLoginItem : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();
    void TestBuildPlistEscapesPath();
    void TestApplyCreatesAndRemovesPlist();
    void TestRefusesBareExecutable();

private:
    QTemporaryDir m_dirHome;
    QByteArray m_baOriginalHome;
};

void TestLoginItem::init()
{
    m_baOriginalHome = qgetenv("HOME");
    qputenv("HOME", m_dirHome.path().toUtf8());
}

void TestLoginItem::cleanup()
{
    qputenv("HOME", m_baOriginalHome);
}

void TestLoginItem::TestBuildPlistEscapesPath()
{
    const QByteArray baPlist = LoginItem::BuildPlist(QStringLiteral("/Applications/A&B.app/Contents/MacOS/TokenViewer"));
    QVERIFY(baPlist.contains("<string>com.tokenviewer.TokenViewer</string>"));
    QVERIFY(baPlist.contains("/Applications/A&amp;B.app/Contents/MacOS/TokenViewer"));
    QVERIFY(baPlist.contains("<key>RunAtLoad</key>"));
}

void TestLoginItem::TestApplyCreatesAndRemovesPlist()
{
    QVERIFY(LoginItem::GetPlistPath().startsWith(m_dirHome.path()));
    QVERIFY(LoginItem::Apply(true, kBundledPath));
    QFile file(LoginItem::GetPlistPath());
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), LoginItem::BuildPlist(kBundledPath));
    file.close();

    QVERIFY(LoginItem::Apply(false, kBundledPath));
    QVERIFY(!QFile::exists(LoginItem::GetPlistPath()));
}

void TestLoginItem::TestRefusesBareExecutable()
{
    QVERIFY(!LoginItem::Apply(true, QStringLiteral("/tmp/build/TokenViewer")));
    QVERIFY(!QFile::exists(LoginItem::GetPlistPath()));
}

QTEST_GUILESS_MAIN(TestLoginItem)
#include "TestLoginItem.moc"
```

`tests/TestSettingsDialog.cpp`:

```cpp
#include "UI/SettingsDialog.h"

#include "Core/Observers.h"
#include "Core/Settings.h"

#include <QCheckBox>
#include <QComboBox>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

class TestSettingsDialog : public QObject
{
    Q_OBJECT

private slots:
    void TestLoadsCurrentValues();
    void TestWarnChangeUpdatesSettingsAndCriticalChoices();
};

void TestSettingsDialog::TestLoadsCurrentValues()
{
    QTemporaryDir dirTemp;
    QSettings storedSettings(dirTemp.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);
    Settings settings(&storedSettings);
    settings.SetRefreshIntervalMinutes(5);
    Observers observers;
    SettingsDialog dialog(observers, &settings);
    dialog.LoadFromSettings();

    QCOMPARE(dialog.findChild<QComboBox*>(QStringLiteral("intervalCombo"))->currentData().toInt(), 5);
    QCOMPARE(dialog.findChild<QComboBox*>(QStringLiteral("warnCombo"))->currentData().toInt(), 70);
    QCOMPARE(dialog.findChild<QComboBox*>(QStringLiteral("criticalCombo"))->currentData().toInt(), 90);
    QVERIFY(dialog.findChild<QCheckBox*>(QStringLiteral("launchCheck"))->isChecked());
}

void TestSettingsDialog::TestWarnChangeUpdatesSettingsAndCriticalChoices()
{
    QTemporaryDir dirTemp;
    QSettings storedSettings(dirTemp.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);
    Settings settings(&storedSettings);
    Observers observers;
    SettingsDialog dialog(observers, &settings);
    dialog.LoadFromSettings();
    QSignalSpy spy(&observers, &Observers::SettingsChanged);

    QComboBox* pWarn = dialog.findChild<QComboBox*>(QStringLiteral("warnCombo"));
    pWarn->setCurrentIndex(pWarn->findData(90));
    QCOMPARE(settings.GetWarnPercent(), 90);
    QCOMPARE(spy.count(), 1);

    QComboBox* pCritical = dialog.findChild<QComboBox*>(QStringLiteral("criticalCombo"));
    QCOMPARE(pCritical->itemData(0).toInt(), 95);
    QCOMPARE(pCritical->currentData().toInt(), settings.GetCriticalPercent());
}

QTEST_MAIN(TestSettingsDialog)
#include "TestSettingsDialog.moc"
```

`tests/CMakeLists.txt` 끝에 `tv_add_test(TestLoginItem)`, `tv_add_test(TestSettingsDialog)` 추가.

Run: `cmake --build build`
Expected: FAIL — `Service/LoginItem.h` file not found

- [ ] **Step 2: LoginItem 구현**

`src/Service/LoginItem.h`:

```cpp
#pragma once

#include <QByteArray>
#include <QString>

class LoginItem
{
public:
    static constexpr const char* kLabel = "com.tokenviewer.TokenViewer";

public:
    static QString GetPlistPath();
    static QByteArray BuildPlist(const QString& executablePath);
    static bool IsBundledExecutable(const QString& executablePath);
    static bool Apply(bool enabled, const QString& executablePath);
};
```

`src/Service/LoginItem.cpp`:

```cpp
#include "Service/LoginItem.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

namespace
{
const QString kLaunchAgentsDirectory = QStringLiteral("/Library/LaunchAgents/");
const QString kBundleExecutableMarker = QStringLiteral(".app/Contents/MacOS/");
const QString kPlistTemplate = QStringLiteral(R"(<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>Label</key>
    <string>%1</string>
    <key>ProgramArguments</key>
    <array>
        <string>%2</string>
    </array>
    <key>RunAtLoad</key>
    <true/>
    <key>LimitLoadToSessionType</key>
    <string>Aqua</string>
    <key>ProcessType</key>
    <string>Interactive</string>
</dict>
</plist>
)");
}

QString LoginItem::GetPlistPath()
{
    return QDir::homePath() + kLaunchAgentsDirectory + QString::fromLatin1(kLabel) + QStringLiteral(".plist");
}

QByteArray LoginItem::BuildPlist(const QString& executablePath)
{
    return kPlistTemplate.arg(QString::fromLatin1(kLabel), executablePath.toHtmlEscaped()).toUtf8();
}

bool LoginItem::IsBundledExecutable(const QString& executablePath)
{
    return executablePath.contains(kBundleExecutableMarker);
}

bool LoginItem::Apply(bool enabled, const QString& executablePath)
{
    const QString strPlistPath = GetPlistPath();
    if (!enabled)
    {
        return !QFile::exists(strPlistPath) || QFile::remove(strPlistPath);
    }
    // Never register a bare build-tree binary; launchd would start it without its bundle.
    if (!IsBundledExecutable(executablePath))
    {
        return false;
    }

    const QByteArray baPlist = BuildPlist(executablePath);
    QFile fileExisting(strPlistPath);
    if (fileExisting.open(QIODevice::ReadOnly) && fileExisting.readAll() == baPlist)
    {
        return true;
    }
    fileExisting.close();

    QDir().mkpath(QFileInfo(strPlistPath).path());
    QSaveFile fileNew(strPlistPath);
    if (!fileNew.open(QIODevice::WriteOnly))
    {
        return false;
    }
    fileNew.write(baPlist);
    return fileNew.commit();
}
```

- [ ] **Step 3: SettingsDialog, LicenseNoticesDialog 구현**

`src/UI/SettingsDialog.h`:

```cpp
#pragma once

#include <QDialog>

class Observers;
class QCheckBox;
class QComboBox;
class QPushButton;
class Settings;

class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    SettingsDialog(Observers& observers, Settings* settings, QWidget* parent = nullptr);
    ~SettingsDialog();

public:
    void LoadFromSettings();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void BuildUi();
    void FillCriticalChoices();

private:
    Observers& m_observers;
    Settings* m_kpSettings;
    QComboBox* m_pIntervalCombo;
    QComboBox* m_pWarnCombo;
    QComboBox* m_pCriticalCombo;
    QCheckBox* m_pLaunchCheck;
    QPushButton* m_pLicenseButton;
    QPushButton* m_pAboutQtButton;
    bool m_bLoading;
};
```

`src/UI/SettingsDialog.cpp`:

```cpp
#include "UI/SettingsDialog.h"

#include "Core/Observers.h"
#include "Core/Settings.h"
#include "UI/LicenseNoticesDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QEvent>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

namespace
{
constexpr int kHintPixelSize = 11;
}

SettingsDialog::SettingsDialog(Observers& observers, Settings* settings, QWidget* parent)
    : QDialog(parent)
    , m_observers(observers)
    , m_kpSettings(settings)
    , m_pIntervalCombo(nullptr)
    , m_pWarnCombo(nullptr)
    , m_pCriticalCombo(nullptr)
    , m_pLaunchCheck(nullptr)
    , m_pLicenseButton(nullptr)
    , m_pAboutQtButton(nullptr)
    , m_bLoading(false)
{
    BuildUi();
}

SettingsDialog::~SettingsDialog() = default;

void SettingsDialog::LoadFromSettings()
{
    m_bLoading = true;
    m_pIntervalCombo->setCurrentIndex(m_pIntervalCombo->findData(m_kpSettings->GetRefreshIntervalMinutes()));
    m_pWarnCombo->setCurrentIndex(m_pWarnCombo->findData(m_kpSettings->GetWarnPercent()));
    FillCriticalChoices();
    m_pLaunchCheck->setChecked(m_kpSettings->GetLaunchAtLogin());
    m_bLoading = false;
}

bool SettingsDialog::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::MouseButtonRelease)
    {
        if (watched == m_pLicenseButton)
        {
            LicenseNoticesDialog dialog(this);
            dialog.exec();
        }
        else if (watched == m_pAboutQtButton)
        {
            QMessageBox::aboutQt(this);
        }
    }
    return QDialog::eventFilter(watched, event);
}

void SettingsDialog::BuildUi()
{
    setWindowTitle(QStringLiteral("TokenViewer 설정"));
    QVBoxLayout* pRoot = new QVBoxLayout(this);
    QFormLayout* pForm = new QFormLayout();

    m_pIntervalCombo = new QComboBox(this);
    m_pIntervalCombo->setObjectName(QStringLiteral("intervalCombo"));
    for (const int iMinutes : Settings::GetAllowedIntervalMinutes())
    {
        m_pIntervalCombo->addItem(QStringLiteral("%1분").arg(iMinutes), iMinutes);
    }
    QLabel* pIntervalHint = new QLabel(QStringLiteral("드롭다운을 열 때 30초가 지났으면 바로 갱신합니다."), this);
    QFont fontHint = pIntervalHint->font();
    fontHint.setPixelSize(kHintPixelSize);
    pIntervalHint->setFont(fontHint);

    m_pWarnCombo = new QComboBox(this);
    m_pWarnCombo->setObjectName(QStringLiteral("warnCombo"));
    for (int iPercent = Settings::kMinWarnPercent; iPercent <= Settings::kMaxWarnPercent; iPercent += Settings::kPercentStep)
    {
        m_pWarnCombo->addItem(QStringLiteral("%1%").arg(iPercent), iPercent);
    }
    m_pCriticalCombo = new QComboBox(this);
    m_pCriticalCombo->setObjectName(QStringLiteral("criticalCombo"));
    m_pLaunchCheck = new QCheckBox(QStringLiteral("로그인 시 자동 실행"), this);
    m_pLaunchCheck->setObjectName(QStringLiteral("launchCheck"));

    pForm->addRow(QStringLiteral("한도 갱신 주기"), m_pIntervalCombo);
    pForm->addRow(QString(), pIntervalHint);
    pForm->addRow(QStringLiteral("주황 경고"), m_pWarnCombo);
    pForm->addRow(QStringLiteral("빨강 경고"), m_pCriticalCombo);
    pForm->addRow(QStringLiteral("시작"), m_pLaunchCheck);
    pRoot->addLayout(pForm);

    QHBoxLayout* pBottom = new QHBoxLayout();
    m_pLicenseButton = new QPushButton(QStringLiteral("오픈소스 라이선스"), this);
    m_pAboutQtButton = new QPushButton(QStringLiteral("Qt 정보"), this);
    m_pLicenseButton->installEventFilter(this);
    m_pAboutQtButton->installEventFilter(this);
    QLabel* pVersion = new QLabel(QStringLiteral("v%1").arg(QCoreApplication::applicationVersion()), this);
    pBottom->addWidget(m_pLicenseButton);
    pBottom->addWidget(m_pAboutQtButton);
    pBottom->addStretch(1);
    pBottom->addWidget(pVersion);
    pRoot->addLayout(pBottom);

    connect(m_pIntervalCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]()
    {
        if (m_bLoading)
        {
            return;
        }
        m_kpSettings->SetRefreshIntervalMinutes(m_pIntervalCombo->currentData().toInt());
        emit m_observers.SettingsChanged();
    });
    connect(m_pWarnCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]()
    {
        if (m_bLoading)
        {
            return;
        }
        m_kpSettings->SetWarnPercent(m_pWarnCombo->currentData().toInt());
        FillCriticalChoices();
        emit m_observers.SettingsChanged();
    });
    connect(m_pCriticalCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]()
    {
        if (m_bLoading)
        {
            return;
        }
        m_kpSettings->SetCriticalPercent(m_pCriticalCombo->currentData().toInt());
        emit m_observers.SettingsChanged();
    });
    connect(m_pLaunchCheck, &QCheckBox::toggled, this, [this](bool checked)
    {
        if (m_bLoading)
        {
            return;
        }
        m_kpSettings->SetLaunchAtLogin(checked);
        emit m_observers.SettingsChanged();
    });
}

void SettingsDialog::FillCriticalChoices()
{
    const bool bWasLoading = m_bLoading;
    m_bLoading = true;
    m_pCriticalCombo->clear();
    for (int iPercent = m_kpSettings->GetWarnPercent() + Settings::kPercentStep; iPercent <= Settings::kMaxPercent; iPercent += Settings::kPercentStep)
    {
        m_pCriticalCombo->addItem(QStringLiteral("%1%").arg(iPercent), iPercent);
    }
    m_pCriticalCombo->setCurrentIndex(m_pCriticalCombo->findData(m_kpSettings->GetCriticalPercent()));
    m_bLoading = bWasLoading;
}
```

`src/UI/LicenseNoticesDialog.h`:

```cpp
#pragma once

#include <QDialog>

class LicenseNoticesDialog : public QDialog
{
    Q_OBJECT

public:
    explicit LicenseNoticesDialog(QWidget* parent = nullptr);
    ~LicenseNoticesDialog();
};
```

`src/UI/LicenseNoticesDialog.cpp`:

```cpp
#include "UI/LicenseNoticesDialog.h"

#include <QDialogButtonBox>
#include <QFile>
#include <QTextBrowser>
#include <QVBoxLayout>

namespace
{
constexpr int kDialogWidth = 520;
constexpr int kDialogHeight = 480;
const QString kNoticesResource = QStringLiteral(":/THIRD_PARTY_NOTICES.md");
}

LicenseNoticesDialog::LicenseNoticesDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("오픈소스 라이선스"));
    resize(kDialogWidth, kDialogHeight);
    QVBoxLayout* pRoot = new QVBoxLayout(this);
    QTextBrowser* pBrowser = new QTextBrowser(this);
    pBrowser->setOpenExternalLinks(true);
    QFile file(kNoticesResource);
    if (file.open(QIODevice::ReadOnly))
    {
        pBrowser->setMarkdown(QString::fromUtf8(file.readAll()));
    }
    pRoot->addWidget(pBrowser);
    QDialogButtonBox* pButtons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(pButtons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    pRoot->addWidget(pButtons);
}

LicenseNoticesDialog::~LicenseNoticesDialog() = default;
```

`resources/THIRD_PARTY_NOTICES.md`:

```markdown
# 오픈소스 고지

TokenViewer 는 아래 오픈소스 소프트웨어를 사용합니다.

## Qt 5.15

- 사용 모듈: Qt Core, Qt Gui, Qt Widgets, Qt Network
- 라이선스: GNU Lesser General Public License v3 (LGPLv3)
- 저작권: The Qt Company Ltd. and other contributors
- Qt 라이브러리는 동적으로 링크되어 있어 호환되는 다른 버전으로 바꿔 쓸 수 있습니다.
- 소스: https://download.qt.io/archive/qt/5.15/
- 라이선스 전문: https://www.gnu.org/licenses/lgpl-3.0.html

## OpenSSL 3

- Qt Network 가 HTTPS 연결에 사용합니다.
- 라이선스: Apache License 2.0
- 저작권: The OpenSSL Project Authors
- https://www.openssl.org/source/license.html
```

`resources/TokenViewer.qrc` 의 `<file>prices.json</file>` 아래에 `<file>THIRD_PARTY_NOTICES.md</file>` 추가.

`CMakeLists.txt` 의 `set(TV_CORE_SOURCES ...)` 안에 추가:

```cmake
    src/Service/LoginItem.cpp
    src/UI/SettingsDialog.h
    src/UI/SettingsDialog.cpp
    src/UI/LicenseNoticesDialog.h
    src/UI/LicenseNoticesDialog.cpp
```

- [ ] **Step 4: Application 에 연결**

`src/Core/Application.h`:
- 전방 선언에 `class SettingsDialog;` 추가.
- private 함수에 `void ShowSettingsDialog();` 추가.
- 멤버 `std::unique_ptr<LogScanThread> m_upLogScanThread;` 아래에 `std::unique_ptr<SettingsDialog> m_upSettingsDialog;` 추가.

`src/Core/Application.cpp`:
- include 에 `#include "Service/LoginItem.h"`, `#include "UI/SettingsDialog.h"` 추가.
- 생성자 초기화 목록의 `, m_upLogScanThread(...)` 다음 줄에 `, m_upSettingsDialog(nullptr)` 추가.
- `ConnectSignals()` 끝에 추가:

```cpp
    connect(m_upObservers.get(), &Observers::SettingsWindowRequested, this, &Application::ShowSettingsDialog);
```

- `ApplySettings()` 끝에 추가:

```cpp
    LoginItem::Apply(m_upSettings->GetLaunchAtLogin(), QCoreApplication::applicationFilePath());
```

- 새 함수:

```cpp
void Application::ShowSettingsDialog()
{
    if (!m_upSettingsDialog)
    {
        m_upSettingsDialog = std::make_unique<SettingsDialog>(*m_upObservers, m_upSettings.get());
    }
    m_upSettingsDialog->LoadFromSettings();
    m_upSettingsDialog->show();
    m_upSettingsDialog->raise();
    m_upSettingsDialog->activateWindow();
}
```

- [ ] **Step 5: 빌드와 전체 테스트**

Run: `cmake --build build && ctest --test-dir build --output-on-failure`
Expected: 모든 테스트 PASS

- [ ] **Step 6: 실제 실행 확인 (사용자와 함께)**

`open build/TokenViewer.app` 후:
1. 드롭다운의 ⚙ 로 설정 창이 열린다.
2. 주황 경고를 50% 로 바꾸면 (현재 5시간 % 가 50 이상일 때) 메뉴바 막대가 바로 주황이 된다. 원래 값으로 돌린다.
3. "오픈소스 라이선스"에 고지 문서가, "Qt 정보"에 Qt 창이 뜬다.
4. `ls ~/Library/LaunchAgents/com.tokenviewer.TokenViewer.plist` 가 있다. 체크를 끄면 사라진다. 사용자에게 개발 빌드 경로로 등록된다는 점을 알리고, 켜 둘지 물어본다.

- [ ] **Step 7: Commit**

```bash
git add CMakeLists.txt tests/CMakeLists.txt tests/TestLoginItem.cpp tests/TestSettingsDialog.cpp src/Service/LoginItem.h src/Service/LoginItem.cpp src/UI/SettingsDialog.h src/UI/SettingsDialog.cpp src/UI/LicenseNoticesDialog.h src/UI/LicenseNoticesDialog.cpp resources/THIRD_PARTY_NOTICES.md resources/TokenViewer.qrc src/Core/Application.h src/Core/Application.cpp
git commit -m "feat: add settings window, launch at login and license notices" -m "Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>"
```

---

### Task 15: 마무리 확인 + README

**Files:**
- Modify: `README.md`, `docs/superpowers/specs/2026-10-05-token-viewer-design.md` (상태 줄)

**Interfaces:**
- Consumes: 전체
- Produces: 빌드·실행 안내

- [ ] **Step 1: README 작성**

`README.md`:

````markdown
# TokenViewer

macOS 메뉴바에서 Claude Code 요금제 한도(5시간·주간)를 보여 주고, 클릭하면 프로젝트·모델·세션별 API 환산 비용을 보여 주는 앱.

- 설계: `docs/superpowers/specs/2026-10-05-token-viewer-design.md`
- 한도는 Claude Code 로그인 정보(Keychain)로 비공식 사용량 API 를 불러 옵니다. 토큰은 읽기만 하고 저장하지 않습니다.
- 비용은 `~/.claude/projects` 의 세션 로그로 계산합니다.

## 빌드

요구: macOS 11+, Qt 5.15 (개발 PC 는 Anaconda Qt 5.15.2), CMake 3.21+, Ninja.

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DQt5_DIR=/opt/homebrew/anaconda3/lib/cmake/Qt5
cmake --build build
ctest --test-dir build --output-on-failure
open build/TokenViewer.app
```

## 처음 실행할 때

- macOS 가 Keychain 의 `Claude Code-credentials` 접근 허용을 묻습니다. "항상 허용"을 누르세요. 다시 빌드하면 서명이 바뀌어 한 번 더 물을 수 있습니다.
- 설정의 "로그인 시 자동 실행"은 `~/Library/LaunchAgents/com.tokenviewer.TokenViewer.plist` 를 만듭니다.

## 배포

배포 패키지는 아직 없습니다. 만들 때는 `docs/LicenseCompliance.md` (공식 Qt 빌드, `macdeployqt`, 고지)를 따릅니다.
````

- [ ] **Step 2: Release 빌드와 전체 테스트**

Run: `cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DQt5_DIR=/opt/homebrew/anaconda3/lib/cmake/Qt5 && cmake --build build-release && ctest --test-dir build-release --output-on-failure`
Expected: 경고 없이 빌드, 모든 테스트 PASS

- [ ] **Step 3: 스펙 8절 수동 확인 목록 (사용자와 함께)**

밝은/어두운 배경화면에서 아이콘, 클릭, (모니터가 둘 이상이면) 다른 모니터에서 팝업 위치, Keychain 허용 창, 로그인 시 자동 실행을 확인한다. 결과를 `docs/superpowers/notes/2026-10-05-tray-spike.md` 끝에 "최종 확인" 으로 덧붙인다.

- [ ] **Step 4: 스펙 상태 갱신과 커밋**

스펙 머리의 `- 상태: 설계 확정, 구현 계획 전` 을 `- 상태: 구현 완료 (v0.1.0)` 로 바꾼다.

```bash
git add README.md docs/superpowers/specs/2026-10-05-token-viewer-design.md docs/superpowers/notes/2026-10-05-tray-spike.md
git commit -m "docs: add build instructions and final check notes" -m "Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>"
```
