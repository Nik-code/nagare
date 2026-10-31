// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "SearchWindowController.h"
#import "Controller.h"
#import "NSStringAdditions.h"
#import "SearchSource.h"
#import "Theme.h"

static CGFloat const kPanelWidth = 840.0;
static CGFloat const kPanelHeight = 520.0;
static CGFloat const kRowHeight = 66.0;
static NSInteger const kHealthDots = 6;
static NSInteger const kAllSourcesTag = -1;
static NSInteger const kManageSourcesTag = -2;

/// A borderless panel still has to be able to take the keyboard.
@interface SearchPanel : NSPanel
@end

@implementation SearchPanel

- (BOOL)canBecomeKeyWindow
{
    return YES;
}

- (void)cancelOperation:(id)sender
{
    [self orderOut:nil];
}

@end

/// Row of dots showing how well seeded a result is.
@interface SearchHealthView : NSView
@property(nonatomic) NSInteger level;
@end

@implementation SearchHealthView

- (NSSize)intrinsicContentSize
{
    return NSMakeSize(kHealthDots * 11.0, 8.0);
}

- (void)drawRect:(NSRect)dirtyRect
{
    for (NSInteger index = 0; index < kHealthDots; ++index)
    {
        [[Theme.bright colorWithAlphaComponent:index < self.level ? 0.9 : 0.18] setFill];
        [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(index * 11.0, 0.5, 7.0, 7.0)] fill];
    }
}

@end

@interface SearchRowView : NSTableRowView
@end

@implementation SearchRowView

- (NSBackgroundStyle)interiorBackgroundStyle
{
    return NSBackgroundStyleNormal;
}

- (void)drawSelectionInRect:(NSRect)dirtyRect
{
    [[Theme.field colorWithAlphaComponent:0.6] setFill];
    [[NSBezierPath bezierPathWithRoundedRect:NSInsetRect(self.bounds, 0.0, 1.0) xRadius:16.0 yRadius:16.0] fill];
}

@end

@interface SearchCell : NSTableCellView
@property(nonatomic) NSTextField* fNameField;
@property(nonatomic) NSTextField* fDetailField;
@property(nonatomic) NSTextField* fSeedsField;
@property(nonatomic) SearchHealthView* fHealthView;
@property(nonatomic) NSButton* fGetButton;
@end

@implementation SearchCell

- (instancetype)initWithFrame:(NSRect)frame
{
    if ((self = [super initWithFrame:frame]))
    {
        _fNameField = [Theme labelWithFont:[NSFont systemFontOfSize:15 weight:NSFontWeightMedium] color:Theme.bright];
        _fDetailField = [Theme labelWithFont:[NSFont systemFontOfSize:13] color:Theme.foreground];
        _fSeedsField = [Theme labelWithFont:[NSFont monospacedDigitSystemFontOfSize:13 weight:NSFontWeightRegular] color:Theme.foreground];
        _fSeedsField.alignment = NSTextAlignmentRight;
        _fHealthView = [[SearchHealthView alloc] initWithFrame:NSZeroRect];
        _fHealthView.translatesAutoresizingMaskIntoConstraints = NO;

        _fGetButton = [NSButton buttonWithTitle:NSLocalizedString(@"Get", "Search -> add button") target:nil action:@selector(getResult:)];
        _fGetButton.bezelStyle = NSBezelStyleGlass;
        _fGetButton.bezelColor = Theme.accent;
        _fGetButton.controlSize = NSControlSizeLarge;
        _fGetButton.translatesAutoresizingMaskIntoConstraints = NO;
        _fGetButton.hidden = YES;

        NSStackView* text = [NSStackView stackViewWithViews:@[ _fNameField, _fDetailField ]];
        text.orientation = NSUserInterfaceLayoutOrientationVertical;
        text.alignment = NSLayoutAttributeLeading;
        text.spacing = 3;
        text.translatesAutoresizingMaskIntoConstraints = NO;

        for (NSView* view in @[ text, _fHealthView, _fSeedsField, _fGetButton ])
        {
            [self addSubview:view];
        }

        [_fNameField setContentCompressionResistancePriority:NSLayoutPriorityDefaultLow forOrientation:NSLayoutConstraintOrientationHorizontal];
        [_fDetailField setContentCompressionResistancePriority:NSLayoutPriorityDefaultLow forOrientation:NSLayoutConstraintOrientationHorizontal];

        [NSLayoutConstraint activateConstraints:@[
            [text.leadingAnchor constraintEqualToAnchor:self.leadingAnchor constant:16],
            [text.centerYAnchor constraintEqualToAnchor:self.centerYAnchor],
            [text.trailingAnchor constraintLessThanOrEqualToAnchor:_fHealthView.leadingAnchor constant:-18],
            [_fGetButton.trailingAnchor constraintEqualToAnchor:self.trailingAnchor constant:-14],
            [_fGetButton.centerYAnchor constraintEqualToAnchor:self.centerYAnchor],
            [_fGetButton.widthAnchor constraintEqualToConstant:64],
            [_fSeedsField.trailingAnchor constraintEqualToAnchor:_fGetButton.leadingAnchor constant:-14],
            [_fSeedsField.centerYAnchor constraintEqualToAnchor:self.centerYAnchor],
            [_fSeedsField.widthAnchor constraintEqualToConstant:70],
            [_fHealthView.trailingAnchor constraintEqualToAnchor:_fSeedsField.leadingAnchor constant:-8],
            [_fHealthView.centerYAnchor constraintEqualToAnchor:self.centerYAnchor],
        ]];
    }
    return self;
}

