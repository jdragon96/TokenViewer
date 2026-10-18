import CoreGraphics
import Foundation
import Testing
@testable import TokenViewerCore

@Suite struct StatusIconRendererTests {
    private let now = TestSupport.date("2026-10-05T12:00:00Z")

    private struct Pixel {
        var red: Int
        var green: Int
        var blue: Int
        var alpha: Int

        func isClose(to color: UInt32, tolerance: Int = 4) -> Bool {
            abs(red - Int((color >> 16) & 0xff)) <= tolerance
                && abs(green - Int((color >> 8) & 0xff)) <= tolerance
                && abs(blue - Int(color & 0xff)) <= tolerance
        }
    }

    /// The icon's pixels as premultiplied RGBA, read at point positions with the origin at the top-left.
    private struct Bitmap {
        let icon: StatusIconImage
        let scale: CGFloat
        private let bytes: [UInt8]

        init(_ icon: StatusIconImage, scale: CGFloat) {
            self.icon = icon
            self.scale = scale
            let image = icon.image
            var bytes = [UInt8](repeating: 0, count: image.width * image.height * 4)
            bytes.withUnsafeMutableBytes { buffer in
                let context = CGContext(data: buffer.baseAddress, width: image.width, height: image.height, bitsPerComponent: 8,
                                        bytesPerRow: image.width * 4, space: CGColorSpace(name: CGColorSpace.sRGB)!,
                                        bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!
                context.draw(image, in: CGRect(x: 0, y: 0, width: image.width, height: image.height))
            }
            self.bytes = bytes
        }

        func pixel(x: CGFloat, y: CGFloat) -> Pixel {
            let offset = (Int(y * scale) * icon.image.width + Int(x * scale)) * 4
            return Pixel(red: Int(bytes[offset]), green: Int(bytes[offset + 1]), blue: Int(bytes[offset + 2]), alpha: Int(bytes[offset + 3]))
        }

        /// True when an opaque pixel of `color` lies inside `rect` (points).
        func contains(_ color: UInt32, in rect: CGRect) -> Bool {
            var y = rect.minY
            while y < rect.maxY {
                var x = rect.minX
                while x < rect.maxX {
                    let candidate = pixel(x: x, y: y)
                    if candidate.alpha > 200 && candidate.isClose(to: color, tolerance: 12) {
                        return true
                    }
                    x += 1 / scale
                }
                y += 1 / scale
            }
            return false
        }
    }

