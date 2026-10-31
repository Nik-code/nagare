// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "SettingsWindowController.h"
#import "PrefsController.h"
#import "SearchSource.h"
#import "Theme.h"

static CGFloat const kSidebarWidth = 212.0;

typedef NS_ENUM(NSInteger, SettingsSection) {
    SettingsSectionGeneral,
    SettingsSectionTransfers,
    SettingsSectionSpeed,
    SettingsSectionSources,
    SettingsSectionAdvanced,
};

@interface SettingsWindowController ()<NSTableViewDataSource, NSTableViewDelegate>
@property(nonatomic, weak) PrefsController* fPrefs;
@property(nonatomic) NSUserDefaults* fDefaults;
@property(nonatomic) NSTableView* fSidebar;
@property(nonatomic) NSView* fDetail;
@property(nonatomic) NSTextField* fTitleField;
@property(nonatomic) NSView* fBody;
@property(nonatomic) NSTextField* fFolderField;
@property(nonatomic) NSTextField* fRatioField;
@property(nonatomic) NSSegmentedControl* fAdvancedTabs;
@property(nonatomic) NSView* fPaneHost;
@end

@implementation SettingsWindowController

- (instancetype)initWithPrefs:(PrefsController*)prefs
{
    NSWindow* window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 900, 640)
                                                   styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable |
                                                       NSWindowStyleMaskFullSizeContentView
                                                     backing:NSBackingStoreBuffered
                                                       defer:YES];
    if ((self = [super initWithWindow:window]))
    {
        _fPrefs = prefs;
        _fDefaults = NSUserDefaults.standardUserDefaults;
        window.releasedWhenClosed = NO;
        window.title = NSLocalizedString(@"Settings", "Settings window title");
        window.collectionBehavior = NSWindowCollectionBehaviorFullScreenNone;
        [self buildInterface];
    }
    return self;
}

- (void)present
{
    NSWindow* window = self.window;
    if (!window.visible)
    {
        [window center];
    }
    [window makeKeyAndOrderFront:nil];
}

- (void)buildInterface
{
    NSSplitViewController* split = [[NSSplitViewController alloc] init];

    NSViewController* sidebarController = [[NSViewController alloc] init];
    sidebarController.view = [self buildSidebar];
    NSSplitViewItem* sidebarItem = [NSSplitViewItem sidebarWithViewController:sidebarController];
    sidebarItem.canCollapse = NO;
    sidebarItem.minimumThickness = kSidebarWidth;
    sidebarItem.maximumThickness = kSidebarWidth;
    [split addSplitViewItem:sidebarItem];

    NSViewController* detailController = [[NSViewController alloc] init];
    SurfaceView* surface = [[SurfaceView alloc] initWithFrame:NSMakeRect(0, 0, 688, 640)];
    self.fDetail = [[NSView alloc] initWithFrame:surface.bounds];
    self.fDetail.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    NSView* holder = [[NSView alloc] initWithFrame:surface.bounds];
    surface.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    [holder addSubview:surface];
    [holder addSubview:self.fDetail];
    detailController.view = holder;
    [split addSplitViewItem:[NSSplitViewItem contentListWithViewController:detailController]];

    self.fTitleField = [Theme labelWithFont:[Theme displayFontOfSize:30] color:Theme.bright];
    [self.fDetail addSubview:self.fTitleField];
    [NSLayoutConstraint activateConstraints:@[
        [self.fTitleField.leadingAnchor constraintEqualToAnchor:self.fDetail.leadingAnchor constant:32],
        [self.fTitleField.topAnchor constraintEqualToAnchor:self.fDetail.topAnchor constant:36],
    ]];

    self.window.contentViewController = split;
    self.window.titlebarAppearsTransparent = YES;
    self.window.titleVisibility = NSWindowTitleHidden;
    [self.window setContentSize:NSMakeSize(900, 640)];

    [self.fSidebar selectRowIndexes:[NSIndexSet indexSetWithIndex:SettingsSectionGeneral] byExtendingSelection:NO];
    [self showSection:SettingsSectionGeneral];
}

