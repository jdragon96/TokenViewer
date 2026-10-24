#include <QDir>
#include <QFile>
#include <QImage>
#include <QRegularExpression>
#include <QVector>
#include <QtEndian>
#include <QtTest>

#include <windows.h>

#include <string>

namespace
{
const QVector<int> kExpectedSizes = { 16, 20, 24, 32, 40, 48, 64, 128, 256 };
const QByteArray kPngSignature("\x89PNG\r\n\x1a\n", 8);
constexpr int kIconDirSize = 6;
constexpr int kEntrySize = 16;
// Where an ICONDIRENTRY keeps its image's byte length and file offset.
constexpr int kEntryLengthOffset = 8;
constexpr int kEntryDataOffset = 12;
constexpr int kOpaqueAlpha = 160;
// RT_GROUP_ICON, spelled for the wide API (the A/W macro follows UNICODE, which is not defined).
constexpr WORD kGroupIconResourceType = 14;

quint16 ReadU16(const QByteArray& data, int offset)
{
    return qFromLittleEndian<quint16>(data.constData() + offset);
}

quint32 ReadU32(const QByteArray& data, int offset)
{
    return qFromLittleEndian<quint32>(data.constData() + offset);
}

QByteArray ReadIconFile()
{
    QFile file(QStringLiteral(TV_ICON_PATH));
    if (!file.open(QIODevice::ReadOnly))
    {
        return QByteArray();
    }
    return file.readAll();
}

QImage DecodeEntry(const QByteArray& ico, int index)
{
    const int iEntry = kIconDirSize + kEntrySize * index;
    const int iOffset = static_cast<int>(ReadU32(ico, iEntry + kEntryDataOffset));
    const int iLength = static_cast<int>(ReadU32(ico, iEntry + kEntryLengthOffset));
    return QImage::fromData(ico.mid(iOffset, iLength), "PNG");
}

bool IsBlue(QRgb pixel)
{
    return qAlpha(pixel) > kOpaqueAlpha && qBlue(pixel) > 150 && qBlue(pixel) > qRed(pixel) + 80;
}

bool IsOrange(QRgb pixel)
{
    return qAlpha(pixel) > kOpaqueAlpha && qRed(pixel) > 200 && qGreen(pixel) > 120 && qBlue(pixel) < 90;
}

int CountMatching(const QImage& image, const QRect& area, bool (*predicate)(QRgb))
{
    int iCount = 0;
    for (int iY = area.top(); iY <= area.bottom(); ++iY)
    {
        for (int iX = area.left(); iX <= area.right(); ++iX)
        {
            iCount += predicate(image.pixel(iX, iY)) ? 1 : 0;
        }
    }
    return iCount;
}

QString QueryVersionString(const QByteArray& info, const wchar_t* name)
{
    void* pValue = nullptr;
    UINT uLength = 0;
    const std::wstring strKey = std::wstring(L"\\StringFileInfo\\040904b0\\") + name;
    if (!VerQueryValueW(info.constData(), strKey.c_str(), &pValue, &uLength) || pValue == nullptr)
    {
        return QString();
    }
    return QString::fromWCharArray(static_cast<const wchar_t*>(pValue));
}
}

class TestAppIcon : public QObject
{
    Q_OBJECT

private slots:
    void TestHeader();
    void TestEntriesArePngImagesOfEachSize();
    void TestDrawing();
    void TestExeEmbedsIcon();
    void TestExeVersionInfo();
};

void TestAppIcon::TestHeader()
{
    const QByteArray baIco = ReadIconFile();
    QVERIFY2(baIco.size() > kIconDirSize + kEntrySize * kExpectedSizes.size(), TV_ICON_PATH);
    QCOMPARE(ReadU16(baIco, 0), quint16(0));
    QCOMPARE(ReadU16(baIco, 2), quint16(1));
    QCOMPARE(int(ReadU16(baIco, 4)), kExpectedSizes.size());
}