    private func render(_ state: StatusIconState, _ appearance: MenuBarAppearance = .light, scale: CGFloat = 2) throws -> Bitmap {
        Bitmap(try #require(StatusIconRenderer.render(state, appearance: appearance, scale: scale)), scale: scale)
    }

    /// Points on the border's stroke, named by where they sit on the box.
    private struct Border {
        let box: CGRect

        init(_ bitmap: Bitmap) {
            box = StatusIconRenderer.boxRect(width: bitmap.icon.size.width)
        }

        var leftBelowStart: CGPoint { CGPoint(x: box.minX, y: box.midY + 2) }
        var bottomLeft: CGPoint { CGPoint(x: box.minX + 7, y: box.maxY) }
        var bottomRight: CGPoint { CGPoint(x: box.maxX - 6, y: box.maxY) }
        var rightMiddle: CGPoint { CGPoint(x: box.maxX, y: box.midY) }
        var topMiddle: CGPoint { CGPoint(x: box.midX, y: box.minY) }
        var leftAboveStart: CGPoint { CGPoint(x: box.minX, y: box.midY - 2) }
        var interior: CGRect { box.insetBy(dx: 2.5, dy: 2.5) }
    }

    private func isFilled(_ bitmap: Bitmap, _ point: CGPoint) -> Bool {
        bitmap.pixel(x: point.x, y: point.y).alpha > 200
    }

    @Test func classifiesLevels() {
        #expect(StatusIconRenderer.level(percent: 69.9, warn: 70, critical: 90) == .normal)
        #expect(StatusIconRenderer.level(percent: 70, warn: 70, critical: 90) == .warning)
        #expect(StatusIconRenderer.level(percent: 89.9, warn: 70, critical: 90) == .warning)
        #expect(StatusIconRenderer.level(percent: 90, warn: 70, critical: 90) == .critical)
    }

    @Test func isTemplateOnlyWhenBothNormal() {
        #expect(StatusIconRenderer.isTemplate(StatusIconState(fiveHourPercent: 10, sevenDayPercent: 20)))
        #expect(!StatusIconRenderer.isTemplate(StatusIconState(fiveHourPercent: 75, sevenDayPercent: 20)))
        #expect(!StatusIconRenderer.isTemplate(StatusIconState(fiveHourPercent: 10, sevenDayPercent: 95)))
        #expect(StatusIconRenderer.isTemplate(StatusIconState()))
    }

    @Test func showsTheFiveHourNumberWithoutPercentSign() {
        #expect(StatusIconRenderer.label(StatusIconState(fiveHourPercent: 41.6)) == "42")
        #expect(StatusIconRenderer.label(StatusIconState(fiveHourPercent: 120)) == "100")
        #expect(StatusIconRenderer.label(StatusIconState(fiveHourPercent: 0)) == "0")
        #expect(StatusIconRenderer.label(StatusIconState(sevenDayPercent: 50)) == "—")
    }

    @Test func fillsTheBorderCounterclockwiseFromNineOClock() throws {
        // A quarter of the way round: down the left side from the middle, round the corner, half the bottom.
        let bitmap = try render(StatusIconState(fiveHourPercent: 10, sevenDayPercent: 25))
        let border = Border(bitmap)
        #expect(isFilled(bitmap, border.leftBelowStart))
        #expect(isFilled(bitmap, border.bottomLeft))
        #expect(!isFilled(bitmap, border.bottomRight))
        #expect(!isFilled(bitmap, border.rightMiddle))
        #expect(!isFilled(bitmap, border.topMiddle))
        #expect(!isFilled(bitmap, border.leftAboveStart))
        #expect(bitmap.icon.isTemplate)
    }

    @Test func aFullWeekClosesTheBorder() throws {
        let bitmap = try render(StatusIconState(fiveHourPercent: 10, sevenDayPercent: 100), .dark)
        let border = Border(bitmap)
        for point in [border.leftBelowStart, border.bottomRight, border.rightMiddle, border.topMiddle, border.leftAboveStart] {
            #expect(isFilled(bitmap, point))
        }
    }

    @Test func anEmptyWeekLeavesOnlyAFaintTrack() throws {
        let bitmap = try render(StatusIconState(fiveHourPercent: 10, sevenDayPercent: 0))
        let track = bitmap.pixel(x: Border(bitmap).bottomLeft.x, y: Border(bitmap).bottomLeft.y)
        #expect(track.alpha > 30 && track.alpha < 130)
    }

    @Test func colorsTheNumberByTheFiveHourLevel() throws {
        let warning = try render(StatusIconState(fiveHourPercent: 75, sevenDayPercent: 10), .light)
        #expect(warning.contains(0xc98500, in: Border(warning).interior))
        let critical = try render(StatusIconState(fiveHourPercent: 95, sevenDayPercent: 10), .dark)
        #expect(critical.contains(0xff6b6b, in: Border(critical).interior))
        #expect(!critical.icon.isTemplate)
    }

    @Test func colorsTheBorderByTheWeeklyLevel() throws {
        let bitmap = try render(StatusIconState(fiveHourPercent: 10, sevenDayPercent: 95), .light)
        let border = Border(bitmap)
        #expect(bitmap.pixel(x: border.bottomLeft.x, y: border.bottomLeft.y).isClose(to: 0xd03b3b, tolerance: 12))
        // The five-hour number stays in the menu bar ink.
        #expect(!bitmap.contains(0xd03b3b, in: border.interior))
        #expect(!bitmap.icon.isTemplate)
    }

    @Test func dimsStaleState() throws {
        let bitmap = try render(StatusIconState(fiveHourPercent: 10, sevenDayPercent: 50, isStale: true))
        let alpha = bitmap.pixel(x: Border(bitmap).bottomLeft.x, y: Border(bitmap).bottomLeft.y).alpha
        #expect((118...137).contains(alpha))
    }

    @Test func emptyStateShowsADashAndNoFill() throws {
        let bitmap = try render(StatusIconState())
        #expect(!isFilled(bitmap, Border(bitmap).bottomLeft))
        #expect(bitmap.icon.isTemplate)
    }

    @Test func keepsOneWidthWhateverTheNumber() throws {
        let widths = try [StatusIconState(fiveHourPercent: 0), StatusIconState(fiveHourPercent: 100), StatusIconState()].map {
            try render($0).icon.size.width
        }
        #expect(Set(widths).count == 1)
    }

    @Test func scalesWithBackingFactor() throws {
        let double = try render(StatusIconState(fiveHourPercent: 50), scale: 2).icon
        #expect(double.size.height == 18)
        #expect(double.image.height == 36)
        #expect(double.image.width == Int((double.size.width * 2).rounded()))
        #expect(try render(StatusIconState(fiveHourPercent: 50), scale: 3).icon.image.height == 54)
    }

    @Test func buildsStateFromUsage() {
        var usage = UsageStatus()
        #expect(StatusIconState(usage: usage, warnPercent: 70, criticalPercent: 90, now: now) == StatusIconState())
        usage.apply(FetchResult(status: .ok, limits: UsageLimits(fiveHour: LimitWindow(percent: 42, resetsAt: nil))), now: now)
        #expect(StatusIconState(usage: usage, warnPercent: 60, criticalPercent: 80, now: now)
                == StatusIconState(fiveHourPercent: 42, warnPercent: 60, criticalPercent: 80))
        #expect(StatusIconState(usage: usage, warnPercent: 70, criticalPercent: 90, now: now.addingTimeInterval(16 * 60)).isStale)
    }

    @Test func paletteMatchesSpec() {
        #expect(Palette.meter(.normal, .light) == RGBA(hex: 0x2a78d6))
        #expect(Palette.meter(.normal, .dark) == RGBA(hex: 0x3987e5))
        #expect(Palette.meter(.critical, .light) == RGBA(hex: 0xd03b3b))
        #expect(Palette.level(.normal, .dark) == RGBA(1, 1, 1))
        #expect(Palette.level(.warning, .light) == RGBA(hex: 0xc98500))
    }
}
