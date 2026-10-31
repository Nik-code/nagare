// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#include <array>

#import "ProgressBarView.h"
#import "ProgressGradients.h"
#import "TorrentTableView.h"
#import "Torrent.h"
#import "Theme.h"

static CGFloat const kPiecesTotalPercent = 0.6;
static NSInteger const kMaxPieces = 18 * 18;

@interface ProgressBarView ()

@property(nonatomic, readonly) NSUserDefaults* fDefaults;

@property(nonatomic, strong) NSBitmapImageRep* fPiecesBitmap;


@end

@implementation ProgressBarView

+ (ProgressBarView*)sharedInstance
{
    static ProgressBarView* sSharedInstance = [[ProgressBarView alloc] init];
    return sSharedInstance;
}

- (instancetype)init
{
    if ((self = [super init]))
    {
        _fDefaults = NSUserDefaults.standardUserDefaults;

        _fPiecesBitmap = [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:nil pixelsWide:kMaxPieces pixelsHigh:1
                                                              bitsPerSample:8
                                                            samplesPerPixel:4
                                                                   hasAlpha:YES
                                                                   isPlanar:NO
                                                             colorSpaceName:NSCalibratedRGBColorSpace
                                                                bytesPerRow:0
                                                               bitsPerPixel:0];
    }
    return self;
}

- (void)drawBarInRect:(NSRect)barRect forTableView:(TorrentTableView*)tableView withTorrent:(Torrent*)torrent
{
    BOOL const minimal = [self.fDefaults boolForKey:@"SmallView"];

    [NSGraphicsContext saveGraphicsState];
    CGFloat const radius = minimal ? 4.0 : NSHeight(barRect) / 2.0;
    [[NSBezierPath bezierPathWithRoundedRect:barRect xRadius:radius yRadius:radius] addClip];

    CGFloat const piecesBarPercent = tableView.piecesBarPercent;
    if (piecesBarPercent > 0.0)
    {
        NSRect piecesBarRect, regularBarRect;
        NSDivideRect(barRect, &piecesBarRect, &regularBarRect, floor(NSHeight(barRect) * kPiecesTotalPercent * piecesBarPercent), NSMaxYEdge);

        [self drawRegularBar:regularBarRect forTorrent:torrent];
        [self drawPiecesBar:piecesBarRect forTorrent:torrent minimal:minimal];
    }
    else
    {
        torrent.previousFinishedPieces = nil;

        [self drawRegularBar:barRect forTorrent:torrent];
    }

    [NSGraphicsContext restoreGraphicsState];
}

- (void)drawRegularBar:(NSRect)barRect forTorrent:(Torrent*)torrent
{
    NSRect haveRect, missingRect;
    NSDivideRect(barRect, &haveRect, &missingRect, round(torrent.progress * NSWidth(barRect)), NSMinXEdge);

    if (!NSIsEmptyRect(haveRect))
    {
        if (torrent.active)
        {
            if (torrent.checking)
            {
                [ProgressGradients.progressYellowGradient drawInRect:haveRect angle:90];
            }
            else if (torrent.seeding)
            {
                NSRect ratioHaveRect, ratioRemainingRect;
                NSDivideRect(haveRect, &ratioHaveRect, &ratioRemainingRect, round(torrent.progressStopRatio * NSWidth(haveRect)), NSMinXEdge);

                [ProgressGradients.progressGreenGradient drawInRect:ratioHaveRect angle:90];
                [ProgressGradients.progressLightGreenGradient drawInRect:ratioRemainingRect angle:90];
            }
            else
            {
                [ProgressGradients.progressBlueGradient drawInRect:haveRect angle:90];
            }
        }
        else
        {
            if (torrent.waitingToStart)
            {
                if (torrent.allDownloaded)
                {
                    [ProgressGradients.progressDarkGreenGradient drawInRect:haveRect angle:90];
                }
                else
                {
                    [ProgressGradients.progressDarkBlueGradient drawInRect:haveRect angle:90];
                }
            }
            else
            {
                [ProgressGradients.progressGrayGradient drawInRect:haveRect angle:90];
            }
        }
    }

    if (!torrent.allDownloaded)
    {
        CGFloat const widthRemaining = round(NSWidth(barRect) * torrent.progressLeft);

        NSRect wantedRect;
        NSDivideRect(missingRect, &wantedRect, &missingRect, widthRemaining, NSMinXEdge);

        //not-available section
        if (torrent.active && !torrent.checking && torrent.availableDesired < 1.0 && [self.fDefaults boolForKey:@"DisplayProgressBarAvailable"])
        {
            NSRect unavailableRect;
            NSDivideRect(wantedRect, &wantedRect, &unavailableRect, round(NSWidth(wantedRect) * torrent.availableDesired), NSMinXEdge);

            [ProgressGradients.progressRedGradient drawInRect:unavailableRect angle:90];
        }

        //remaining section
        [ProgressGradients.progressWhiteGradient drawInRect:wantedRect angle:90];
    }

    //unwanted section
    if (!NSIsEmptyRect(missingRect))
    {
        if (!torrent.magnet)
        {
            [ProgressGradients.progressLightGrayGradient drawInRect:missingRect angle:90];
        }
        else
        {
            [ProgressGradients.progressRedGradient drawInRect:missingRect angle:90];
        }
    }
}

