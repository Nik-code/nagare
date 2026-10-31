// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <AppKit/AppKit.h>

@class Torrent;

extern CGFloat const kGroupSeparatorHeight;

@interface TorrentTableView : NSOutlineView<NSOutlineViewDelegate, NSAnimationDelegate, NSPopoverDelegate, NSMenuItemValidation>

- (void)reloadVisibleRows;

- (BOOL)isGroupCollapsed:(NSInteger)value;
- (void)removeCollapsedGroup:(NSInteger)value;
- (void)removeAllCollapsedGroups;
- (void)saveCollapsedGroups;

@property(nonatomic) NSArray<Torrent*>* selectedTorrents;

- (NSRect)iconRectForRow:(NSInteger)row;

- (void)copy:(id)sender;
- (void)paste:(id)sender;


- (void)toggleGroupRowRatio;

- (IBAction)toggleControlForTorrent:(id)sender;



- (void)selectAndScrollToRow:(NSInteger)row;

@end