void TestAppIcon::TestEntriesArePngImagesOfEachSize()
{
    const QByteArray baIco = ReadIconFile();
    QVERIFY(baIco.size() > kIconDirSize + kEntrySize * kExpectedSizes.size());
    const quint32 uDirectoryEnd = kIconDirSize + kEntrySize * kExpectedSizes.size();
    quint32 uPreviousEnd = uDirectoryEnd;
    for (int iIndex = 0; iIndex < kExpectedSizes.size(); ++iIndex)
    {
        const int iSize = kExpectedSizes.at(iIndex);
        const int iEntry = kIconDirSize + kEntrySize * iIndex;
        // One byte each; 0 means 256.
        QCOMPARE(int(quint8(baIco.at(iEntry))), iSize % 256);
        QCOMPARE(int(quint8(baIco.at(iEntry + 1))), iSize % 256);
        QCOMPARE(int(quint8(baIco.at(iEntry + 2))), 0);
        QCOMPARE(int(quint8(baIco.at(iEntry + 3))), 0);
        QCOMPARE(ReadU16(baIco, iEntry + 4), quint16(1));
        QCOMPARE(ReadU16(baIco, iEntry + 6), quint16(32));
        const quint32 uLength = ReadU32(baIco, iEntry + kEntryLengthOffset);
        const quint32 uOffset = ReadU32(baIco, iEntry + kEntryDataOffset);
        QVERIFY(uLength > 0);
        QVERIFY(uOffset >= uPreviousEnd);
        QVERIFY(uOffset + uLength <= quint32(baIco.size()));
        uPreviousEnd = uOffset + uLength;
        QCOMPARE(baIco.mid(static_cast<int>(uOffset), kPngSignature.size()), kPngSignature);

        const QImage imgIcon = DecodeEntry(baIco, iIndex);
        QVERIFY2(!imgIcon.isNull(), qPrintable(QString::number(iSize)));
        QCOMPARE(imgIcon.size(), QSize(iSize, iSize));
        QVERIFY(imgIcon.hasAlphaChannel());
    }
    QCOMPARE(uPreviousEnd, quint32(baIco.size()));
}

void TestAppIcon::TestDrawing()
{
    const QByteArray baIco = ReadIconFile();
    QVERIFY(baIco.size() > kIconDirSize + kEntrySize * kExpectedSizes.size());
    for (int iIndex = 0; iIndex < kExpectedSizes.size(); ++iIndex)
    {
        const int iSize = kExpectedSizes.at(iIndex);
        const QImage imgIcon = DecodeEntry(baIco, iIndex).convertToFormat(QImage::Format_ARGB32);
        QVERIFY(!imgIcon.isNull());
        const QByteArray baContext = QByteArray::number(iSize) + " px";
        const int iQuarter = iSize / 4;
        // Rounded tile: the corner is see-through, the middle is a solid dark tile (between the two bars).
        QVERIFY2(qAlpha(imgIcon.pixel(0, 0)) == 0, baContext);
        const QRgb rgbCenter = imgIcon.pixel(iSize / 2, iSize / 2);
        QVERIFY2(qAlpha(rgbCenter) == 255 && qGray(rgbCenter) < 80, baContext);
        // The border fills from nine o'clock downward: bottom and right are blue, the upper left is not.
        QVERIFY2(CountMatching(imgIcon, QRect(iQuarter, iSize - iQuarter, iSize / 2, iQuarter), IsBlue) > 0, baContext);
        QVERIFY2(CountMatching(imgIcon, QRect(iSize - iQuarter, iQuarter, iQuarter, iSize / 2), IsBlue) > 0, baContext);
        QVERIFY2(CountMatching(imgIcon, QRect(0, 0, iSize / 2, iSize / 2 - 1), IsBlue) == 0, baContext);
        // The upper bar is orange.
        QVERIFY2(CountMatching(imgIcon, QRect(iQuarter, iQuarter, iSize / 2, iSize / 4), IsOrange) > 0, baContext);
    }
}