- (NSView*)buildSidebar
{
    self.fSidebar = [[NSTableView alloc] initWithFrame:NSMakeRect(0, 0, kSidebarWidth, 600)];
    self.fSidebar.style = NSTableViewStyleSourceList;
    self.fSidebar.headerView = nil;
    self.fSidebar.rowHeight = 34;
    self.fSidebar.backgroundColor = NSColor.clearColor;
    self.fSidebar.dataSource = self;
    self.fSidebar.delegate = self;
    NSTableColumn* column = [[NSTableColumn alloc] initWithIdentifier:@"Section"];
    [self.fSidebar addTableColumn:column];

    NSScrollView* scroll = [[NSScrollView alloc] initWithFrame:NSMakeRect(0, 0, kSidebarWidth, 640)];
    scroll.documentView = self.fSidebar;
    scroll.drawsBackground = NO;
    scroll.hasVerticalScroller = NO;
    scroll.automaticallyAdjustsContentInsets = NO;
    scroll.contentInsets = NSEdgeInsetsMake(44, 0, 0, 0);
    return scroll;
}

- (NSInteger)numberOfRowsInTableView:(NSTableView*)tableView
{
    return 5;
}

- (NSString*)titleForSection:(SettingsSection)section
{
    NSArray<NSString*>* titles = @[
        NSLocalizedString(@"General", "Settings -> section"),
        NSLocalizedString(@"Transfers", "Settings -> section"),
        NSLocalizedString(@"Speed", "Settings -> section"),
        NSLocalizedString(@"Search sources", "Settings -> section"),
        NSLocalizedString(@"Advanced", "Settings -> section"),
    ];
    return titles[section];
}

- (NSView*)tableView:(NSTableView*)tableView viewForTableColumn:(NSTableColumn*)tableColumn row:(NSInteger)row
{
    NSArray<NSString*>* icons = @[ @"Settings", @"Down", @"Gauge", @"Search", @"Filter" ];
    NSTableCellView* cell = [[NSTableCellView alloc] init];

    NSImageView* icon = [NSImageView imageViewWithImage:[Theme icon:icons[row]]];
    icon.contentTintColor = Theme.bright;
    icon.translatesAutoresizingMaskIntoConstraints = NO;
    NSTextField* label = [Theme labelWithFont:[NSFont systemFontOfSize:14] color:Theme.bright];
    label.stringValue = [self titleForSection:static_cast<SettingsSection>(row)];
    [cell addSubview:icon];
    [cell addSubview:label];

    [NSLayoutConstraint activateConstraints:@[
        [icon.leadingAnchor constraintEqualToAnchor:cell.leadingAnchor constant:6],
        [icon.centerYAnchor constraintEqualToAnchor:cell.centerYAnchor],
        [icon.widthAnchor constraintEqualToConstant:18],
        [icon.heightAnchor constraintEqualToConstant:18],
        [label.leadingAnchor constraintEqualToAnchor:icon.trailingAnchor constant:10],
        [label.centerYAnchor constraintEqualToAnchor:cell.centerYAnchor],
    ]];
    return cell;
}

- (void)tableViewSelectionDidChange:(NSNotification*)notification
{
    NSInteger const row = self.fSidebar.selectedRow;
    if (row >= 0)
    {
        [self showSection:static_cast<SettingsSection>(row)];
    }
}

