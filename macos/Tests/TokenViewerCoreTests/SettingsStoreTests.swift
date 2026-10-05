import Foundation
import Testing
@testable import TokenViewerCore

@Suite struct SettingsStoreTests {
    /// Runs `body` against an empty, throwaway defaults domain.
    private func withDefaults(_ body: (UserDefaults, SettingsStore) -> Void) {
        let name = "TokenViewerTests-\(UUID().uuidString)"
        let defaults = UserDefaults(suiteName: name)!
        defer { defaults.removePersistentDomain(forName: name) }
        body(defaults, SettingsStore(defaults: defaults))
    }

    @Test func defaultsWhenEmpty() {
        withDefaults { _, store in
            #expect(store.values == SettingsValues(intervalMinutes: 3, warnPercent: 70, criticalPercent: 90,
                                                   launchAtLogin: true, dimension: .project, period: .fiveHourWindow))
        }
    }

    @Test func rejectsUnknownInterval() {
        withDefaults { defaults, store in
            defaults.set(7, forKey: "refresh.intervalMinutes")
            #expect(store.values.intervalMinutes == 3)
            defaults.set(10, forKey: "refresh.intervalMinutes")
            #expect(store.values.intervalMinutes == 10)
        }
    }

    @Test func rejectsInvalidWarnPercent() {
        withDefaults { defaults, store in
            for (stored, expected) in [(52, 70), (45, 70), (100, 70), (85, 85)] {
                defaults.set(stored, forKey: "alert.warnPercent")
                #expect(store.values.warnPercent == expected)
            }
        }
    }

    @Test func keepsCriticalAboveWarn() {
        withDefaults { defaults, store in
            defaults.set(90, forKey: "alert.warnPercent")
            defaults.set(85, forKey: "alert.criticalPercent")
            #expect(store.values.criticalPercent == 95)
            defaults.set(95, forKey: "alert.warnPercent")
            defaults.removeObject(forKey: "alert.criticalPercent")
            #expect(store.values.criticalPercent == 100)
            defaults.set(60, forKey: "alert.warnPercent")
            defaults.set(100, forKey: "alert.criticalPercent")
            #expect(store.values.criticalPercent == 100)
            defaults.set(70, forKey: "alert.warnPercent")
            defaults.set(92, forKey: "alert.criticalPercent")
            #expect(store.values.criticalPercent == 90)
        }
    }

    @Test func roundTripsAllValues() {
        withDefaults { defaults, store in
            let values = SettingsValues(intervalMinutes: 5, warnPercent: 60, criticalPercent: 80,
                                        launchAtLogin: false, dimension: .session, period: .thirtyDays)
            store.save(values)
            #expect(store.values == values)
            #expect(defaults.integer(forKey: "refresh.intervalMinutes") == 5)
            #expect(defaults.integer(forKey: "popup.dimension") == 2)
            #expect(defaults.object(forKey: "app.launchAtLogin") as? Bool == false)
        }
    }

    @Test func savesCorrectedValues() {
        withDefaults { defaults, store in
            var values = store.values
            values.warnPercent = 90
            values.criticalPercent = 85
            values.intervalMinutes = 7
            store.save(values)
            // The file keeps what every reader would see, not the out-of-range input.
            #expect(defaults.integer(forKey: "alert.criticalPercent") == 95)
            #expect(defaults.integer(forKey: "refresh.intervalMinutes") == 3)
        }
    }

    @Test func readsValuesQtStoredAsStrings() {
        withDefaults { defaults, store in
            defaults.set("85", forKey: "alert.warnPercent")
            defaults.set("false", forKey: "app.launchAtLogin")
            #expect(store.values.warnPercent == 85)
            #expect(!store.values.launchAtLogin)
        }
    }

    @Test func ignoresUnknownEnumValues() {
        withDefaults { defaults, store in
            defaults.set(7, forKey: "popup.dimension")
            defaults.set(-1, forKey: "popup.period")
            #expect(store.values.dimension == .project)
            #expect(store.values.period == .fiveHourWindow)
        }
    }
}