@end

@interface SearchWindowController ()<NSTableViewDataSource, NSTableViewDelegate, NSTextFieldDelegate>
@property(nonatomic, weak) Controller* fController;
@property(nonatomic) NSTextField* fQueryField;
@property(nonatomic) NSPopUpButton* fSourcesButton;
@property(nonatomic) NSTableView* fTableView;
@property(nonatomic) NSTextField* fStatusField;
@property(nonatomic) NSURLSession* fSession;
@property(nonatomic, copy) NSArray<SearchSource*>* fSources;
@property(nonatomic, copy) NSArray<SearchResult*>* fResults;
@property(nonatomic) NSInteger fSearchGeneration;
@property(nonatomic) BOOL fQueryChanged;
@end

@implementation SearchWindowController

- (instancetype)initWithController:(Controller*)controller
{
    SearchPanel* panel = [[SearchPanel alloc] initWithContentRect:NSMakeRect(0, 0, kPanelWidth, kPanelHeight)
                                                        styleMask:NSWindowStyleMaskBorderless | NSWindowStyleMaskResizable
                                                          backing:NSBackingStoreBuffered
                                                            defer:YES];
    if ((self = [super initWithWindow:panel]))
    {
        _fController = controller;
        _fResults = @[];

        NSURLSessionConfiguration* configuration = NSURLSessionConfiguration.ephemeralSessionConfiguration;
        configuration.timeoutIntervalForRequest = 20;
        configuration.HTTPCookieStorage = nil;
        configuration.HTTPAdditionalHeaders = @{ @"Accept" : @"application/json", @"User-Agent" : @"Nagare" };
        _fSession = [NSURLSession sessionWithConfiguration:configuration];

        panel.opaque = NO;
        panel.backgroundColor = NSColor.clearColor;
        panel.hasShadow = YES;
        panel.movableByWindowBackground = YES;
        panel.releasedWhenClosed = NO;
        panel.minSize = NSMakeSize(640, 360);
        panel.collectionBehavior = NSWindowCollectionBehaviorFullScreenAuxiliary;
        [self buildInterface:panel];
    }
    return self;
}