- (void)drawPiecesBar:(NSRect)barRect forTorrent:(Torrent*)torrent minimal:(BOOL)minimal
{
    // Fill a solid color bar for magnet links
    if (torrent.magnet)
    {
        [[Theme.track colorWithAlphaComponent:minimal ? 0.25 : 1.0] set];
        NSRectFillUsingOperation(barRect, NSCompositingOperationSourceOver);
        return;
    }

    int const pieceCount = static_cast<int>(MIN(torrent.pieceCount, kMaxPieces));
    if (pieceCount <= 0)
    {
        torrent.previousFinishedPieces = nil;
        return;
    }

    auto piecesPercent = std::array<float, kMaxPieces>{};
    [torrent getAmountFinished:piecesPercent.data() size:pieceCount];

    NSBitmapImageRep* bitmap = self.fPiecesBitmap;

    NSIndexSet* previousFinishedIndexes = torrent.previousFinishedPieces;
    NSMutableIndexSet* finishedIndexes = [NSMutableIndexSet indexSet];

    NSColor* const pieceBgColor = [Theme.track colorUsingColorSpace:NSColorSpace.sRGBColorSpace];
    NSColor* const pieceColorDone = [Theme.accent colorUsingColorSpace:NSColorSpace.sRGBColorSpace];
    NSColor* const pieceColorNew = [Theme.bright colorUsingColorSpace:NSColorSpace.sRGBColorSpace];

    for (int i = 0; i < pieceCount; i++)
    {
        NSColor* pieceColor;
        if (piecesPercent[i] == 1.0f)
        {
            if (previousFinishedIndexes && ![previousFinishedIndexes containsIndex:i])
            {
                pieceColor = pieceColorNew;
            }
            else
            {
                pieceColor = pieceColorDone;
            }
            [finishedIndexes addIndex:i];
        }
        else
        {
            pieceColor = [pieceBgColor blendedColorWithFraction:piecesPercent[i] ofColor:pieceColorDone];
        }

        //it's faster to just set color instead of checking previous color
        // faster and non-broken alternative to `[bitmap setColor:pieceColor atX:i y:0]`
        unsigned char* data = bitmap.bitmapData + (i << 2);
        data[0] = pieceColor.redComponent * 255;
        data[1] = pieceColor.greenComponent * 255;
        data[2] = pieceColor.blueComponent * 255;
        data[3] = pieceColor.alphaComponent * 255;
    }

    torrent.previousFinishedPieces = finishedIndexes.count > 0 ? finishedIndexes : nil; //don't bother saving if none are complete

    //actually draw image
    [bitmap drawInRect:barRect fromRect:NSMakeRect(0.0, 0.0, pieceCount, 1.0) operation:NSCompositingOperationSourceOver
              fraction:minimal ? 0.25 : 1.0
        respectFlipped:YES
                 hints:nil];
}

@end
