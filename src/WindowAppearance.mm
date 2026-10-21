#include "WindowAppearance.h"
#include <QQuickWindow>
#import <AppKit/AppKit.h>

void configureWindowAppearance(QQuickWindow *window, bool dark) {
    if (!window) return;
    // Keep the macOS window machinery (traffic lights, resizing, fullscreen)
    // while the workspace toolbar occupies the title area.
    window->setFlags(window->flags() | Qt::ExpandedClientAreaHint
                     | Qt::NoTitleBarBackgroundHint);
    window->create();
    NSView *view = reinterpret_cast<NSView *>(window->winId());
    NSWindow *native = view.window;
    if (!native) return;
    native.titleVisibility = NSWindowTitleHidden;
    native.titlebarAppearsTransparent = YES;
    native.styleMask |= NSWindowStyleMaskFullSizeContentView;
    native.movableByWindowBackground = NO;
    native.appearance = [NSAppearance appearanceNamed:dark ? NSAppearanceNameDarkAqua : NSAppearanceNameAqua];
    if (@available(macOS 11.0, *))
        native.titlebarSeparatorStyle = NSTitlebarSeparatorStyleNone;
}
