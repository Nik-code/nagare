// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <cmath>
#include <vector>

#import "DropletView.h"
#import "Theme.h"
#import "Torrent.h"

static CGFloat const kCornerRadius = 14.0;
static CGFloat const kPadding = 14.0;
static CGFloat const kPitch = 12.6;
static CGFloat const kRowPitch = 10.5;
static CGFloat const kDefaultSeedGoal = 2.0;

typedef NS_ENUM(uint8_t, DotInk) {
    DotInkMain,
    DotInkFresh,
    DotInkCrest,
    DotInkScan,
    DotInkBad,
};

struct Dot
{
    float size;
    float alpha;
    DotInk ink;

    bool operator==(Dot const& other) const
    {
        return size == other.size && alpha == other.alpha && ink == other.ink;
    }
};

typedef NS_ENUM(NSInteger, DropletState) {
    DropletStateNone,
    DropletStateDownload,
    DropletStateFetching,
    DropletStateVerifying,
    DropletStateSeeding,
    DropletStatePaused,
    DropletStateFinished,
    DropletStateProblem,
};

/// Only the dots repaint. The art is a layer behind them and stays untouched.
@interface DropletDotsView : NSView
@property(nonatomic) std::vector<Dot> dots;
@property(nonatomic) NSInteger columns;
@property(nonatomic) NSInteger rows;
@property(nonatomic) DropletState state;
@end

@interface DropletView ()
@property(nonatomic) DropletDotsView* fDotsView;
@property(nonatomic) NSString* fArtName;
@property(nonatomic) CGFloat fPhase;
@property(nonatomic, weak) Torrent* fTorrent;
@end

@implementation DropletView

- (instancetype)initWithFrame:(NSRect)frame
{
    if ((self = [super initWithFrame:frame]))
    {
        _dotRows = 5;
        _tracksNewPieces = YES;
        self.wantsLayer = YES;
        self.layer.cornerRadius = kCornerRadius;
        self.layer.cornerCurve = kCACornerCurveContinuous;
        self.layer.masksToBounds = YES;
        self.layer.contentsGravity = kCAGravityResizeAspectFill;

        _fDotsView = [[DropletDotsView alloc] initWithFrame:self.bounds];
        _fDotsView.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
        [self addSubview:_fDotsView];
    }
    return self;
}

- (NSView*)hitTest:(NSPoint)point
{
    return nil;
}

- (void)setFrameSize:(NSSize)newSize
{
    BOOL const changed = !NSEqualSizes(newSize, self.frame.size);
    [super setFrameSize:newSize];
    if (changed && self.fTorrent != nil)
    {
        [self updateWithTorrent:self.fTorrent];
    }
}

- (void)viewDidChangeEffectiveAppearance
{
    [super viewDidChangeEffectiveAppearance];
    [self refreshArt];
    self.fDotsView.needsDisplay = YES;
}

- (void)refreshArt
{
    if (self.fArtName != nil)
    {
        self.layer.contents = [Theme layerContents:self.fArtName appearance:self.effectiveAppearance scale:1];
    }
}

+ (CGFloat)tideForTorrent:(Torrent*)torrent
{
    CGFloat const stop = torrent.progressStopRatio;
    if (stop > 0.0 && stop < 1.0)
    {
        return stop;
    }
    return MIN(MAX(torrent.ratio, 0.0) / kDefaultSeedGoal, 1.0);
}

+ (DropletState)stateForTorrent:(Torrent*)torrent
{
    if (torrent == nil)
    {
        return DropletStateNone;
    }
    if (torrent.error)
    {
        return DropletStateProblem;
    }
    if (torrent.checking || torrent.checkingWaiting)
    {
        return DropletStateVerifying;
    }
    if (torrent.magnet)
    {
        return torrent.active ? DropletStateFetching : DropletStatePaused;
    }
    if (torrent.seeding)
    {
        return DropletStateSeeding;
    }
    if (torrent.active)
    {
        return DropletStateDownload;
    }
    return torrent.allDownloaded ? DropletStateFinished : DropletStatePaused;
}

+ (NSString*)artForState:(DropletState)state torrent:(Torrent*)torrent
{
    switch (state)
    {
    case DropletStateSeeding:
        return @"ArtRowSeed";
    case DropletStatePaused:
    case DropletStateProblem:
        return @"ArtRowPaused";
    case DropletStateFinished:
        return @"ArtRowDone";
    case DropletStateFetching:
    case DropletStateVerifying:
        return @"ArtRowCheck";
    default:
        return torrent.name.hash % 2 == 0 ? @"ArtRowDownload1" : @"ArtRowDownload2";
    }
}

