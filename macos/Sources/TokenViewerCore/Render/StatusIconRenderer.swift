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

/// Draws the menu bar item: the 5-hour % as a number inside a rounded box whose border fills with the
/// weekly %, starting at nine o'clock and running counterclockwise. A full border means the week is used up.
public enum StatusIconRenderer {
    public static let height: CGFloat = 18
    public static let fontSize: CGFloat = 12
    public static let dimmedOpacity: CGFloat = 0.5
    static let lineWidth: CGFloat = 1.5
    private static let cornerRadius: CGFloat = 4.5
    private static let horizontalPadding: CGFloat = 5
    private static let trackAlpha = 0.3
    private static let emptyLabel = "—"
    // The box is sized for the widest number so the menu bar does not shift as the value changes.
    private static let widestLabel = "100"

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
        return "\(Int(clamp(percent).rounded()))"
    }

    /// The border's centre line for an icon of `width` points, in top-left-origin points.
    static func boxRect(width: CGFloat) -> CGRect {
        CGRect(x: lineWidth / 2, y: 1 + lineWidth / 2, width: width - lineWidth, height: height - 2 - lineWidth)
    }

    public static func render(_ state: StatusIconState, appearance: MenuBarAppearance, scale: CGFloat) -> StatusIconImage? {
        let isTemplate = isTemplate(state)
        // A template image only carries alpha and macOS picks the color, so draw it in plain black.
        let ink = Palette.ink(isTemplate ? .light : appearance)
        let (fiveLevel, sevenLevel) = levels(state)
        let numberColor = fiveLevel == .normal ? ink : Palette.level(fiveLevel, appearance)
        let borderColor = sevenLevel == .normal ? ink : Palette.level(sevenLevel, appearance)

        let font = monospacedDigitFont()
        let line = textLine(label(state), font: font, color: numberColor)
        let widestWidth = typographicWidth(textLine(widestLabel, font: font, color: numberColor))
        let width = ceil(widestWidth + horizontalPadding * 2 + lineWidth)

        let pixelWidth = Int((width * scale).rounded())
        let pixelHeight = Int((height * scale).rounded())
        guard let colorSpace = CGColorSpace(name: CGColorSpace.sRGB),
              let context = CGContext(data: nil, width: pixelWidth, height: pixelHeight, bitsPerComponent: 8, bytesPerRow: 0,
                                      space: colorSpace, bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue) else {
            return nil
        }
        // Draw in points with the origin at the top-left.
        context.translateBy(x: 0, y: CGFloat(pixelHeight))
        context.scaleBy(x: scale, y: -scale)
        let isEmpty = state.fiveHourPercent == nil && state.sevenDayPercent == nil
        // Dim the finished drawing as one layer; dimming each stroke would let the track show through the progress.
        context.setAlpha(state.isStale || isEmpty ? dimmedOpacity : 1)
        context.beginTransparencyLayer(auxiliaryInfo: nil)

        let box = boxRect(width: width)
        let border = borderPath(box)
        context.setLineWidth(lineWidth)
        context.addPath(border)
        context.setStrokeColor(ink.withAlpha(trackAlpha).cgColor)
        context.strokePath()
        if let weekly = state.sevenDayPercent, weekly > 0 {
            drawProgress(context, border: border, box: box, fraction: clamp(weekly) / 100, color: borderColor)
        }

        // Core Text draws upward from the baseline, so flip back around it.
        var ascent: CGFloat = 0
        var descent: CGFloat = 0
        var leading: CGFloat = 0
        let textWidth = CGFloat(CTLineGetTypographicBounds(line, &ascent, &descent, &leading))
        context.saveGState()
        context.translateBy(x: box.midX - textWidth / 2, y: box.midY + (ascent - descent) / 2)
        context.scaleBy(x: 1, y: -1)
        context.textMatrix = .identity
        context.textPosition = .zero
        CTLineDraw(line, context)
        context.restoreGState()
        context.endTransparencyLayer()

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

    /// The rounded box traced from nine o'clock downward, i.e. counterclockwise on screen.
    private static func borderPath(_ box: CGRect) -> CGPath {
        let radius = min(cornerRadius, box.width / 2, box.height / 2)
        let path = CGMutablePath()
        path.move(to: CGPoint(x: box.minX, y: box.midY))
        path.addArc(tangent1End: CGPoint(x: box.minX, y: box.maxY), tangent2End: CGPoint(x: box.maxX, y: box.maxY), radius: radius)
        path.addArc(tangent1End: CGPoint(x: box.maxX, y: box.maxY), tangent2End: CGPoint(x: box.maxX, y: box.minY), radius: radius)
        path.addArc(tangent1End: CGPoint(x: box.maxX, y: box.minY), tangent2End: CGPoint(x: box.minX, y: box.minY), radius: radius)
        path.addArc(tangent1End: CGPoint(x: box.minX, y: box.minY), tangent2End: CGPoint(x: box.minX, y: box.maxY), radius: radius)
        path.closeSubpath()
        return path
    }

    /// Strokes the first `fraction` of the border with a dash as long as that share of the perimeter.
    private static func drawProgress(_ context: CGContext, border: CGPath, box: CGRect, fraction: Double, color: RGBA) {
        context.saveGState()
        context.addPath(border)
        context.setStrokeColor(color.cgColor)
        context.setLineWidth(lineWidth)
        if fraction < 1 {
            let radius = min(cornerRadius, box.width / 2, box.height / 2)
            let perimeter = 2 * (box.width - 2 * radius) + 2 * (box.height - 2 * radius) + 2 * .pi * radius
            context.setLineDash(phase: 0, lengths: [perimeter * CGFloat(fraction), perimeter * 2])
        }
        context.strokePath()
        context.restoreGState()
    }

    private static func monospacedDigitFont() -> CTFont {
        let base = CTFontCreateUIFontForLanguage(.system, fontSize, nil) ?? CTFontCreateWithName("Helvetica" as CFString, fontSize, nil)
        // Equal-width digits keep the number from wobbling inside the box as it changes.
        let descriptor = CTFontDescriptorCreateCopyWithFeature(CTFontCopyFontDescriptor(base),
                                                               kNumberSpacingType as CFNumber,
                                                               kMonospacedNumbersSelector as CFNumber)
        return CTFontCreateWithFontDescriptor(descriptor, fontSize, nil)
    }

    private static func textLine(_ text: String, font: CTFont, color: RGBA) -> CTLine {
        let attributes: [NSAttributedString.Key: Any] = [
            NSAttributedString.Key(kCTFontAttributeName as String): font,
            NSAttributedString.Key(kCTForegroundColorAttributeName as String): color.cgColor,
        ]
        return CTLineCreateWithAttributedString(NSAttributedString(string: text, attributes: attributes))
    }

    private static func typographicWidth(_ line: CTLine) -> CGFloat {
        CGFloat(CTLineGetTypographicBounds(line, nil, nil, nil))
    }

    private static func clamp(_ percent: Double) -> Double {
        min(max(percent, 0), 100)
    }
}
