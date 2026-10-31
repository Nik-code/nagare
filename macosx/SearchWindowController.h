// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <AppKit/AppKit.h>

@class Controller;

NS_ASSUME_NONNULL_BEGIN

/// A floating glass panel for finding torrents across the configured sources.
@interface SearchWindowController : NSWindowController

- (instancetype)initWithController:(Controller*)controller;

/// Brings the panel up with the search field ready for typing.
- (void)present;

@end

NS_ASSUME_NONNULL_END
