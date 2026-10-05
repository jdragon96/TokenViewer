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