- (void)updateWithTorrent:(Torrent*)torrent
{
    self.fTorrent = torrent;

    DropletState const state = [DropletView stateForTorrent:torrent];
    NSString* art = state == DropletStateNone ? nil : [DropletView artForState:state torrent:torrent];
    if (![art isEqualToString:self.fArtName])
    {
        self.fArtName = art;
        [self refreshArt];
    }

    NSInteger const rows = self.dotRows;
    NSInteger const columns = MAX(4, static_cast<NSInteger>((NSWidth(self.bounds) - kPadding * 2.0) / kPitch));
    auto const count = columns * rows;
    auto dots = std::vector<Dot>(count, Dot{ 3.0F, 0.34F, DotInkMain });

    switch (state)
    {
    case DropletStateDownload:
    case DropletStatePaused:
    case DropletStateProblem:
        [self fillPieces:dots columns:columns rows:rows torrent:torrent fresh:state == DropletStateDownload];
        break;
    case DropletStateVerifying:
        {
            auto const sweep = static_cast<NSInteger>(torrent.checkingProgress * columns);
            for (NSInteger x = 0; x < columns; ++x)
            {
                for (NSInteger y = 0; y < rows; ++y)
                {
                    dots[x * rows + y] = x == sweep ? Dot{ 7.0F, 1.0F, DotInkScan } :
                        x < sweep                  ? Dot{ 6.0F, 0.95F, DotInkMain } :
                                                     Dot{ 3.0F, 0.42F, DotInkMain };
                }
            }
        }
        break;
    case DropletStateFetching:
        for (NSInteger x = 0; x < columns; ++x)
        {
            for (NSInteger y = 0; y < rows; ++y)
            {
                CGFloat const distance = hypot((x - columns / 2.0 + 0.5) / 2.2, y - (rows - 1) / 2.0);
                bool const ring = fabs(distance - 1.6) < 0.5;
                dots[x * rows + y] = ring ? Dot{ 6.0F, 1.0F, DotInkMain } : Dot{ 2.6F, 0.34F, DotInkMain };
            }
        }
        break;
    case DropletStateSeeding:
        {
            if (torrent.uploadRate > 0.0)
            {
                self.fPhase += 0.45;
            }
            CGFloat const level = [DropletView tideForTorrent:torrent];
            for (NSInteger x = 0; x < columns; ++x)
            {
                CGFloat const surface = (rows - 1) * (1.0 - level * 1.06) + 1.05 * sin(x / 3.4 + self.fPhase) + 0.5 * sin(x / 1.5 + 1.3 + self.fPhase * 0.6);
                for (NSInteger y = 0; y < rows; ++y)
                {
                    CGFloat const depth = y - surface;
                    dots[x * rows + y] = depth > 0.7 ? Dot{ 7.0F, 1.0F, DotInkMain } :
                        depth > -0.5                 ? Dot{ 5.0F, 1.0F, DotInkCrest } :
                                                       Dot{ 2.4F, 0.5F, DotInkMain };
                }
            }
        }
        break;
    case DropletStateFinished:
        std::fill(dots.begin(), dots.end(), Dot{ 5.0F, 0.55F, DotInkMain });
        break;
    default:
        break;
    }

    DropletDotsView* view = self.fDotsView;
    if (state != view.state || columns != view.columns || rows != view.rows || dots != view.dots)
    {
        view.state = state;
        view.columns = columns;
        view.rows = rows;
        view.dots = std::move(dots);
        view.needsDisplay = YES;
    }
}

- (void)fillPieces:(std::vector<Dot>&)dots columns:(NSInteger)columns rows:(NSInteger)rows torrent:(Torrent*)torrent fresh:(BOOL)fresh
{
    auto finished = std::vector<float>(dots.size());
    [torrent getAmountFinished:finished.data() size:static_cast<int>(finished.size())];

    NSIndexSet* previous = self.tracksNewPieces ? torrent.previousFinishedPieces : nil;
    NSMutableIndexSet* done = [NSMutableIndexSet indexSet];
    for (size_t i = 0; i < dots.size(); ++i)
    {
        // Shares of the file run down each column, so the bar still fills left to right.
        if (finished[i] >= 1.0F)
        {
            [done addIndex:i];
            BOOL const isNew = fresh && previous != nil && ![previous containsIndex:i];
            dots[i] = Dot{ 7.0F, 1.0F, isNew ? DotInkFresh : DotInkMain };
        }
        else
        {
            dots[i] = Dot{ static_cast<float>(3.0 + finished[i] * 3.0), static_cast<float>(0.4 + finished[i] * 0.5), DotInkMain };
        }
    }
    if (self.tracksNewPieces)
    {
        torrent.previousFinishedPieces = done.count > 0 ? done : nil;
    }
}

