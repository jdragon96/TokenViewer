import AppKit
import TokenViewerCore

/// The menu bar item: draws the bars and the 5-hour % and reports clicks (v0.1.0 spec 2.1).
@MainActor
final class StatusItemController: NSObject {
    var onClick: @MainActor () -> Void = {}

    private struct DrawKey: Equatable {
        var state: StatusIconState
        var appearance: MenuBarAppearance
        var scale: CGFloat
    }

    private let item = NSStatusBar.system.statusItem(withLength: NSStatusItem.variableLength)
    private var iconState = StatusIconState()
    private var drawnKey: DrawKey?
    private var appearanceObservation: NSKeyValueObservation?

    override init() {
        super.init()
        guard let button = item.button else {
            return
        }
        button.target = self
        button.action = #selector(handleClick)
        appearanceObservation = button.observe(\.effectiveAppearance) { [weak self] _, _ in
            MainActor.assumeIsolated { self?.redraw() }
        }
        redraw()
    }

    /// The item's frame in screen coordinates, for placing the dropdown under it.
    var anchorFrame: NSRect? { item.button?.window?.frame }

    func update(_ state: StatusIconState) {
        iconState = state
        redraw()
    }

    @objc private func handleClick() {
        // An LSUIElement app is never active on its own; activate it so the dropdown gets focus and closes on outside clicks.
        NSApp.activate()
        onClick()
    }

    /// Redraws only when the state, the menu bar appearance or the screen scale changed.
    private func redraw() {
        guard let button = item.button else {
            return
        }
        let isDark = button.effectiveAppearance.bestMatch(from: [.aqua, .darkAqua]) == .darkAqua
        let scale = max(button.window?.backingScaleFactor ?? NSScreen.main?.backingScaleFactor ?? 2, 2)
        let key = DrawKey(state: iconState, appearance: isDark ? .dark : .light, scale: scale)
        guard key != drawnKey, let icon = StatusIconRenderer.render(key.state, appearance: key.appearance, scale: scale) else {
            return
        }
        let image = NSImage(cgImage: icon.image, size: icon.size)
        image.isTemplate = icon.isTemplate
        button.image = image
        drawnKey = key
    }
}
