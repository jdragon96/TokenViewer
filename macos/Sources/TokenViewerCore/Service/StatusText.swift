import Foundation

/// The dropdown's notice box and footer line (v0.1.0 spec 2.2).
public enum StatusText {
    public struct Footer: Sendable, Equatable {
        public var text: String
        public var isFailure: Bool

        public init(text: String, isFailure: Bool) {
            self.text = text
            self.isFailure = isFailure
        }
    }

    public static func notice(_ status: UsageStatus, now: Date) -> String {
        guard status.lastAttemptAt != nil else {
            return "한도를 불러오는 중…"
        }
        switch status.lastStatus {
        case .ok:
            return ""
        case .notLoggedIn:
            return "Claude Code 로그인 정보를 찾지 못했습니다.\n터미널에서 claude 실행 후 /login 하세요."
        case .keychainDenied:
            return "Keychain 접근이 거부되었습니다.\n'다시 확인'을 누르면 허용 창이 다시 뜹니다."
        case .tokenExpired:
            return "로그인 토큰이 만료되었습니다. Claude Code 를 실행하면 갱신됩니다."
        case .badResponse:
            return "사용량 API 응답 형식이 바뀌었습니다.\n~/Library/Logs/TokenViewer 를 확인하세요."
        case .rateLimited, .networkError, .serverError:
            break
        }
        let failure = "갱신 실패: \(status.lastDetail)"
        guard let lastSuccessAt = status.lastSuccessAt else {
            return failure
        }
        return "\(Formatters.age(since: lastSuccessAt, now: now)) 값입니다. \(failure)"
    }

    public static func footer(_ status: UsageStatus, now: Date) -> Footer {
        let isFailing = status.lastAttemptAt != nil && status.lastStatus != .ok
        guard let lastSuccessAt = status.lastSuccessAt else {
            return Footer(text: "한도 정보 없음", isFailure: isFailing)
        }
        let age = Formatters.age(since: lastSuccessAt, now: now)
        return isFailing ? Footer(text: "갱신 실패 · \(age)", isFailure: true) : Footer(text: "\(age) 업데이트", isFailure: false)
    }
}
