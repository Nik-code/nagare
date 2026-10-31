// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "FilterBarController.h"
#import "PillBar.h"
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
@property(nonatomic) PillBar* fFilters;
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

    self.fFilters = [[PillBar alloc] initWithLabels:@[ @"All", @"Downloading", @"Seeding", @"Paused" ] target:self action:@selector(setFilter:)];
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
    NSInteger const count = static_cast<NSInteger>(self.fTypes.count);
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
    // Problems only get a pill while something has gone wrong.
    BOOL const showProblems = error > 0 || [[NSUserDefaults.standardUserDefaults stringForKey:@"Filter"] isEqualToString:FilterTypeError];
    self.fTypes = showProblems ? @[ FilterTypeNone, FilterTypeDownload, FilterTypeSeed, FilterTypePause, FilterTypeError ] :
                                 @[ FilterTypeNone, FilterTypeDownload, FilterTypeSeed, FilterTypePause ];

    NSString* (^label)(NSString*, NSUInteger) = ^(NSString* title, NSUInteger count) {
        return count > 0 ? [NSString stringWithFormat:@"%@ %lu", title, count] : title;
    };
    NSMutableArray<NSString*>* labels = [NSMutableArray arrayWithArray:@[
        NSLocalizedString(@"All", "Filter bar"),
        label(NSLocalizedString(@"Downloading", "Filter bar"), downloading),
        label(NSLocalizedString(@"Seeding", "Filter bar"), seeding),
        label(NSLocalizedString(@"Paused", "Filter bar"), paused),
    ]];
    if (showProblems)
    {
        [labels addObject:label(NSLocalizedString(@"Problems", "Filter bar"), error)];
    }
    self.fFilters.labels = labels;
}

@end
