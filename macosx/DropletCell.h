// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <AppKit/AppKit.h>

@class DropletView;
@class Torrent;
@class TorrentTableView;

NS_ASSUME_NONNULL_BEGIN

/// One transfer in the main list: its name and state, its droplets, and its speed or controls.
@interface DropletCell : NSTableCellView

@property(nonatomic, readonly) DropletView* dropletView;

- (void)configureWithTorrent:(Torrent*)torrent table:(TorrentTableView*)table;

/// Cheap in-place update, run on every tick for visible rows.
- (void)refresh;

@end

NS_ASSUME_NONNULL_END
