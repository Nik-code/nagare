// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <cmath>
#include <deque>
#include <vector>

#import <QuartzCore/QuartzCore.h>

#import "SwellHeaderView.h"
#import "NSStringAdditions.h"
#import "Theme.h"

static NSTimeInterval const kHistorySeconds = 600.0;
static size_t const kPoints = 22;
static CGFloat const kCornerRadius = 18.0;

struct Sample
{
    NSTimeInterval time;
    CGFloat download;
    CGFloat upload;
};

@interface SwellHeaderView ()
@property(nonatomic) CALayer* fSky;
@property(nonatomic) CALayer* fDownWater;
@property(nonatomic) CALayer* fUpWater;
@property(nonatomic) CAShapeLayer* fDownMask;
@property(nonatomic) CAShapeLayer* fUpMask;
@property(nonatomic) CAShapeLayer* fDownCrest;
@property(nonatomic) CAShapeLayer* fUpCrest;
@property(nonatomic) NSTextField* fDownField;
@property(nonatomic) NSTextField* fDownUnitField;
@property(nonatomic) NSTextField* fUpField;
@property(nonatomic) NSTextField* fUpUnitField;
@property(nonatomic) NSGlassEffectView* fGlass;
@property(nonatomic) std::deque<Sample> samples;
@property(nonatomic) CGFloat fLastDownload;
@property(nonatomic) CGFloat fLastUpload;
@property(nonatomic) BOOL fDrawnIdle;
@end

@implementation SwellHeaderView

- (instancetype)initWithFrame:(NSRect)frame
{
    if ((self = [super initWithFrame:frame]))
    {
        self.wantsLayer = YES;
        CALayer* root = self.layer;
        root.cornerRadius = kCornerRadius;
        root.cornerCurve = kCACornerCurveContinuous;
        root.masksToBounds = YES;
        root.borderWidth = 0.5;

        _fSky = [self artLayer];
        _fDownWater = [self artLayer];
        _fUpWater = [self artLayer];
        _fDownMask = [CAShapeLayer layer];
        _fUpMask = [CAShapeLayer layer];
        _fDownWater.mask = _fDownMask;
        _fUpWater.mask = _fUpMask;
        _fDownCrest = [self crestLayer:2.2];
        _fUpCrest = [self crestLayer:1.6];

        [root addSublayer:_fSky];
        [root addSublayer:_fDownWater];
        [root addSublayer:_fDownCrest];
        [root addSublayer:_fUpWater];
        [root addSublayer:_fUpCrest];

        NSTextField* wordmark = [Theme labelWithFont:[Theme displayFontOfSize:25] color:Theme.bright];
        wordmark.stringValue = @"Nagare";
        [self addSubview:wordmark];

        _fGlass = [[NSGlassEffectView alloc] initWithFrame:NSZeroRect];
        _fGlass.cornerRadius = 20;
        _fGlass.translatesAutoresizingMaskIntoConstraints = NO;

        NSView* down = [self statColumn:NSLocalizedString(@"Down", "Header -> stat") value:&_fDownField unit:&_fDownUnitField];
        NSView* up = [self statColumn:NSLocalizedString(@"Up", "Header -> stat") value:&_fUpField unit:&_fUpUnitField];
        NSStackView* stats = [NSStackView stackViewWithViews:@[ down, up ]];
        stats.spacing = 32;
        stats.edgeInsets = NSEdgeInsetsMake(14, 20, 14, 24);
        stats.translatesAutoresizingMaskIntoConstraints = NO;
        _fGlass.contentView = stats;
        [self addSubview:_fGlass];

        [NSLayoutConstraint activateConstraints:@[
            [wordmark.leadingAnchor constraintEqualToAnchor:self.leadingAnchor constant:84],
            [wordmark.topAnchor constraintEqualToAnchor:self.topAnchor constant:6],
            [_fGlass.leadingAnchor constraintEqualToAnchor:self.leadingAnchor constant:16],
            [_fGlass.bottomAnchor constraintEqualToAnchor:self.bottomAnchor constant:-16],
        ]];

        [self applyArt];
        [self refresh];
    }
    return self;
}

- (CALayer*)artLayer
{
    CALayer* layer = [CALayer layer];
    layer.contentsGravity = kCAGravityResizeAspectFill;
    layer.masksToBounds = YES;
    return layer;
}

- (CAShapeLayer*)crestLayer:(CGFloat)width
{
    CAShapeLayer* layer = [CAShapeLayer layer];
    layer.fillColor = nil;
    layer.lineWidth = width;
    layer.lineCap = kCALineCapRound;
    return layer;
}

