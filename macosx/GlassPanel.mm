// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "GlassPanel.h"
#import "Theme.h"

@implementation GlassPanel

- (instancetype)initWithSize:(NSSize)size style:(NSGlassEffectViewStyle)style
{
    if ((self = [super initWithContentRect:NSMakeRect(0, 0, size.width, size.height)
                                 styleMask:NSWindowStyleMaskBorderless
                                   backing:NSBackingStoreBuffered
                                     defer:YES]))
    {
        self.opaque = NO;
        self.backgroundColor = NSColor.clearColor;
        self.hasShadow = YES;
        self.movableByWindowBackground = YES;
        self.releasedWhenClosed = NO;
        self.hidesOnDeactivate = NO;
        self.collectionBehavior = NSWindowCollectionBehaviorFullScreenAuxiliary;

        NSGlassEffectView* glass = [[NSGlassEffectView alloc] initWithFrame:NSMakeRect(0, 0, size.width, size.height)];
        glass.cornerRadius = 28;
        glass.style = style;
        if (style == NSGlassEffectViewStyleClear)
        {
            // Clear glass over the dot rows needs a little tint to keep text readable.
            glass.tintColor = [Theme.surface colorWithAlphaComponent:0.4];
        }
        self.contentView = glass;

        _root = [[NSView alloc] initWithFrame:glass.bounds];
        _root.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
        glass.contentView = _root;
    }
    return self;
}

- (BOOL)canBecomeKeyWindow
{
    return YES;
}

- (BOOL)canBecomeMainWindow
{
    return YES;
}

- (void)resignKeyWindow
{
    [super resignKeyWindow];
    if (self.closesOnResignKey && self.attachedSheet == nil)
    {
        [self orderOut:nil];
    }
}

- (void)cancelOperation:(id)sender
{
    if (self.cancelHandler != nil)
    {
        self.cancelHandler();
    }
    else
    {
        [self orderOut:nil];
    }
}

- (void)placeOverMainWindow
{
    NSWindow* main = NSApp.mainWindow;
    if (main == nil || [main isKindOfClass:GlassPanel.class])
    {
        // A link from a browser arrives before Nagare is frontmost, so nothing is main yet.
        main = nil;
        for (NSWindow* window in NSApp.orderedWindows)
        {
            if (window.isVisible && ![window isKindOfClass:GlassPanel.class])
            {
                main = window;
                break;
            }
        }
    }
    NSRect const anchor = main != nil ? main.frame : NSScreen.mainScreen.visibleFrame;
    NSSize const size = self.frame.size;

    // Several files added at once shouldn't land exactly on top of each other.
    CGFloat cascade = 0.0;
    for (NSWindow* window in NSApp.windows)
    {
        if (window != self && window.visible && [window isKindOfClass:GlassPanel.class])
        {
            cascade += 22.0;
        }
    }
    [self setFrameOrigin:NSMakePoint(NSMidX(anchor) - size.width / 2.0 + cascade, NSMaxY(anchor) - size.height - 72.0 - cascade)];
}

@end
