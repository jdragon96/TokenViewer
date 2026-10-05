// Draws TokenViewer's app icon into an .iconset folder: `swift make-icon.swift <out>/AppIcon.iconset`.
// Like the menu bar item, the icon is drawn in code so the repository holds no image assets.
// It shows the same two bars: the 5-hour window on top (orange) and the week below (blue).
import CoreGraphics
import Foundation
import ImageIO
import UniformTypeIdentifiers

let canvas: CGFloat = 1024
// macOS icon grid: an 824 pt rounded square centred on the 1024 pt canvas.
let body = CGRect(x: 100, y: 100, width: 824, height: 824)

func color(_ hex: UInt32, alpha: CGFloat = 1) -> CGColor {
    CGColor(srgbRed: CGFloat((hex >> 16) & 0xff) / 255, green: CGFloat((hex >> 8) & 0xff) / 255, blue: CGFloat(hex & 0xff) / 255, alpha: alpha)
}

/// The rounded square macOS icons use: a superellipse, which has smoother corners than a plain rounded rect.
func squircle(in rect: CGRect, exponent: CGFloat = 5) -> CGPath {
    let path = CGMutablePath()
    let steps = 720
    for step in 0...steps {
        let angle = CGFloat(step) / CGFloat(steps) * 2 * .pi
        let cosine = cos(angle)
        let sine = sin(angle)
        let x = pow(abs(cosine), 2 / exponent) * (cosine < 0 ? -1 : 1)
        let y = pow(abs(sine), 2 / exponent) * (sine < 0 ? -1 : 1)
        let point = CGPoint(x: rect.midX + x * rect.width / 2, y: rect.midY + y * rect.height / 2)
        step == 0 ? path.move(to: point) : path.addLine(to: point)
    }
    path.closeSubpath()
    return path
}

func bar(_ rect: CGRect) -> CGPath {
    CGPath(roundedRect: rect, cornerWidth: rect.height / 2, cornerHeight: rect.height / 2, transform: nil)
}

func drawIcon(in context: CGContext) {
    // Core Graphics puts y = 0 at the bottom; flip so the layout reads top-down.
    context.translateBy(x: 0, y: canvas)
    context.scaleBy(x: 1, y: -1)

    let shape = squircle(in: body)
    context.saveGState()
    context.setShadow(offset: CGSize(width: 0, height: 10), blur: 28, color: color(0x000000, alpha: 0.3))
    context.addPath(shape)
    context.setFillColor(color(0x1b1e26))
    context.fillPath()
    context.restoreGState()

    context.saveGState()
    context.addPath(shape)
    context.clip()
    let background = CGGradient(colorsSpace: CGColorSpace(name: CGColorSpace.sRGB), colors: [color(0x323846), color(0x15171d)] as CFArray, locations: [0, 1])!
    context.drawLinearGradient(background, start: CGPoint(x: 0, y: body.minY), end: CGPoint(x: 0, y: body.maxY), options: [])

    let trackWidth: CGFloat = 560
    let barHeight: CGFloat = 96
    let gap: CGFloat = 72
    let left = (canvas - trackWidth) / 2
    let top = (canvas - (barHeight * 2 + gap)) / 2
    let bars: [(top: CGFloat, fraction: CGFloat, fill: CGColor)] = [
        (top, 0.72, color(0xfab219)),
        (top + barHeight + gap, 0.38, color(0x3987e5)),
    ]
    for item in bars {
        context.addPath(bar(CGRect(x: left, y: item.top, width: trackWidth, height: barHeight)))
        context.setFillColor(color(0xffffff, alpha: 0.14))
        context.fillPath()
        context.addPath(bar(CGRect(x: left, y: item.top, width: trackWidth * item.fraction, height: barHeight)))
        context.setFillColor(item.fill)
        context.fillPath()
    }
    context.restoreGState()
}

func writePNG(pixels: Int, to url: URL) throws {
    guard let space = CGColorSpace(name: CGColorSpace.sRGB),
          let context = CGContext(data: nil, width: pixels, height: pixels, bitsPerComponent: 8, bytesPerRow: 0,
                                  space: space, bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue) else {
        throw CocoaError(.fileWriteUnknown)
    }
    context.scaleBy(x: CGFloat(pixels) / canvas, y: CGFloat(pixels) / canvas)
    drawIcon(in: context)
    guard let image = context.makeImage(),
          let destination = CGImageDestinationCreateWithURL(url as CFURL, UTType.png.identifier as CFString, 1, nil) else {
        throw CocoaError(.fileWriteUnknown)
    }
    CGImageDestinationAddImage(destination, image, nil)
    guard CGImageDestinationFinalize(destination) else {
        throw CocoaError(.fileWriteUnknown)
    }
}

guard CommandLine.arguments.count == 2 else {
    FileHandle.standardError.write(Data("usage: swift make-icon.swift <out>/AppIcon.iconset\n".utf8))
    exit(2)
}
let iconset = URL(fileURLWithPath: CommandLine.arguments[1])
try FileManager.default.createDirectory(at: iconset, withIntermediateDirectories: true)
// iconutil expects these names: each point size at 1x and 2x. The 1024 px image (512@2x) is left out on
// purpose: with it, macOS 26 judges this squircle as off-shape and boxes the icon in a gray plate.
for points in [16, 32, 128, 256, 512] {
    try writePNG(pixels: points, to: iconset.appendingPathComponent("icon_\(points)x\(points).png"))
    if points < 512 {
        try writePNG(pixels: points * 2, to: iconset.appendingPathComponent("icon_\(points)x\(points)@2x.png"))
    }
}
