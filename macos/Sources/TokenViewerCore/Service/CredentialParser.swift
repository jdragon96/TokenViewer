import Foundation

/// Reads Claude Code's OAuth credential (v0.1.0 spec 3.2). Only the access token, expiry and plan are read;
/// the refresh token is never touched so the app cannot disturb Claude Code's login.
public enum CredentialParser {
    private static let planNames = ["max": "Max", "pro": "Pro", "team": "Team", "enterprise": "Enterprise"]

    /// Keychain first; the JSON file is what Claude Code writes when the Keychain is unavailable.
    public static func resolve(keychain: SecretLookup, readFile: () -> Data?, now: Date) -> CredentialResult {
        switch keychain {
        case .found(let data):
            return parse(data, now: now)
        case .denied:
            return CredentialResult(status: .accessDenied)
        case .notFound:
            guard let data = readFile() else {
                return CredentialResult(status: .notFound)
            }
            return parse(data, now: now)
        }
    }

    public static func parse(_ json: Data, now: Date) -> CredentialResult {
        guard let root = (try? JSONSerialization.jsonObject(with: json)) as? [String: Any],
              let oauth = root["claudeAiOauth"] as? [String: Any],
              let token = oauth["accessToken"] as? String, !token.isEmpty else {
            return CredentialResult(status: .malformed)
        }
        let credential = Credential(accessToken: token,
                                    expiresAt: JSONValues.date(oauth["expiresAt"]),
                                    subscriptionType: oauth["subscriptionType"] as? String ?? "",
                                    rateLimitTier: oauth["rateLimitTier"] as? String ?? "")
        let isExpired = credential.expiresAt.map { $0 <= now } ?? false
        return CredentialResult(status: isExpired ? .expired : .ok, credential: credential)
    }

    /// `max` + `default_claude_max_5x` → `Max 5x`; unknown plans give an empty label so the badge hides.
    public static func planLabel(subscriptionType: String, rateLimitTier: String) -> String {
        guard let name = planNames[subscriptionType.lowercased()] else {
            return ""
        }
        if name == "Max", let match = rateLimitTier.lowercased().firstMatch(of: #/(\d+)x/#) {
            return "Max \(match.1)x"
        }
        return name
    }
}
