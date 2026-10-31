// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <AppKit/AppKit.h>

@class Controller;

NS_ASSUME_NONNULL_BEGIN

/// A small clear-glass panel for pasting a magnet or torrent link.
@interface LinkPanelController : NSWindowController

- (instancetype)initWithController:(Controller*)controller;
- (void)present;

@end

NS_ASSUME_NONNULL_END