- (void)showSection:(SettingsSection)section
{
    [self.fDetail.window makeFirstResponder:nil];
    [self.fBody removeFromSuperview];
    self.fTitleField.stringValue = [self titleForSection:section];

    NSView* body;
    switch (section)
    {
    case SettingsSectionGeneral:
        body = [self generalSection];
        break;
    case SettingsSectionTransfers:
        body = [self transfersSection];
        break;
    case SettingsSectionSpeed:
        body = [self embeddedPane:@"Bandwidth"];
        break;
    case SettingsSectionSources:
        body = [self sourcesSection];
        break;
    case SettingsSectionAdvanced:
        body = [self advancedSection];
        break;
    }

    self.fBody = body;
    body.translatesAutoresizingMaskIntoConstraints = NO;
    [self.fDetail addSubview:body];
    [NSLayoutConstraint activateConstraints:@[
        [body.leadingAnchor constraintEqualToAnchor:self.fDetail.leadingAnchor constant:32],
        [body.trailingAnchor constraintEqualToAnchor:self.fDetail.trailingAnchor constant:-32],
        [body.topAnchor constraintEqualToAnchor:self.fTitleField.bottomAnchor constant:18],
        [body.bottomAnchor constraintLessThanOrEqualToAnchor:self.fDetail.bottomAnchor constant:-16],
    ]];
}

#pragma mark - Building rows

- (NSView*)rowWithTitle:(NSString*)title hint:(NSString*)hint control:(NSView*)control
{
    NSTextField* titleField = [Theme labelWithFont:[NSFont systemFontOfSize:14 weight:NSFontWeightMedium] color:Theme.bright];
    titleField.stringValue = title;
    NSTextField* hintField = [Theme labelWithFont:[NSFont systemFontOfSize:12] color:Theme.foreground];
    hintField.stringValue = hint;
    NSStackView* text = [NSStackView stackViewWithViews:@[ titleField, hintField ]];
    text.orientation = NSUserInterfaceLayoutOrientationVertical;
    text.alignment = NSLayoutAttributeLeading;
    text.spacing = 2;
    text.translatesAutoresizingMaskIntoConstraints = NO;

    NSView* line = [[NSView alloc] initWithFrame:NSZeroRect];
    line.translatesAutoresizingMaskIntoConstraints = NO;
    NSBox* hairline = [[NSBox alloc] initWithFrame:NSZeroRect];
    hairline.boxType = NSBoxSeparator;
    hairline.translatesAutoresizingMaskIntoConstraints = NO;
    control.translatesAutoresizingMaskIntoConstraints = NO;
    [line addSubview:hairline];
    [line addSubview:text];
    [line addSubview:control];

    [NSLayoutConstraint activateConstraints:@[
        [line.heightAnchor constraintEqualToConstant:60],
        [hairline.topAnchor constraintEqualToAnchor:line.topAnchor],
        [hairline.leadingAnchor constraintEqualToAnchor:line.leadingAnchor],
        [hairline.trailingAnchor constraintEqualToAnchor:line.trailingAnchor],
        [text.leadingAnchor constraintEqualToAnchor:line.leadingAnchor],
        [text.centerYAnchor constraintEqualToAnchor:line.centerYAnchor],
        [control.trailingAnchor constraintEqualToAnchor:line.trailingAnchor],
        [control.centerYAnchor constraintEqualToAnchor:line.centerYAnchor],
        [text.trailingAnchor constraintLessThanOrEqualToAnchor:control.leadingAnchor constant:-16],
    ]];
    return line;
}

- (NSSwitch*)switchForKey:(NSString*)key action:(SEL)action
{
    NSSwitch* toggle = [[NSSwitch alloc] initWithFrame:NSZeroRect];
    toggle.state = [self.fDefaults boolForKey:key] ? NSControlStateValueOn : NSControlStateValueOff;
    toggle.identifier = key;
    toggle.target = self;
    toggle.action = action;
    return toggle;
}

- (NSButton*)buttonWithTitle:(NSString*)title action:(SEL)action target:(id)target
{
    NSButton* button = [NSButton buttonWithTitle:title target:target action:action];
    button.bezelStyle = NSBezelStyleGlass;
    return button;
}

