// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <AppKit/AppKit.h>

@class PrefsController;

NS_ASSUME_NONNULL_BEGIN

/// Settings with a glass sidebar. Everyday options are built here; PrefsController
/// still owns session changes, and its panes live on under Speed and Advanced.
@interface SettingsWindowController : NSWindowController

- (instancetype)initWithPrefs:(PrefsController*)prefs;

- (void)present;

@end

NS_ASSUME_NONNULL_END
