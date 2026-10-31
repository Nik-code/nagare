// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "FileOutlineController.h"
#import "Torrent.h"
#import "FileListNode.h"
#import "FileOutlineView.h"
#import "FileNameCellView.h"
#import "FilePriorityCellView.h"
#import "FileCheckCellView.h"
#import "NSMutableArrayAdditions.h"
#import "NSStringAdditions.h"
#import "Theme.h"

static CGFloat const kRowHeight = 44.0;

typedef NS_ENUM(NSUInteger, FileCheckMenuTag) { //
    FileCheckMenuTagCheck,
    FileCheckMenuTagUncheck
};

typedef NS_ENUM(NSUInteger, FilePriorityMenuTag) { //
    FilePriorityMenuTagHigh,
    FilePriorityMenuTagNormal,
    FilePriorityMenuTagLow
};

@interface FileOutlineController ()<NSOutlineViewDelegate, NSOutlineViewDataSource, NSMenuItemValidation, FileNameCellDelegate>

@property(nonatomic) NSMutableArray<FileListNode*>* fFileList;

@property(nonatomic) FileOutlineView* fOutline;
@property(nonatomic) BOOL fRenaming;

@property(nonatomic, readonly) NSMenu* menu;

@end

@implementation FileOutlineController

- (instancetype)initWithPriorities:(BOOL)priorities
{
    if ((self = [super init]))
    {
        _fFileList = [[NSMutableArray alloc] init];

        _fOutline = [[FileOutlineView alloc] initWithFrame:NSMakeRect(0, 0, 480, 240)];
        [Theme styleList:_fOutline rowHeight:kRowHeight];
        _fOutline.autoresizesOutlineColumn = NO;
        _fOutline.indentationPerLevel = 14.0;
        _fOutline.allowsMultipleSelection = YES;

        NSTableColumn* name = [[NSTableColumn alloc] initWithIdentifier:@"Name"];
        name.width = 400;
        name.resizingMask = NSTableColumnAutoresizingMask;
        [_fOutline addTableColumn:name];
        _fOutline.outlineTableColumn = name;

        if (priorities)
        {
            NSTableColumn* priority = [[NSTableColumn alloc] initWithIdentifier:@"Priority"];
            priority.width = 30;
            priority.resizingMask = NSTableColumnNoResizing;
            [_fOutline addTableColumn:priority];
        }

        NSTableColumn* check = [[NSTableColumn alloc] initWithIdentifier:@"Check"];
        check.width = 30;
        check.resizingMask = NSTableColumnNoResizing;
        [_fOutline addTableColumn:check];

        _fOutline.dataSource = self;
        _fOutline.delegate = self;
        _fOutline.target = self;
        _fOutline.doubleAction = @selector(doubleClicked:);
        _fOutline.menu = self.menu;

        _scrollView = [Theme scrollViewWithDocument:_fOutline];
    }
    return self;
}

- (FileOutlineView*)outlineView
{
    return _fOutline;
}

- (void)setTorrent:(Torrent*)torrent
{
    _torrent = torrent;

    [self.fFileList setArray:torrent.fileList ?: @[]];

    self.filterText = nil;

    // A single file has nothing to tick: it's the whole transfer.
    [self.fOutline tableColumnWithIdentifier:@"Check"].hidden = torrent != nil && !torrent.folder;

    [self.fOutline reloadData];
    [self.fOutline deselectAll:nil]; //do this after reloading the data #4575
}