- (NSView*)statColumn:(NSString*)title value:(NSTextField* __strong*)value unit:(NSTextField* __strong*)unit
{
    NSTextField* titleField = [Theme labelWithFont:[NSFont systemFontOfSize:13 weight:NSFontWeightMedium] color:Theme.bright];
    titleField.stringValue = title;
    NSTextField* number = [Theme labelWithFont:[Theme displayFontOfSize:42] color:Theme.bright];
    NSTextField* unitField = [Theme labelWithFont:[NSFont systemFontOfSize:13] color:Theme.bright];

    NSStackView* line = [NSStackView stackViewWithViews:@[ number, unitField ]];
    line.alignment = NSLayoutAttributeLastBaseline;
    line.spacing = 6;

    NSStackView* column = [NSStackView stackViewWithViews:@[ titleField, line ]];
    column.orientation = NSUserInterfaceLayoutOrientationVertical;
    column.alignment = NSLayoutAttributeLeading;
    column.spacing = 3;

    *value = number;
    *unit = unitField;
    return column;
}

- (BOOL)mouseDownCanMoveWindow
{
    return YES;
}

- (void)viewDidChangeEffectiveAppearance
{
    [super viewDidChangeEffectiveAppearance];
    [self applyArt];
    self.fDrawnIdle = NO;
    [self refresh];
}

- (void)applyArt
{
    NSAppearance* appearance = self.effectiveAppearance;
    self.fSky.contents = [Theme layerContents:@"ArtSky" appearance:appearance scale:1];
    self.fDownWater.contents = [Theme layerContents:@"ArtWaterDown" appearance:appearance scale:1];
    self.fUpWater.contents = [Theme layerContents:@"ArtWaterUp" appearance:appearance scale:1];

    BOOL const dark = [appearance bestMatchFromAppearancesWithNames:@[ NSAppearanceNameAqua, NSAppearanceNameDarkAqua ]] == NSAppearanceNameDarkAqua;
    self.fDownCrest.strokeColor = [NSColor colorWithSRGBRed:dark ? 0.659 : 0.91 green:dark ? 0.894 : 0.984 blue:dark ? 0.855 : 0.965 alpha:1].CGColor;
    self.fUpCrest.strokeColor = [NSColor colorWithSRGBRed:dark ? 0.941 : 1 green:dark ? 0.706 : 0.957 blue:dark ? 0.58 : 0.902 alpha:1].CGColor;
    self.layer.borderColor = [NSColor colorWithSRGBRed:1 green:0.941 blue:0.863 alpha:dark ? 0.08 : 0.5].CGColor;
}

- (void)layout
{
    [super layout];
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    for (CALayer* layer in @[ self.fSky, self.fDownWater, self.fUpWater, self.fDownCrest, self.fUpCrest ])
    {
        layer.frame = self.bounds;
    }
    self.fDownMask.frame = self.bounds;
    self.fUpMask.frame = self.bounds;
    self.fDrawnIdle = NO;
    [self drawSwell:NO];
    [CATransaction commit];
}

- (void)recordDownload:(CGFloat)download upload:(CGFloat)upload
{
    NSTimeInterval const now = NSDate.timeIntervalSinceReferenceDate;
    _samples.push_back(Sample{ now, download, upload });
    while (!_samples.empty() && now - _samples.front().time > kHistorySeconds)
    {
        _samples.pop_front();
    }
    self.fLastDownload = download;
    self.fLastUpload = upload;
}

- (void)setSpeed:(CGFloat)speed value:(NSTextField*)value unit:(NSTextField*)unit
{
    NSString* text = [NSString stringForSpeed:speed];
    NSRange const space = [text rangeOfString:@" " options:NSBackwardsSearch];
    if (space.location == NSNotFound)
    {
        value.stringValue = text;
        unit.stringValue = @"";
        return;
    }
    value.stringValue = [text substringToIndex:space.location];
    unit.stringValue = [text substringFromIndex:NSMaxRange(space)];
}

- (void)refresh
{
    [self setSpeed:self.fLastDownload value:self.fDownField unit:self.fDownUnitField];
    [self setSpeed:self.fLastUpload value:self.fUpField unit:self.fUpUnitField];
    [self drawSwell:YES];
}

