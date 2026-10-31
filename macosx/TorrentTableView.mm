// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "CocoaCompatibility.h"

#import "TorrentTableView.h"
#import "Controller.h"
#import "FileListNode.h"
#import "InfoOptionsViewController.h"
#import "NSStringAdditions.h"
#import "Torrent.h"
#import "DropletCell.h"
#import "DropletView.h"
#import "GroupCell.h"
#import "TorrentGroup.h"
#import "Theme.h"
#import "GroupsController.h"
#import "NSImageAdditions.h"

CGFloat const kGroupSeparatorHeight = 18.0;

static NSInteger const kMaxGroup = 999999;

@interface NSIndexSet (Transmission)
- (NSIndexSet*)symmetricDifference:(NSIndexSet*)otherSet;
@end

@implementation NSIndexSet (Transmission)

- (NSIndexSet*)symmetricDifference:(NSIndexSet*)otherSet
{
    NSMutableIndexSet* result = [self mutableCopy];
    [result addIndexes:otherSet];

    [self enumerateIndexesUsingBlock:^(NSUInteger idx, BOOL*) {
        if ([otherSet containsIndex:idx])
        {
            [result removeIndex:idx];
        }
    }];

    return [result copy];
}

@end

@interface TorrentTableView ()

@property(nonatomic) IBOutlet Controller* fController;

@property(nonatomic, readonly) NSUserDefaults* fDefaults;

@property(nonatomic, readonly) NSMutableIndexSet* fCollapsedGroups;

@property(nonatomic) IBOutlet NSMenu* fContextRow;
@property(nonatomic) IBOutlet NSMenu* fContextNoRow;

@property(nonatomic) NSIndexSet* fSelectedRowIndexes;




@property(nonatomic) NSMutableIndexSet* fPendingSelectionReloadRows;

@end

@implementation TorrentTableView

- (instancetype)initWithCoder:(NSCoder*)decoder
{
    if ((self = [super initWithCoder:decoder]))
    {
        _fDefaults = NSUserDefaults.standardUserDefaults;

        NSData* groupData;
        if ((groupData = [_fDefaults dataForKey:@"CollapsedGroupIndexes"]))
        {
            _fCollapsedGroups = [NSKeyedUnarchiver unarchivedObjectOfClass:NSMutableIndexSet.class fromData:groupData error:nil];
        }
        else if ([_fDefaults objectForKey:@"CollapsedGroups"])
        {
            //ancient key that was read via unrestricted NSUnarchiver; discarded rather than read (GHSA-v7mp-fpv3-jf5r)
            [_fDefaults removeObjectForKey:@"CollapsedGroups"];
        }
        if (_fCollapsedGroups == nil)
        {
            _fCollapsedGroups = [[NSMutableIndexSet alloc] init];
        }

        self.delegate = self;
        self.indentationPerLevel = 0;

        self.style = NSTableViewStyleFullWidth;
    }

    return self;
}

- (void)awakeFromNib
{
    [super awakeFromNib];
    [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(refreshTorrentTable) name:@"RefreshTorrentTable"
                                             object:nil];
}

- (void)refreshTorrentTable
{
    self.needsDisplay = YES;
}

//make sure we don't lose selection on manual reloads
- (void)reloadData
{
    NSArray<Torrent*>* selectedTorrents = self.selectedTorrents;
    [super reloadData];
    self.selectedTorrents = selectedTorrents;
}

- (void)reloadVisibleRows
{
    // Rows update in place, which keeps hover state and skips rebuilding views on every tick.
    NSRange const visible = [self rowsInRect:self.visibleRect];
    NSMutableIndexSet* others = [NSMutableIndexSet indexSet];
    for (NSUInteger row = visible.location; row < NSMaxRange(visible); ++row)
    {
        id view = [self viewAtColumn:0 row:row makeIfNecessary:NO];
        if ([view isKindOfClass:DropletCell.class])
        {
            [(DropletCell*)view refresh];
        }
        else
        {
            [others addIndex:row];
        }
    }
    if (others.count == 0)
    {
        return;
    }

    NSRect visibleRect = self.visibleRect;
    NSRange range = [self rowsInRect:visibleRect];

    //since we use floating group rows, we need some magic to find visible group rows
    if ([self.fDefaults boolForKey:@"SortByGroup"])
    {
        NSInteger location = range.location;
        NSInteger length = range.length;
        NSRange fullRange = NSMakeRange(0, length + location);
        NSIndexSet* fullIndexSet = [NSIndexSet indexSetWithIndexesInRange:fullRange];
        NSMutableIndexSet* visibleIndexSet = [[NSMutableIndexSet alloc] init];

        [fullIndexSet enumerateIndexesUsingBlock:^(NSUInteger row, BOOL*) {
            id rowItem = [self itemAtRow:row];
            if ([rowItem isKindOfClass:[TorrentGroup class]])
            {
                [visibleIndexSet addIndex:row];
            }
            else if (NSIntersectsRect(visibleRect, [self rectOfRow:row]))
            {
                [visibleIndexSet addIndex:row];
            }
        }];

        [self reloadDataForRowIndexes:visibleIndexSet columnIndexes:[NSIndexSet indexSetWithIndex:0]];
    }
    else
    {
        [self reloadDataForRowIndexes:[NSIndexSet indexSetWithIndexesInRange:range] columnIndexes:[NSIndexSet indexSetWithIndex:0]];
    }
}