- (void)setFilterText:(NSString*)text
{
    NSArray* components = [text nonEmptyComponentsSeparatedByCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet];
    if (!components || components.count == 0)
    {
        text = nil;
        components = nil;
    }

    if ((!text && !_filterText) || (text && _filterText && [text isEqualToString:_filterText]))
    {
        return;
    }

    [self.fOutline beginUpdates];

    NSUInteger currentIndex = 0, totalCount = 0;
    NSMutableArray* itemsToAdd = [NSMutableArray array];
    NSMutableIndexSet* itemsToAddIndexes = [NSMutableIndexSet indexSet];

    NSMutableDictionary* removedIndexesForParents = nil; //ugly, but we can't modify the actual file nodes

    NSArray* tempList = !text ? self.torrent.fileList : self.torrent.flatFileList;
    for (FileListNode* item in tempList)
    {
        __block BOOL filter = NO;
        if (components)
        {
            [components enumerateObjectsWithOptions:NSEnumerationConcurrent usingBlock:^(NSString* obj, NSUInteger /*idx*/, BOOL* stop) {
                if ([item.name rangeOfString:obj options:(NSCaseInsensitiveSearch | NSDiacriticInsensitiveSearch)].location == NSNotFound)
                {
                    filter = YES;
                    *stop = YES;
                }
            }];
        }

        if (!filter)
        {
            FileListNode* parent = nil;
            NSUInteger previousIndex = !item.isFolder ?
                [self findFileNode:item inList:self.fFileList
                         atIndexes:[NSIndexSet indexSetWithIndexesInRange:NSMakeRange(currentIndex, self.fFileList.count - currentIndex)]
                     currentParent:nil
                       finalParent:&parent] :
                NSNotFound;

            if (previousIndex == NSNotFound)
            {
                [itemsToAdd addObject:item];
                [itemsToAddIndexes addIndex:totalCount];
            }
            else
            {
                BOOL move = YES;
                if (!parent)
                {
                    if (previousIndex != currentIndex)
                    {
                        [self.fFileList moveObjectAtIndex:previousIndex toIndex:currentIndex];
                    }
                    else
                    {
                        move = NO;
                    }
                }
                else
                {
                    [self.fFileList insertObject:item atIndex:currentIndex];

                    //figure out the index within the semi-edited table - UGLY
                    if (!removedIndexesForParents)
                    {
                        removedIndexesForParents = [NSMutableDictionary dictionary];
                    }

                    NSMutableIndexSet* removedIndexes = removedIndexesForParents[parent];
                    if (!removedIndexes)
                    {
                        removedIndexes = [NSMutableIndexSet indexSetWithIndex:previousIndex];
                        removedIndexesForParents[parent] = removedIndexes;
                    }
                    else
                    {
                        [removedIndexes addIndex:previousIndex];
                        previousIndex -= [removedIndexes countOfIndexesInRange:NSMakeRange(0, previousIndex)];
                    }
                }

                if (move)
                {
                    [self.fOutline moveItemAtIndex:previousIndex inParent:parent toIndex:currentIndex inParent:nil];
                }

                ++currentIndex;
            }

            ++totalCount;
        }
    }

    //remove trailing items - those are the unused
    if (currentIndex < self.fFileList.count)
    {
        NSRange const removeRange = NSMakeRange(currentIndex, self.fFileList.count - currentIndex);
        [self.fFileList removeObjectsInRange:removeRange];
        [self.fOutline removeItemsAtIndexes:[NSIndexSet indexSetWithIndexesInRange:removeRange] inParent:nil
                              withAnimation:NSTableViewAnimationSlideDown];
    }

    //add new items
    [self.fFileList insertObjects:itemsToAdd atIndexes:itemsToAddIndexes];
    [self.fOutline insertItemsAtIndexes:itemsToAddIndexes inParent:nil withAnimation:NSTableViewAnimationSlideUp];

    [self.fOutline endUpdates];

    _filterText = text;
}

- (void)reloadVisibleRows
{
    // Reloading would take the text field away from under the person typing.
    if (self.fRenaming)
    {
        return;
    }

    NSRect visibleRect = self.fOutline.visibleRect;
    NSRange range = [self.fOutline rowsInRect:visibleRect];

    NSIndexSet* rowIndexes = [NSIndexSet indexSetWithIndexesInRange:range];
    NSIndexSet* columnIndexes = [NSIndexSet indexSetWithIndexesInRange:NSMakeRange(0, self.fOutline.numberOfColumns)];

    [self.fOutline reloadDataForRowIndexes:rowIndexes columnIndexes:columnIndexes];
}