- (NSStackView*)stackOfRows:(NSArray<NSView*>*)rows
{
    NSStackView* stack = [NSStackView stackViewWithViews:rows];
    stack.orientation = NSUserInterfaceLayoutOrientationVertical;
    stack.alignment = NSLayoutAttributeLeading;
    stack.spacing = 0;
    for (NSView* row in rows)
    {
        [row.widthAnchor constraintEqualToAnchor:stack.widthAnchor].active = YES;
    }
    return stack;
}

#pragma mark - Sections

- (NSView*)generalSection
{
    NSSegmentedControl* appearance = [NSSegmentedControl segmentedControlWithLabels:@[
        NSLocalizedString(@"Match system", "Settings -> appearance"),
        NSLocalizedString(@"Light", "Settings -> appearance"),
        NSLocalizedString(@"Dark", "Settings -> appearance"),
    ]
                                                                       trackingMode:NSSegmentSwitchTrackingSelectOne
                                                                             target:self
                                                                             action:@selector(setAppearance:)];
    appearance.selectedSegment = Theme.mode;

    return [self stackOfRows:@[
        [self rowWithTitle:NSLocalizedString(@"Appearance", "Settings") hint:NSLocalizedString(@"Light paper or dark warm grey", "Settings") control:appearance],
        [self rowWithTitle:NSLocalizedString(@"Magnet links", "Settings")
                      hint:NSLocalizedString(@"Open magnet links in Nagare", "Settings")
                   control:[self buttonWithTitle:NSLocalizedString(@"Make Default", "Settings") action:@selector(setDefaultForMagnets:) target:self.fPrefs]],
        [self rowWithTitle:NSLocalizedString(@"Torrent files", "Settings")
                      hint:NSLocalizedString(@"Open .torrent files in Nagare", "Settings")
                   control:[self buttonWithTitle:NSLocalizedString(@"Make Default", "Settings") action:@selector(setDefaultForTorrentFiles:) target:self.fPrefs]],
        [self rowWithTitle:NSLocalizedString(@"Keep the Mac awake", "Settings")
                      hint:NSLocalizedString(@"Don't sleep while something is transferring", "Settings")
                   control:[self switchForKey:@"SleepPrevent" action:@selector(toggleSetting:)]],
        [self rowWithTitle:NSLocalizedString(@"Ask before removing", "Settings")
                      hint:NSLocalizedString(@"Confirm when a transfer is still active", "Settings")
                   control:[self switchForKey:@"CheckRemove" action:@selector(toggleSetting:)]],
        [self rowWithTitle:NSLocalizedString(@"Ask before quitting", "Settings")
                      hint:NSLocalizedString(@"Confirm when transfers are still active", "Settings")
                   control:[self switchForKey:@"CheckQuit" action:@selector(toggleSetting:)]],
    ]];
}

