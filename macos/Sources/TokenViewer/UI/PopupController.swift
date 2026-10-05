import AppKit
import SwiftUI

/// Shows the dropdown right below the menu bar item and closes it on an outside click (v0.1.0 spec 2.2).
@MainActor
final class PopupController: NSObject, NSWindowDelegate {
    private static let gap: CGFloat = 4
    private static let screenMargin: CGFloat = 8
    private static let reopenGuard: TimeInterval = 0.3

    private let panel: PopupPanel
    private let hostingView: NSView
    private var anchorTop: CGFloat?
    private var closedAt: Date?
    private var outsideClickMonitor: Any?

    init<Content: View>(content: Content) {
        let hostingView = NSHostingView(rootView: content)
        self.hostingView = hostingView
        panel = PopupPanel(contentView: hostingView)
        super.init()
        panel.delegate = self
        panel.onCancel = { [weak self] in self?.close() }
    }

    var isShown: Bool { panel.isVisible }

    /// The click that closes the panel from outside can also land on the menu bar item; that click must not reopen it.
    var wasJustClosed: Bool { closedAt.map { Date().timeIntervalSince($0) < Self.reopenGuard } ?? false }

    func show(below itemFrame: NSRect?) {
        hostingView.layoutSubtreeIfNeeded()
        let size = hostingView.fittingSize
        let itemScreen = itemFrame.flatMap { frame in
            NSScreen.screens.first { $0.frame.contains(NSPoint(x: frame.midX, y: frame.midY)) }
        }
        // The item can report a frame on no screen (seen while Mission Control was open); use the main screen's top-right then.
        let anchor = itemScreen == nil ? nil : itemFrame
        let visible = (itemScreen ?? NSScreen.main)?.visibleFrame ?? NSRect(x: 0, y: 0, width: 1440, height: 900)
        let minX = visible.minX + Self.screenMargin
        let maxX = max(minX, visible.maxX - size.width - Self.screenMargin)
        let wantedX = anchor.map { $0.midX - size.width / 2 } ?? maxX
        let top = (anchor?.minY ?? visible.maxY) - Self.gap
        anchorTop = top
        panel.setFrame(NSRect(x: min(max(wantedX, minX), maxX), y: top - size.height, width: size.width, height: size.height), display: true)
        panel.makeKeyAndOrderFront(nil)
        startOutsideClickMonitor()
    }

    func close() {
        guard panel.isVisible else {
            return
        }
        panel.orderOut(nil)
        stopOutsideClickMonitor()
        closedAt = Date()
    }

    func windowDidResignKey(_ notification: Notification) {
        close()
    }

    func windowDidResize(_ notification: Notification) {
        // Rows arrive after the panel opens; keep its top edge under the menu bar item while it grows.
        guard let anchorTop, panel.frame.maxY != anchorTop else {
            return
        }
        panel.setFrameOrigin(NSPoint(x: panel.frame.minX, y: anchorTop - panel.frame.height))
        panel.invalidateShadow()
    }

    private func startOutsideClickMonitor() {
        guard outsideClickMonitor == nil else {
            return
        }
        // Clicks in other apps never reach this app's windows, so watch them globally.
        outsideClickMonitor = NSEvent.addGlobalMonitorForEvents(matching: [.leftMouseDown, .rightMouseDown]) { [weak self] _ in
            MainActor.assumeIsolated { self?.close() }
        }
    }

    private func stopOutsideClickMonitor() {
        if let outsideClickMonitor {
            NSEvent.removeMonitor(outsideClickMonitor)
        }
        outsideClickMonitor = nil
    }
}
