import Foundation
import Testing
@testable import TokenViewerCore

@Suite struct ISODateTests {
    @Test func parsesCommonForms() {
        let base = TestSupport.date("2026-10-05T06:50:00Z")
        #expect(ISODate.parse("2026-10-05T06:50:00Z") == base)
        #expect(ISODate.parse("2026-10-05T06:50:00.500Z") == base.addingTimeInterval(0.5))
        #expect(ISODate.parse("2026-10-05T15:50:00+09:00") == base)
    }

    @Test func cutsMicrosecondsToMilliseconds() throws {
        let parsed = try #require(ISODate.parse("2026-10-05T06:50:00.180191+00:00"))
        #expect(abs(parsed.timeIntervalSince(TestSupport.date("2026-10-05T06:50:00Z")) - 0.180) < 0.0005)
    }

    @Test func rejectsGarbage() {
        #expect(ISODate.parse("yesterday") == nil)
        #expect(ISODate.parse("") == nil)
    }

    @Test func readsJsonNumbersAndDates() {
        #expect(JSONValues.date(NSNumber(value: 1_791_183_000)) == Date(timeIntervalSince1970: 1_791_183_000))
        #expect(JSONValues.date(NSNumber(value: 1_791_183_000_000.0)) == Date(timeIntervalSince1970: 1_791_183_000))
        #expect(JSONValues.date("2026-10-05T06:50:00Z") == TestSupport.date("2026-10-05T06:50:00Z"))
        #expect(JSONValues.date(NSNumber(value: true)) == nil)
        #expect(JSONValues.number(NSNumber(value: false)) == nil)
        #expect(JSONValues.number(NSNumber(value: 42.5)) == 42.5)
        #expect(JSONValues.number("42") == nil)
    }
}
