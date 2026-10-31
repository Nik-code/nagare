// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "SettingsWindowController.h"
#import "Controller.h"
#import "PillBar.h"
#import "PrefsController.h"
#import "SearchSource.h"
#import "Theme.h"

#include <libtransmission/transmission.h>

static CGFloat const kSidebarWidth = 212.0;

typedef NS_ENUM(NSInteger, SettingsSection) {
    SettingsSectionGeneral,
    SettingsSectionTransfers,
    SettingsSectionSpeed,
    SettingsSectionSources,
    SettingsSectionAdvanced,
};

/// A quiet selection, so the sidebar's text stays readable on the glass.
@interface SettingsSidebarRow : NSTableRowView
@end

@implementation SettingsSidebarRow

- (NSBackgroundStyle)interiorBackgroundStyle
{
    return NSBackgroundStyleNormal;
}

- (void)drawSelectionInRect:(NSRect)dirtyRect
{
    [[Theme.bright colorWithAlphaComponent:0.1] setFill];
    [[NSBezierPath bezierPathWithRoundedRect:NSInsetRect(self.bounds, 8.0, 1.0) xRadius:9.0 yRadius:9.0] fill];
}

@end

@interface SettingsWindowController ()<NSTableViewDataSource, NSTableViewDelegate>
@property(nonatomic, weak) PrefsController* fPrefs;
@property(nonatomic) NSUserDefaults* fDefaults;
@property(nonatomic) NSTableView* fSidebar;
@property(nonatomic) NSView* fDetail;
@property(nonatomic) NSTextField* fTitleField;
@property(nonatomic) NSView* fBody;
@property(nonatomic) NSTextField* fFolderField;
@property(nonatomic) NSTextField* fRatioField;
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

