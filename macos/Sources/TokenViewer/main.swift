import AppKit

if CommandLine.arguments.contains("--unregister-login-item") {
    LoginItem.unregister()
    exit(0)
}

let application = NSApplication.shared
let delegate = AppDelegate()
application.delegate = delegate
application.setActivationPolicy(.accessory)
application.run()
