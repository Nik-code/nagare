// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "InfoTrackersViewController.h"
#import "Theme.h"
#import "Torrent.h"
#import "TrackerNode.h"
#import "TrackerTableView.h"

static CGFloat const kTierRowHeight = 30.0;
static CGFloat const kTrackerRowHeight = 54.0;

/// One tracker: where it is, how it answered last, and the swarm it reports.
@interface TrackerRowCell : NSTableCellView
@property(nonatomic) NSTextField* fHostField;
@property(nonatomic) NSTextField* fStatusField;
@property(nonatomic) NSTextField* fSwarmField;
@end

@implementation TrackerRowCell

- (instancetype)initWithFrame:(NSRect)frame
{
    if ((self = [super initWithFrame:frame]))
    {
        _fHostField = [Theme labelWithFont:[NSFont systemFontOfSize:13 weight:NSFontWeightMedium] color:Theme.bright];
        _fStatusField = [Theme labelWithFont:[NSFont systemFontOfSize:12] color:Theme.foreground];
        _fSwarmField = [Theme labelWithFont:[NSFont monospacedDigitSystemFontOfSize:12 weight:NSFontWeightRegular] color:Theme.foreground];
        _fSwarmField.alignment = NSTextAlignmentRight;
        [_fSwarmField setContentCompressionResistancePriority:NSLayoutPriorityRequired forOrientation:NSLayoutConstraintOrientationHorizontal];
        [self addSubview:_fSwarmField];

        NSStackView* text = [NSStackView stackViewWithViews:@[ _fHostField, _fStatusField ]];
        text.orientation = NSUserInterfaceLayoutOrientationVertical;
        text.alignment = NSLayoutAttributeLeading;
        text.spacing = 1;
        text.translatesAutoresizingMaskIntoConstraints = NO;
        [self addSubview:text];

        [NSLayoutConstraint activateConstraints:@[
            [text.leadingAnchor constraintEqualToAnchor:self.leadingAnchor constant:20],
            [text.centerYAnchor constraintEqualToAnchor:self.centerYAnchor],
            [text.trailingAnchor constraintEqualToAnchor:_fSwarmField.leadingAnchor constant:-12],
            [_fStatusField.widthAnchor constraintEqualToAnchor:text.widthAnchor],
            [_fSwarmField.trailingAnchor constraintEqualToAnchor:self.trailingAnchor constant:-20],
            [_fSwarmField.centerYAnchor constraintEqualToAnchor:_fHostField.centerYAnchor],
        ]];
    }
    return self;
}

@end

@interface InfoTrackersViewController ()<NSTableViewDataSource, NSTableViewDelegate>

@property(nonatomic, copy) NSArray<Torrent*>* fTorrents;

@property(nonatomic) BOOL fSet;

@property(nonatomic) NSMutableArray* fTrackers;

@property(nonatomic) TrackerTableView* fTrackerTable;
@property(nonatomic) NSTextField* fAddField;
@property(nonatomic) NSButton* fAddButton;
@property(nonatomic) NSButton* fRemoveButton;

@end

@implementation InfoTrackersViewController

- (instancetype)init
{
    if ((self = [super initWithNibName:nil bundle:nil]))
    {
        self.title = NSLocalizedString(@"Trackers", "Inspector view -> title");
    }

    return self;
}

