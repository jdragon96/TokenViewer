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
