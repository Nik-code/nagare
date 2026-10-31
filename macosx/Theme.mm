// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <CoreText/CoreText.h>
#import <objc/runtime.h>

#import "Theme.h"

static NSString* const kThemeModeKey = @"ThemeMode";

static NSColor* hexColor(uint32_t rgb)
{
    return [NSColor colorWithSRGBRed:((rgb >> 16) & 0xFF) / 255.0 green:((rgb >> 8) & 0xFF) / 255.0 blue:(rgb & 0xFF) / 255.0 alpha:1.0];
}

static BOOL isDark(NSAppearance* appearance)
{
    return [appearance bestMatchFromAppearancesWithNames:@[ NSAppearanceNameAqua, NSAppearanceNameDarkAqua ]] == NSAppearanceNameDarkAqua;
}

static NSColor* dynamicColor(NSString* name, uint32_t light, uint32_t dark)
{
    NSColor* lightColor = hexColor(light);
    NSColor* darkColor = hexColor(dark);
    return [NSColor colorWithName:name dynamicProvider:^NSColor*(NSAppearance* appearance) {
        return isDark(appearance) ? darkColor : lightColor;
    }];
}

static NSColor* faded(NSString* name, NSColor* color, CGFloat alpha)
{
    return [NSColor colorWithName:name dynamicProvider:^NSColor*(NSAppearance* appearance) {
        __block NSColor* resolved;
        [appearance performAsCurrentDrawingAppearance:^{
            resolved = [[color colorUsingColorSpace:NSColorSpace.sRGBColorSpace] colorWithAlphaComponent:alpha];
        }];
        return resolved;
    }];
}

// Nibs name system colors in the "System" catalog, so the same table serves both routes in.
static NSDictionary<NSString*, NSColor*>* systemColorTable()
{
    static NSDictionary<NSString*, NSColor*>* sTable = @{
        @"labelColor" : Theme.bright,
        @"textColor" : Theme.bright,
        @"controlTextColor" : Theme.bright,
        @"headerTextColor" : Theme.bright,
        @"selectedControlTextColor" : Theme.bright,
        @"selectedTextColor" : Theme.bright,
        @"secondaryLabelColor" : Theme.foreground,
        @"tertiaryLabelColor" : faded(@"themeTertiary", Theme.foreground, 0.72),
        @"quaternaryLabelColor" : faded(@"themeQuaternary", Theme.foreground, 0.45),
        @"disabledControlTextColor" : faded(@"themeDisabled", Theme.foreground, 0.55),
        @"windowBackgroundColor" : Theme.surface,
        @"underPageBackgroundColor" : Theme.surface,
        @"controlColor" : Theme.field,
        @"controlBackgroundColor" : Theme.surface,
        @"textBackgroundColor" : Theme.field,
        @"separatorColor" : Theme.border,
        @"gridColor" : Theme.border,
        @"controlAccentColor" : Theme.accent,
        @"keyboardFocusIndicatorColor" : Theme.accent,
        @"linkColor" : Theme.accent,
        @"selectedContentBackgroundColor" : Theme.selected,
        @"unemphasizedSelectedContentBackgroundColor" : Theme.selected,
        @"selectedTextBackgroundColor" : Theme.selected,
        @"unemphasizedSelectedTextBackgroundColor" : Theme.selected,
        @"selectedControlColor" : Theme.selected,
    };
    return sTable;
}

@implementation Theme

+ (void)install
{
    // Blue is the closest system accent, and it reaches the controls AppKit draws itself.
    NSUserDefaults* defaults = NSUserDefaults.standardUserDefaults;
    [defaults setInteger:4 forKey:@"AppleAccentColor"];
    [defaults setObject:@"0.698039 0.843137 1.000000 Blue" forKey:@"AppleHighlightColor"];

    NSURL* font = [NSBundle.mainBundle URLForResource:@"Outfit-Medium" withExtension:@"ttf"];
    if (font != nil)
    {
        CTFontManagerRegisterFontsForURL((__bridge CFURLRef)font, kCTFontManagerScopeProcess, nil);
    }

    [systemColorTable() enumerateKeysAndObjectsUsingBlock:^(NSString* name, NSColor* color, BOOL*) {
        Method method = class_getClassMethod(NSColor.class, NSSelectorFromString(name));
        if (method != nil)
        {
            method_setImplementation(method, imp_implementationWithBlock(^NSColor*(id) {
                return color;
            }));
        }
    }];

    Method alternating = class_getClassMethod(NSColor.class, @selector(alternatingContentBackgroundColors));
    method_setImplementation(alternating, imp_implementationWithBlock(^NSArray<NSColor*>*(id) {
        return @[ NSColor.clearColor, NSColor.clearColor ];
    }));

    Method catalog = class_getClassMethod(NSColor.class, @selector(colorWithCatalogName:colorName:));
    typedef NSColor* (*CatalogFunc)(id, SEL, NSColorListName, NSColorName);
    auto const original = reinterpret_cast<CatalogFunc>(method_getImplementation(catalog));
    method_setImplementation(catalog, imp_implementationWithBlock(^NSColor*(id self, NSColorListName list, NSColorName name) {
        NSColor* themed = [list isEqualToString:@"System"] ? systemColorTable()[name] : nil;
        return themed ?: original(self, @selector(colorWithCatalogName:colorName:), list, name);
    }));
}

