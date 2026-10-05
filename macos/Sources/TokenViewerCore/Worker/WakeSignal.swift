import Foundation

/// Lets a worker loop sleep until a timeout or an early wake-up, whichever comes first.
/// A wake-up sent while nobody waits is kept and ends the next wait at once.
public actor WakeSignal {
    private var waiter: CheckedContinuation<Void, Never>?
    private var timer: Task<Void, Never>?
    private var generation = 0
    private var isPending = false

    public init() {}

    /// Waits for `wake()` or the timeout. A nil timeout waits for `wake()` only.
    public func wait(timeout: TimeInterval?) async {
        if isPending {
            isPending = false
            return
        }
        generation += 1
        let current = generation
        if let timeout {
            timer = Task { [weak self] in
                try? await Task.sleep(nanoseconds: UInt64(max(timeout, 0) * 1_000_000_000))
                await self?.timeOut(generation: current)
            }
        }
        await withCheckedContinuation { waiter = $0 }
    }

    public func wake() {
        if waiter == nil {
            isPending = true
        } else {
            resume()
        }
    }

    private func timeOut(generation timedOut: Int) {
        // A cancelled or older timer must not end a newer wait.
        guard timedOut == generation, waiter != nil else {
            return
        }
        resume()
    }

    private func resume() {
        timer?.cancel()
        timer = nil
        let continuation = waiter
        waiter = nil
        continuation?.resume()
    }
}
