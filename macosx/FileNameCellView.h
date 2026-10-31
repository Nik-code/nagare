// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <AppKit/AppKit.h>

@class FileListNode;
@class FileNameCellView;

@protocol FileNameCellDelegate

- (void)fileCellWillRename:(FileNameCellView*)cell;
/// `name` is nil if the rename was cancelled or changed nothing.
- (void)fileCell:(FileNameCellView*)cell didFinishRenamingTo:(NSString*)name;

@end

@interface FileNameCellView : NSTableCellView

@property(nonatomic, weak) FileListNode* node;
@property(nonatomic, weak) id<FileNameCellDelegate> delegate;

/// Turns the name into an inline text field with its stem selected.
- (void)beginRename;

@end
