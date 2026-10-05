import Foundation

/// ISO 8601 parsing for log timestamps and API reset times.
enum ISODate {
    // ISO8601DateFormatter is thread-safe; these are configured once and only read afterwards.
    nonisolated(unsafe) private static let fractional: ISO8601DateFormatter = {
        let formatter = ISO8601DateFormatter()
        formatter.formatOptions = [.withInternetDateTime, .withFractionalSeconds]
        return formatter
    }()
    nonisolated(unsafe) private static let whole = ISO8601DateFormatter()

    static func parse(_ text: String) -> Date? {
        if let date = fractional.date(from: text) ?? whole.date(from: text) {
            return date
        }
        // The usage API sends microseconds; cut the fraction to milliseconds and try again.
        guard let dot = text.firstIndex(of: ".") else {
            return nil
        }
        let digitsStart = text.index(after: dot)
        let digitsEnd = text[digitsStart...].firstIndex { !($0.isASCII && $0.isNumber) } ?? text.endIndex
        guard text.distance(from: digitsStart, to: digitsEnd) > 3 else {
            return nil
        }
        let millisecondsEnd = text.index(digitsStart, offsetBy: 3)
        return fractional.date(from: String(text[..<millisecondsEnd]) + String(text[digitsEnd...]))
    }
}

enum JSONValues {
    private static let millisecondEpochThreshold = 1e12

    /// A JSON number. Booleans are rejected even though Foundation bridges them to NSNumber.
    static func number(_ value: Any?) -> Double? {
        guard let number = value as? NSNumber, CFGetTypeID(number) != CFBooleanGetTypeID() else {
            return nil
        }
        return number.doubleValue
    }

    /// Epoch seconds, epoch milliseconds or an ISO 8601 string.
    static func date(_ value: Any?) -> Date? {
        if let epoch = number(value) {
            let seconds = epoch > millisecondEpochThreshold ? epoch / 1000 : epoch
            return Date(timeIntervalSince1970: seconds)
        }
        if let text = value as? String {
            return ISODate.parse(text)
        }
        return nil
    }
}
