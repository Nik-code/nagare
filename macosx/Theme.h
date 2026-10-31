// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <AppKit/AppKit.h>

NS_ASSUME_NONNULL_BEGIN

typedef NS_ENUM(NSInteger, ThemeMode) {
    ThemeModeSystem = 0,
    ThemeModeLight = 1,
    ThemeModeDark = 2,
};

/// Nagare's palette: warm paper in light mode, warm grey in dark.
///
/// Every color is dynamic, so switching theme only changes the appearance of NSApp.
@interface Theme : NSObject

/// Points AppKit's semantic colors at the palette and registers Outfit. Call once, before any nib loads.
+ (void)install;

/// Applies the saved theme. Needs NSApp to exist.
+ (void)applySavedMode;

/// Adds the Theme submenu to the main menu's View menu.
+ (void)addThemeMenu;

@property(class, nonatomic) ThemeMode mode;

@property(class, nonatomic, readonly) NSColor* surface;
@property(class, nonatomic, readonly) NSColor* field;
@property(class, nonatomic, readonly) NSColor* border;
@property(class, nonatomic, readonly) NSColor* foreground;
@property(class, nonatomic, readonly) NSColor* bright;
@property(class, nonatomic, readonly) NSColor* hover;
@property(class, nonatomic, readonly) NSColor* selected;
@property(class, nonatomic, readonly) NSColor* accent;
@property(class, nonatomic, readonly) NSColor* seed;
@property(class, nonatomic, readonly) NSColor* danger;

/// Outfit Medium, for the wordmark, big numbers and window titles.
+ (NSFont*)displayFontOfSize:(CGFloat)size;

/// A Tabler icon from the asset catalog, as a template image.
+ (NSImage*)icon:(NSString*)name;

/// A borderless, bezel-free button showing a Tabler icon.
+ (NSButton*)iconButton:(NSString*)name target:(nullable id)target action:(nullable SEL)action;

/// A Liquid Glass push button.
+ (NSButton*)glassButton:(NSString*)title target:(nullable id)target action:(nullable SEL)action;

/// A plain text label.
+ (NSTextField*)labelWithFont:(NSFont*)font color:(NSColor*)color;

/// A small label over a big number, for the stat rows.
+ (NSView*)statWithTitle:(NSString*)title value:(NSTextField*)value;

/// One setting: title on the left with an optional hint under it, its control on the right, a hairline above.
+ (NSView*)rowWithTitle:(NSString*)title hintField:(nullable NSTextField*)hintField control:(NSView*)control height:(CGFloat)height;

/// A quiet caption field for use as a row's hint.
+ (NSTextField*)hintField:(NSString*)text;

/// A borderless, see-through scroll view around a list.
+ (NSScrollView*)scrollViewWithDocument:(NSView*)document;

/// A headerless, gridless list on a clear background. Its rows should be ThemedRowViews.
+ (void)styleList:(NSTableView*)list rowHeight:(CGFloat)rowHeight;

/// A value field for a row, with a rounded bezel.
+ (NSTextField*)valueField:(CGFloat)width formatter:(NSFormatter*)formatter target:(nullable id)target action:(nullable SEL)action;

/// The image as it should look under `appearance`, ready to use as layer contents.
+ (nullable id)layerContents:(NSString*)imageName appearance:(NSAppearance*)appearance scale:(CGFloat)scale;

/// Blends the titlebar into the window and puts the textured surface behind the content.
+ (void)styleWindow:(NSWindow*)window;

@end

/// The textured paper or ash that fills a window.
@interface SurfaceView : NSView
@end

/// Row with a hairline under it and a quiet rounded selection.
@interface ThemedRowView : NSTableRowView

/// Defaults suit a full-width list: 24 for the hairline, 12 for the selection. A negative hairline inset draws none.
- (instancetype)initWithHairlineInset:(CGFloat)hairlineInset selectionInset:(CGFloat)selectionInset;

@end

NS_ASSUME_NONNULL_END
