#import <AppKit/AppKit.h>

#include "UI/TrayIcon.h"

#include <algorithm>
#include <functional>

@interface TVStatusItemTarget : NSObject
{
@public
    std::function<void()> m_funcClicked;
    std::function<void()> m_funcAppearanceChanged;
}
- (void)onClick:(id)sender;
@end

@implementation TVStatusItemTarget
- (void)onClick:(id)sender
{
    (void)sender;
    // An LSUIElement app is never active on its own; activate it so the popup gets focus and closes on outside clicks.
    if (@available(macOS 14.0, *))
    {
        [NSApp activate];
    }
    else
    {
        [NSApp activateIgnoringOtherApps:YES];
    }
    if (m_funcClicked)
    {
        m_funcClicked();
    }
}

- (void)observeValueForKeyPath:(NSString*)keyPath ofObject:(id)object change:(NSDictionary*)change context:(void*)context
{
    (void)keyPath;
    (void)object;
    (void)change;
    (void)context;
    if (m_funcAppearanceChanged)
    {
        m_funcAppearanceChanged();
    }
}
@end

namespace
{
constexpr CGFloat kMinBackingScale = 2.0;
NSString* const kAppearanceKeyPath = @"effectiveAppearance";

NSImage* CreateNSImage(const QImage& image)
{
    const QImage imgArgb = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    CGColorSpaceRef colorSpace = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGContextRef context = CGBitmapContextCreate(const_cast<uchar*>(imgArgb.constBits()), imgArgb.width(), imgArgb.height(), 8,
        imgArgb.bytesPerLine(), colorSpace, kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Host);
    CGImageRef cgImage = CGBitmapContextCreateImage(context);
    const qreal dDevicePixelRatio = image.devicePixelRatio();
    NSImage* nsImage = [[NSImage alloc] initWithCGImage:cgImage
                                                   size:NSMakeSize(image.width() / dDevicePixelRatio, image.height() / dDevicePixelRatio)];
    CGImageRelease(cgImage);
    CGContextRelease(context);
    CGColorSpaceRelease(colorSpace);
    return nsImage;
}
}

class TrayIcon::TrayIconImpl
{
public:
    NSStatusItem* m_pStatusItem = nil;
    TVStatusItemTarget* m_pTarget = nil;
    TrayIconState m_state;

    EMenuBarAppearance DetectAppearance() const
    {
        NSAppearance* pAppearance = m_pStatusItem.button.effectiveAppearance;
        NSAppearanceName strName = [pAppearance bestMatchFromAppearancesWithNames:@[ NSAppearanceNameAqua, NSAppearanceNameDarkAqua ]];
        return [strName isEqualToString:NSAppearanceNameDarkAqua] ? EMenuBarAppearance::DARK : EMenuBarAppearance::LIGHT;
    }

    void Redraw()
    {
        NSWindow* pWindow = m_pStatusItem.button.window;
        const CGFloat dScale = pWindow ? pWindow.backingScaleFactor : NSScreen.mainScreen.backingScaleFactor;
        const TrayIconImage image = TrayIconPainter::Render(m_state, DetectAppearance(), std::max(dScale, kMinBackingScale));
        NSImage* pImage = CreateNSImage(image.m_imgIcon);
        // `template` is a C++ keyword, so use the setter instead of dot syntax.
        [pImage setTemplate:image.m_bTemplate ? YES : NO];
        m_pStatusItem.button.image = pImage;
    }
};

TrayIcon::TrayIcon(QObject* parent)
    : QObject(parent)
    , m_pimpl(std::make_unique<TrayIconImpl>())
{
    TrayIconImpl* pImpl = m_pimpl.get();
    pImpl->m_pStatusItem = [[NSStatusBar systemStatusBar] statusItemWithLength:NSVariableStatusItemLength];
    pImpl->m_pTarget = [[TVStatusItemTarget alloc] init];
    pImpl->m_pTarget->m_funcClicked = [this]() { emit Clicked(); };
    pImpl->m_pTarget->m_funcAppearanceChanged = [pImpl]() { pImpl->Redraw(); };

    NSStatusBarButton* pButton = pImpl->m_pStatusItem.button;
    pButton.target = pImpl->m_pTarget;
    pButton.action = @selector(onClick:);
    [pButton addObserver:pImpl->m_pTarget forKeyPath:kAppearanceKeyPath options:NSKeyValueObservingOptionNew context:nullptr];
    pImpl->m_pStatusItem.visible = NO;
    pImpl->Redraw();
}

TrayIcon::~TrayIcon()
{
    [m_pimpl->m_pStatusItem.button removeObserver:m_pimpl->m_pTarget forKeyPath:kAppearanceKeyPath];
    [[NSStatusBar systemStatusBar] removeStatusItem:m_pimpl->m_pStatusItem];
}

void TrayIcon::Show()
{
    m_pimpl->m_pStatusItem.visible = YES;
}

void TrayIcon::SetState(const TrayIconState& state)
{
    m_pimpl->m_state = state;
    m_pimpl->Redraw();
}

QRect TrayIcon::GetAnchorGeometry() const
{
    NSWindow* pWindow = m_pimpl->m_pStatusItem.button.window;
    if (pWindow == nil)
    {
        return QRect();
    }
    // Cocoa measures from the bottom-left of the primary screen; Qt from its top-left.
    const NSRect rcFrame = pWindow.frame;
    const CGFloat dPrimaryHeight = NSScreen.screens.firstObject.frame.size.height;
    return QRect(qRound(rcFrame.origin.x), qRound(dPrimaryHeight - rcFrame.origin.y - rcFrame.size.height),
        qRound(rcFrame.size.width), qRound(rcFrame.size.height));
}
