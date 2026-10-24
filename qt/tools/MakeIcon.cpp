// Writes TokenViewer's app icon as a PNG-compressed .ico at build time: `MakeIcon <out.ico>` (spec 5.1).
// Drawn in code like the tray icon, so the repository holds no image files. Shapes only: no fonts at build time.
// A dark rounded tile, the tray icon's rounded border filled about two-thirds in blue from nine o'clock
// downward, and two short bars in the middle.

#include <QBuffer>
#include <QByteArray>
#include <QColor>
#include <QFile>
#include <QImage>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPair>
#include <QPen>
#include <QRectF>
#include <QSizeF>
#include <QVector>
#include <QtEndian>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace
{
constexpr int kSizes[] = { 16, 20, 24, 32, 40, 48, 64, 128, 256 };
constexpr int kIconDirSize = 6;
constexpr int kIconDirEntrySize = 16;
constexpr quint16 kIconTypeIcon = 1;
constexpr quint16 kColorPlanes = 1;
constexpr quint16 kBitsPerPixel = 32;
// The one-byte width and height store this side as 0.
constexpr int kLargestSide = 256;
constexpr int kMarginDivisor = 32;
constexpr double kGapDivisor = 16.0;
constexpr int kMinGap = 1;
constexpr double kLineWidthRatio = 0.09;
constexpr int kMinLineWidth = 2;
constexpr double kBarHeightRatio = 0.125;
constexpr int kMinBarHeight = 2;
constexpr double kTileRadiusRatio = 0.25;
constexpr double kMinBorderRadiusRatio = 0.75;
constexpr double kProgressFraction = 0.68;
constexpr double kTopBarFraction = 0.75;
constexpr double kBottomBarFraction = 0.45;
const QColor kTileTop(0x32, 0x38, 0x46);
const QColor kTileBottom(0x15, 0x17, 0x1d);
const QColor kProgress(0x39, 0x87, 0xe5);
const QColor kTrack(0xff, 0xff, 0xff, 0x38);
const QColor kTopBar(0xfa, 0xb2, 0x19);
const QColor kBottomBar(0xe6, 0xe9, 0xef);
const QColor kBarTrack(0xff, 0xff, 0xff, 0x24);

int RoundToInt(double value)
{
    return static_cast<int>(std::lround(value));
}

// The rounded box traced from nine o'clock downward (counter-clockwise on screen), as the tray icon fills it.
QPainterPath BuildBorderPath(const QRectF& box, double radius)
{
    const double dDiameter = radius * 2.0;
    QPainterPath path;
    path.moveTo(box.left(), box.center().y());
    path.lineTo(box.left(), box.bottom() - radius);
    path.arcTo(QRectF(box.left(), box.bottom() - dDiameter, dDiameter, dDiameter), 180.0, 90.0);
    path.lineTo(box.right() - radius, box.bottom());
    path.arcTo(QRectF(box.right() - dDiameter, box.bottom() - dDiameter, dDiameter, dDiameter), 270.0, 90.0);
    path.lineTo(box.right(), box.top() + radius);
    path.arcTo(QRectF(box.right() - dDiameter, box.top(), dDiameter, dDiameter), 0.0, 90.0);
    path.lineTo(box.left() + radius, box.top());
    path.arcTo(QRectF(box.left(), box.top(), dDiameter, dDiameter), 90.0, 90.0);
    path.closeSubpath();
    return path;
}

void FillBar(QPainter& painter, const QRectF& rect, const QColor& color)
{
    if (rect.width() <= 0.0)
    {
        return;
    }
    const double dRadius = std::min(rect.height(), rect.width()) / 2.0;
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    painter.drawRoundedRect(rect, dRadius, dRadius);
}

// Every length is a whole pixel so the straight edges stay crisp at 16 px.
QImage Render(int size)
{
    const int iMargin = size / kMarginDivisor;
    const int iLineWidth = std::max(kMinLineWidth, RoundToInt(size * kLineWidthRatio));
    const int iGap = std::max(kMinGap, RoundToInt(size / kGapDivisor));
    const int iBarHeight = std::max(kMinBarHeight, RoundToInt(size * kBarHeightRatio));

    QImage imgIcon(size, size, QImage::Format_ARGB32_Premultiplied);
    imgIcon.fill(Qt::transparent);
    QPainter painter(&imgIcon);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRectF rectTile(iMargin, iMargin, size - 2 * iMargin, size - 2 * iMargin);
    const double dTileRadius = rectTile.width() * kTileRadiusRatio;
    QLinearGradient gradient(rectTile.topLeft(), rectTile.bottomLeft());
    gradient.setColorAt(0.0, kTileTop);
    gradient.setColorAt(1.0, kTileBottom);
    painter.setPen(Qt::NoPen);
    painter.setBrush(gradient);
    painter.drawRoundedRect(rectTile, dTileRadius, dTileRadius);

    // The stroke covers [inset, inset + width] exactly.
    const double dInset = iMargin + iGap + iLineWidth / 2.0;
    const QRectF rectBorder(dInset, dInset, size - 2 * dInset, size - 2 * dInset);
    const double dBorderRadius = std::max(iLineWidth * kMinBorderRadiusRatio, dTileRadius - (iGap + iLineWidth / 2.0));
    const QPainterPath pathBorder = BuildBorderPath(rectBorder, dBorderRadius);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(kTrack, iLineWidth, Qt::SolidLine, Qt::FlatCap, Qt::MiterJoin));
    painter.drawPath(pathBorder);
    // A single dash trims the stroke to the progress; the pattern is measured in line widths.
    const double dPerimeter = pathBorder.length();
    QPen penProgress(kProgress, iLineWidth, Qt::CustomDashLine, Qt::FlatCap, Qt::MiterJoin);
    penProgress.setDashPattern({ dPerimeter * kProgressFraction / iLineWidth, dPerimeter / iLineWidth });
    painter.setPen(penProgress);
    painter.drawPath(pathBorder);

    const int iInnerLeft = iMargin + iGap + iLineWidth + iGap;
    const int iInnerWidth = size - 2 * iInnerLeft;
    const int iBarsTop = (size - 3 * iBarHeight) / 2;
    const QRectF rectTopBar(iInnerLeft, iBarsTop, iInnerWidth, iBarHeight);
    const QRectF rectBottomBar(iInnerLeft, iBarsTop + 2 * iBarHeight, iInnerWidth, iBarHeight);
    FillBar(painter, rectTopBar, kBarTrack);
    FillBar(painter, rectBottomBar, kBarTrack);
    FillBar(painter, QRectF(rectTopBar.topLeft(), QSizeF(RoundToInt(iInnerWidth * kTopBarFraction), iBarHeight)), kTopBar);
    FillBar(painter, QRectF(rectBottomBar.topLeft(), QSizeF(RoundToInt(iInnerWidth * kBottomBarFraction), iBarHeight)), kBottomBar);
    painter.end();
    return imgIcon;
}

