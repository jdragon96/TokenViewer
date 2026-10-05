import AppKit

/// A borderless, see-through panel; the SwiftUI content draws the rounded background itself.
final class PopupPanel: NSPanel {
    var onCancel: () -> Void = {}

    init(contentView: NSView) {
        super.init(contentRect: NSRect(x: 0, y: 0, width: 300, height: 300), styleMask: [.borderless], backing: .buffered, defer: true)
        level = .popUpMenu
        collectionBehavior = [.canJoinAllSpaces, .fullScreenAuxiliary]
        isReleasedWhenClosed = false
        hidesOnDeactivate = false
        backgroundColor = .clear
        isOpaque = false
        hasShadow = true
        self.contentView = contentView
    }

    // Borderless windows refuse key status by default; the pickers and buttons need it.
    override var canBecomeKey: Bool { true }

    override func cancelOperation(_ sender: Any?) {
        onCancel()
    }
}
