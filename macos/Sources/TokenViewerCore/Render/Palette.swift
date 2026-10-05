import CoreGraphics

public struct RGBA: Sendable, Equatable {
    public var red: Double
    public var green: Double
    public var blue: Double
    public var alpha: Double

    public init(_ red: Double, _ green: Double, _ blue: Double, _ alpha: Double = 1) {
        self.red = red
        self.green = green
        self.blue = blue
        self.alpha = alpha
    }

    init(hex: UInt32) {
        self.init(Double((hex >> 16) & 0xff) / 255, Double((hex >> 8) & 0xff) / 255, Double(hex & 0xff) / 255)
    }

    public var cgColor: CGColor { CGColor(srgbRed: red, green: green, blue: blue, alpha: alpha) }

    func withAlpha(_ alpha: Double) -> RGBA {
        RGBA(red, green, blue, alpha)
    }
}

public enum BarLevel: Sendable, Equatable {
    case normal
    case warning
    case critical
}

public enum MenuBarAppearance: Sendable, Equatable {
    case light
    case dark
}

/// Colors from v0.1.0 spec 2.1 and 2.2.
public enum Palette {
    public static func ink(_ appearance: MenuBarAppearance) -> RGBA {
        appearance == .dark ? RGBA(1, 1, 1) : RGBA(0, 0, 0)
    }

    public static func level(_ level: BarLevel, _ appearance: MenuBarAppearance) -> RGBA {
        let isDark = appearance == .dark
        switch level {
        case .warning:
            return RGBA(hex: isDark ? 0xfab219 : 0xc98500)
        case .critical:
            return RGBA(hex: isDark ? 0xff6b6b : 0xd03b3b)
        case .normal:
            return ink(appearance)
        }
    }

    /// The dropdown meters are blue rather than ink while nothing is wrong.
    public static func meter(_ level: BarLevel, _ appearance: MenuBarAppearance) -> RGBA {
        level == .normal ? RGBA(hex: appearance == .dark ? 0x3987e5 : 0x2a78d6) : self.level(level, appearance)
    }
}