- (NSView*)transfersSection
{
    self.fFolderField = [Theme labelWithFont:[NSFont systemFontOfSize:13] color:Theme.bright];
    self.fFolderField.lineBreakMode = NSLineBreakByTruncatingHead;
    [self updateFolderField];
    NSButton* change = [self buttonWithTitle:NSLocalizedString(@"Change", "Settings") action:@selector(chooseFolder:) target:self];
    NSStackView* folder = [NSStackView stackViewWithViews:@[ self.fFolderField, change ]];
    folder.spacing = 12;

    self.fRatioField = [NSTextField textFieldWithString:[NSString stringWithFormat:@"%.1f", [self.fDefaults floatForKey:@"RatioLimit"]]];
    self.fRatioField.alignment = NSTextAlignmentRight;
    self.fRatioField.target = self;
    self.fRatioField.action = @selector(setRatioGoal:);
    [self.fRatioField.widthAnchor constraintEqualToConstant:52].active = YES;
    NSSwitch* ratioSwitch = [self switchForKey:@"RatioCheck" action:@selector(toggleRatio:)];
    NSStackView* ratio = [NSStackView stackViewWithViews:@[ self.fRatioField, ratioSwitch ]];
    ratio.spacing = 12;

    return [self stackOfRows:@[
        [self rowWithTitle:NSLocalizedString(@"Save to", "Settings") hint:NSLocalizedString(@"Where new downloads go", "Settings") control:folder],
        [self rowWithTitle:NSLocalizedString(@"Ask where to save", "Settings")
                      hint:NSLocalizedString(@"Show the add window for every torrent", "Settings")
                   control:[self switchForKey:@"DownloadAsk" action:@selector(toggleAddWindow:)]],
        [self rowWithTitle:NSLocalizedString(@"Start right away", "Settings")
                      hint:NSLocalizedString(@"Begin as soon as a torrent is added", "Settings")
                   control:[self switchForKey:@"AutoStartDownload" action:@selector(toggleAutoStart:)]],
        [self rowWithTitle:NSLocalizedString(@"Seed until ratio", "Settings") hint:NSLocalizedString(@"Then stop and call it finished", "Settings") control:ratio],
        [self rowWithTitle:NSLocalizedString(@"Open when done", "Settings")
                      hint:NSLocalizedString(@"Double-click a finished transfer to play it", "Settings")
                   control:[self switchForKey:@"OpenFinishedOnDoubleClick" action:@selector(toggleSetting:)]],
    ]];
}

- (NSView*)sourcesSection
{
    NSMutableArray<NSView*>* rows = [NSMutableArray array];
    for (SearchSource* source in SearchSource.allSources)
    {
        NSTextField* tag = [Theme labelWithFont:[NSFont systemFontOfSize:12] color:Theme.foreground];
        tag.stringValue = [source.name isEqualToString:@"Internet Archive"] ? NSLocalizedString(@"Built in", "Settings") : NSLocalizedString(@"Added by you", "Settings");
        [rows addObject:[self rowWithTitle:source.name hint:@"" control:tag]];
    }
    [rows addObject:[self rowWithTitle:NSLocalizedString(@"Add a source", "Settings")
                                  hint:NSLocalizedString(@"Sources are small JSON files, and nothing in them is run", "Settings")
                               control:[self buttonWithTitle:NSLocalizedString(@"Open Folder", "Settings") action:@selector(openSources:) target:self]]];
    return [self stackOfRows:rows];
}

- (NSView*)embeddedPane:(NSString*)name
{
    NSView* pane = [self.fPrefs paneNamed:name];
    [pane removeFromSuperview];
    pane.autoresizingMask = NSViewNotSizable;
    pane.hidden = NO;

    NSSize const size = pane.frame.size;
    NSView* host = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, size.width, size.height)];
    [pane setFrameOrigin:NSZeroPoint];
    [host addSubview:pane];
    [host.heightAnchor constraintEqualToConstant:size.height].active = YES;
    return host;
}

- (NSView*)advancedSection
{
    self.fAdvancedTabs = [NSSegmentedControl segmentedControlWithLabels:@[
        NSLocalizedString(@"Queue", "Settings -> advanced"),
        NSLocalizedString(@"Peers", "Settings -> advanced"),
        NSLocalizedString(@"Network", "Settings -> advanced"),
        NSLocalizedString(@"Remote", "Settings -> advanced"),
        NSLocalizedString(@"Groups", "Settings -> advanced"),
        NSLocalizedString(@"Alerts", "Settings -> advanced"),
    ]
                                                           trackingMode:NSSegmentSwitchTrackingSelectOne
                                                                 target:self
                                                                 action:@selector(showAdvancedPane:)];
    self.fAdvancedTabs.selectedSegment = 0;
    self.fAdvancedTabs.translatesAutoresizingMaskIntoConstraints = NO;

    NSView* body = [[NSView alloc] initWithFrame:NSZeroRect];
    self.fPaneHost = [[NSView alloc] initWithFrame:NSZeroRect];
    self.fPaneHost.translatesAutoresizingMaskIntoConstraints = NO;
    [body addSubview:self.fAdvancedTabs];
    [body addSubview:self.fPaneHost];

    [NSLayoutConstraint activateConstraints:@[
        [self.fAdvancedTabs.leadingAnchor constraintEqualToAnchor:body.leadingAnchor],
        [self.fAdvancedTabs.topAnchor constraintEqualToAnchor:body.topAnchor],
        [self.fPaneHost.leadingAnchor constraintEqualToAnchor:body.leadingAnchor],
        [self.fPaneHost.trailingAnchor constraintEqualToAnchor:body.trailingAnchor],
        [self.fPaneHost.topAnchor constraintEqualToAnchor:self.fAdvancedTabs.bottomAnchor constant:12],
        [self.fPaneHost.bottomAnchor constraintEqualToAnchor:body.bottomAnchor],
    ]];
    [self showAdvancedPane:nil];
    return body;
}

