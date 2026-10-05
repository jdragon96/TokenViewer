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

    /// Reads the pixel at a point position (origin top-left) from the rendered icon.
    private func pixel(_ icon: StatusIconImage, x: CGFloat, y: CGFloat, scale: CGFloat = 2) -> Pixel {
        let image = icon.image
        var bytes = [UInt8](repeating: 0, count: image.width * image.height * 4)
        bytes.withUnsafeMutableBytes { buffer in
            let context = CGContext(data: buffer.baseAddress, width: image.width, height: image.height, bitsPerComponent: 8,
                                    bytesPerRow: image.width * 4, space: CGColorSpace(name: CGColorSpace.sRGB)!,
                                    bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!
            context.draw(image, in: CGRect(x: 0, y: 0, width: image.width, height: image.height))
        }
        let offset = (Int(y * scale) * image.width + Int(x * scale)) * 4
        return Pixel(red: Int(bytes[offset]), green: Int(bytes[offset + 1]), blue: Int(bytes[offset + 2]), alpha: Int(bytes[offset + 3]))
    }

    private func render(_ state: StatusIconState, _ appearance: MenuBarAppearance = .light, scale: CGFloat = 2) throws -> StatusIconImage {
        try #require(StatusIconRenderer.render(state, appearance: appearance, scale: scale))
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

    @Test func formatsLabel() {
        #expect(StatusIconRenderer.label(StatusIconState(fiveHourPercent: 41.6)) == "42%")
        #expect(StatusIconRenderer.label(StatusIconState(fiveHourPercent: 120)) == "100%")
        #expect(StatusIconRenderer.label(StatusIconState(sevenDayPercent: 50)) == "—")
    }

    @Test func fillsFiveHourBarProportionally() throws {
        let icon = try render(StatusIconState(fiveHourPercent: 50, sevenDayPercent: 0))
        #expect(pixel(icon, x: 4, y: 6).alpha > 200)
        #expect(pixel(icon, x: 15, y: 6).alpha < 40)
        #expect(icon.isTemplate)
    }

    @Test func usesLevelColorWhenWarning() throws {
        #expect(pixel(try render(StatusIconState(fiveHourPercent: 75), .light), x: 4, y: 6).isClose(to: 0xc98500))
        #expect(pixel(try render(StatusIconState(fiveHourPercent: 75), .dark), x: 4, y: 6).isClose(to: 0xfab219))
        #expect(pixel(try render(StatusIconState(fiveHourPercent: 95), .dark), x: 4, y: 6).isClose(to: 0xff6b6b))
    }

    @Test func colorsSevenDayBarIndependently() throws {
        let icon = try render(StatusIconState(fiveHourPercent: 10, sevenDayPercent: 95), .light)
        #expect(pixel(icon, x: 4, y: 12).isClose(to: 0xd03b3b))
        let fiveHour = pixel(icon, x: 1, y: 6)
        #expect(fiveHour.isClose(to: 0x000000, tolerance: 30))
        #expect(fiveHour.alpha > 200)
        #expect(!icon.isTemplate)
    }

    @Test func dimsStaleState() throws {
        let alpha = pixel(try render(StatusIconState(fiveHourPercent: 50, isStale: true)), x: 4, y: 6).alpha
        #expect((118...137).contains(alpha))
    }

    @Test func emptyStateHasNoFill() throws {
        let icon = try render(StatusIconState())
        #expect(pixel(icon, x: 4, y: 6).alpha < 40)
        #expect(icon.isTemplate)
    }

    @Test func scalesWithBackingFactor() throws {
        let double = try render(StatusIconState(fiveHourPercent: 50), scale: 2)
        #expect(double.size.height == 18)
        #expect(double.image.height == 36)
        #expect(double.image.width == Int((double.size.width * 2).rounded()))
        #expect(try render(StatusIconState(fiveHourPercent: 50), scale: 3).image.height == 54)
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