- (void)reloadDataForRowIndexes:(NSIndexSet*)rowIndexes columnIndexes:(NSIndexSet*)columnIndexes
{
    [super reloadDataForRowIndexes:rowIndexes columnIndexes:columnIndexes];
}

- (BOOL)usesAlternatingRowBackgroundColors
{
    return NO;
}

- (NSTableRowView*)outlineView:(NSOutlineView*)outlineView rowViewForItem:(id)item
{
    return [[ThemedRowView alloc] init];
}

- (BOOL)isGroupCollapsed:(NSInteger)value
{
    if (value == -1)
    {
        value = kMaxGroup;
    }

    return [self.fCollapsedGroups containsIndex:value];
}

- (void)removeCollapsedGroup:(NSInteger)value
{
    if (value == -1)
    {
        value = kMaxGroup;
    }

    [self.fCollapsedGroups removeIndex:value];
}

- (void)removeAllCollapsedGroups
{
    [self.fCollapsedGroups removeAllIndexes];
}

- (void)saveCollapsedGroups
{
    [self.fDefaults setObject:[NSKeyedArchiver archivedDataWithRootObject:self.fCollapsedGroups requiringSecureCoding:YES error:nil]
                       forKey:@"CollapsedGroupIndexes"];
}

- (BOOL)outlineView:(NSOutlineView*)outlineView isGroupItem:(id)item
{
    // We are implementing our own group styling.
    // Apple's default group styling conflicts with this.
    return NO;
}

- (CGFloat)outlineView:(NSOutlineView*)outlineView heightOfRowByItem:(id)item
{
    return [item isKindOfClass:[Torrent class]] ? self.rowHeight : kGroupSeparatorHeight;
}

