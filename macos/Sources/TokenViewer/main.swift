import AppKit

let application = NSApplication.shared
application.setActivationPolicy(.accessory)
let statusItem = NSStatusBar.system.statusItem(withLength: NSStatusItem.variableLength)
statusItem.button?.title = "TV"
application.run()