- (void)showAdvancedPane:(id)sender
{
    NSArray<NSString*>* panes = @[ @"Transfers", @"Peers", @"Network", @"Remote", @"Groups", @"General" ];
    for (NSView* view in self.fPaneHost.subviews.copy)
    {
        [view removeFromSuperview];
    }

    NSView* host = [self embeddedPane:panes[self.fAdvancedTabs.selectedSegment]];
    host.translatesAutoresizingMaskIntoConstraints = NO;
    [self.fPaneHost addSubview:host];
    [NSLayoutConstraint activateConstraints:@[
        [host.leadingAnchor constraintEqualToAnchor:self.fPaneHost.leadingAnchor],
        [host.topAnchor constraintEqualToAnchor:self.fPaneHost.topAnchor],
        [host.widthAnchor constraintEqualToConstant:NSWidth(host.frame)],
        [self.fPaneHost.heightAnchor constraintEqualToConstant:NSHeight(host.frame)],
    ]];
}

#pragma mark - Actions

- (void)setAppearance:(NSSegmentedControl*)sender
{
    Theme.mode = static_cast<ThemeMode>(sender.selectedSegment);
}

- (void)toggleSetting:(NSSwitch*)sender
{
    [self.fDefaults setBool:sender.state == NSControlStateValueOn forKey:sender.identifier];
}

- (void)toggleAddWindow:(NSSwitch*)sender
{
    BOOL const ask = sender.state == NSControlStateValueOn;
    [self.fDefaults setBool:ask forKey:@"DownloadAsk"];
    [self.fDefaults setBool:ask forKey:@"MagnetOpenAsk"];
}

- (void)toggleAutoStart:(NSSwitch*)sender
{
    [self.fDefaults setBool:sender.state == NSControlStateValueOn forKey:@"AutoStartDownload"];
    [self.fPrefs setAutoStartDownloads:sender];
}

- (void)toggleRatio:(NSSwitch*)sender
{
    [self.fDefaults setBool:sender.state == NSControlStateValueOn forKey:@"RatioCheck"];
    [self.fPrefs applyRatioSetting:sender];
}

- (void)setRatioGoal:(NSTextField*)sender
{
    CGFloat const goal = MAX(0.1, sender.floatValue);
    sender.stringValue = [NSString stringWithFormat:@"%.1f", goal];
    [self.fDefaults setFloat:goal forKey:@"RatioLimit"];
    [self.fPrefs applyRatioSetting:sender];
}

- (void)updateFolderField
{
    NSString* folder = [self.fDefaults stringForKey:@"DownloadFolder"] ?: @"~/Downloads";
    self.fFolderField.stringValue = folder.stringByAbbreviatingWithTildeInPath;
}

- (void)chooseFolder:(id)sender
{
    [self.fPrefs chooseDownloadFolderInWindow:self.window completion:^{
        [self updateFolderField];
    }];
}

- (void)openSources:(id)sender
{
    [NSWorkspace.sharedWorkspace openURL:SearchSource.sourcesFolder];
}

@end
