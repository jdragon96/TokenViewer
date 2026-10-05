import Foundation
import Testing
@testable import TokenViewerCore

@Suite struct UsageResponseParserTests {
    private func body(_ object: [String: Any]) -> Data {
        Data(LogLines.json(object).utf8)
    }

    @Test func parsesRecordedResponseShape() throws {
        let result = UsageResponseParser.parse(try TestSupport.fixture("usage_response.json"))
        #expect(result.status == .ok)
        #expect(result.limits.fiveHour?.percent == 42)
        #expect(result.limits.sevenDay?.percent == 18)
        let resetsAt = try #require(result.limits.fiveHour?.resetsAt)
        #expect(abs(resetsAt.timeIntervalSince(TestSupport.date("2026-10-05T07:40:00Z")) - 0.123) < 0.001)
    }

    @Test func acceptsUsedPercentageAndEpoch() {
        let result = UsageResponseParser.parse(body(["five_hour": ["used_percentage": 55.5, "resets_at": 1_791_201_600]]))
        #expect(result.status == .ok)
        #expect(result.limits.fiveHour == LimitWindow(percent: 55.5, resetsAt: Date(timeIntervalSince1970: 1_791_201_600)))
        #expect(result.limits.sevenDay == nil)
    }

    @Test func acceptsEpochMilliseconds() {
        let result = UsageResponseParser.parse(body(["seven_day": ["utilization": 1, "resets_at": 1_791_201_600_000]]))
        #expect(result.limits.sevenDay?.resetsAt == Date(timeIntervalSince1970: 1_791_201_600))
    }

    @Test func clampsOutOfRange() {
        let result = UsageResponseParser.parse(body(["five_hour": ["utilization": 130], "seven_day": ["utilization": -5]]))
        #expect(result.limits.fiveHour?.percent == 100)
        #expect(result.limits.sevenDay?.percent == 0)
    }

    @Test func nullWindowLeavesMeterEmpty() {
        let result = UsageResponseParser.parse(body(["five_hour": NSNull(), "seven_day": ["utilization": 10]]))
        #expect(result.status == .ok)
        #expect(result.limits.fiveHour == nil)
        #expect(result.limits.sevenDay?.percent == 10)
    }

    @Test func nullPercentLeavesMeterEmpty() {
        let result = UsageResponseParser.parse(body(["five_hour": ["utilization": NSNull()]]))
        #expect(result.status == .ok)
        #expect(result.limits.fiveHour == nil)
    }

    @Test func missingWindowsIsBadResponse() {
        let data = body(["other": 1])
        let result = UsageResponseParser.parse(data)
        #expect(result.status == .badResponse)
        #expect(result.detail == "five_hour / seven_day 필드가 없습니다")
        #expect(result.rawBody == data)
    }

    @Test func windowFormatDriftIsBadResponse() {
        let drifts: [Any] = ["42%", ["percent": 42], ["utilization": "42"]]
        for drift in drifts {
            let result = UsageResponseParser.parse(body(["five_hour": drift]))
            #expect(result.status == .badResponse)
            #expect(result.detail == "five_hour / seven_day 형식이 바뀌었습니다")
            #expect(result.limits == UsageLimits())
        }
    }

    @Test func notJsonIsBadResponse() {
        #expect(UsageResponseParser.parse(Data("<html>login</html>".utf8)).status == .badResponse)
        #expect(UsageResponseParser.parse(Data()).status == .badResponse)
    }

    @Test func mapsHttpStatuses() {
        func map(_ status: Int, error: String? = nil) -> FetchResult {
            UsageResponseParser.map(HTTPResponse(status: status, body: Data("x".utf8), errorText: error))
        }
        #expect(map(401).status == .tokenExpired)
        #expect(map(403).detail == "인증이 만료되었습니다 (HTTP 403)")
        #expect(map(429).status == .rateLimited)
        #expect(map(503).status == .serverError)
        #expect(map(503).detail == "서버 오류 (HTTP 503)")
        #expect(map(0, error: "오프라인").detail == "오프라인")
        #expect(map(0).status == .networkError)
        #expect(map(0).detail == "네트워크 연결 없음")
        #expect(map(200, error: "끊김").status == .networkError)
        #expect(map(404).status == .badResponse)
        #expect(map(404).rawBody == Data("x".utf8))
        #expect(UsageResponseParser.map(HTTPResponse(status: 200, body: body(["five_hour": ["utilization": 1]]))).status == .ok)
    }
}
