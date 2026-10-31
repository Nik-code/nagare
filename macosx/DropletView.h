// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <AppKit/AppKit.h>

@class Torrent;

NS_ASSUME_NONNULL_BEGIN

/// A torrent drawn as droplets over a calm swell.
///
/// While downloading each dot is a share of the file, solid once that share is complete.
/// While seeding the dots form a tide whose height is the share ratio, and which only moves during upload.
@interface DropletView : NSView

/// Rows of dots. The number of columns follows the width.
@property(nonatomic) NSInteger dotRows;

/// Colors newly finished dots. Only one view per torrent can do this, since it keeps the record on the torrent.
@property(nonatomic) BOOL tracksNewPieces;

/// Redraws only if the dots changed, so idle transfers cost nothing.
- (void)updateWithTorrent:(nullable Torrent*)torrent;

/// How far the share ratio has come towards the seeding goal, from 0 to 1.
+ (CGFloat)tideForTorrent:(Torrent*)torrent;

@end

NS_ASSUME_NONNULL_END