#pragma mark - NSOutlineViewDataSource

- (NSInteger)outlineView:(NSOutlineView*)outlineView numberOfChildrenOfItem:(id)item
{
    if (!item)
    {
        return self.fFileList ? self.fFileList.count : 0;
    }
    else
    {
        FileListNode* node = (FileListNode*)item;
        return node.isFolder ? node.children.count : 0;
    }
}

- (BOOL)outlineView:(NSOutlineView*)outlineView isItemExpandable:(id)item
{
    return ((FileListNode*)item).isFolder;
}

- (id)outlineView:(NSOutlineView*)outlineView child:(NSInteger)index ofItem:(id)item
{
    return (item ? ((FileListNode*)item).children : self.fFileList)[index];
}

#pragma mark - NSOutlineViewDelegate

- (NSView*)outlineView:(NSOutlineView*)outlineView viewForTableColumn:(NSTableColumn*)tableColumn item:(id)item
{
    NSString* identifier = tableColumn.identifier;
    FileListNode* node = (FileListNode*)item;

    if ([identifier isEqualToString:@"Name"])
    {
        FileNameCellView* cellView = [outlineView makeViewWithIdentifier:@"NameCell" owner:self];
        if (!cellView)
        {
            cellView = [[FileNameCellView alloc] initWithFrame:NSZeroRect];
            cellView.identifier = @"NameCell";
        }
        cellView.delegate = self;
        cellView.node = node;

        return cellView;
    }
    else if ([identifier isEqualToString:@"Priority"])
    {
        FilePriorityCellView* cellView = [outlineView makeViewWithIdentifier:@"PriorityCell" owner:self];
        if (!cellView)
        {
            cellView = [[FilePriorityCellView alloc] initWithFrame:NSZeroRect];
            cellView.identifier = @"PriorityCell";
        }
        cellView.node = node;

        return cellView;
    }
    else if ([identifier isEqualToString:@"Check"])
    {
        FileCheckCellView* cellView = [outlineView makeViewWithIdentifier:@"CheckCell" owner:self];
        if (!cellView)
        {
            cellView = [[FileCheckCellView alloc] initWithFrame:NSZeroRect];
            cellView.identifier = @"CheckCell";
        }
        cellView.node = node;

        return cellView;
    }

    return nil;
}

- (NSString*)outlineView:(NSOutlineView*)outlineView typeSelectStringForTableColumn:(NSTableColumn*)tableColumn item:(id)item
{
    return ((FileListNode*)item).name;
}

- (void)outlineViewSelectionDidChange:(NSNotification*)notification
{
    [self reloadVisibleRows];
    if ([QLPreviewPanel sharedPreviewPanelExists] && [QLPreviewPanel sharedPreviewPanel].visible)
    {
        [[QLPreviewPanel sharedPreviewPanel] reloadData];
    }
}

- (NSTableRowView*)outlineView:(NSOutlineView*)outlineView rowViewForItem:(id)item
{
    return [[ThemedRowView alloc] initWithHairlineInset:0 selectionInset:0];
}

#pragma mark - Actions

- (void)setCheck:(id)sender
{
    NSControlStateValue state = [sender tag] == FileCheckMenuTagUncheck ? NSControlStateValueOff : NSControlStateValueOn;

    NSIndexSet* indexSet = self.fOutline.selectedRowIndexes;
    NSMutableIndexSet* itemIndexes = [NSMutableIndexSet indexSet];
    for (NSInteger i = indexSet.firstIndex; i != NSNotFound; i = [indexSet indexGreaterThanIndex:i])
    {
        FileListNode* item = [self.fOutline itemAtRow:i];
        [itemIndexes addIndexes:item.indexes];
    }

    [self.torrent setFileCheckState:state forIndexes:itemIndexes];

    [self reloadVisibleRows];
}

