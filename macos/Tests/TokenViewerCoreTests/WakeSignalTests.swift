import Foundation
import Testing
@testable import TokenViewerCore

@Suite struct WakeSignalTests {
    @Test func timesOut() async {
        let signal = WakeSignal()
        let start = Date()
        await signal.wait(timeout: 0.05)
        #expect(Date().timeIntervalSince(start) >= 0.04)
    }

    @Test func wakeEndsWaitEarly() async {
        let signal = WakeSignal()
        let start = Date()
        Task {
            try? await Task.sleep(nanoseconds: 50_000_000)
            await signal.wake()
        }
        await signal.wait(timeout: 10)
        #expect(Date().timeIntervalSince(start) < 5)
    }

    @Test func wakeBeforeWaitIsKept() async {
        let signal = WakeSignal()
        await signal.wake()
        let start = Date()
        await signal.wait(timeout: 10)
        #expect(Date().timeIntervalSince(start) < 1)
    }

    @Test func waitWithoutTimeoutNeedsWake() async {
        let signal = WakeSignal()
        let finished = Recorder<Bool>()
        Task {
            await signal.wait(timeout: nil)
            await finished.append(true)
        }
        try? await Task.sleep(nanoseconds: 150_000_000)
        #expect(await finished.values.isEmpty)
        await signal.wake()
        #expect(await waitUntil { await !finished.values.isEmpty })
    }
}