- (NSView*)outlineView:(NSOutlineView*)outlineView viewForTableColumn:(NSTableColumn*)tableColumn item:(id)item
{
    if ([item isKindOfClass:[Torrent class]])
    {
        DropletCell* cell = [outlineView makeViewWithIdentifier:@"DropletCell" owner:self];
        if (cell == nil)
        {
            cell = [[DropletCell alloc] initWithFrame:NSMakeRect(0, 0, NSWidth(self.bounds), self.rowHeight)];
            cell.identifier = @"DropletCell";
        }
        [cell configureWithTorrent:(Torrent*)item table:self];
        return cell;
    }
    else
    {
        TorrentGroup* group = (TorrentGroup*)item;
        GroupCell* groupCell = [outlineView makeViewWithIdentifier:@"GroupCell" owner:self];

        NSInteger groupIndex = group.groupIndex;

        NSColor* groupColor = groupIndex != -1 ? [GroupsController.groups colorForIndex:groupIndex] :
                                                 [NSColor colorWithWhite:1.0 alpha:0];
        groupCell.fGroupIndicatorView.image = [NSImage discIconWithColor:groupColor insetFactor:0];

        NSString* groupName = groupIndex != -1 ? [GroupsController.groups nameForIndex:groupIndex] :
                                                 NSLocalizedString(@"No Group", "Group table row");

        NSInteger row = [self rowForItem:item];
        if ([self isRowSelected:row])
        {
            NSMutableAttributedString* string = [[NSMutableAttributedString alloc] initWithString:groupName];
            NSDictionary* attributes = @{
                NSFontAttributeName : [NSFont boldSystemFontOfSize:11.0],
                NSForegroundColorAttributeName : [NSColor labelColor]
            };

            [string addAttributes:attributes range:NSMakeRange(0, string.length)];
            groupCell.fGroupTitleField.attributedStringValue = string;
        }
        else
        {
            groupCell.fGroupTitleField.stringValue = groupName;
        }

        groupCell.fGroupDownloadField.stringValue = [NSString stringForSpeed:group.downloadRate];
        groupCell.fGroupDownloadView.image = [NSImage imageNamed:@"DownArrowGroupTemplate"];

        NSString* tooltipDownload = NSLocalizedString(@"Download speed", "Torrent table -> group row -> tooltip");
        groupCell.fGroupDownloadField.toolTip = tooltipDownload;
        groupCell.fGroupDownloadView.toolTip = tooltipDownload;

        BOOL displayGroupRowRatio = [self.fDefaults boolForKey:@"DisplayGroupRowRatio"];
        groupCell.fGroupDownloadField.hidden = displayGroupRowRatio;
        groupCell.fGroupDownloadView.hidden = displayGroupRowRatio;

        if (displayGroupRowRatio)
        {
            groupCell.fGroupUploadAndRatioView.image = [NSImage imageNamed:@"YingYangGroupTemplate"];
            groupCell.fGroupUploadAndRatioView.image.accessibilityDescription = NSLocalizedString(@"Ratio", "Torrent -> status image");

            groupCell.fGroupUploadAndRatioField.stringValue = [NSString stringForRatio:group.ratio];

            NSString* tooltipRatio = NSLocalizedString(@"Ratio", "Torrent table -> group row -> tooltip");
            groupCell.fGroupUploadAndRatioField.toolTip = tooltipRatio;
            groupCell.fGroupUploadAndRatioView.toolTip = tooltipRatio;
        }
        else
        {
            groupCell.fGroupUploadAndRatioView.image = [NSImage imageNamed:@"UpArrowGroupTemplate"];
            groupCell.fGroupUploadAndRatioView.image.accessibilityDescription = NSLocalizedString(@"UL", "Torrent -> status image");

            groupCell.fGroupUploadAndRatioField.stringValue = [NSString stringForSpeed:group.uploadRate];

            NSString* tooltipUpload = NSLocalizedString(@"Upload speed", "Torrent table -> group row -> tooltip");
            groupCell.fGroupUploadAndRatioField.toolTip = tooltipUpload;
            groupCell.fGroupUploadAndRatioView.toolTip = tooltipUpload;
        }

        NSString* tooltipGroup;
        NSUInteger count = group.torrents.count;
        if (count == 1)
        {
            tooltipGroup = NSLocalizedString(@"1 transfer", "Torrent table -> group row -> tooltip");
        }
        else
        {
            tooltipGroup = NSLocalizedString(@"%lu transfers", "Torrent table -> group row -> tooltip");
            tooltipGroup = [NSString localizedStringWithFormat:tooltipGroup, count];
        }
        groupCell.toolTip = tooltipGroup;

        return groupCell;
    }
    return nil;
}

- (NSString*)outlineView:(NSOutlineView*)outlineView typeSelectStringForTableColumn:(NSTableColumn*)tableColumn item:(id)item
{
    if ([item isKindOfClass:[Torrent class]])
    {
        return ((Torrent*)item).name;
    }
    else
    {
        return [self.dataSource outlineView:outlineView objectValueForTableColumn:[self tableColumnWithIdentifier:@"Group"]
                                     byItem:item];
    }
}

- (void)outlineViewSelectionDidChange:(NSNotification*)notification
{
    NSIndexSet* oldSelection = self.fSelectedRowIndexes ?: [NSIndexSet indexSet];
    NSIndexSet* newSelection = self.selectedRowIndexes;
    self.fSelectedRowIndexes = newSelection;

    NSIndexSet* changedRows = [oldSelection symmetricDifference:newSelection];
    if (changedRows.count > 0)
    {
        if (!self.fPendingSelectionReloadRows)
        {
            self.fPendingSelectionReloadRows = [[NSMutableIndexSet alloc] init];
            [self performSelector:@selector(flushSelectionReload) withObject:nil afterDelay:0 inModes:@[ NSRunLoopCommonModes ]];
        }

        [self.fPendingSelectionReloadRows addIndexes:changedRows];
    }
}

- (void)flushSelectionReload
{
    NSMutableIndexSet* rows = self.fPendingSelectionReloadRows;
    self.fPendingSelectionReloadRows = nil;

    NSInteger const numberOfRows = self.numberOfRows;
    [rows removeIndexesInRange:NSMakeRange(numberOfRows, NSIntegerMax - numberOfRows)];
    if (rows.count > 0)
    {
        [self reloadDataForRowIndexes:rows columnIndexes:[NSIndexSet indexSetWithIndex:0]];
    }
}

- (void)outlineViewItemDidExpand:(NSNotification*)notification
{
    TorrentGroup* group = notification.userInfo[@"NSObject"];
    NSInteger value = group.groupIndex;
    if (value < 0)
    {
        value = kMaxGroup;
    }

    if ([self.fCollapsedGroups containsIndex:value])
    {
        [self.fCollapsedGroups removeIndex:value];
        [NSNotificationCenter.defaultCenter postNotificationName:@"OutlineExpandCollapse" object:self];
    }
}

