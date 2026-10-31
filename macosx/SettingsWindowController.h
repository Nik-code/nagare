// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <AppKit/AppKit.h>

@class PrefsController;

NS_ASSUME_NONNULL_BEGIN

/// Settings with a glass sidebar. The rows are built here; PrefsController still
/// passes each change on to the session.
@interface SettingsWindowController : NSWindowController

- (instancetype)initWithPrefs:(PrefsController*)prefs;

- (void)present;

@end

NS_ASSUME_NONNULL_END