void TestAppIcon::TestExeEmbedsIcon()
{
    // Loaded as data only: nothing in the app runs.
    const std::wstring strExe = QDir::toNativeSeparators(QStringLiteral(TV_APP_EXE_PATH)).toStdWString();
    const HMODULE hModule = LoadLibraryExW(strExe.c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    QVERIFY2(hModule != nullptr, TV_APP_EXE_PATH);
    const HRSRC hGroup = FindResourceW(hModule, L"IDI_ICON1", MAKEINTRESOURCEW(kGroupIconResourceType));
    const bool bFound = hGroup != nullptr;
    int iCount = -1;
    if (bFound)
    {
        const HGLOBAL hData = LoadResource(hModule, hGroup);
        const quint16* pDirectory = hData != nullptr ? static_cast<const quint16*>(LockResource(hData)) : nullptr;
        if (pDirectory != nullptr && SizeofResource(hModule, hGroup) >= kIconDirSize)
        {
            iCount = pDirectory[2];
        }
    }
    FreeLibrary(hModule);
    QVERIFY(bFound);
    QCOMPARE(iCount, kExpectedSizes.size());
}

void TestAppIcon::TestExeVersionInfo()
{
    const std::wstring strExe = QDir::toNativeSeparators(QStringLiteral(TV_APP_EXE_PATH)).toStdWString();
    DWORD dwHandle = 0;
    const DWORD dwSize = GetFileVersionInfoSizeW(strExe.c_str(), &dwHandle);
    QVERIFY(dwSize > 0);
    QByteArray baInfo(static_cast<int>(dwSize), '\0');
    QVERIFY(GetFileVersionInfoW(strExe.c_str(), 0, dwSize, baInfo.data()));

    QCOMPARE(QueryVersionString(baInfo, L"ProductName"), QStringLiteral("TokenViewer"));
    QCOMPARE(QueryVersionString(baInfo, L"ProductVersion"), QStringLiteral(TV_VERSION));
    QCOMPARE(QueryVersionString(baInfo, L"FileVersion"), QStringLiteral(TV_VERSION));
    QCOMPARE(QueryVersionString(baInfo, L"OriginalFilename"), QStringLiteral("TokenViewer.exe"));
    QVERIFY(QueryVersionString(baInfo, L"LegalCopyright").contains(QStringLiteral("MIT")));

    void* pFixed = nullptr;
    UINT uLength = 0;
    QVERIFY(VerQueryValueW(baInfo.constData(), L"\\", &pFixed, &uLength));
    QVERIFY(uLength >= sizeof(VS_FIXEDFILEINFO));
    const VS_FIXEDFILEINFO* pInfo = static_cast<const VS_FIXEDFILEINFO*>(pFixed);
    const QRegularExpressionMatch match = QRegularExpression(QStringLiteral("^(\\d+)\\.(\\d+)\\.(\\d+)")).match(QStringLiteral(TV_VERSION));
    QVERIFY(match.hasMatch());
    QCOMPARE(int(HIWORD(pInfo->dwFileVersionMS)), match.captured(1).toInt());
    QCOMPARE(int(LOWORD(pInfo->dwFileVersionMS)), match.captured(2).toInt());
    QCOMPARE(int(HIWORD(pInfo->dwFileVersionLS)), match.captured(3).toInt());
    QCOMPARE(int(LOWORD(pInfo->dwFileVersionLS)), 0);
    QCOMPARE(pInfo->dwProductVersionMS, pInfo->dwFileVersionMS);
    QCOMPARE(pInfo->dwProductVersionLS, pInfo->dwFileVersionLS);
}

QTEST_GUILESS_MAIN(TestAppIcon)
#include "TestAppIcon.moc"