- (void)loadView
{
    NSView* view = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 540, 480)];

    self.fTrackerTable = [[TrackerTableView alloc] initWithFrame:NSMakeRect(0, 0, 540, 300)];
    [Theme styleList:self.fTrackerTable rowHeight:kTrackerRowHeight];
    self.fTrackerTable.allowsMultipleSelection = YES;
    NSTableColumn* column = [[NSTableColumn alloc] initWithIdentifier:@"Tracker"];
    column.width = 540;
    column.resizingMask = NSTableColumnAutoresizingMask;
    [self.fTrackerTable addTableColumn:column];
    self.fTrackerTable.dataSource = self;
    self.fTrackerTable.delegate = self;

    NSScrollView* list = [Theme scrollViewWithDocument:self.fTrackerTable];
    [view addSubview:list];

    self.fAddField = [[NSTextField alloc] initWithFrame:NSZeroRect];
    self.fAddField.bezeled = YES;
    self.fAddField.bezelStyle = NSTextFieldRoundedBezel;
    self.fAddField.placeholderString = NSLocalizedString(@"Add a tracker address", "Inspector view -> tracker buttons");
    self.fAddField.target = self;
    self.fAddField.action = @selector(addTracker:);
    self.fAddField.translatesAutoresizingMaskIntoConstraints = NO;
    [view addSubview:self.fAddField];

    self.fAddButton = [Theme glassButton:NSLocalizedString(@"Add", "Inspector view -> tracker buttons") target:self action:@selector(addTracker:)];
    self.fRemoveButton = [Theme glassButton:NSLocalizedString(@"Remove", "Inspector view -> tracker buttons") target:self action:@selector(removeSelected:)];
    self.fRemoveButton.toolTip = NSLocalizedString(@"Remove selected trackers", "Inspector view -> tracker buttons");
    [view addSubview:self.fAddButton];
    [view addSubview:self.fRemoveButton];

    [NSLayoutConstraint activateConstraints:@[
        [list.leadingAnchor constraintEqualToAnchor:view.leadingAnchor],
        [list.trailingAnchor constraintEqualToAnchor:view.trailingAnchor],
        [list.topAnchor constraintEqualToAnchor:view.topAnchor constant:6],
        [list.bottomAnchor constraintEqualToAnchor:self.fAddField.topAnchor constant:-10],
        [self.fAddField.leadingAnchor constraintEqualToAnchor:view.leadingAnchor constant:20],
        [self.fAddField.bottomAnchor constraintEqualToAnchor:view.bottomAnchor constant:-16],
        [self.fAddField.trailingAnchor constraintEqualToAnchor:self.fAddButton.leadingAnchor constant:-10],
        [self.fAddButton.centerYAnchor constraintEqualToAnchor:self.fAddField.centerYAnchor],
        [self.fAddButton.trailingAnchor constraintEqualToAnchor:self.fRemoveButton.leadingAnchor constant:-6],
        [self.fRemoveButton.centerYAnchor constraintEqualToAnchor:self.fAddField.centerYAnchor],
        [self.fRemoveButton.trailingAnchor constraintEqualToAnchor:view.trailingAnchor constant:-20],
    ]];

    self.view = view;
}

- (void)setInfoForTorrents:(NSArray<Torrent*>*)torrents
{
    //don't check if it's the same in case the metadata changed
    self.fTorrents = torrents;

    self.fSet = NO;
}

- (void)updateInfo
{
    if (!self.fSet)
    {
        [self setupInfo];
    }

    if (self.fTorrents.count == 0)
    {
        return;
    }

    //get updated tracker stats
    NSMutableArray* trackers = [[NSMutableArray alloc] init];
    for (Torrent* torrent in self.fTorrents)
    {
        [trackers addObjectsFromArray:torrent.allTrackerStats];
    }
    self.fTrackers = trackers;
    self.fTrackerTable.trackers = self.fTrackers;
    [self.fTrackerTable reloadData];
}

- (void)clearView
{
    self.fTrackers = nil;
}

- (NSInteger)numberOfRowsInTableView:(NSTableView*)tableView
{
    return self.fTrackers ? self.fTrackers.count : 0;
}

