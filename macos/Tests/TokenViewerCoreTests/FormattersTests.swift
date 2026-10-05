import Foundation
import Testing
@testable import TokenViewerCore

@Suite struct FormattersTests {
    private let now = TestSupport.date("2026-10-05T03:00:00Z")

    @Test func formatsUsd() {
        #expect(Formatters.usd(12.4, approximate: false) == "$12.40")
        #expect(Formatters.usd(1234.5, approximate: false) == "$1,234.50")
        #expect(Formatters.usd(3, approximate: true) == "≈$3.00")
        #expect(Formatters.usd(0.004, approximate: false) == "<$0.01")
        #expect(Formatters.usd(0, approximate: false) == "$0.00")
    }

    @Test func formatsTokens() {
        #expect(Formatters.tokens(999) == "999")
        #expect(Formatters.tokens(1_234) == "1.2K")
        #expect(Formatters.tokens(123_456) == "123K")
        #expect(Formatters.tokens(1_234_567) == "1.23M")
        #expect(Formatters.tokens(12_345_678) == "12.3M")
    }

    @Test func formatsResetCountdown() {
        #expect(Formatters.resetCountdown(nil, now: now) == "리셋 시각 없음")
        #expect(Formatters.resetCountdown(now.addingTimeInterval(30), now: now) == "곧 리셋")
        #expect(Formatters.resetCountdown(now.addingTimeInterval(25 * 60 + 10), now: now) == "25분 후 리셋")
        #expect(Formatters.resetCountdown(now.addingTimeInterval(2 * 3600 + 13 * 60), now: now) == "2:13 후 리셋")
        #expect(Formatters.resetCountdown(now.addingTimeInterval(3600 + 5 * 60), now: now) == "1:05 후 리셋")
    }

    @Test func formatsResetDay() {
        // 2026-10-08T00:00Z is Thursday 09:00 in Seoul.
        #expect(Formatters.resetDay(TestSupport.date("2026-10-08T00:00:00Z"), now: now, timeZone: TestSupport.seoul) == "목 9:00 리셋")
        #expect(Formatters.resetDay(now.addingTimeInterval(3 * 3600), now: now, timeZone: TestSupport.seoul) == "3:00 후 리셋")
        #expect(Formatters.resetDay(nil, now: now) == "리셋 시각 없음")
    }

    @Test func formatsAge() {
        #expect(Formatters.age(since: nil, now: now) == "")
        #expect(Formatters.age(since: now.addingTimeInterval(-30), now: now) == "방금")
        #expect(Formatters.age(since: now.addingTimeInterval(-18 * 60), now: now) == "18분 전")
        #expect(Formatters.age(since: now.addingTimeInterval(-3 * 3600 - 59), now: now) == "3시간 전")
        #expect(Formatters.age(since: now.addingTimeInterval(60), now: now) == "방금")
    }

    @Test func formatsPercent() {
        #expect(Formatters.percent(41.5) == "42%")
        #expect(Formatters.percent(120) == "100%")
        #expect(Formatters.percent(-3) == "0%")
    }

    @Test func formatsRowAmountAndTooltip() {
        var row = BreakdownRow()
        row.detail = "/Users/me/work/app"
        row.costUsd = 12.4
        row.input = 1_200
        row.output = 340
        row.cacheWrite = 5_000
        row.cacheRead = 2_000_000
        #expect(Formatters.amount(row) == "$12.40")
        #expect(Formatters.tooltip(row) == "/Users/me/work/app\n입력 1.2K · 출력 340\n캐시 쓰기 5.0K · 캐시 읽기 2.00M")
        row.isUnpriced = true
        #expect(Formatters.amount(row) == "2.01M 토큰")
    }
}
