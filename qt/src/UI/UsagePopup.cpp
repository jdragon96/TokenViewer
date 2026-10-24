#include "UI/UsagePopup.h"

#include "Analysis/PriceTable.h"
#include "Analysis/TokenAggregator.h"
#include "Core/AppPaths.h"
#include "Core/Observers.h"
#include "Core/Settings.h"
#include "Core/SystemStatus.h"
#include "UI/BreakdownListWidget.h"
#include "UI/ChevronComboBox.h"
#include "UI/Formatters.h"
#include "UI/LimitMeterWidget.h"
#include "UI/TrayIconPainter.h"
#include "UI/WindowsTheme.h"

#include <QButtonGroup>
#include <QComboBox>
#include <QDir>
#include <QEvent>
#include <QFontDatabase>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
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
// Fixed so switching tabs or periods never resizes the popup; the "기타 N개" row takes the last slot.
constexpr int kBreakdownSlotCount = 4;
constexpr int kMarginLeft = 16;
constexpr int kMarginTop = 14;
constexpr int kMarginRight = 16;
constexpr int kMarginBottom = 10;
constexpr int kSectionSpacing = 10;
constexpr int kHeaderSpacing = 8;
constexpr int kMeterSpacing = 16;
constexpr int kSegmentSpacing = 2;
constexpr int kSegmentTrackPadding = 2;
constexpr int kFooterSpacing = 2;
constexpr int kSeparatorHeight = 1;
constexpr int kTitlePixelSize = 14;
constexpr int kIconPixelSize = 14;
constexpr int kIconButtonSize = 28;
constexpr int kDarkWindowLightness = 128;
constexpr double kSeparatorAlpha = 0.1;
constexpr ushort kZeroWidthSpace = 0x200B;
constexpr ushort kRefreshGlyph = 0xE72C;
constexpr ushort kSettingsGlyph = 0xE713;
constexpr const char* kWindowsPlatformName = "windows";

const QColor kBlueLight(0x2a, 0x78, 0xd6);
const QColor kBlueDark(0x39, 0x87, 0xe5);

// %1 secondary text, %2 segment track, %3 track border, %4 selected segment, %5 selected border,
// %6 subtle hover, %7 subtle pressed, %8 badge, %9 notice.
const char* const kPopupStyleSheet = R"(
QFrame#segmentTrack { background: %2; border: 1px solid %3; border-radius: 6px; }
QPushButton[segment="true"] { background: transparent; border: 1px solid transparent; border-radius: 4px; padding: 3px 0px; min-height: 0px; color: %1; }
QPushButton[segment="true"]:hover { background: %6; }
QPushButton[segment="true"]:pressed { background: %7; }
QPushButton[segment="true"]:checked { background: %4; border-color: %5; color: palette(window-text); font-weight: 600; }
QToolButton { background: transparent; border: none; border-radius: 4px; padding: 4px 8px; }
QToolButton:hover { background: %6; }
QToolButton:pressed { background: %7; }
QLabel#planBadge { background: %8; border-radius: 4px; padding: 1px 6px; color: %1; }
QLabel#noticeLabel { background: %9; border-radius: 6px; padding: 8px 10px; }
QLabel#emptyLabel { color: %1; }
)";

struct PopupColors
{
    const char* m_pszSecondaryText;
    const char* m_pszTrack;
    const char* m_pszTrackBorder;
    const char* m_pszSelected;
    const char* m_pszSelectedBorder;
    const char* m_pszSubtleHover;
    const char* m_pszSubtlePressed;
    const char* m_pszBadge;
    const char* m_pszNotice;
};

constexpr PopupColors kLightPopupColors = { "#5f5f5f", "#efefef", "#e3e3e3", "#ffffff", "#dcdcdc", "#ececec", "#e4e4e4", "#ececec", "#f0f0f0" };
constexpr PopupColors kDarkPopupColors = { "#c8c8c8", "#262626", "#383838", "#3d3d3d", "#474747", "#383838", "#333333", "#3a3a3a", "#353535" };

QString BuildPopupStyleSheet(bool dark)
{
    const PopupColors& colors = dark ? kDarkPopupColors : kLightPopupColors;
    return WindowsTheme::BuildControlStyleSheet(dark)
        + QString::fromLatin1(kPopupStyleSheet)
              .arg(QLatin1String(colors.m_pszSecondaryText), QLatin1String(colors.m_pszTrack), QLatin1String(colors.m_pszTrackBorder),
                  QLatin1String(colors.m_pszSelected), QLatin1String(colors.m_pszSelectedBorder), QLatin1String(colors.m_pszSubtleHover),
                  QLatin1String(colors.m_pszSubtlePressed), QLatin1String(colors.m_pszBadge), QLatin1String(colors.m_pszNotice));
}