- (void)setOnlySelectedCheck:(id)sender
{
    NSIndexSet* indexSet = self.fOutline.selectedRowIndexes;
    NSMutableIndexSet* itemIndexes = [NSMutableIndexSet indexSet];
    for (NSInteger i = indexSet.firstIndex; i != NSNotFound; i = [indexSet indexGreaterThanIndex:i])
    {
        FileListNode* item = [self.fOutline itemAtRow:i];
        [itemIndexes addIndexes:item.indexes];
    }

    [self.torrent setFileCheckState:NSControlStateValueOn forIndexes:itemIndexes];

    NSMutableIndexSet* remainingItemIndexes = [NSMutableIndexSet indexSetWithIndexesInRange:NSMakeRange(0, self.torrent.fileCount)];
    [remainingItemIndexes removeIndexes:itemIndexes];
    [self.torrent setFileCheckState:NSControlStateValueOff forIndexes:remainingItemIndexes];

    [self reloadVisibleRows];
}

- (void)checkAll
{
    NSIndexSet* indexSet = [NSIndexSet indexSetWithIndexesInRange:NSMakeRange(0, self.torrent.fileCount)];
    [self.torrent setFileCheckState:NSControlStateValueOn forIndexes:indexSet];

    [self reloadVisibleRows];
}

- (void)uncheckAll
{
    NSIndexSet* indexSet = [NSIndexSet indexSetWithIndexesInRange:NSMakeRange(0, self.torrent.fileCount)];
    [self.torrent setFileCheckState:NSControlStateValueOff forIndexes:indexSet];

    [self reloadVisibleRows];
}

- (void)setPriority:(id)sender
{
    tr_priority_t priority;
    switch ([sender tag])
    {
    case FilePriorityMenuTagHigh:
        priority = TR_PRI_HIGH;
        break;
    case FilePriorityMenuTagNormal:
        priority = TR_PRI_NORMAL;
        break;
    case FilePriorityMenuTagLow:
        priority = TR_PRI_LOW;
        break;
    default:
        NSAssert1(NO, @"Unknown sender tag: %ld", [sender tag]);
        return;
    }

    NSIndexSet* indexSet = self.fOutline.selectedRowIndexes;
    NSMutableIndexSet* itemIndexes = [NSMutableIndexSet indexSet];
    for (NSInteger i = indexSet.firstIndex; i != NSNotFound; i = [indexSet indexGreaterThanIndex:i])
    {
        FileListNode* item = [self.fOutline itemAtRow:i];
        [itemIndexes addIndexes:item.indexes];
    }

    [self.torrent setFilePriority:priority forIndexes:itemIndexes];

    [self reloadVisibleRows];
}

- (void)revealFile:(id)sender
{
    NSIndexSet* indexes = self.fOutline.selectedRowIndexes;
    NSMutableArray* paths = [NSMutableArray arrayWithCapacity:indexes.count];
    for (NSUInteger i = indexes.firstIndex; i != NSNotFound; i = [indexes indexGreaterThanIndex:i])
    {
        NSString* path = [self.torrent fileLocation:[self.fOutline itemAtRow:i]];
        if (path)
        {
            [paths addObject:[NSURL fileURLWithPath:path]];
        }
    }

    if (paths.count > 0)
    {
        [NSWorkspace.sharedWorkspace activateFileViewerSelectingURLs:paths];
    }
}

- (void)renameSelected:(id)sender
{
    NSIndexSet* indexes = self.fOutline.selectedRowIndexes;
    NSAssert(indexes.count == 1, @"1 file needs to be selected to rename, but %ld are selected", indexes.count);

    [self beginRenameAtRow:indexes.firstIndex];
}

