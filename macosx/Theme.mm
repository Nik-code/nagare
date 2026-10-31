// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <objc/runtime.h>

#import "Theme.h"

static NSString* const kThemeModeKey = @"ThemeMode";

static NSColor* hexColor(uint32_t rgb)
{
    return [NSColor colorWithSRGBRed:((rgb >> 16) & 0xFF) / 255.0 green:((rgb >> 8) & 0xFF) / 255.0 blue:(rgb & 0xFF) / 255.0 alpha:1.0];
}

static NSColor* dynamicColor(NSString* name, uint32_t oat, uint32_t sumi)
{
    NSColor* light = hexColor(oat);
    NSColor* dark = hexColor(sumi);
    return [NSColor colorWithName:name dynamicProvider:^NSColor*(NSAppearance* appearance) {
        BOOL const isDark = [appearance bestMatchFromAppearancesWithNames:@[ NSAppearanceNameAqua, NSAppearanceNameDarkAqua ]] ==
            NSAppearanceNameDarkAqua;
        return isDark ? dark : light;
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
        @"selectedMenuItemTextColor" : Theme.bright,
        @"secondaryLabelColor" : Theme.foreground,
        @"tertiaryLabelColor" : faded(@"themeTertiary", Theme.foreground, 0.72),
        @"quaternaryLabelColor" : faded(@"themeQuaternary", Theme.foreground, 0.45),
        @"disabledControlTextColor" : faded(@"themeDisabled", Theme.foreground, 0.55),
        @"windowBackgroundColor" : Theme.background,
        @"underPageBackgroundColor" : Theme.background,
        @"controlColor" : Theme.field,
        @"controlBackgroundColor" : Theme.panel,
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
    // Orange is the closest system accent, and it reaches the controls AppKit draws itself.
    NSUserDefaults* defaults = NSUserDefaults.standardUserDefaults;
    [defaults setInteger:1 forKey:@"AppleAccentColor"];
    [defaults setObject:@"1.000000 0.874510 0.701961 Orange" forKey:@"AppleHighlightColor"];

    Class meta = object_getClass(NSColor.class);
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
        return @[ Theme.panel, Theme.panel ];
    }));

    Method catalog = class_getClassMethod(meta, @selector(colorWithCatalogName:colorName:));
    catalog = catalog ?: class_getClassMethod(NSColor.class, @selector(colorWithCatalogName:colorName:));
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

+ (void)applySavedMode
{
    switch (Theme.mode)
    {
    case ThemeModeOat:
        NSApp.appearance = [NSAppearance appearanceNamed:NSAppearanceNameAqua];
        break;
    case ThemeModeSumi:
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
    NSArray<NSString*>* titles = @[ @"Match System", @"Oat", @"Sumi" ];
    for (NSUInteger index = 0; index < titles.count; ++index)
    {
        NSMenuItem* item = [themeMenu addItemWithTitle:titles[index] action:@selector(chooseMode:) keyEquivalent:@""];
        item.target = self;
        item.tag = index;
        item.state = static_cast<ThemeMode>(index) == Theme.mode ? NSControlStateValueOn : NSControlStateValueOff;
    }

    NSMenuItem* themeItem = [[NSMenuItem alloc] initWithTitle:@"Theme" action:nil keyEquivalent:@""];
    themeItem.submenu = themeMenu;
    [viewMenu addItem:NSMenuItem.separatorItem];
    [viewMenu addItem:themeItem];
}

+ (void)chooseMode:(NSMenuItem*)sender
{
    [NSUserDefaults.standardUserDefaults setInteger:sender.tag forKey:kThemeModeKey];
    for (NSMenuItem* item in sender.menu.itemArray)
    {
        item.state = item == sender ? NSControlStateValueOn : NSControlStateValueOff;
    }
    [self applySavedMode];
}

#define THEME_COLOR(NAME, OAT, SUMI) \
    +(NSColor*)NAME \
    { \
        static NSColor* sColor = dynamicColor(@"theme." #NAME, OAT, SUMI); \
        return sColor; \
    }

THEME_COLOR(background, 0xe1dfd1, 0x1c1b18)
THEME_COLOR(panel, 0xebebdf, 0x141412)
THEME_COLOR(field, 0xf4f3ec, 0x100f0e)
THEME_COLOR(border, 0xc9c7b9, 0x2e2d29)
THEME_COLOR(foreground, 0x4a4942, 0x9c9a8e)
THEME_COLOR(bright, 0x1a1a17, 0xebebdf)
THEME_COLOR(hover, 0xd6d4c5, 0x262521)
THEME_COLOR(selected, 0xcbc9ba, 0x34332e)
THEME_COLOR(accent, 0xe9631a, 0xf0702b)
THEME_COLOR(seed, 0x6b7a4f, 0x8b9a6b)
THEME_COLOR(check, 0xa8852c, 0xc2a14a)
THEME_COLOR(paused, 0xa8a696, 0x56544c)
THEME_COLOR(track, 0xd6d4c5, 0x262521)
THEME_COLOR(danger, 0xd33718, 0xd33718)

+ (NSImage*)symbol:(NSString*)name color:(NSColor*)color size:(CGFloat)size
{
    NSImage* image = [NSImage imageWithSystemSymbolName:name accessibilityDescription:nil];
    NSImageSymbolConfiguration* configuration = [[NSImageSymbolConfiguration configurationWithPointSize:size weight:NSFontWeightMedium]
        configurationByApplyingConfiguration:[NSImageSymbolConfiguration configurationWithHierarchicalColor:color]];
    return [image imageWithSymbolConfiguration:configuration];
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
    NSRectFill(NSMakeRect(14.0, NSMaxY(self.bounds) - 1.0 / scale, NSWidth(self.bounds) - 28.0, 1.0 / scale));
}

- (void)drawSelectionInRect:(NSRect)dirtyRect
{
    [Theme.selected setFill];
    [[NSBezierPath bezierPathWithRoundedRect:NSInsetRect(self.bounds, 6.0, 2.0) xRadius:7.0 yRadius:7.0] fill];
}

- (void)drawSeparatorInRect:(NSRect)dirtyRect
{
}

@end
