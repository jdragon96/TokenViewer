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