QByteArray EncodePng(const QImage& image)
{
    QByteArray baPng;
    QBuffer buffer(&baPng);
    buffer.open(QIODevice::WriteOnly);
    if (!image.save(&buffer, "PNG"))
    {
        return QByteArray();
    }
    return baPng;
}

void AppendLittleEndian16(QByteArray& data, quint16 value)
{
    std::array<char, sizeof(quint16)> arrBytes;
    qToLittleEndian(value, arrBytes.data());
    data.append(arrBytes.data(), static_cast<int>(arrBytes.size()));
}

void AppendLittleEndian32(QByteArray& data, quint32 value)
{
    std::array<char, sizeof(quint32)> arrBytes;
    qToLittleEndian(value, arrBytes.data());
    data.append(arrBytes.data(), static_cast<int>(arrBytes.size()));
}

// ICONDIR, one ICONDIRENTRY per image, then the PNG streams (Windows Vista and later read PNG entries).
QByteArray BuildIco(const QVector<QPair<int, QByteArray>>& images)
{
    QByteArray baIco;
    // Reserved, type, image count.
    AppendLittleEndian16(baIco, 0);
    AppendLittleEndian16(baIco, kIconTypeIcon);
    AppendLittleEndian16(baIco, static_cast<quint16>(images.size()));
    quint32 uOffset = kIconDirSize + kIconDirEntrySize * images.size();
    for (const auto& pairImage : images)
    {
        const char cSide = static_cast<char>(pairImage.first >= kLargestSide ? 0 : pairImage.first);
        baIco.append(cSide);
        baIco.append(cSide);
        // No palette, reserved.
        baIco.append('\0');
        baIco.append('\0');
        AppendLittleEndian16(baIco, kColorPlanes);
        AppendLittleEndian16(baIco, kBitsPerPixel);
        AppendLittleEndian32(baIco, static_cast<quint32>(pairImage.second.size()));
        AppendLittleEndian32(baIco, uOffset);
        uOffset += static_cast<quint32>(pairImage.second.size());
    }
    for (const auto& pairImage : images)
    {
        baIco.append(pairImage.second);
    }
    return baIco;
}
}

int main(int argc, char* argv[])
{
    if (argc != 2)
    {
        std::fprintf(stderr, "usage: MakeIcon <out.ico>\n");
        return 2;
    }
    QVector<QPair<int, QByteArray>> vecImages;
    for (const int iSize : kSizes)
    {
        const QByteArray baPng = EncodePng(Render(iSize));
        if (baPng.isEmpty())
        {
            std::fprintf(stderr, "MakeIcon: cannot encode the %d px image as PNG\n", iSize);
            return 1;
        }
        vecImages.append(qMakePair(iSize, baPng));
    }
    QFile file(QString::fromLocal8Bit(argv[1]));
    const QByteArray baIco = BuildIco(vecImages);
    if (!file.open(QIODevice::WriteOnly) || file.write(baIco) != baIco.size() || !file.flush())
    {
        std::fprintf(stderr, "MakeIcon: cannot write %s\n", argv[1]);
        return 1;
    }
    return 0;
}