- (void)beginRenameAtRow:(NSInteger)row
{
    NSView* cell = [self.fOutline viewAtColumn:[self.fOutline columnWithIdentifier:@"Name"] row:row makeIfNecessary:NO];
    if ([cell isKindOfClass:FileNameCellView.class])
    {
        [(FileNameCellView*)cell beginRename];
    }
}

- (void)doubleClicked:(id)sender
{
    NSInteger const row = self.fOutline.clickedRow;
    if (row < 0)
    {
        return;
    }

    if (self.doubleClickRenames)
    {
        [self beginRenameAtRow:row];
    }
    else
    {
        [self revealFile:sender];
    }
}

- (void)fileCellWillRename:(FileNameCellView*)cell
{
    self.fRenaming = YES;
}

- (void)fileCell:(FileNameCellView*)cell didFinishRenamingTo:(NSString*)name
{
    self.fRenaming = NO;
    if (name == nil)
    {
        return;
    }

    FileListNode* node = cell.node;
    Torrent* torrent = node.torrent;
    void (^finished)(BOOL) = ^(BOOL didRename) {
        if (!didRename)
        {
            NSBeep();
        }
        [self reloadVisibleRows];
        [NSNotificationCenter.defaultCenter postNotificationName:@"UpdateTorrentsState" object:nil];
        [NSNotificationCenter.defaultCenter postNotificationName:@"ResetInspector" object:self userInfo:@{ @"Torrent" : torrent }];
    };

    if (torrent.folder)
    {
        [torrent renameFileNode:node withName:name completionHandler:finished];
    }
    else
    {
        [torrent renameTorrent:name completionHandler:finished];
    }
}

#pragma mark - NSMenuItemValidation

#warning make real view controller (Leopard-only) so that Command-R will work
- (BOOL)validateMenuItem:(NSMenuItem*)menuItem
{
    if (!self.torrent)
    {
        return NO;
    }

    SEL action = menuItem.action;

    if (action == @selector(revealFile:))
    {
        NSIndexSet* indexSet = self.fOutline.selectedRowIndexes;
        for (NSInteger i = indexSet.firstIndex; i != NSNotFound; i = [indexSet indexGreaterThanIndex:i])
        {
            if ([self.torrent fileLocation:[self.fOutline itemAtRow:i]] != nil)
            {
                return YES;
            }
        }
        return NO;
    }

    if (action == @selector(setCheck:))
    {
        if (self.fOutline.numberOfSelectedRows == 0)
        {
            return NO;
        }

        NSIndexSet* indexSet = self.fOutline.selectedRowIndexes;
        NSMutableIndexSet* itemIndexes = [NSMutableIndexSet indexSet];
        for (NSInteger i = indexSet.firstIndex; i != NSNotFound; i = [indexSet indexGreaterThanIndex:i])
        {
            FileListNode* node = [self.fOutline itemAtRow:i];
            [itemIndexes addIndexes:node.indexes];
        }

        NSControlStateValue state = (menuItem.tag == FileCheckMenuTagCheck) ? NSControlStateValueOn : NSControlStateValueOff;
        return [self.torrent checkForFiles:itemIndexes] != state && [self.torrent canChangeDownloadCheckForFiles:itemIndexes];
    }

    if (action == @selector(setOnlySelectedCheck:))
    {
        if (self.fOutline.numberOfSelectedRows == 0)
        {
            return NO;
        }

        NSIndexSet* indexSet = self.fOutline.selectedRowIndexes;
        NSMutableIndexSet* itemIndexes = [NSMutableIndexSet indexSet];
        for (NSInteger i = indexSet.firstIndex; i != NSNotFound; i = [indexSet indexGreaterThanIndex:i])
        {
            FileListNode* node = [self.fOutline itemAtRow:i];
            [itemIndexes addIndexes:node.indexes];
        }

        return [self.torrent canChangeDownloadCheckForFiles:itemIndexes];
    }

    if (action == @selector(setPriority:))
    {
        if (self.fOutline.numberOfSelectedRows == 0)
        {
            menuItem.state = NSControlStateValueOff;
            return NO;
        }

        //determine which priorities are checked
        NSIndexSet* indexSet = self.fOutline.selectedRowIndexes;
        tr_priority_t priority;
        switch (menuItem.tag)
        {
        case FilePriorityMenuTagHigh:
            priority = TR_PRI_HIGH;
            break;
        case FilePriorityMenuTagNormal:
            priority = TR_PRI_NORMAL;
            break;
        case FilePriorityMenuTagLow:
            priority = TR_PRI_LOW;
            break;
        default:
            NSAssert1(NO, @"Unknown menuItem tag: %ld", menuItem.tag);
            return NO;
        }

        BOOL current = NO, canChange = NO;
        for (NSInteger i = indexSet.firstIndex; i != NSNotFound; i = [indexSet indexGreaterThanIndex:i])
        {
            FileListNode* node = [self.fOutline itemAtRow:i];
            NSIndexSet* fileIndexSet = node.indexes;
            if (![self.torrent canChangeDownloadCheckForFiles:fileIndexSet])
            {
                continue;
            }

            canChange = YES;
            if ([self.torrent hasFilePriority:priority forIndexes:fileIndexSet])
            {
                current = YES;
                break;
            }
        }

        menuItem.state = current ? NSControlStateValueOn : NSControlStateValueOff;
        return canChange;
    }

    if (action == @selector(renameSelected:))
    {
        return self.fOutline.numberOfSelectedRows == 1;
    }

    return YES;
}

