// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <AppKit/AppKit.h>

@class Torrent;
@class FileOutlineView;

@interface FileOutlineController : NSObject

/// Builds the list. Priorities add a column; the Add panel leaves them out.
- (instancetype)initWithPriorities:(BOOL)priorities;

@property(nonatomic, readonly) NSScrollView* scrollView;
@property(nonatomic, readonly) FileOutlineView* outlineView;
/// Double-clicking a row renames it, for files that aren't on disk yet. Otherwise it reveals them.
@property(nonatomic) BOOL doubleClickRenames;
@property(nonatomic) Torrent* torrent;
@property(nonatomic) NSString* filterText;

- (void)reloadVisibleRows;

- (void)setCheck:(id)sender;
- (void)setOnlySelectedCheck:(id)sender;
- (void)checkAll;
- (void)uncheckAll;
- (void)setPriority:(id)sender;

- (void)revealFile:(id)sender;

- (void)renameSelected:(id)sender;

@end