QColor GetMeterColor(EBarLevel level, bool dark)
{
    if (level == EBarLevel::NORMAL)
    {
        return dark ? kBlueDark : kBlueLight;
    }
    return TrayIconPainter::GetLevelColor(level, dark ? EAppearance::DARK : EAppearance::LIGHT);
}

// QFrame::HLine draws in full text colour; this is the faint Windows 11 divider.
class SeparatorLine : public QWidget
{
public:
    explicit SeparatorLine(QWidget* parent)
        : QWidget(parent)
    {
        setFixedHeight(kSeparatorHeight);
    }

protected:
    void paintEvent(QPaintEvent* event) override
    {
        Q_UNUSED(event);
        QPainter painter(this);
        QColor clrLine = palette().color(QPalette::WindowText);
        clrLine.setAlphaF(kSeparatorAlpha);
        painter.fillRect(rect(), clrLine);
    }
};

QWidget* CreateSeparator(QWidget* parent)
{
    return new SeparatorLine(parent);
}

void SetIconText(QToolButton* button, const QString& iconFamily, ushort glyph, const QString& fallbackText)
{
    if (iconFamily.isEmpty())
    {
        button->setText(fallbackText);
        return;
    }
    QFont fontIcon(iconFamily);
    fontIcon.setPixelSize(kIconPixelSize);
    button->setFont(fontIcon);
    button->setText(QString(QChar(glyph)));
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
    , m_rcAnchor()
    , m_bSystemCorners(false)
{
    // Opaque on purpose: a translucent window is layered, and layered windows lose ClearType text.
    setAttribute(Qt::WA_TranslucentBackground, false);
    setFixedWidth(kPopupWidth);
    ApplyStyleSheet();
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

QPoint UsagePopup::CalculatePosition(const QRect& anchor, const QSize& popupSize, const QRect& available)
{
    const int iWantedX = anchor.x() + (anchor.width() - popupSize.width()) / 2;
    // Doubled coordinates compare the centres without rounding odd sizes.
    const bool bAbove = 2 * anchor.y() + anchor.height() > 2 * available.y() + available.height();
    const int iWantedY = bAbove
        ? anchor.y() - kPopupGap - popupSize.height()
        : anchor.y() + anchor.height() + kPopupGap;
    const int iMinX = available.x() + kScreenMargin;
    const int iMaxX = std::max(iMinX, available.x() + available.width() - kScreenMargin - popupSize.width());
    const int iMinY = available.y() + kScreenMargin;
    const int iMaxY = std::max(iMinY, available.y() + available.height() - kScreenMargin - popupSize.height());
    return QPoint(std::clamp(iWantedX, iMinX, iMaxX), std::clamp(iWantedY, iMinY, iMaxY));
}

QRect UsagePopup::ResolveAnchor(const QRect& trayGeometry, const QPoint& fallbackPoint)
{
    // QSystemTrayIcon::geometry() is empty when Windows does not report where the icon is.
    if (!trayGeometry.isEmpty())
    {
        return trayGeometry;
    }
    return QRect(fallbackPoint, QSize(1, 1));
}

void UsagePopup::ShowNear(const QRect& anchor)
{
    m_rcAnchor = anchor;
    FitToContents();
    MoveToAnchor();
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

bool UsagePopup::HasSystemCorners() const
{
    return m_bSystemCorners;
}

bool UsagePopup::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::MouseButtonRelease)
    {
        const QMouseEvent* pMouse = static_cast<QMouseEvent*>(event);
        const QWidget* pWatched = qobject_cast<QWidget*>(watched);
        if (pWatched == nullptr || pMouse->button() != Qt::LeftButton || !pWatched->rect().contains(pMouse->pos()))
        {
            return QWidget::eventFilter(watched, event);
        }
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
    // The native window exists by now but is not on screen yet, so the first frame already has the corners.
    ApplyWindowFrame();
    QWidget::showEvent(event);
}

void UsagePopup::hideEvent(QHideEvent* event)
{
    // Qt::Popup closes itself on an outside click; that same click may also reach the tray icon.
    m_timerSinceHide.start();
    m_timerAge.stop();
    QWidget::hideEvent(event);
}

void UsagePopup::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    // Placing again keeps the edge next to the icon fixed, so a popup above the icon grows upward.
    if (isVisible() && !m_rcAnchor.isNull())
    {
        MoveToAnchor();
    }
}

void UsagePopup::changeEvent(QEvent* event)
{
    QWidget::changeEvent(event);
    if (event->type() != QEvent::PaletteChange)
    {
        return;
    }
    // The style sheet, meter, footer and border colours are chosen for light or dark, so a new app palette needs them again.
    ApplyStyleSheet();
    if (isVisible())
    {
        ApplyWindowFrame();
        Refresh();
    }
}

void UsagePopup::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.fillRect(rect(), palette().color(QPalette::Window));
    if (!m_bSystemCorners)
    {
        // Windows 10: no DWM rounding, so a square 1 px border keeps the popup apart from what is behind it.
        painter.setPen(WindowsTheme::GetFrameBorderColor(IsDarkAppearance()));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(rect().adjusted(0, 0, -1, -1));
    }
}

