import Foundation

public struct Credential: Sendable, Equatable {
    public var accessToken: String
    public var expiresAt: Date?
    public var subscriptionType: String
    public var rateLimitTier: String

    public init(accessToken: String, expiresAt: Date?, subscriptionType: String, rateLimitTier: String) {
        self.accessToken = accessToken
        self.expiresAt = expiresAt
        self.subscriptionType = subscriptionType
        self.rateLimitTier = rateLimitTier
    }
}

public enum CredentialStatus: Sendable, Equatable {
    case ok
    case notFound
    case accessDenied
    case malformed
    case expired
}

public struct CredentialResult: Sendable, Equatable {
    public var status: CredentialStatus
    public var credential: Credential?

    public init(status: CredentialStatus, credential: Credential? = nil) {
        self.status = status
        self.credential = credential
    }
}

/// What the Keychain answered for Claude Code's credential item.
public enum SecretLookup: Sendable, Equatable {
    case found(Data)
    case notFound
    case denied
}
