import Foundation

public enum AppConstants {
    public static let bundleIdentifier = "com.tokenviewer.TokenViewer"
    public static let keychainService = "Claude Code-credentials"
    public static let usageURL = URL(string: "https://api.anthropic.com/api/oauth/usage")!
    public static let usageBetaHeader = "oauth-2025-04-20"
    public static let requestTimeout: TimeInterval = 10
    public static let logScanInterval: TimeInterval = 60
    public static let wakeRefreshDelay: TimeInterval = 8
    public static let clockTickInterval: TimeInterval = 5
}