- (NSView*)tableView:(NSTableView*)tableView viewForTableColumn:(NSTableColumn*)tableColumn row:(NSInteger)row
{
    id item = self.fTrackers[row];

    if ([item isKindOfClass:[TrackerNode class]])
    {
        TrackerRowCell* cell = [tableView makeViewWithIdentifier:@"TrackerCell" owner:self];
        if (cell == nil)
        {
            cell = [[TrackerRowCell alloc] initWithFrame:NSZeroRect];
            cell.identifier = @"TrackerCell";
        }

        TrackerNode* node = item;
        cell.fHostField.stringValue = node.host;
        cell.fStatusField.stringValue = node.lastAnnounceStatusString;
        cell.fSwarmField.stringValue = node.totalSeeders >= 0 || node.totalLeechers >= 0 ?
            [NSString stringWithFormat:NSLocalizedString(@"%ld seeds  ·  %ld peers", "Inspector -> tracker table"),
                                       MAX(node.totalSeeders, 0),
                                       MAX(node.totalLeechers, 0)] :
            @"";
        cell.toolTip = [NSString stringWithFormat:@"%@\n%@\n%@", node.fullAnnounceAddress, node.nextAnnounceStatusString, node.lastScrapeStatusString];
        return cell;
    }

    NSTableCellView* cell = [tableView makeViewWithIdentifier:@"TierCell" owner:self];
    if (cell == nil)
    {
        cell = [[NSTableCellView alloc] initWithFrame:NSZeroRect];
        cell.identifier = @"TierCell";
        NSTextField* label = [Theme labelWithFont:[NSFont systemFontOfSize:12 weight:NSFontWeightMedium] color:Theme.foreground];
        label.lineBreakMode = NSLineBreakByTruncatingTail;
        [cell addSubview:label];
        cell.textField = label;
        [NSLayoutConstraint activateConstraints:@[
            [label.leadingAnchor constraintEqualToAnchor:cell.leadingAnchor constant:20],
            [label.trailingAnchor constraintEqualToAnchor:cell.trailingAnchor constant:-20],
            [label.bottomAnchor constraintEqualToAnchor:cell.bottomAnchor constant:-4],
        ]];
    }

    NSString* tierString = [NSString stringWithFormat:NSLocalizedString(@"Tier %ld", "Inspector -> tracker table"), [item[@"Tier"] integerValue]];
    if (self.fTorrents.count > 1)
    {
        tierString = [tierString stringByAppendingFormat:@"  ·  %@", item[@"Name"]];
    }
    cell.textField.stringValue = tierString;
    return cell;
}

- (NSTableRowView*)tableView:(NSTableView*)tableView rowViewForRow:(NSInteger)row
{
    return [[ThemedRowView alloc] init];
}

- (CGFloat)tableView:(NSTableView*)tableView heightOfRow:(NSInteger)row
{
    return [self.fTrackers[row] isKindOfClass:[TrackerNode class]] ? kTrackerRowHeight : kTierRowHeight;
}

- (void)tableViewSelectionDidChange:(NSNotification*)notification
{
    self.fRemoveButton.enabled = self.fTrackerTable.numberOfSelectedRows > 0;
}

- (BOOL)tableView:(NSTableView*)tableView shouldSelectRow:(NSInteger)row
{
    return [self.fTrackers[row] isKindOfClass:[TrackerNode class]];
}

- (void)addTracker:(id)sender
{
    NSString* text = [self.fAddField.stringValue stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet];
    if (text.length == 0 || self.fTorrents.count != 1)
    {
        return;
    }

    Torrent* torrent = self.fTorrents[0];
    BOOL added = NO;
    for (NSString* tracker in [text componentsSeparatedByCharactersInSet:NSCharacterSet.newlineCharacterSet])
    {
        if ([torrent addTrackerToNewTier:tracker])
        {
            added = YES;
        }
    }

    if (!added)
    {
        NSBeep();
        return;
    }

    self.fAddField.stringValue = @"";
    [self updateInfo];
    [NSNotificationCenter.defaultCenter postNotificationName:@"UpdateUI" object:nil]; //in case sort by tracker
}

- (void)removeSelected:(id)sender
{
    [self updateInfo];
    [self removeTrackers];
}

#pragma mark - Private

- (void)setupInfo
{
    [self view];

    NSUInteger const numberSelected = self.fTorrents.count;
    if (numberSelected == 0)
    {
        self.fTrackers = nil;
        self.fTrackerTable.trackers = nil;
        [self.fTrackerTable reloadData];
    }

    self.fTrackerTable.torrent = numberSelected == 1 ? self.fTorrents[0] : nil;
    self.fAddField.enabled = numberSelected == 1;
    self.fAddButton.enabled = numberSelected == 1;
    self.fRemoveButton.enabled = NO;

    [self.fTrackerTable deselectAll:self];

    self.fSet = YES;
}