void UsagePopup::BuildUi()
{
    QVBoxLayout* pRoot = new QVBoxLayout(this);
    pRoot->setContentsMargins(kMarginLeft, kMarginTop, kMarginRight, kMarginBottom);
    pRoot->setSpacing(kSectionSpacing);

    QHBoxLayout* pHeader = new QHBoxLayout();
    pHeader->setSpacing(kHeaderSpacing);
    QLabel* pTitle = new QLabel(QStringLiteral("Claude 사용량"), this);
    pTitle->setObjectName(QStringLiteral("titleLabel"));
    pTitle->setFont(WindowsTheme::BuildSemiboldFont(kTitlePixelSize));
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

    QFrame* pSegmentTrack = new QFrame(this);
    pSegmentTrack->setObjectName(QStringLiteral("segmentTrack"));
    QHBoxLayout* pSegments = new QHBoxLayout(pSegmentTrack);
    pSegments->setContentsMargins(kSegmentTrackPadding, kSegmentTrackPadding, kSegmentTrackPadding, kSegmentTrackPadding);
    pSegments->setSpacing(kSegmentSpacing);
    m_pSegmentGroup = new QButtonGroup(this);
    m_pSegmentGroup->setExclusive(true);
    m_pProjectButton = CreateSegmentButton(QStringLiteral("프로젝트"), QStringLiteral("projectButton"));
    m_pModelButton = CreateSegmentButton(QStringLiteral("모델"), QStringLiteral("modelButton"));
    m_pSessionButton = CreateSegmentButton(QStringLiteral("세션"), QStringLiteral("sessionButton"));
    pSegments->addWidget(m_pProjectButton);
    pSegments->addWidget(m_pModelButton);
    pSegments->addWidget(m_pSessionButton);
    pRoot->addWidget(pSegmentTrack);

    m_pBreakdownList = new BreakdownListWidget(kBreakdownSlotCount, this);
    m_pBreakdownList->setObjectName(QStringLiteral("breakdownList"));
    pRoot->addWidget(m_pBreakdownList);

    m_pPeriodCombo = new ChevronComboBox(this);
    m_pPeriodCombo->setObjectName(QStringLiteral("periodCombo"));
    m_pPeriodCombo->addItems({ QStringLiteral("이번 5시간 창"), QStringLiteral("오늘"), QStringLiteral("7일"), QStringLiteral("30일") });
    m_pPeriodCombo->setCurrentIndex(static_cast<int>(m_kpSettings->GetPeriod()));
    pRoot->addWidget(m_pPeriodCombo, 0, Qt::AlignLeft);

    pRoot->addWidget(CreateSeparator(this));

    QHBoxLayout* pFooter = new QHBoxLayout();
    pFooter->setSpacing(kFooterSpacing);
    m_pFooterLabel = new QLabel(this);
    m_pFooterLabel->setObjectName(QStringLiteral("footerLabel"));
    const QString strIconFamily = WindowsTheme::ChooseIconFontFamily(QFontDatabase().families());
    m_pRefreshButton = new QToolButton(this);
    m_pRefreshButton->setObjectName(QStringLiteral("refreshButton"));
    SetIconText(m_pRefreshButton, strIconFamily, kRefreshGlyph, QStringLiteral("↻"));
    m_pRefreshButton->setToolTip(QStringLiteral("새로고침"));
    // The glyph is a private-use character, so screen readers need the name spelled out.
    m_pRefreshButton->setAccessibleName(QStringLiteral("새로고침"));
    m_pSettingsButton = new QToolButton(this);
    m_pSettingsButton->setObjectName(QStringLiteral("settingsButton"));
    SetIconText(m_pSettingsButton, strIconFamily, kSettingsGlyph, QStringLiteral("⚙"));
    m_pSettingsButton->setToolTip(QStringLiteral("설정"));
    m_pSettingsButton->setAccessibleName(QStringLiteral("설정"));
    for (QToolButton* pButton : { m_pRefreshButton, m_pSettingsButton })
    {
        pButton->setFixedSize(kIconButtonSize, kIconButtonSize);
    }
    m_pQuitButton = new QToolButton(this);
    m_pQuitButton->setObjectName(QStringLiteral("quitButton"));
    m_pQuitButton->setText(QStringLiteral("종료"));
    m_pQuitButton->setFixedHeight(kIconButtonSize);
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
    query.m_iMaxRows = kBreakdownSlotCount;
    query.m_dtFrom = TokenAggregator::CalculatePeriodStart(m_kpSettings->GetPeriod(), now, m_kpSystemStatus->GetLimits().m_fiveHour.m_dtResetsAt);
    const Breakdown breakdown = TokenAggregator::Aggregate(m_kpSystemStatus->GetLogSnapshot(), *m_kpPriceTable, query);
    m_pBreakdownList->SetBreakdown(breakdown, GetMeterColor(EBarLevel::NORMAL, IsDarkAppearance()));
    FitToContents();
}

