import Foundation
import Security
import TokenViewerCore

/// Reads Claude Code's credential (v0.1.0 spec 3.2): Keychain first, then `~/.claude/.credentials.json`.
enum KeychainReader {
    // SecItemCopyMatching blocks while the macOS "allow access" prompt is open; keep that off the Swift concurrency pool.
    private static let queue = DispatchQueue(label: "com.tokenviewer.keychain")

    static func loadCredential(now: Date) async -> CredentialResult {
        let lookup = await withCheckedContinuation { continuation in
            queue.async {
                continuation.resume(returning: copySecret(service: AppConstants.keychainService))
            }
        }
        let fileURL = FileManager.default.homeDirectoryForCurrentUser.appendingPathComponent(".claude/.credentials.json")
        return CredentialParser.resolve(keychain: lookup, readFile: { try? Data(contentsOf: fileURL) }, now: now)
    }

    private static func copySecret(service: String) -> SecretLookup {
        let query: [String: Any] = [
            kSecClass as String: kSecClassGenericPassword,
            kSecAttrService as String: service,
            kSecReturnData as String: true,
            kSecMatchLimit as String: kSecMatchLimitOne,
        ]
        var result: CFTypeRef?
        switch SecItemCopyMatching(query as CFDictionary, &result) {
        case errSecSuccess:
            guard let data = result as? Data else {
                return .notFound
            }
            return .found(data)
        case errSecItemNotFound:
            return .notFound
        default:
            return .denied
        }
    }
}