- (void)removeTrackers
{
    NSMutableDictionary* removeIdentifiers = [NSMutableDictionary dictionaryWithCapacity:self.fTorrents.count];
    NSUInteger removeTrackerCount = 0;

    NSIndexSet* selectedIndexes = self.fTrackerTable.selectedRowIndexes;
    BOOL groupSelected = NO;
    NSUInteger groupRowIndex = NSNotFound;
    NSMutableIndexSet* removeIndexes = [NSMutableIndexSet indexSet];
    for (NSUInteger i = 0; i < self.fTrackers.count; ++i)
    {
        id object = self.fTrackers[i];
        if ([object isKindOfClass:[TrackerNode class]])
        {
            TrackerNode* node = (TrackerNode*)object;
            if (groupSelected || [selectedIndexes containsIndex:i])
            {
                Torrent* torrent = node.torrent;
                NSMutableSet* removeSet;
                if (!(removeSet = removeIdentifiers[torrent]))
                {
                    removeSet = [NSMutableSet set];
                    removeIdentifiers[torrent] = removeSet;
                }

                [removeSet addObject:node.fullAnnounceAddress];
                ++removeTrackerCount;

                [removeIndexes addIndex:i];
            }
            else
            {
                groupRowIndex = NSNotFound; //don't remove the group row
            }
        }
        else
        {
            //mark the previous group row for removal, if necessary
            if (groupRowIndex != NSNotFound)
            {
                [removeIndexes addIndex:groupRowIndex];
            }

            groupSelected = [selectedIndexes containsIndex:i];
            if (!groupSelected && i > selectedIndexes.lastIndex)
            {
                groupRowIndex = NSNotFound;
                break;
            }

            groupRowIndex = i;
        }
    }

    //mark the last group for removal, too
    if (groupRowIndex != NSNotFound)
    {
        [removeIndexes addIndex:groupRowIndex];
    }

    NSAssert2(
        removeTrackerCount <= removeIndexes.count,
        @"Marked %ld trackers to remove, but only removing %ld rows",
        removeTrackerCount,
        removeIndexes.count);

//we might have no trackers if remove right after a failed add (race condition ftw)
#warning look into having a failed add apply right away, so that this can become an assert
    if (removeTrackerCount == 0)
    {
        return;
    }

    if ([NSUserDefaults.standardUserDefaults boolForKey:@"WarningRemoveTrackers"])
    {
        NSAlert* alert = [[NSAlert alloc] init];

        if (removeTrackerCount > 1)
        {
            alert.messageText = [NSString
                localizedStringWithFormat:NSLocalizedString(@"Are you sure you want to remove %lu trackers?", "Remove trackers alert -> title"),
                                          removeTrackerCount];
            alert.informativeText = NSLocalizedString(
                @"Once removed, Nagare will no longer attempt to contact them."
                 " This cannot be undone.",
                "Remove trackers alert -> message");
        }
        else
        {
            alert.messageText = NSLocalizedString(@"Are you sure you want to remove this tracker?", "Remove trackers alert -> title");
            alert.informativeText = NSLocalizedString(
                @"Once removed, Nagare will no longer attempt to contact it."
                 " This cannot be undone.",
                "Remove trackers alert -> message");
        }

        [alert addButtonWithTitle:NSLocalizedString(@"Remove", "Remove trackers alert -> button")];
        [alert addButtonWithTitle:NSLocalizedString(@"Cancel", "Remove trackers alert -> button")];

        alert.showsSuppressionButton = YES;

        NSInteger result = [alert runModal];
        if (alert.suppressionButton.state == NSControlStateValueOn)
        {
            [NSUserDefaults.standardUserDefaults setBool:NO forKey:@"WarningRemoveTrackers"];
        }

        if (result != NSAlertFirstButtonReturn)
        {
            return;
        }
    }

    [self.fTrackerTable beginUpdates];

    for (Torrent* torrent in removeIdentifiers)
    {
        [torrent removeTrackers:removeIdentifiers[torrent]];
    }

    //reset table with either new or old value
    self.fTrackers = [[NSMutableArray alloc] init];
    for (Torrent* torrent in self.fTorrents)
    {
        [self.fTrackers addObjectsFromArray:torrent.allTrackerStats];
    }

    [self.fTrackerTable removeRowsAtIndexes:removeIndexes withAnimation:NSTableViewAnimationSlideLeft];

    self.fTrackerTable.trackers = self.fTrackers;

    [self.fTrackerTable endUpdates];

    [NSNotificationCenter.defaultCenter postNotificationName:@"UpdateUI" object:nil]; //in case sort by tracker
}

@end
