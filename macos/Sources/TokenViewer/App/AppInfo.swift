import Foundation

enum AppInfo {
    static var version: String {
        Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String ?? "dev"
    }

    /// Set by build-app.sh from the git remote; nil in builds without one.
    static var repositoryURL: URL? {
        guard let repository = Bundle.main.object(forInfoDictionaryKey: "TVRepository") as? String, !repository.isEmpty else {
            return nil
        }
        return URL(string: "https://github.com/\(repository)")
    }

    static var pricesURL: URL? {
        Bundle.main.url(forResource: "prices", withExtension: "json")
    }
}
