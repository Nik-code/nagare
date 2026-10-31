// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <AppKit/AppKit.h>

NS_ASSUME_NONNULL_BEGIN

/// A borderless floating panel made of Liquid Glass, used for search and the add windows.
@interface GlassPanel : NSPanel

- (instancetype)initWithSize:(NSSize)size style:(NSGlassEffectViewStyle)style;

/// Add content here. It sits inside the glass and resizes with the panel.
@property(nonatomic, readonly) NSView* root;

/// Dismisses the panel as soon as anything else takes the keyboard, such as a click on the main window.
@property(nonatomic) BOOL closesOnResignKey;

/// Runs instead of the plain hide when Esc is pressed.
@property(nonatomic, copy, nullable) void (^cancelHandler)(void);

/// Centers the panel near the top of the main window.
- (void)placeOverMainWindow;

@end

NS_ASSUME_NONNULL_END
