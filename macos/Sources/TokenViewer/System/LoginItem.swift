import Foundation
import os
import ServiceManagement
import TokenViewerCore

/// Launch at login through SMAppService, so System Settings lists the app by name and removal leaves nothing behind.
enum LoginItem {
    private static let log = Logger(subsystem: AppConstants.bundleIdentifier, category: "LoginItem")

    /// Matches the registration to `enabled`.
    static func apply(enabled: Bool) {
        let service = SMAppService.mainApp
        do {
            if enabled {
                guard isInstalled, service.status == .notRegistered || service.status == .notFound else {
                    return
                }
                try service.register()
            } else if service.status == .enabled || service.status == .requiresApproval {
                try service.unregister()
            }
        } catch {
            log.error("launch-at-login update failed: \(error.localizedDescription, privacy: .public)")
        }
    }

    /// Used by `install.sh --uninstall` through the `--unregister-login-item` argument.
    static func unregister() {
        try? SMAppService.mainApp.unregister()
    }

    /// Only an installed app registers; a build in macos/dist or `swift run` would leave a stale entry once rebuilt or moved.
    private static var isInstalled: Bool {
        let path = Bundle.main.bundleURL.path
        let userApplications = FileManager.default.homeDirectoryForCurrentUser.appendingPathComponent("Applications").path
        return Bundle.main.bundleURL.pathExtension == "app" && (path.hasPrefix("/Applications/") || path.hasPrefix(userApplications + "/"))
    }
}