@end

@implementation DropletDotsView

- (NSView*)hitTest:(NSPoint)point
{
    return nil;
}

- (BOOL)isOpaque
{
    return NO;
}

- (void)drawRect:(NSRect)dirtyRect
{
    if (self.dots.empty())
    {
        return;
    }

    BOOL const dark = [NSAppearance.currentDrawingAppearance bestMatchFromAppearancesWithNames:@[ NSAppearanceNameAqua, NSAppearanceNameDarkAqua ]] ==
        NSAppearanceNameDarkAqua;

    NSColor* main = dark ? [NSColor colorWithSRGBRed:0.937 green:0.902 blue:0.839 alpha:1] : NSColor.whiteColor;
    NSColor* crest = main;
    NSColor* scan = dark ? [NSColor colorWithSRGBRed:0.561 green:0.847 blue:0.8 alpha:1] : [NSColor colorWithSRGBRed:0.039 green:0.192 blue:0.278 alpha:1];
    NSColor* fresh = [NSColor colorWithSRGBRed:0.965 green:0.663 blue:0.545 alpha:1];
    NSColor* bad = [NSColor colorWithSRGBRed:1 green:0.478 blue:0.333 alpha:1];

    switch (self.state)
    {
    case DropletStateSeeding:
        main = dark ? [NSColor colorWithSRGBRed:0.941 green:0.706 blue:0.58 alpha:1] : [NSColor colorWithSRGBRed:0.047 green:0.373 blue:0.51 alpha:1];
        crest = dark ? [NSColor colorWithSRGBRed:1 green:0.941 blue:0.878 alpha:1] : [NSColor colorWithSRGBRed:0.878 green:0.412 blue:0.29 alpha:1];
        break;
    case DropletStateFinished:
        main = dark ? [NSColor colorWithSRGBRed:0.561 green:0.529 blue:0.482 alpha:1] : [NSColor colorWithSRGBRed:0.047 green:0.373 blue:0.51 alpha:1];
        break;
    case DropletStatePaused:
    case DropletStateProblem:
        main = dark ? [NSColor colorWithSRGBRed:0.659 green:0.627 blue:0.588 alpha:1] : NSColor.whiteColor;
        break;
    default:
        break;
    }

    // The property returns a copy, so take one here rather than one per dot.
    std::vector<Dot> const dots = self.dots;

    CGContextRef context = NSGraphicsContext.currentContext.CGContext;
    CGFloat const originY = (NSHeight(self.bounds) - (self.rows - 1) * kRowPitch) / 2.0;
    CGFloat const width = NSWidth(self.bounds) - kPadding * 2.0;
    CGFloat const stepX = self.columns > 1 ? (width - 8.0) / (self.columns - 1) : 0.0;

    for (NSInteger x = 0; x < self.columns; ++x)
    {
        for (NSInteger y = 0; y < self.rows; ++y)
        {
            Dot const& dot = dots[x * self.rows + y];
            NSColor* color = main;
            switch (dot.ink)
            {
            case DotInkFresh:
                color = fresh;
                break;
            case DotInkCrest:
                color = crest;
                break;
            case DotInkScan:
                color = scan;
                break;
            case DotInkBad:
                color = bad;
                break;
            default:
                break;
            }
            CGContextSetFillColorWithColor(context, [color colorWithAlphaComponent:dot.alpha].CGColor);
            CGFloat const centerX = kPadding + 4.0 + x * stepX;
            // The view isn't flipped, so count rows down from the top.
            CGFloat const centerY = NSHeight(self.bounds) - originY - y * kRowPitch;
            CGContextFillEllipseInRect(context, CGRectMake(centerX - dot.size / 2.0, centerY - dot.size / 2.0, dot.size, dot.size));
        }
    }
}

@end
