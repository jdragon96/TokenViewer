// swift-tools-version: 6.0
import PackageDescription

let package = Package(
    name: "TokenViewer",
    platforms: [.macOS(.v14)],
    targets: [
        .target(name: "TokenViewerCore"),
        .executableTarget(name: "TokenViewer", dependencies: ["TokenViewerCore"]),
    ]
)
