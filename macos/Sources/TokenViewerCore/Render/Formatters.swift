import Foundation

public enum Formatters {
    private static let smallestCent = 0.005
    private static let noReset = "리셋 시각 없음"

    public static func usd(_ usd: Double, approximate: Bool) -> String {
        let prefix = approximate ? "≈" : ""
        if usd > 0 && usd < smallestCent {
            return prefix + "<$0.01"
        }
        return prefix + "$" + usd.formatted(.number.precision(.fractionLength(2)).locale(Locale(identifier: "en_US")))
    }

    public static func tokens(_ count: Int) -> String {
        if count < 1_000 {
            return String(count)
        }
        if count < 1_000_000 {
            return String(format: count < 100_000 ? "%.1fK" : "%.0fK", Double(count) / 1_000)
        }
        return String(format: count < 10_000_000 ? "%.2fM" : "%.1fM", Double(count) / 1_000_000)
    }

    public static func resetCountdown(_ resetsAt: Date?, now: Date) -> String {
        guard let resetsAt else {
            return noReset
        }
        let seconds = Int(resetsAt.timeIntervalSince(now))
        if seconds <= 60 {
            return "곧 리셋"
        }
        let hours = seconds / 3600
        let minutes = (seconds % 3600) / 60
        if hours > 0 {
            return String(format: "%d:%02d 후 리셋", hours, minutes)
        }
        return "\(minutes)분 후 리셋"
    }

    public static func resetDay(_ resetsAt: Date?, now: Date, timeZone: TimeZone = .current) -> String {
        guard let resetsAt else {
            return noReset
        }
        if resetsAt.timeIntervalSince(now) < 86_400 {
            return resetCountdown(resetsAt, now: now)
        }
        let formatter = DateFormatter()
        formatter.locale = Locale(identifier: "ko_KR")
        formatter.timeZone = timeZone
        formatter.dateFormat = "E H:mm"
        return formatter.string(from: resetsAt) + " 리셋"
    }

    public static func age(since past: Date?, now: Date) -> String {
        guard let past else {
            return ""
        }
        let seconds = max(0, Int(now.timeIntervalSince(past)))
        if seconds < 60 {
            return "방금"
        }
        if seconds < 3600 {
            return "\(seconds / 60)분 전"
        }
        return "\(seconds / 3600)시간 전"
    }

    public static func percent(_ percent: Double) -> String {
        "\(Int(min(max(percent, 0), 100).rounded()))%"
    }

    /// The amount column of a breakdown row: dollars, or tokens when no price is known.
    public static func amount(_ row: BreakdownRow) -> String {
        row.isUnpriced ? "\(tokens(row.totalTokens)) 토큰" : usd(row.costUsd, approximate: row.isApproximate)
    }

    public static func tooltip(_ row: BreakdownRow) -> String {
        "\(row.detail)\n입력 \(tokens(row.input)) · 출력 \(tokens(row.output))\n캐시 쓰기 \(tokens(row.cacheWrite)) · 캐시 읽기 \(tokens(row.cacheRead))"
    }
}