void UsagePopup::RefreshFooter(const QDateTime& now)
{
    const QDateTime dtSuccess = m_kpSystemStatus->GetLastSuccessAt();
    const bool bFailing = m_kpSystemStatus->GetLastAttemptAt().isValid() && m_kpSystemStatus->GetLastStatus() != EFetchStatus::OK;
    QPalette palFooter = palette();
    palFooter.setColor(QPalette::WindowText, WindowsTheme::GetSecondaryTextColor(IsDarkAppearance()));
    if (bFailing)
    {
        m_pFooterLabel->setText(dtSuccess.isValid()
            ? QStringLiteral("갱신 실패 · %1").arg(Formatters::FormatAge(dtSuccess, now))
            : QStringLiteral("한도 정보 없음"));
        palFooter.setColor(QPalette::WindowText, TrayIconPainter::GetLevelColor(EBarLevel::CRITICAL, IsDarkAppearance() ? EAppearance::DARK : EAppearance::LIGHT));
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
    {
        // Qt never wraps next to '\', so a zero-width space after each one lets the path wrap.
        QString strDirectory = QDir::toNativeSeparators(AppPaths::GetLogDirectory());
        strDirectory.replace(QLatin1Char('\\'), QString(QLatin1Char('\\')) + QChar(kZeroWidthSpace));
        return QStringLiteral("사용량 API 응답 형식이 바뀌었습니다.\n%1 를 확인하세요.").arg(strDirectory);
    }
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

void UsagePopup::ApplyStyleSheet()
{
    const QString strStyleSheet = BuildPopupStyleSheet(IsDarkAppearance());
    // Re-polishing every child is not free, and the 5 s theme check would otherwise do it for nothing.
    if (strStyleSheet != styleSheet())
    {
        setStyleSheet(strStyleSheet);
    }
}

void UsagePopup::ApplyWindowFrame()
{
    // Off the windows platform (tests run offscreen) winId() is not an HWND.
    m_bSystemCorners = QGuiApplication::platformName() == QLatin1String(kWindowsPlatformName)
        && WindowsTheme::ApplyRoundedCorners(winId(), WindowsTheme::GetFrameBorderColor(IsDarkAppearance()));
}

void UsagePopup::MoveToAnchor()
{
    QScreen* pScreen = QGuiApplication::screenAt(m_rcAnchor.center());
    if (pScreen == nullptr)
    {
        pScreen = QGuiApplication::primaryScreen();
    }
    if (pScreen == nullptr)
    {
        return;
    }
    move(CalculatePosition(m_rcAnchor, size(), pScreen->availableGeometry()));
}

void UsagePopup::FitToContents()
{
    // Hiding a child (the plan badge, the notice) marks only the top layout dirty, and the header row would
    // keep its old height until the next layout pass; activating refreshes every nested layout first.
    layout()->activate();
    adjustSize();
}
