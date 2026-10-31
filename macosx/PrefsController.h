// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "PortChecker.h"
#import <AppKit/AppKit.h>

#include <libtransmission/transmission.h>

@interface PrefsController : NSWindowController<NSToolbarDelegate, PortCheckerDelegate>

@property(nonatomic, readonly) NSArray<NSString*>* sounds;

/// - returns: number of minutes
+ (int)dateToTimeSum:(NSDate*)date;

- (instancetype)initWithHandle:(tr_session*)handle;

- (void)rpcUpdatePrefs;

/// Passes a changed default on to the session.
- (void)applyChangeToKey:(NSString*)key;

- (void)chooseDownloadFolderInWindow:(NSWindow*)window completion:(void (^)(void))completion;
- (void)applyRatioSetting:(id)sender;
- (void)setAutoStartDownloads:(id)sender;
- (IBAction)setDefaultForMagnets:(id)sender;
- (IBAction)setDefaultForTorrentFiles:(id)sender;

@end
