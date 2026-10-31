// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "FilterBarController.h"
#import "Theme.h"

FilterType const FilterTypeNone = @"None";
FilterType const FilterTypeActive = @"Active";
FilterType const FilterTypeDownload = @"Download";
FilterType const FilterTypeSeed = @"Seed";
FilterType const FilterTypePause = @"Pause";
FilterType const FilterTypeError = @"Error";

FilterSearchType const FilterSearchTypeName = @"Name";
FilterSearchType const FilterSearchTypeTracker = @"Tracker";

NSInteger const kGroupFilterAllTag = -2;

@interface FilterBarController ()
@property(nonatomic) NSSegmentedControl* fFilters;
@property(nonatomic) NSTextField* fTotalField;
@property(nonatomic, copy) NSArray<FilterType>* fTypes;
@end

@implementation FilterBarController

- (instancetype)init
{
    return [super initWithNibName:nil bundle:nil];
}

- (void)loadView
{
    NSView* view = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 900, 36)];
    view.translatesAutoresizingMaskIntoConstraints = NO;

    self.fTypes = @[ FilterTypeNone, FilterTypeDownload, FilterTypeSeed, FilterTypePause ];

    self.fFilters = [NSSegmentedControl segmentedControlWithLabels:@[ @"All", @"Downloading", @"Seeding", @"Paused" ]
                                                      trackingMode:NSSegmentSwitchTrackingSelectOne
                                                            target:self
                                                            action:@selector(setFilter:)];
    self.fFilters.translatesAutoresizingMaskIntoConstraints = NO;
    [view addSubview:self.fFilters];

    self.fTotalField = [Theme labelWithFont:[NSFont systemFontOfSize:13] color:Theme.foreground];
    self.fTotalField.alignment = NSTextAlignmentRight;
    [view addSubview:self.fTotalField];

    [NSLayoutConstraint activateConstraints:@[
        [self.fFilters.leadingAnchor constraintEqualToAnchor:view.leadingAnchor],
        [self.fFilters.centerYAnchor constraintEqualToAnchor:view.centerYAnchor],
        [self.fTotalField.trailingAnchor constraintEqualToAnchor:view.trailingAnchor],
        [self.fTotalField.centerYAnchor constraintEqualToAnchor:view.centerYAnchor],
        [view.heightAnchor constraintEqualToConstant:36],
    ]];

    self.view = view;

    FilterType current = [NSUserDefaults.standardUserDefaults stringForKey:@"Filter"];
    if (![self.fTypes containsObject:current])
    {
        current = FilterTypeNone;
        [NSUserDefaults.standardUserDefaults setObject:current forKey:@"Filter"];
    }
    self.fFilters.selectedSegment = [self.fTypes indexOfObject:current];
    [self.fFilters setToolTip:NSLocalizedString(@"Show every transfer", "Filter bar") forSegment:0];
}

- (void)setFilter:(id)sender
{
    NSInteger const index = self.fFilters.selectedSegment;
    if (index >= 0 && index < static_cast<NSInteger>(self.fTypes.count))
    {
        [NSUserDefaults.standardUserDefaults setObject:self.fTypes[index] forKey:@"Filter"];
    }
    [NSNotificationCenter.defaultCenter postNotificationName:@"ApplyFilter" object:nil];
}

- (void)switchFilter:(BOOL)right
{
    NSInteger const count = self.fFilters.segmentCount;
    self.fFilters.selectedSegment = (self.fFilters.selectedSegment + (right ? 1 : count - 1)) % count;
    [self setFilter:self.fFilters];
}

- (void)reset
{
    [NSUserDefaults.standardUserDefaults setInteger:kGroupFilterAllTag forKey:@"FilterGroup"];
    self.fFilters.selectedSegment = 0;
    [self setFilter:self.fFilters];
}

- (NSArray<NSString*>*)searchStrings
{
    return @[];
}

- (void)setTotalText:(NSString*)text
{
    self.fTotalField.stringValue = text;
}

- (void)setCountAll:(NSUInteger)all
             active:(NSUInteger)active
        downloading:(NSUInteger)downloading
            seeding:(NSUInteger)seeding
             paused:(NSUInteger)paused
              error:(NSUInteger)error
{
    // The fifth segment only exists while something has gone wrong.
    BOOL const showProblems = error > 0 || [[NSUserDefaults.standardUserDefaults stringForKey:@"Filter"] isEqualToString:FilterTypeError];
    if (showProblems != (self.fFilters.segmentCount == 5))
    {
        self.fFilters.segmentCount = showProblems ? 5 : 4;
        self.fTypes = showProblems ? @[ FilterTypeNone, FilterTypeDownload, FilterTypeSeed, FilterTypePause, FilterTypeError ] :
                                     @[ FilterTypeNone, FilterTypeDownload, FilterTypeSeed, FilterTypePause ];
    }

    NSString* (^label)(NSString*, NSUInteger) = ^(NSString* title, NSUInteger count) {
        return count > 0 ? [NSString stringWithFormat:@"%@ %lu", title, count] : title;
    };
    [self.fFilters setLabel:NSLocalizedString(@"All", "Filter bar") forSegment:0];
    [self.fFilters setLabel:label(NSLocalizedString(@"Downloading", "Filter bar"), downloading) forSegment:1];
    [self.fFilters setLabel:label(NSLocalizedString(@"Seeding", "Filter bar"), seeding) forSegment:2];
    [self.fFilters setLabel:label(NSLocalizedString(@"Paused", "Filter bar"), paused) forSegment:3];
    if (showProblems)
    {
        [self.fFilters setLabel:label(NSLocalizedString(@"Problems", "Filter bar"), error) forSegment:4];
    }
    for (NSInteger segment = 0; segment < self.fFilters.segmentCount; ++segment)
    {
        [self.fFilters setWidth:0 forSegment:segment];
    }
}

@end