/// Averages the samples into evenly spaced buckets, oldest first.
- (void)buckets:(std::vector<CGFloat>&)down up:(std::vector<CGFloat>&)up
{
    down.assign(kPoints, 0.0);
    up.assign(kPoints, 0.0);
    auto counts = std::vector<int>(kPoints, 0);
    NSTimeInterval const now = NSDate.timeIntervalSinceReferenceDate;
    for (Sample const& sample : _samples)
    {
        auto const age = std::clamp((now - sample.time) / kHistorySeconds, 0.0, 1.0);
        auto const index = std::min(kPoints - 1, static_cast<size_t>((1.0 - age) * (kPoints - 1) + 0.5));
        down[index] += sample.download;
        up[index] += sample.upload;
        counts[index] += 1;
    }
    for (size_t i = 0; i < kPoints; ++i)
    {
        if (counts[i] > 0)
        {
            down[i] /= counts[i];
            up[i] /= counts[i];
        }
    }
}

- (CGPathRef)newPathThrough:(std::vector<CGPoint> const&)points closed:(BOOL)closed CF_RETURNS_RETAINED
{
    CGMutablePathRef path = CGPathCreateMutable();
    CGPathMoveToPoint(path, nil, points[0].x, points[0].y);
    for (size_t i = 0; i + 1 < points.size(); ++i)
    {
        CGPoint const a = points[i > 0 ? i - 1 : 0];
        CGPoint const b = points[i];
        CGPoint const c = points[i + 1];
        CGPoint const d = points[std::min(i + 2, points.size() - 1)];
        CGPathAddCurveToPoint(
            path,
            nil,
            b.x + (c.x - a.x) / 6.0,
            b.y + (c.y - a.y) / 6.0,
            c.x - (d.x - b.x) / 6.0,
            c.y - (d.y - b.y) / 6.0,
            c.x,
            c.y);
    }
    if (closed)
    {
        CGPathAddLineToPoint(path, nil, points.back().x, 0);
        CGPathAddLineToPoint(path, nil, points.front().x, 0);
        CGPathCloseSubpath(path);
    }
    return path;
}

- (void)drawSwell:(BOOL)animated
{
    NSSize const size = self.bounds.size;
    if (size.width < 1)
    {
        return;
    }

    std::vector<CGFloat> down;
    std::vector<CGFloat> up;
    [self buckets:down up:up];

    CGFloat peak = 0.0;
    for (size_t i = 0; i < kPoints; ++i)
    {
        peak = MAX(peak, MAX(down[i], up[i]));
    }

    // With nothing moving the sea just lies still, so there's nothing to redraw.
    if (peak <= 0.0)
    {
        if (self.fDrawnIdle)
        {
            return;
        }
        self.fDrawnIdle = YES;
    }
    else
    {
        self.fDrawnIdle = NO;
    }

    auto downPoints = std::vector<CGPoint>(kPoints);
    auto upPoints = std::vector<CGPoint>(kPoints);
    for (size_t i = 0; i < kPoints; ++i)
    {
        CGFloat const x = size.width * i / (kPoints - 1);
        // A small standing swell keeps the water looking like water when speeds are flat.
        CGFloat const swell = 0.05 * sin(i * 0.72 + 0.4) + 0.02 * sin(i * 1.9 + 1.0);
        CGFloat const swellUp = 0.022 * sin(i * 1.05 + 2.0);
        CGFloat const downShare = peak > 0.0 ? down[i] / peak : 0.0;
        CGFloat const upShare = peak > 0.0 ? up[i] / peak : 0.0;
        // Still water rests about halfway up the tile, so the swell always clears it.
        downPoints[i] = CGPointMake(x, size.height * (0.5 + 0.3 * downShare + swell));
        upPoints[i] = CGPointMake(x, size.height * (0.15 + 0.24 * upShare + swellUp));
    }

    CGPathRef downLine = [self newPathThrough:downPoints closed:NO];
    CGPathRef downArea = [self newPathThrough:downPoints closed:YES];
    CGPathRef upLine = [self newPathThrough:upPoints closed:NO];
    CGPathRef upArea = [self newPathThrough:upPoints closed:YES];

    if (animated && self.fDownMask.path != nil && peak > 0.0)
    {
        for (CAShapeLayer* layer in @[ self.fDownMask, self.fDownCrest, self.fUpMask, self.fUpCrest ])
        {
            CABasicAnimation* animation = [CABasicAnimation animationWithKeyPath:@"path"];
            animation.fromValue = (__bridge id)layer.path;
            animation.duration = 0.9;
            animation.timingFunction = [CAMediaTimingFunction functionWithName:kCAMediaTimingFunctionLinear];
            [layer addAnimation:animation forKey:@"swell"];
        }
    }

    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    self.fDownMask.path = downArea;
    self.fDownCrest.path = downLine;
    self.fUpMask.path = upArea;
    self.fUpCrest.path = upLine;
    [CATransaction commit];

    CGPathRelease(downLine);
    CGPathRelease(downArea);
    CGPathRelease(upLine);
    CGPathRelease(upArea);
}

@end