- (void)outlineViewItemDidCollapse:(NSNotification*)notification
{
    TorrentGroup* group = notification.userInfo[@"NSObject"];
    NSInteger value = group.groupIndex;
    if (value < 0)
    {
        value = kMaxGroup;
    }

    [self.fCollapsedGroups addIndex:value];
    [NSNotificationCenter.defaultCenter postNotificationName:@"OutlineExpandCollapse" object:self];
}

- (void)mouseDown:(NSEvent*)event
{
    NSPoint point = [self convertPoint:event.locationInWindow fromView:nil];
    NSInteger const row = [self rowAtPoint:point];

    [super mouseDown:event];

    id item = nil;
    if (row != -1)
    {
        item = [self itemAtRow:row];
    }

    if (event.clickCount == 2) //double click
    {
        Torrent* torrent = [item isKindOfClass:[Torrent class]] ? item : nil;
        if (torrent.allDownloaded && torrent.dataLocation != nil && [self.fDefaults boolForKey:@"OpenFinishedOnDoubleClick"])
        {
            [NSWorkspace.sharedWorkspace openURL:[NSURL fileURLWithPath:torrent.dataLocation]];
        }
        else if (!item || [item isKindOfClass:[Torrent class]])
        {
            [self.fController showInfo:nil];
        }
        else
        {
            if ([self isItemExpanded:item])
            {
                [self collapseItem:item];
            }
            else
            {
                [self expandItem:item];
            }
        }
    }
    else if ([self pointInGroupStatusRect:point])
    {
        //we check for this here rather than in the GroupCell
        //as using floating group rows causes all sorts of weirdness...
        [self toggleGroupRowRatio];
    }
}

- (NSArray<Torrent*>*)selectedTorrents
{
    NSIndexSet* selectedIndexes = self.selectedRowIndexes;
    NSMutableArray* torrents = [NSMutableArray arrayWithCapacity:selectedIndexes.count]; //take a shot at guessing capacity

    for (NSUInteger i = selectedIndexes.firstIndex; i != NSNotFound; i = [selectedIndexes indexGreaterThanIndex:i])
    {
        id item = [self itemAtRow:i];
        if ([item isKindOfClass:[Torrent class]])
        {
            [torrents addObject:item];
        }
        else
        {
            NSArray* groupTorrents = ((TorrentGroup*)item).torrents;
            [torrents addObjectsFromArray:groupTorrents];
            if ([self isItemExpanded:item])
            {
                i += groupTorrents.count;
            }
        }
    }

    return torrents;
}

- (void)setSelectedTorrents:(NSArray<Torrent*>*)selectedTorrents
{
    NSMutableIndexSet* selectedIndexes = [NSMutableIndexSet new];
    for (Torrent* i in selectedTorrents)
    {
        [selectedIndexes addIndex:[self rowForItem:i]];
    }
    [self selectRowIndexes:selectedIndexes byExtendingSelection:NO];
}

- (NSMenu*)menuForEvent:(NSEvent*)event
{
    NSInteger row = [self rowAtPoint:[self convertPoint:event.locationInWindow fromView:nil]];
    if (row >= 0)
    {
        if (![self isRowSelected:row])
        {
            [self selectRowIndexes:[NSIndexSet indexSetWithIndex:row] byExtendingSelection:NO];
        }
        return self.fContextRow;
    }
    else
    {
        [self deselectAll:self];
        return self.fContextNoRow;
    }
}

//make sure that the pause buttons become orange when holding down the option key
- (void)flagsChanged:(NSEvent*)event
{
    [self display];
    [super flagsChanged:event];
}

//option-command-f will focus the filter bar's search field
- (void)keyDown:(NSEvent*)event
{
    unichar const firstChar = [event.charactersIgnoringModifiers characterAtIndex:0];

    if (firstChar == 'f' && event.modifierFlags & NSEventModifierFlagOption && event.modifierFlags & NSEventModifierFlagCommand)
    {
        [self.fController focusFilterField];
    }
    else if (firstChar == ' ')
    {
        [self.fController toggleQuickLook:nil];
    }
    else if (event.keyCode == 53) //esc key
    {
        [self deselectAll:nil];
    }
    else
    {
        [super keyDown:event];
    }
}