#pragma mark - Private

- (NSMenu*)menu
{
    NSMenu* menu = [[NSMenu alloc] initWithTitle:@""];

    //check and uncheck
    NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:NSLocalizedString(@"Check Selected", "File Outline -> Menu")
                                                  action:@selector(setCheck:)
                                           keyEquivalent:@""];
    if (@available(macOS 26.0, *))
    {
        item.image = [NSImage imageWithSystemSymbolName:@"checkmark.circle" accessibilityDescription:nil];
    }
    item.target = self;
    item.tag = FileCheckMenuTagCheck;
    [menu addItem:item];

    item = [[NSMenuItem alloc] initWithTitle:NSLocalizedString(@"Uncheck Selected", "File Outline -> Menu")
                                      action:@selector(setCheck:)
                               keyEquivalent:@""];
    if (@available(macOS 26.0, *))
    {
        item.image = [NSImage imageWithSystemSymbolName:@"circle" accessibilityDescription:nil];
    }
    item.target = self;
    item.tag = FileCheckMenuTagUncheck;
    [menu addItem:item];

    //only check selected
    item = [[NSMenuItem alloc] initWithTitle:NSLocalizedString(@"Only Check Selected", "File Outline -> Menu")
                                      action:@selector(setOnlySelectedCheck:)
                               keyEquivalent:@""];
    if (@available(macOS 26.0, *))
    {
        item.image = [NSImage imageWithSystemSymbolName:@"checkmark.circle.dotted" accessibilityDescription:nil];
    }
    item.target = self;
    [menu addItem:item];

    [menu addItem:[NSMenuItem separatorItem]];

    //priority
    item = [[NSMenuItem alloc] initWithTitle:NSLocalizedString(@"Priority", "File Outline -> Menu") action:NULL keyEquivalent:@""];
    NSMenu* priorityMenu = [[NSMenu alloc] initWithTitle:@""];
    if (@available(macOS 26.0, *))
    {
        item.image = [NSImage imageWithSystemSymbolName:@"chevron.up.chevron.down" accessibilityDescription:nil];
    }
    item.submenu = priorityMenu;
    [menu addItem:item];

    item = [[NSMenuItem alloc] initWithTitle:NSLocalizedString(@"High", "File Outline -> Priority Menu")
                                      action:@selector(setPriority:)
                               keyEquivalent:@""];
    item.target = self;
    item.tag = FilePriorityMenuTagHigh;
    item.image = [NSImage imageNamed:@"PriorityHighTemplate"];
    [priorityMenu addItem:item];

    item = [[NSMenuItem alloc] initWithTitle:NSLocalizedString(@"Normal", "File Outline -> Priority Menu")
                                      action:@selector(setPriority:)
                               keyEquivalent:@""];
    item.target = self;
    item.tag = FilePriorityMenuTagNormal;
    item.image = [NSImage imageNamed:@"PriorityNormalTemplate"];
    [priorityMenu addItem:item];

    item = [[NSMenuItem alloc] initWithTitle:NSLocalizedString(@"Low", "File Outline -> Priority Menu")
                                      action:@selector(setPriority:)
                               keyEquivalent:@""];
    item.target = self;
    item.tag = FilePriorityMenuTagLow;
    item.image = [NSImage imageNamed:@"PriorityLowTemplate"];
    [priorityMenu addItem:item];

    [menu addItem:[NSMenuItem separatorItem]];

    //reveal in finder
    item = [[NSMenuItem alloc] initWithTitle:NSLocalizedString(@"Show in Finder", "File Outline -> Menu")
                                      action:@selector(revealFile:)
                               keyEquivalent:@""];
    if (@available(macOS 26.0, *))
    {
        item.image = [NSImage imageWithSystemSymbolName:@"finder" accessibilityDescription:nil];
    }
    item.target = self;
    [menu addItem:item];

    [menu addItem:[NSMenuItem separatorItem]];

    //rename
    item = [[NSMenuItem alloc] initWithTitle:[NSLocalizedString(@"Rename File", "File Outline -> Menu") stringByAppendingEllipsis]
                                      action:@selector(renameSelected:)
                               keyEquivalent:@""];
    if (@available(macOS 26.0, *))
    {
        item.image = [NSImage imageWithSystemSymbolName:@"pencil" accessibilityDescription:nil];
    }
    item.target = self;
    [menu addItem:item];

    return menu;
}

