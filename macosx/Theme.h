// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <AppKit/AppKit.h>

NS_ASSUME_NONNULL_BEGIN

typedef NS_ENUM(NSInteger, ThemeMode) {
    ThemeModeSystem = 0,
    ThemeModeOat = 1,
    ThemeModeSumi = 2,
};

/// Oat (light) and Sumi (dark), the palettes of the Nagi themes.
///
/// Every color is dynamic: Oat is the app's light appearance and Sumi its dark one,
/// so switching theme only changes the appearance of NSApp.
@interface Theme : NSObject

/// Points AppKit's semantic colors at the palette. Call once, before any nib loads.
+ (void)install;

/// Applies the saved theme. Needs NSApp to exist.
+ (void)applySavedMode;

/// Adds the Theme submenu to the main menu's View menu.
+ (void)addThemeMenu;

@property(class, nonatomic, readonly) ThemeMode mode;

@property(class, nonatomic, readonly) NSColor* background;
@property(class, nonatomic, readonly) NSColor* panel;
@property(class, nonatomic, readonly) NSColor* field;
@property(class, nonatomic, readonly) NSColor* border;
@property(class, nonatomic, readonly) NSColor* foreground;
@property(class, nonatomic, readonly) NSColor* bright;
@property(class, nonatomic, readonly) NSColor* hover;
@property(class, nonatomic, readonly) NSColor* selected;
@property(class, nonatomic, readonly) NSColor* accent;
@property(class, nonatomic, readonly) NSColor* seed;
@property(class, nonatomic, readonly) NSColor* check;
@property(class, nonatomic, readonly) NSColor* paused;
@property(class, nonatomic, readonly) NSColor* track;
@property(class, nonatomic, readonly) NSColor* danger;

/// A template-free SF Symbol in one of the palette's colors.
+ (nullable NSImage*)symbol:(NSString*)name color:(NSColor*)color size:(CGFloat)size;

@end

/// Row with a hairline under it and a quiet rounded selection.
@interface ThemedRowView : NSTableRowView
@end

NS_ASSUME_NONNULL_END