- (NSTableRowView*)tableView:(NSTableView*)tableView rowViewForRow:(NSInteger)row
{
    return [[SettingsSidebarRow alloc] init];
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
        body = [self speedSection];
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
    return [Theme rowWithTitle:title hintField:[Theme hintField:hint] control:control height:60];
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
    return [Theme glassButton:title target:target action:action];
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
    PillBar* appearance = [[PillBar alloc] initWithLabels:@[
        NSLocalizedString(@"Match system", "Settings -> appearance"),
        NSLocalizedString(@"Light", "Settings -> appearance"),
        NSLocalizedString(@"Dark", "Settings -> appearance"),
    ]
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

- (NSTextField*)numberFieldForKey:(NSString*)key
{
    NSTextField* field = [NSTextField textFieldWithString:[NSString stringWithFormat:@"%ld", [self.fDefaults integerForKey:key]]];
    field.bezelStyle = NSTextFieldRoundedBezel;
    field.alignment = NSTextAlignmentRight;
    field.identifier = key;
    field.target = self;
    field.action = @selector(numberChanged:);
    NSNumberFormatter* formatter = [[NSNumberFormatter alloc] init];
    formatter.numberStyle = NSNumberFormatterNoStyle;
    formatter.minimum = @0;
    formatter.allowsFloats = NO;
    field.formatter = formatter;
    [field.widthAnchor constraintEqualToConstant:64].active = YES;
    return field;
}

/// A number, its unit, and optionally the switch that turns it on.
- (NSView*)numberControlForKey:(NSString*)key unit:(NSString*)unit switchKey:(NSString*)switchKey
{
    NSMutableArray<NSView*>* views = [NSMutableArray arrayWithObject:[self numberFieldForKey:key]];
    if (unit.length > 0)
    {
        NSTextField* unitField = [Theme labelWithFont:[NSFont systemFontOfSize:13] color:Theme.foreground];
        unitField.stringValue = unit;
        [views addObject:unitField];
    }
    if (switchKey != nil)
    {
        [views addObject:[self switchForKey:switchKey action:@selector(switchChanged:)]];
    }
    NSStackView* stack = [NSStackView stackViewWithViews:views];
    stack.spacing = 10;
    return stack;
}

- (NSDatePicker*)timePickerForKey:(NSString*)key
{
    NSDatePicker* picker = [[NSDatePicker alloc] initWithFrame:NSZeroRect];
    picker.datePickerStyle = NSDatePickerStyleTextField;
    picker.datePickerElements = NSDatePickerElementFlagHourMinute;
    picker.dateValue = [self.fDefaults objectForKey:key] ?: NSDate.date;
    picker.identifier = key;
    picker.target = self;
    picker.action = @selector(timeChanged:);
    return picker;
}

- (NSTextField*)headingWithTitle:(NSString*)title
{
    NSTextField* heading = [Theme labelWithFont:[Theme displayFontOfSize:17] color:Theme.bright];
    heading.stringValue = title;
    return heading;
}

- (NSView*)speedSection
{
    NSPopUpButton* days = [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
    NSArray<NSArray*>* choices = @[
        @[ NSLocalizedString(@"Every day", "Settings"), @(TR_SCHED_ALL) ],
        @[ NSLocalizedString(@"Weekdays", "Settings"), @(TR_SCHED_WEEKDAY) ],
        @[ NSLocalizedString(@"Weekends", "Settings"), @(TR_SCHED_WEEKEND) ],
    ];
    for (NSArray* choice in choices)
    {
        [days addItemWithTitle:choice[0]];
        days.lastItem.tag = [choice[1] integerValue];
    }
    [days selectItemWithTag:[self.fDefaults integerForKey:@"SpeedLimitAutoDay"]];
    days.target = self;
    days.action = @selector(dayChanged:);

    NSStackView* schedule = [NSStackView stackViewWithViews:@[ days, [self timePickerForKey:@"SpeedLimitAutoOnDate"], [self timePickerForKey:@"SpeedLimitAutoOffDate"] ]];
    schedule.spacing = 10;

    NSSwitch* slow = [[NSSwitch alloc] initWithFrame:NSZeroRect];
    slow.state = [self.fDefaults boolForKey:@"SpeedLimit"] ? NSControlStateValueOn : NSControlStateValueOff;
    slow.target = self;
    slow.action = @selector(toggleSlowMode:);

    return [self stackOfRows:@[
        [self rowWithTitle:NSLocalizedString(@"Limit downloads", "Settings")
                      hint:NSLocalizedString(@"A ceiling for all transfers together", "Settings")
                   control:[self numberControlForKey:@"DownloadLimit" unit:@"KB/s" switchKey:@"CheckDownload"]],
        [self rowWithTitle:NSLocalizedString(@"Limit uploads", "Settings")
                      hint:NSLocalizedString(@"A ceiling for all transfers together", "Settings")
                   control:[self numberControlForKey:@"UploadLimit" unit:@"KB/s" switchKey:@"CheckUpload"]],
        [self rowWithTitle:NSLocalizedString(@"Slow mode", "Settings") hint:NSLocalizedString(@"Overrides the limits above while it's on", "Settings") control:slow],
        [self rowWithTitle:NSLocalizedString(@"Slow mode downloads", "Settings")
                      hint:NSLocalizedString(@"Speed while slow mode is on", "Settings")
                   control:[self numberControlForKey:@"SpeedLimitDownloadLimit" unit:@"KB/s" switchKey:nil]],
        [self rowWithTitle:NSLocalizedString(@"Slow mode uploads", "Settings")
                      hint:NSLocalizedString(@"Speed while slow mode is on", "Settings")
                   control:[self numberControlForKey:@"SpeedLimitUploadLimit" unit:@"KB/s" switchKey:nil]],
        [self rowWithTitle:NSLocalizedString(@"Run on a schedule", "Settings")
                      hint:NSLocalizedString(@"Turn slow mode on and off by itself", "Settings")
                   control:[self switchForKey:@"SpeedLimitAuto" action:@selector(switchChanged:)]],
        [self rowWithTitle:NSLocalizedString(@"Schedule", "Settings") hint:NSLocalizedString(@"When slow mode runs", "Settings") control:schedule],
    ]];
}

- (NSView*)advancedSection
{
    NSPopUpButton* encryption = [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
    [encryption addItemsWithTitles:@[
        NSLocalizedString(@"Allow", "Settings -> encryption"),
        NSLocalizedString(@"Prefer", "Settings -> encryption"),
        NSLocalizedString(@"Require", "Settings -> encryption"),
    ]];
    [encryption selectItemAtIndex:[self.fDefaults boolForKey:@"EncryptionPrefer"] ? ([self.fDefaults boolForKey:@"EncryptionRequire"] ? 2 : 1) : 0];
    encryption.target = self;
    encryption.action = @selector(encryptionChanged:);

    NSSwitch* (^toggle)(NSString*) = ^(NSString* key) {
        return [self switchForKey:key action:@selector(switchChanged:)];
    };

    NSStackView* stack = [self stackOfRows:@[
        [self headingWithTitle:NSLocalizedString(@"Queue", "Settings -> advanced")],
        [self rowWithTitle:NSLocalizedString(@"Downloads at once", "Settings")
                      hint:NSLocalizedString(@"Others wait their turn", "Settings")
                   control:[self numberControlForKey:@"QueueDownloadNumber" unit:@"" switchKey:@"Queue"]],
        [self rowWithTitle:NSLocalizedString(@"Seeding at once", "Settings")
                      hint:NSLocalizedString(@"Others wait their turn", "Settings")
                   control:[self numberControlForKey:@"QueueSeedNumber" unit:@"" switchKey:@"QueueSeed"]],
        [self rowWithTitle:NSLocalizedString(@"Skip stalled transfers", "Settings")
                      hint:NSLocalizedString(@"Stop counting a transfer that has gone quiet", "Settings")
                   control:[self numberControlForKey:@"StalledMinutes" unit:NSLocalizedString(@"min", "Settings") switchKey:@"CheckStalled"]],
        [self headingWithTitle:NSLocalizedString(@"Peers", "Settings -> advanced")],
        [self rowWithTitle:NSLocalizedString(@"Most peers overall", "Settings") hint:NSLocalizedString(@"Across all transfers", "Settings") control:[self numberFieldForKey:@"PeersTotal"]],
        [self rowWithTitle:NSLocalizedString(@"Most peers per transfer", "Settings") hint:NSLocalizedString(@"For each transfer", "Settings") control:[self numberFieldForKey:@"PeersTorrent"]],
        [self rowWithTitle:NSLocalizedString(@"Encryption", "Settings") hint:NSLocalizedString(@"Require can mean fewer peers", "Settings") control:encryption],
        [self rowWithTitle:NSLocalizedString(@"DHT", "Settings") hint:NSLocalizedString(@"Find peers without a tracker", "Settings") control:toggle(@"DHTGlobal")],
        [self rowWithTitle:NSLocalizedString(@"Peer exchange", "Settings") hint:NSLocalizedString(@"Let peers introduce each other", "Settings") control:toggle(@"PEXGlobal")],
        [self rowWithTitle:NSLocalizedString(@"Local peers", "Settings") hint:NSLocalizedString(@"Find peers on this network", "Settings") control:toggle(@"LocalPeerDiscoveryGlobal")],
        [self rowWithTitle:NSLocalizedString(@"µTP", "Settings") hint:NSLocalizedString(@"A gentler transfer protocol", "Settings") control:toggle(@"UTPGlobal")],
        [self headingWithTitle:NSLocalizedString(@"Connection", "Settings -> advanced")],
        [self rowWithTitle:NSLocalizedString(@"Port", "Settings") hint:NSLocalizedString(@"For incoming connections", "Settings") control:[self numberFieldForKey:@"BindPort"]],
        [self rowWithTitle:NSLocalizedString(@"New port each launch", "Settings") hint:NSLocalizedString(@"Pick a random port on start", "Settings") control:toggle(@"RandomPort")],
        [self rowWithTitle:NSLocalizedString(@"Map the port automatically", "Settings") hint:NSLocalizedString(@"Ask the router with UPnP or NAT-PMP", "Settings") control:toggle(@"NatTraversal")],
        [self headingWithTitle:NSLocalizedString(@"Dock", "Settings -> advanced")],
        [self rowWithTitle:NSLocalizedString(@"Show download speed", "Settings") hint:NSLocalizedString(@"On the Dock icon", "Settings") control:toggle(@"BadgeDownloadRate")],
        [self rowWithTitle:NSLocalizedString(@"Show upload speed", "Settings") hint:NSLocalizedString(@"On the Dock icon", "Settings") control:toggle(@"BadgeUploadRate")],
    ]];
    NSArray<NSView*>* rows = stack.arrangedSubviews;
    for (NSUInteger index = 0; index < rows.count; ++index)
    {
        if ([rows[index] isKindOfClass:NSTextField.class])
        {
            [stack setCustomSpacing:8 afterView:rows[index]];
            if (index > 0)
            {
                [stack setCustomSpacing:26 afterView:rows[index - 1]];
            }
        }
    }
    stack.translatesAutoresizingMaskIntoConstraints = NO;

    NSView* document = [[NSView alloc] initWithFrame:NSZeroRect];
    document.translatesAutoresizingMaskIntoConstraints = NO;
    [document addSubview:stack];

    NSClipView* clip = [[NSClipView alloc] initWithFrame:NSZeroRect];
    clip.drawsBackground = NO;
    NSScrollView* scroll = [[NSScrollView alloc] initWithFrame:NSZeroRect];
    scroll.contentView = clip;
    scroll.documentView = document;
    scroll.drawsBackground = NO;
    scroll.hasVerticalScroller = YES;
    scroll.autohidesScrollers = YES;
    scroll.automaticallyAdjustsContentInsets = NO;

    [NSLayoutConstraint activateConstraints:@[
        [scroll.heightAnchor constraintEqualToConstant:500],
        [document.leadingAnchor constraintEqualToAnchor:clip.leadingAnchor],
        [document.trailingAnchor constraintEqualToAnchor:clip.trailingAnchor],
        [document.topAnchor constraintEqualToAnchor:clip.topAnchor],
        [stack.leadingAnchor constraintEqualToAnchor:document.leadingAnchor],
        [stack.trailingAnchor constraintEqualToAnchor:document.trailingAnchor constant:-16],
        [stack.topAnchor constraintEqualToAnchor:document.topAnchor],
        [stack.bottomAnchor constraintEqualToAnchor:document.bottomAnchor],
    ]];
    return scroll;
}

#pragma mark - Actions

- (void)setAppearance:(PillBar*)sender
{
    Theme.mode = static_cast<ThemeMode>(sender.selectedSegment);
}

- (void)switchChanged:(NSSwitch*)sender
{
    [self.fDefaults setBool:sender.state == NSControlStateValueOn forKey:sender.identifier];
    [self.fPrefs applyChangeToKey:sender.identifier];
}

- (void)numberChanged:(NSTextField*)sender
{
    [self.fDefaults setInteger:sender.integerValue forKey:sender.identifier];
    [self.fPrefs applyChangeToKey:sender.identifier];
}

- (void)timeChanged:(NSDatePicker*)sender
{
    [self.fDefaults setObject:sender.dateValue forKey:sender.identifier];
    [self.fPrefs applyChangeToKey:sender.identifier];
}

- (void)dayChanged:(NSPopUpButton*)sender
{
    [self.fDefaults setInteger:sender.selectedTag forKey:@"SpeedLimitAutoDay"];
    [self.fPrefs applyChangeToKey:@"SpeedLimitAutoDay"];
}

- (void)encryptionChanged:(NSPopUpButton*)sender
{
    [self.fDefaults setBool:sender.indexOfSelectedItem > 0 forKey:@"EncryptionPrefer"];
    [self.fDefaults setBool:sender.indexOfSelectedItem == 2 forKey:@"EncryptionRequire"];
    [self.fPrefs applyChangeToKey:@"EncryptionPrefer"];
}

- (void)toggleSlowMode:(id)sender
{
    [(Controller*)NSApp.delegate toggleSpeedLimit:sender];
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
