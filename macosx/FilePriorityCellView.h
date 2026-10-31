// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <AppKit/AppKit.h>

@class FileListNode;

/// Shows a file's priority as one quiet icon. Clicking it steps to the next priority.
@interface FilePriorityCellView : NSTableCellView

@property(nonatomic, weak) FileListNode* node;

@end