+ (ThemeMode)mode
{
    return static_cast<ThemeMode>([NSUserDefaults.standardUserDefaults integerForKey:kThemeModeKey]);
}

+ (void)setMode:(ThemeMode)mode
{
    [NSUserDefaults.standardUserDefaults setInteger:mode forKey:kThemeModeKey];
    [self applySavedMode];
}

+ (void)applySavedMode
{
    switch (Theme.mode)
    {
    case ThemeModeLight:
        NSApp.appearance = [NSAppearance appearanceNamed:NSAppearanceNameAqua];
        break;
    case ThemeModeDark:
        NSApp.appearance = [NSAppearance appearanceNamed:NSAppearanceNameDarkAqua];
        break;
    default:
        NSApp.appearance = nil;
        break;
    }
}

+ (void)addThemeMenu
{
    NSMenu* viewMenu;
    for (NSMenuItem* item in NSApp.mainMenu.itemArray)
    {
        if ([item.submenu.title isEqualToString:@"View"])
        {
            viewMenu = item.submenu;
            break;
        }
    }
    if (viewMenu == nil)
    {
        return;
    }

    NSMenu* themeMenu = [[NSMenu alloc] initWithTitle:@"Theme"];
    NSArray<NSString*>* titles = @[ @"Match System", @"Light", @"Dark" ];
    for (NSUInteger index = 0; index < titles.count; ++index)
    {
        NSMenuItem* item = [themeMenu addItemWithTitle:titles[index] action:@selector(chooseMode:) keyEquivalent:@""];
        item.target = self;
        item.tag = index;
    }

    NSMenuItem* themeItem = [[NSMenuItem alloc] initWithTitle:@"Theme" action:nil keyEquivalent:@""];
    themeItem.submenu = themeMenu;
    [viewMenu addItem:NSMenuItem.separatorItem];
    [viewMenu addItem:themeItem];
}

+ (void)chooseMode:(NSMenuItem*)sender
{
    self.mode = static_cast<ThemeMode>(sender.tag);
}

+ (BOOL)validateMenuItem:(NSMenuItem*)item
{
    item.state = item.tag == Theme.mode ? NSControlStateValueOn : NSControlStateValueOff;
    return YES;
}

