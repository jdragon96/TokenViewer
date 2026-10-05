import Foundation
import Testing
@testable import TokenViewerCore

@Suite struct CredentialParserTests {
    private let now = TestSupport.date("2026-10-05T12:00:00Z")
    /// 2026-10-06T12:00Z in epoch milliseconds, one day after `now`.
    private let tomorrowMs = 1_791_288_000_000

    private func json(expiresAt: Any? = nil, token: String = "sk-test", type: String = "max", tier: String = "default_claude_max_5x") -> Data {
        let oauth: [String: Any] = [
            "accessToken": token,
            "refreshToken": "never-read",
            "expiresAt": expiresAt ?? tomorrowMs,
            "subscriptionType": type,
            "rateLimitTier": tier,
        ]
        return Data(LogLines.json(["claudeAiOauth": oauth]).utf8)
    }

    @Test func parsesTokenAndPlan() {
        let result = CredentialParser.parse(json(), now: now)
        #expect(result.status == .ok)
        #expect(result.credential?.accessToken == "sk-test")
        #expect(result.credential?.expiresAt == TestSupport.date("2026-10-06T12:00:00Z"))
        #expect(result.credential?.subscriptionType == "max")
        #expect(result.credential?.rateLimitTier == "default_claude_max_5x")
    }

    @Test func reportsExpiredToken() {
        let result = CredentialParser.parse(json(expiresAt: 1_791_100_000_000), now: now)
        #expect(result.status == .expired)
        #expect(result.credential?.subscriptionType == "max")
    }

    @Test func acceptsEpochSecondsAndIsoStrings() {
        #expect(CredentialParser.parse(json(expiresAt: 1_791_288_000), now: now).status == .ok)
        #expect(CredentialParser.parse(json(expiresAt: "2026-10-06T00:00:00Z"), now: now).status == .ok)
        #expect(CredentialParser.parse(json(expiresAt: "2026-10-01T00:00:00Z"), now: now).status == .expired)
    }

    @Test func missingTokenIsMalformed() {
        #expect(CredentialParser.parse(json(token: ""), now: now).status == .malformed)
        #expect(CredentialParser.parse(Data("nope".utf8), now: now).status == .malformed)
        #expect(CredentialParser.parse(Data(#"{"mcpOAuth": {}}"#.utf8), now: now).status == .malformed)
    }

    @Test func formatsPlanLabel() {
        #expect(CredentialParser.planLabel(subscriptionType: "max", rateLimitTier: "default_claude_max_5x") == "Max 5x")
        #expect(CredentialParser.planLabel(subscriptionType: "MAX", rateLimitTier: "max_20x") == "Max 20x")
        #expect(CredentialParser.planLabel(subscriptionType: "max", rateLimitTier: "") == "Max")
        #expect(CredentialParser.planLabel(subscriptionType: "pro", rateLimitTier: "") == "Pro")
        #expect(CredentialParser.planLabel(subscriptionType: "team", rateLimitTier: "default_claude_max_5x") == "Team")
        #expect(CredentialParser.planLabel(subscriptionType: "enterprise", rateLimitTier: "") == "Enterprise")
        #expect(CredentialParser.planLabel(subscriptionType: "free", rateLimitTier: "") == "")
    }

    @Test func resolvesKeychainThenFile() {
        #expect(CredentialParser.resolve(keychain: .found(json()), readFile: { nil }, now: now).status == .ok)
        #expect(CredentialParser.resolve(keychain: .denied, readFile: { json() }, now: now).status == .accessDenied)
        #expect(CredentialParser.resolve(keychain: .notFound, readFile: { json() }, now: now).status == .ok)
        #expect(CredentialParser.resolve(keychain: .notFound, readFile: { nil }, now: now).status == .notFound)
    }
}
