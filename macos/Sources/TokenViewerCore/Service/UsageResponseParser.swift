import Foundation

/// Turns `/api/oauth/usage` answers into a FetchResult (v0.1.0 spec 3.1, 7). The API is undocumented,
/// so the parser accepts known variants and reports anything else as format drift instead of guessing.
public enum UsageResponseParser {
    private static let fiveHourKey = "five_hour"
    private static let sevenDayKey = "seven_day"
    private static let utilizationKey = "utilization"
    private static let usedPercentageKey = "used_percentage"

    private struct FormatDrift: Error {}

    public static func map(_ response: HTTPResponse) -> FetchResult {
        let status = response.status
        if status == 200 && response.errorText == nil {
            return parse(response.body)
        }
        switch status {
        case 401, 403:
            return FetchResult(status: .tokenExpired, detail: "인증이 만료되었습니다 (HTTP \(status))", httpStatus: status)
        case 429:
            return FetchResult(status: .rateLimited, detail: "요청이 너무 많습니다 (HTTP 429)", httpStatus: status)
        case 500...:
            return FetchResult(status: .serverError, detail: "서버 오류 (HTTP \(status))", httpStatus: status)
        case 0, 200:
            let detail = response.errorText.flatMap { $0.isEmpty ? nil : $0 } ?? "네트워크 연결 없음"
            return FetchResult(status: .networkError, detail: detail, httpStatus: status)
        default:
            return FetchResult(status: .badResponse, detail: "예상하지 못한 응답 (HTTP \(status))", httpStatus: status, rawBody: response.body)
        }
    }

    public static func parse(_ body: Data) -> FetchResult {
        guard let root = (try? JSONSerialization.jsonObject(with: body)) as? [String: Any],
              root[fiveHourKey] != nil || root[sevenDayKey] != nil else {
            return FetchResult(status: .badResponse, detail: "five_hour / seven_day 필드가 없습니다", httpStatus: 200, rawBody: body)
        }
        do {
            let limits = UsageLimits(fiveHour: try window(root[fiveHourKey]), sevenDay: try window(root[sevenDayKey]))
            return FetchResult(status: .ok, limits: limits, httpStatus: 200)
        } catch {
            return FetchResult(status: .badResponse, detail: "five_hour / seven_day 형식이 바뀌었습니다", httpStatus: 200, rawBody: body)
        }
    }

    private static func window(_ value: Any?) throws -> LimitWindow? {
        guard let value, !(value is NSNull) else {
            return nil
        }
        guard let object = value as? [String: Any],
              object[utilizationKey] != nil || object[usedPercentageKey] != nil else {
            throw FormatDrift()
        }
        guard let percent = JSONValues.number(object[utilizationKey]) ?? JSONValues.number(object[usedPercentageKey]) else {
            // A percent key that is null leaves the meter empty; any other type is format drift.
            if (object[utilizationKey] ?? object[usedPercentageKey]) is NSNull {
                return nil
            }
            throw FormatDrift()
        }
        return LimitWindow(percent: min(max(percent, 0), 100), resetsAt: JSONValues.date(object["resets_at"]))
    }
}
