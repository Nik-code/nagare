// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <AppKit/AppKit.h>

NS_ASSUME_NONNULL_BEGIN

/// The top of the main window. The water is a graph: its blue swell is the last ten minutes
/// of download speed, and the sand in front of it is upload.
@interface SwellHeaderView : NSView

/// Takes one sample. Call on every tick, even while the window is hidden, so history stays true.
- (void)recordDownload:(CGFloat)download upload:(CGFloat)upload;

/// Redraws from the recorded samples.
- (void)refresh;

@end

NS_ASSUME_NONNULL_END