- (NSUInteger)findFileNode:(FileListNode*)node
                    inList:(NSArray<FileListNode*>*)list
                 atIndexes:(NSIndexSet*)indexes
             currentParent:(FileListNode*)currentParent
               finalParent:(FileListNode* __autoreleasing*)parent
{
    NSAssert(!node.isFolder, @"Looking up folder node!");

    __block FileListNode* retNode;
    __block NSUInteger retIndex = NSNotFound;

    using FindFileNode = void (^)(FileListNode*, NSArray<FileListNode*>*, NSIndexSet*, FileListNode*);
    __weak __block FindFileNode weakFindFileNode;
    FindFileNode findFileNode;
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wshadow"
    weakFindFileNode = findFileNode = ^(FileListNode* node, NSArray<FileListNode*>* list, NSIndexSet* indexes, FileListNode* currentParent) {
#pragma clang diagnostic pop
        [list enumerateObjectsAtIndexes:indexes options:NSEnumerationConcurrent
                             usingBlock:^(FileListNode* checkNode, NSUInteger index, BOOL* stop) {
                                 if ([checkNode.indexes containsIndex:node.indexes.firstIndex])
                                 {
                                     if (!checkNode.isFolder)
                                     {
                                         NSAssert([checkNode isEqualTo:node], @"Expected file nodes to be equal: %@ %@", checkNode, node);
                                         retNode = currentParent;
                                         retIndex = index;
                                     }
                                     else
                                     {
                                         weakFindFileNode(
                                             node,
                                             checkNode.children,
                                             [NSIndexSet indexSetWithIndexesInRange:NSMakeRange(0, checkNode.children.count)],
                                             checkNode);
                                         NSAssert(retIndex != NSNotFound, @"We didn't find an expected file node.");
                                     }
                                     *stop = YES;
                                 }
                             }];
    };
    findFileNode(node, list, indexes, currentParent);

    if (retNode)
    {
        *parent = retNode;
    }
    return retIndex;
}

@end
