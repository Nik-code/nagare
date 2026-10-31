// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <AppKit/AppKit.h>

NS_ASSUME_NONNULL_BEGIN

/// A capsule of choices with one raised pill, used wherever the app switches between views.
@interface PillBar : NSView

- (instancetype)initWithLabels:(NSArray<NSString*>*)labels target:(nullable id)target action:(nullable SEL)action;

@property(nonatomic) NSInteger selectedSegment;
@property(nonatomic, copy) NSArray<NSString*>* labels;

@end

NS_ASSUME_NONNULL_END