- (NSRect)iconRectForRow:(NSInteger)row
{
    DropletCell* cell = [self viewAtColumn:0 row:row makeIfNecessary:NO];
    NSRect rect = [cell isKindOfClass:DropletCell.class] ? cell.dropletView.frame : NSMakeRect(24, 0, 48, 48);
    rect.origin.y += NSMinY([self rectOfRow:row]);
    return rect;
}

- (BOOL)acceptsFirstResponder
{
    // add support to `copy:`
    return YES;
}

- (void)copy:(id)sender
{
    NSArray<Torrent*>* selectedTorrents = self.selectedTorrents;
    if (selectedTorrents.count == 0)
    {
        return;
    }
    NSPasteboard* pasteBoard = NSPasteboard.generalPasteboard;
    NSString* links = [[selectedTorrents valueForKeyPath:@"magnetLink"] componentsJoinedByString:@"\n"];
    [pasteBoard declareTypes:@[ NSPasteboardTypeString ] owner:nil];
    [pasteBoard setString:links forType:NSPasteboardTypeString];
}

- (void)paste:(id)sender
{
    [self.fController openPasteboard];
}

- (BOOL)validateMenuItem:(NSMenuItem*)menuItem
{
    SEL action = menuItem.action;

    if (action == @selector(paste:))
    {
        if ([NSPasteboard.generalPasteboard.types containsObject:NSPasteboardTypeURL])
        {
            return YES;
        }

        NSArray* items = [NSPasteboard.generalPasteboard readObjectsForClasses:@[ [NSString class] ] options:nil];
        if (items)
        {
            NSDataDetector* detector = [NSDataDetector dataDetectorWithTypes:NSTextCheckingTypeLink error:nil];
            for (__strong NSString* pbItem in items)
            {
                pbItem = [pbItem stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet];
                if (([pbItem rangeOfString:@"magnet:" options:(NSAnchoredSearch | NSCaseInsensitiveSearch)].location != NSNotFound) ||
                    [detector firstMatchInString:pbItem options:0 range:NSMakeRange(0, pbItem.length)])
                {
                    return YES;
                }
            }
        }

        return NO;
    }

    return YES;
}

- (void)toggleGroupRowRatio
{
    BOOL displayGroupRowRatio = [self.fDefaults boolForKey:@"DisplayGroupRowRatio"];
    [self.fDefaults setBool:!displayGroupRowRatio forKey:@"DisplayGroupRowRatio"];
    [self reloadVisibleRows];
}

- (IBAction)toggleControlForTorrent:(id)sender
{
    Torrent* torrent = [self itemAtRow:[self rowForView:[sender superview]]];
    if (torrent.active)
    {
        [self.fController stopTorrents:@[ torrent ]];
    }
    else
    {
        if (NSEvent.modifierFlags & NSEventModifierFlagOption)
        {
            [self.fController resumeTorrentsNoWait:@[ torrent ]];
        }
        else if (torrent.waitingToStart)
        {
            [self.fController stopTorrents:@[ torrent ]];
        }
        else
        {
            [self.fController resumeTorrents:@[ torrent ]];
        }
    }
}

- (IBAction)revealTorrentFile:(id)sender
{
    Torrent* torrent = [self itemAtRow:[self rowForView:[sender superview]]];
    NSString* location = torrent.dataLocation;
    if (location)
    {
        NSURL* file = [NSURL fileURLWithPath:location];
        [NSWorkspace.sharedWorkspace activateFileViewerSelectingURLs:@[ file ]];
    }
}

- (void)selectAndScrollToRow:(NSInteger)row
{
    NSParameterAssert(row >= 0);
    NSParameterAssert(row < self.numberOfRows);

    [self selectRowIndexes:[NSIndexSet indexSetWithIndex:row] byExtendingSelection:NO];

    NSRect const rowRect = [self rectOfRow:row];
    NSRect const viewRect = self.superview.frame;

    NSPoint scrollOrigin = rowRect.origin;
    scrollOrigin.y += (rowRect.size.height - viewRect.size.height) / 2;
    if (scrollOrigin.y < 0)
    {
        scrollOrigin.y = 0;
    }

    [[self.superview animator] setBoundsOrigin:scrollOrigin];
}

#pragma mark - Private

- (BOOL)pointInGroupStatusRect:(NSPoint)point
{
    NSInteger row = [self rowAtPoint:point];
    if (![[self itemAtRow:row] isKindOfClass:[TorrentGroup class]])
    {
        return NO;
    }

    //check if click is within the status/ratio rect
    GroupCell* groupCell = [self viewAtColumn:0 row:row makeIfNecessary:NO];
    NSRect titleRect = groupCell.fGroupTitleField.frame;
    CGFloat maxX = NSMaxX(titleRect);

    return point.x > maxX;
}

@end
