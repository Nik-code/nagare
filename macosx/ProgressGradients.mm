// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "ProgressGradients.h"
#import "Theme.h"

@implementation ProgressGradients

+ (NSGradient*)progressWhiteGradient
{
    return [[self class] gradientForColor:Theme.track];
}

+ (NSGradient*)progressGrayGradient
{
    return [[self class] gradientForColor:Theme.paused];
}

+ (NSGradient*)progressLightGrayGradient
{
    return [[self class] gradientForColor:Theme.hover];
}

+ (NSGradient*)progressBlueGradient
{
    return [[self class] gradientForColor:Theme.accent];
}

+ (NSGradient*)progressDarkBlueGradient
{
    return [[self class] gradientForColor:Theme.paused];
}

+ (NSGradient*)progressGreenGradient
{
    return [[self class] gradientForColor:Theme.seed];
}

+ (NSGradient*)progressLightGreenGradient
{
    return [[self class] gradientForColor:Theme.seed];
}

+ (NSGradient*)progressDarkGreenGradient
{
    return [[self class] gradientForColor:Theme.paused];
}

+ (NSGradient*)progressRedGradient
{
    return [[self class] gradientForColor:Theme.danger];
}

+ (NSGradient*)progressYellowGradient
{
    return [[self class] gradientForColor:Theme.check];
}

#pragma mark - Private

+ (NSGradient*)gradientForColor:(NSColor*)color
{
    CGFloat const alpha = [NSUserDefaults.standardUserDefaults boolForKey:@"SmallView"] ? 0.27 : 1.0;
    NSColor* flat = [[color colorUsingColorSpace:NSColorSpace.sRGBColorSpace] colorWithAlphaComponent:alpha];
    return [[NSGradient alloc] initWithStartingColor:flat endingColor:flat];
}

@end