#define THEME_COLOR(NAME, LIGHT, DARK) \
    +(NSColor*)NAME \
    { \
        static NSColor* sColor = dynamicColor(@"theme." #NAME, LIGHT, DARK); \
        return sColor; \
    }

THEME_COLOR(surface, 0xf3eee4, 0x23211f)
THEME_COLOR(field, 0xfbf8f1, 0x1c1a18)
THEME_COLOR(border, 0xdcd6c8, 0x383531)
THEME_COLOR(foreground, 0x47524f, 0xb5ac9f)
THEME_COLOR(bright, 0x1c2a30, 0xefe6d6)
THEME_COLOR(hover, 0xebe5d8, 0x2c2a27)
THEME_COLOR(selected, 0xe4decf, 0x34312d)
THEME_COLOR(accent, 0x0c5f82, 0x8fd8cc)
THEME_COLOR(seed, 0xa3442a, 0xf0a98a)
THEME_COLOR(danger, 0xa3301a, 0xff9a7a)

+ (NSFont*)displayFontOfSize:(CGFloat)size
{
    return [NSFont fontWithName:@"Outfit-Medium" size:size] ?: [NSFont systemFontOfSize:size weight:NSFontWeightMedium];
}

+ (NSImage*)icon:(NSString*)name
{
    return [NSImage imageNamed:[@"Icon" stringByAppendingString:name]];
}

+ (NSButton*)iconButton:(NSString*)name target:(id)target action:(SEL)action
{
    NSButton* button = [NSButton buttonWithImage:[self icon:name] target:target action:action];
    button.bordered = NO;
    button.imageScaling = NSImageScaleProportionallyDown;
    button.contentTintColor = Theme.foreground;
    button.translatesAutoresizingMaskIntoConstraints = NO;
    return button;
}

+ (NSTextField*)labelWithFont:(NSFont*)font color:(NSColor*)color
{
    NSTextField* label = [NSTextField labelWithString:@""];
    label.font = font;
    label.textColor = color;
    label.lineBreakMode = NSLineBreakByTruncatingMiddle;
    label.translatesAutoresizingMaskIntoConstraints = NO;
    return label;
}

+ (id)layerContents:(NSString*)imageName appearance:(NSAppearance*)appearance scale:(CGFloat)scale
{
    __block id contents;
    [appearance performAsCurrentDrawingAppearance:^{
        NSImage* image = [NSImage imageNamed:imageName];
        // Catalog images pick a variant on first draw, so render through a CGImage for this appearance.
        NSRect rect = NSMakeRect(0, 0, image.size.width, image.size.height);
        contents = (__bridge id)[image CGImageForProposedRect:&rect context:nil hints:nil];
    }];
    (void)scale;
    return contents;
}

+ (void)styleWindow:(NSWindow*)window
{
    window.titlebarAppearsTransparent = YES;
    window.titleVisibility = NSWindowTitleHidden;
    window.styleMask |= NSWindowStyleMaskFullSizeContentView;
    window.titlebarSeparatorStyle = NSTitlebarSeparatorStyleNone;

    NSView* content = window.contentView;
    SurfaceView* surface = [[SurfaceView alloc] initWithFrame:content.bounds];
    surface.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    [content addSubview:surface positioned:NSWindowBelow relativeTo:nil];
}

+ (void)styleAddWindow:(NSWindow*)window nameField:(NSTextField*)nameField
{
    window.titlebarAppearsTransparent = YES;
    window.titleVisibility = NSWindowTitleHidden;
    window.titlebarSeparatorStyle = NSTitlebarSeparatorStyleNone;

    NSView* content = window.contentView;
    SurfaceView* surface = [[SurfaceView alloc] initWithFrame:content.bounds];
    surface.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    [content addSubview:surface positioned:NSWindowBelow relativeTo:nil];

    nameField.font = [self displayFontOfSize:20];

    NSButton* action = (NSButton*)window.defaultButtonCell.controlView;
    if ([action isKindOfClass:NSButton.class])
    {
        action.title = NSLocalizedString(@"Get", "Add window -> action");
        action.bezelStyle = NSBezelStyleGlass;
        action.bezelColor = Theme.accent;
    }
}

@end

@implementation SurfaceView

- (BOOL)wantsUpdateLayer
{
    return YES;
}

- (void)updateLayer
{
    self.layer.contents = [Theme layerContents:@"ArtSurface" appearance:self.effectiveAppearance scale:1];
    self.layer.contentsGravity = kCAGravityResizeAspectFill;
}

- (instancetype)initWithFrame:(NSRect)frame
{
    if ((self = [super initWithFrame:frame]))
    {
        self.wantsLayer = YES;
        self.layerContentsRedrawPolicy = NSViewLayerContentsRedrawOnSetNeedsDisplay;
    }
    return self;
}

- (void)viewDidChangeEffectiveAppearance
{
    [super viewDidChangeEffectiveAppearance];
    self.needsDisplay = YES;
}

- (NSView*)hitTest:(NSPoint)point
{
    return nil;
}

@end

@implementation ThemedRowView

- (NSBackgroundStyle)interiorBackgroundStyle
{
    return NSBackgroundStyleNormal;
}

- (void)drawBackgroundInRect:(NSRect)dirtyRect
{
    [Theme.border setFill];
    CGFloat const scale = self.window.backingScaleFactor ?: 2.0;
    NSRectFill(NSMakeRect(24.0, NSMaxY(self.bounds) - 1.0 / scale, NSWidth(self.bounds) - 48.0, 1.0 / scale));
}

- (void)drawSelectionInRect:(NSRect)dirtyRect
{
    [Theme.selected setFill];
    [[NSBezierPath bezierPathWithRoundedRect:NSInsetRect(self.bounds, 12.0, 3.0) xRadius:12.0 yRadius:12.0] fill];
}

- (void)drawSeparatorInRect:(NSRect)dirtyRect
{
}

@end
