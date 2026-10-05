import Foundation
import Testing
@testable import TokenViewerCore

@Suite struct TokenAggregatorTests {
    private let now = TestSupport.date("2026-10-05T12:00:00Z")

    private func record(_ key: String, project: String = "/w/app", model: String = "claude-opus-5-5", session: String = "s1",
                        at time: String = "2026-10-05T10:00:00Z", input: Int = 1_000_000) -> TokenRecord {
        TokenRecord(key: key, timestamp: TestSupport.date(time), sessionId: session, projectPath: project, model: model, input: input)
    }

    private func aggregate(_ records: [TokenRecord], titles: [String: String] = [:], by dimension: BreakdownDimension,
                           from start: Date? = nil) throws -> Breakdown {
        TokenAggregator.aggregate(LogSnapshot(records: records, sessionTitles: titles), prices: try TestSupport.prices(),
                                  dimension: dimension, from: start, timeZone: TestSupport.seoul)
    }

    @Test func groupsByProjectAndSortsByCost() throws {
        let breakdown = try aggregate([
            record("a", project: "/w/small", input: 1_000_000),
            record("b", project: "/w/big", input: 3_000_000),
            record("c", project: "/w/big", input: 1_000_000),
        ], by: .project)
        #expect(breakdown.rows.map(\.label) == ["big", "small"])
        #expect(breakdown.rows[0].detail == "/w/big")
        #expect(abs(breakdown.rows[0].costUsd - 16) < 1e-9)
        #expect(breakdown.rows[0].input == 4_000_000)
        #expect(abs(breakdown.totalUsd - 20) < 1e-9)
        #expect(breakdown.otherCount == 0)
    }

    @Test func groupsByModelWithDisplayNames() throws {
        let breakdown = try aggregate([
            record("a", model: "claude-opus-5-5"),
            record("b", model: "claude-haiku-4-5-20251001"),
        ], by: .model)
        #expect(breakdown.rows.map(\.label) == ["Opus 5.5", "Haiku 4.5"])
    }

    @Test func disambiguatesSameProjectNames() throws {
        let breakdown = try aggregate([record("a", project: "/a/x/app"), record("b", project: "/b/y/app")], by: .project)
        #expect(Set(breakdown.rows.map(\.label)) == ["x/app", "y/app"])
    }

    @Test func keepsNonAsciiProjectNames() throws {
        let breakdown = try aggregate([record("a", project: "/Users/me/작업/앱")], by: .project)
        #expect(breakdown.rows.map(\.label) == ["앱"])
    }

    @Test func sessionUsesTitleOrFallback() throws {
        let breakdown = try aggregate([
            record("a", session: "s1"),
            record("b", project: "/w/web", session: "s2", at: "2026-10-05T06:27:00Z"),
        ], titles: ["s1": "로그 파서"], by: .session)
        // 06:27Z is 15:27 in Seoul.
        #expect(Set(breakdown.rows.map(\.label)) == ["로그 파서", "web · 10/5 15:27"])
    }

    @Test func foldsBeyondTopCountIntoOther() throws {
        let records = (1...7).map { record("k\($0)", project: "/w/p\($0)", input: (8 - $0) * 1_000_000) }
        let breakdown = try aggregate(records, by: .project)
        #expect(breakdown.rows.map(\.label) == ["p1", "p2", "p3", "p4", "p5"])
        #expect(breakdown.otherCount == 2)
        #expect(breakdown.otherRow.label == "기타 2개")
        // p6 and p7 hold 2M and 1M input tokens at $4 per 1M.
        #expect(abs(breakdown.otherRow.costUsd - 12) < 1e-9)
        #expect(abs(breakdown.totalUsd - 112) < 1e-9)
        #expect(breakdown.displayRows.count == 6)
    }

    @Test func marksApproximateAndUnpriced() throws {
        let breakdown = try aggregate([
            record("a", project: "/w/family", model: "claude-opus-6"),
            record("b", project: "/w/unknown", model: "gpt-5"),
            record("c", project: "/w/mixed", model: "claude-opus-5-5"),
            record("d", project: "/w/mixed", model: "gpt-5"),
        ], by: .project)
        let rows = Dictionary(uniqueKeysWithValues: breakdown.rows.map { ($0.label, $0) })
        #expect(rows["family"]?.isApproximate == true)
        #expect(rows["unknown"]?.isUnpriced == true)
        #expect(rows["unknown"]?.costUsd == 0)
        #expect(rows["mixed"]?.isApproximate == true)
        #expect(rows["mixed"]?.isUnpriced == false)
    }

    @Test func filtersByPeriodStart() throws {
        let breakdown = try aggregate([
            record("early", project: "/w/early", at: "2026-10-05T08:00:00Z"),
            record("late", project: "/w/late", at: "2026-10-05T11:00:00Z"),
        ], by: .project, from: TestSupport.date("2026-10-05T10:00:00Z"))
        #expect(breakdown.rows.map(\.label) == ["late"])
    }

    @Test func calculatesFiveHourWindowStart() {
        let resets = now.addingTimeInterval(2 * 3600)
        #expect(TokenAggregator.periodStart(.fiveHourWindow, now: now, fiveHourResetsAt: resets) == resets.addingTimeInterval(-5 * 3600))
        // A reset time that already passed belongs to the previous window.
        #expect(TokenAggregator.periodStart(.fiveHourWindow, now: now, fiveHourResetsAt: now.addingTimeInterval(-60)) == now.addingTimeInterval(-5 * 3600))
        #expect(TokenAggregator.periodStart(.fiveHourWindow, now: now, fiveHourResetsAt: nil) == now.addingTimeInterval(-5 * 3600))
    }

    @Test func calculatesTodayFromLocalMidnight() {
        var calendar = Calendar(identifier: .gregorian)
        calendar.timeZone = TestSupport.seoul
        // 12:00Z is 21:00 in Seoul, so the local day began at 15:00Z the day before.
        #expect(TokenAggregator.periodStart(.today, now: now, fiveHourResetsAt: nil, calendar: calendar) == TestSupport.date("2026-10-04T15:00:00Z"))
    }

    @Test func calculatesDayPeriods() {
        #expect(TokenAggregator.periodStart(.sevenDays, now: now, fiveHourResetsAt: nil) == now.addingTimeInterval(-7 * 86_400))
        #expect(TokenAggregator.periodStart(.thirtyDays, now: now, fiveHourResetsAt: nil) == now.addingTimeInterval(-30 * 86_400))
    }

    @Test func formatsModelNames() {
        #expect(TokenAggregator.modelDisplayName("claude-opus-5-5") == "Opus 5.5")
        #expect(TokenAggregator.modelDisplayName("claude-haiku-4-5-20251001") == "Haiku 4.5")
        #expect(TokenAggregator.modelDisplayName("claude-fable-5") == "Fable 5")
        #expect(TokenAggregator.modelDisplayName("gpt-5") == "gpt-5")
    }
}