- (void)buildInterface:(NSPanel*)panel
{
    NSGlassEffectView* glass = [[NSGlassEffectView alloc] initWithFrame:NSMakeRect(0, 0, kPanelWidth, kPanelHeight)];
    glass.cornerRadius = 30;
    panel.contentView = glass;

    NSView* root = [[NSView alloc] initWithFrame:glass.bounds];
    root.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    glass.contentView = root;

    NSImageView* icon = [NSImageView imageViewWithImage:[Theme icon:@"Search"]];
    icon.contentTintColor = Theme.bright;
    icon.symbolConfiguration = nil;
    icon.translatesAutoresizingMaskIntoConstraints = NO;

    self.fQueryField = [[NSTextField alloc] initWithFrame:NSZeroRect];
    self.fQueryField.bordered = NO;
    self.fQueryField.drawsBackground = NO;
    self.fQueryField.focusRingType = NSFocusRingTypeNone;
    self.fQueryField.font = [NSFont systemFontOfSize:21];
    self.fQueryField.textColor = Theme.bright;
    self.fQueryField.placeholderString = NSLocalizedString(@"Search for a torrent", "Search -> field");
    self.fQueryField.target = self;
    self.fQueryField.action = @selector(search:);
    self.fQueryField.delegate = self;
    self.fQueryField.translatesAutoresizingMaskIntoConstraints = NO;

    self.fSourcesButton = [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
    self.fSourcesButton.bezelStyle = NSBezelStyleGlass;
    self.fSourcesButton.target = self;
    self.fSourcesButton.action = @selector(sourceChanged:);
    self.fSourcesButton.translatesAutoresizingMaskIntoConstraints = NO;

    self.fTableView = [[NSTableView alloc] initWithFrame:NSZeroRect];
    self.fTableView.headerView = nil;
    self.fTableView.backgroundColor = NSColor.clearColor;
    self.fTableView.rowHeight = kRowHeight;
    self.fTableView.intercellSpacing = NSZeroSize;
    self.fTableView.style = NSTableViewStylePlain;
    self.fTableView.dataSource = self;
    self.fTableView.delegate = self;
    self.fTableView.target = self;
    self.fTableView.doubleAction = @selector(getSelected:);
    NSTableColumn* column = [[NSTableColumn alloc] initWithIdentifier:@"Result"];
    column.resizingMask = NSTableColumnAutoresizingMask;
    [self.fTableView addTableColumn:column];

    NSScrollView* scroll = [[NSScrollView alloc] initWithFrame:NSZeroRect];
    scroll.documentView = self.fTableView;
    scroll.drawsBackground = NO;
    scroll.hasVerticalScroller = YES;
    scroll.autohidesScrollers = YES;
    scroll.automaticallyAdjustsContentInsets = NO;
    scroll.translatesAutoresizingMaskIntoConstraints = NO;

    self.fStatusField = [Theme labelWithFont:[NSFont systemFontOfSize:13] color:Theme.foreground];
    self.fStatusField.lineBreakMode = NSLineBreakByTruncatingTail;

    NSTextField* hint = [Theme labelWithFont:[NSFont systemFontOfSize:13] color:Theme.foreground];
    hint.stringValue = NSLocalizedString(@"Return to get, Esc to close", "Search -> hint");

    for (NSView* view in @[ icon, self.fQueryField, self.fSourcesButton, scroll, self.fStatusField, hint ])
    {
        [root addSubview:view];
    }

    [NSLayoutConstraint activateConstraints:@[
        [icon.leadingAnchor constraintEqualToAnchor:root.leadingAnchor constant:26],
        [icon.centerYAnchor constraintEqualToAnchor:self.fQueryField.centerYAnchor],
        [icon.widthAnchor constraintEqualToConstant:22],
        [icon.heightAnchor constraintEqualToConstant:22],
        [self.fQueryField.leadingAnchor constraintEqualToAnchor:icon.trailingAnchor constant:12],
        [self.fQueryField.topAnchor constraintEqualToAnchor:root.topAnchor constant:26],
        [self.fQueryField.trailingAnchor constraintEqualToAnchor:self.fSourcesButton.leadingAnchor constant:-12],
        [self.fSourcesButton.trailingAnchor constraintEqualToAnchor:root.trailingAnchor constant:-22],
        [self.fSourcesButton.centerYAnchor constraintEqualToAnchor:self.fQueryField.centerYAnchor],
        [scroll.leadingAnchor constraintEqualToAnchor:root.leadingAnchor constant:12],
        [scroll.trailingAnchor constraintEqualToAnchor:root.trailingAnchor constant:-12],
        [scroll.topAnchor constraintEqualToAnchor:self.fQueryField.bottomAnchor constant:18],
        [scroll.bottomAnchor constraintEqualToAnchor:self.fStatusField.topAnchor constant:-10],
        [self.fStatusField.leadingAnchor constraintEqualToAnchor:root.leadingAnchor constant:28],
        [self.fStatusField.bottomAnchor constraintEqualToAnchor:root.bottomAnchor constant:-16],
        [self.fStatusField.trailingAnchor constraintLessThanOrEqualToAnchor:hint.leadingAnchor constant:-16],
        [hint.trailingAnchor constraintEqualToAnchor:root.trailingAnchor constant:-28],
        [hint.bottomAnchor constraintEqualToAnchor:root.bottomAnchor constant:-16],
    ]];
}

- (void)reloadSources
{
    self.fSources = SearchSource.allSources;

    NSInteger const selected = self.fSourcesButton.selectedTag;
    NSMenu* menu = self.fSourcesButton.menu;
    [menu removeAllItems];
    [menu addItemWithTitle:NSLocalizedString(@"All sources", "Search -> sources") action:nil keyEquivalent:@""].tag = kAllSourcesTag;
    [self.fSources enumerateObjectsUsingBlock:^(SearchSource* source, NSUInteger index, BOOL*) {
        [menu addItemWithTitle:source.name action:nil keyEquivalent:@""].tag = index;
    }];
    [menu addItem:NSMenuItem.separatorItem];
    [menu addItemWithTitle:NSLocalizedString(@"Add sources…", "Search -> sources") action:nil keyEquivalent:@""].tag = kManageSourcesTag;

    if (![self.fSourcesButton selectItemWithTag:selected] || selected == kManageSourcesTag)
    {
        [self.fSourcesButton selectItemWithTag:kAllSourcesTag];
    }
}

- (void)present
{
    [self reloadSources];

    NSWindow* window = self.window;
    if (!window.visible)
    {
        NSWindow* main = self.fController != nil ? NSApp.mainWindow : nil;
        NSRect const anchor = main != nil ? main.frame : window.screen.visibleFrame;
        NSSize const size = window.frame.size;
        [window setFrameOrigin:NSMakePoint(NSMidX(anchor) - size.width / 2.0, NSMaxY(anchor) - size.height - 72.0)];
    }
    [window makeKeyAndOrderFront:nil];
    [window makeFirstResponder:self.fQueryField];
    if (self.fResults.count == 0)
    {
        self.fStatusField.stringValue = NSLocalizedString(@"Type and press Return to search", "Search -> status");
    }
}

- (void)sourceChanged:(id)sender
{
    if (self.fSourcesButton.selectedTag == kManageSourcesTag)
    {
        [self.fSourcesButton selectItemWithTag:kAllSourcesTag];
        [NSWorkspace.sharedWorkspace openURL:SearchSource.sourcesFolder];
        return;
    }
    if (self.fQueryField.stringValue.length > 0)
    {
        [self search:sender];
    }
}

- (void)search:(id)sender
{
    NSString* query = [self.fQueryField.stringValue stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceCharacterSet];
    if (query.length == 0)
    {
        return;
    }

    NSInteger const selected = self.fSourcesButton.selectedTag;
    NSArray<SearchSource*>* sources = selected >= 0 ? @[ self.fSources[selected] ] : self.fSources;

    self.fQueryChanged = NO;
    NSInteger const generation = ++self.fSearchGeneration;
    self.fResults = @[];
    [self.fTableView reloadData];
    self.fStatusField.stringValue = NSLocalizedString(@"Searching…", "Search -> status");

    dispatch_group_t group = dispatch_group_create();
    NSMutableArray<SearchResult*>* found = [NSMutableArray array];
    NSMutableSet<NSString*>* responded = [NSMutableSet set];

    for (SearchSource* source in sources)
    {
        NSURL* url = [source URLForQuery:query];
        if (url == nil)
        {
            continue;
        }

        dispatch_group_enter(group);
        NSURLSessionDataTask* task = [self.fSession dataTaskWithURL:url completionHandler:^(NSData* data, NSURLResponse* response, NSError* error) {
            NSArray<SearchResult*>* results = data != nil && error == nil ? [source resultsFromData:data error:nil] : @[];
            dispatch_async(dispatch_get_main_queue(), ^{
                [found addObjectsFromArray:results];
                if (error == nil)
                {
                    [responded addObject:source.name];
                }
                dispatch_group_leave(group);
            });
        }];
        [task resume];
    }

    dispatch_group_notify(group, dispatch_get_main_queue(), ^{
        if (generation == self.fSearchGeneration)
        {
            [self showResults:found responded:responded.count of:sources.count];
        }
    });
}

- (void)showResults:(NSArray<SearchResult*>*)results responded:(NSUInteger)responded of:(NSUInteger)asked
{
    self.fResults = [results sortedArrayUsingComparator:^NSComparisonResult(SearchResult* a, SearchResult* b) {
        if (a.seeds != b.seeds)
        {
            return a.seeds > b.seeds ? NSOrderedAscending : NSOrderedDescending;
        }
        return NSOrderedSame;
    }];
    [self.fTableView reloadData];

    if (self.fResults.count > 0)
    {
        [self.fTableView selectRowIndexes:[NSIndexSet indexSetWithIndex:0] byExtendingSelection:NO];
    }

    if (responded == 0 && asked > 0)
    {
        self.fStatusField.stringValue = NSLocalizedString(@"Couldn't reach the sources", "Search -> status");
    }
    else if (self.fResults.count == 0)
    {
        self.fStatusField.stringValue = NSLocalizedString(@"Nothing found", "Search -> status");
    }
    else
    {
        self.fStatusField.stringValue = [NSString stringWithFormat:NSLocalizedString(@"%lu results from %lu of %lu sources", "Search -> status"),
                                                                   self.fResults.count,
                                                                   responded,
                                                                   asked];
    }
}

- (void)getResult:(NSButton*)sender
{
    NSInteger const row = [self.fTableView rowForView:sender];
    if (row >= 0)
    {
        [self addResult:self.fResults[row]];
    }
}

- (void)getSelected:(id)sender
{
    NSInteger const row = self.fTableView.selectedRow;
    if (row >= 0 && row < static_cast<NSInteger>(self.fResults.count))
    {
        [self addResult:self.fResults[row]];
    }
}

- (void)addResult:(SearchResult*)result
{
    [self.fController openURL:result.link];
    self.fStatusField.stringValue = [NSString stringWithFormat:NSLocalizedString(@"Added %@", "Search -> status"), result.title];
}

- (BOOL)control:(NSControl*)control textView:(NSTextView*)textView doCommandBySelector:(SEL)selector
{
    if (selector == @selector(moveDown:) || selector == @selector(moveUp:))
    {
        NSInteger const count = static_cast<NSInteger>(self.fResults.count);
        if (count > 0)
        {
            NSInteger const delta = selector == @selector(moveDown:) ? 1 : -1;
            NSInteger const row = MAX(0, MIN(count - 1, self.fTableView.selectedRow + delta));
            [self.fTableView selectRowIndexes:[NSIndexSet indexSetWithIndex:row] byExtendingSelection:NO];
            [self.fTableView scrollRowToVisible:row];
        }
        return YES;
    }
    if (selector == @selector(insertNewline:) && self.fResults.count > 0 && !self.fQueryChanged)
    {
        [self getSelected:nil];
        return YES;
    }
    if (selector == @selector(cancelOperation:))
    {
        [self.window orderOut:nil];
        return YES;
    }
    return NO;
}

- (void)controlTextDidChange:(NSNotification*)notification
{
    self.fQueryChanged = YES;
}

#pragma mark - Table

- (NSInteger)numberOfRowsInTableView:(NSTableView*)tableView
{
    return static_cast<NSInteger>(self.fResults.count);
}

- (NSTableRowView*)tableView:(NSTableView*)tableView rowViewForRow:(NSInteger)row
{
    return [[SearchRowView alloc] init];
}

- (NSView*)tableView:(NSTableView*)tableView viewForTableColumn:(NSTableColumn*)tableColumn row:(NSInteger)row
{
    SearchCell* cell = [tableView makeViewWithIdentifier:@"SearchCell" owner:self];
    if (cell == nil)
    {
        cell = [[SearchCell alloc] initWithFrame:NSMakeRect(0, 0, kPanelWidth, kRowHeight)];
        cell.identifier = @"SearchCell";
        cell.fGetButton.target = self;
    }

    SearchResult* result = self.fResults[row];
    cell.fNameField.stringValue = result.title;
    cell.fDetailField.stringValue = result.size > 0 ? [NSString stringWithFormat:@"%@  ·  %@", result.sourceName, [NSString stringForFileSize:result.size]] : result.sourceName;
    cell.fSeedsField.stringValue = result.seeds >= 0 ? [NSString stringWithFormat:NSLocalizedString(@"%ld seeds", "Search -> seeds"), result.seeds] : @"";
    cell.fHealthView.hidden = result.seeds < 0;
    // Each dot is roughly a doubling, so a healthy swarm fills the row.
    cell.fHealthView.level = result.seeds > 0 ? MIN(kHealthDots, 1 + static_cast<NSInteger>(log2(static_cast<double>(result.seeds)) * 0.8)) : 0;
    cell.fHealthView.needsDisplay = YES;
    cell.fGetButton.hidden = ![tableView isRowSelected:row];
    return cell;
}

- (void)tableViewSelectionDidChange:(NSNotification*)notification
{
    [self.fTableView enumerateAvailableRowViewsUsingBlock:^(NSTableRowView* rowView, NSInteger row) {
        SearchCell* cell = [rowView viewAtColumn:0];
        cell.fGetButton.hidden = !rowView.selected;
    }];
}

@end
