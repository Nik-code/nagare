// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "AddMagnetWindowController.h"
#import "Controller.h"
#import "ExpandedPathToIconTransformer.h"
#import "GlassPanel.h"
#import "GroupsController.h"
#import "NSStringAdditions.h"
#import "Theme.h"
#import "Torrent.h"

typedef NS_ENUM(NSUInteger, PopupPriority) {
    PopupPriorityHigh = 0,
    PopupPriorityNormal = 1,
    PopupPriorityLow = 2,
};

@interface AddMagnetWindowController ()

@property(nonatomic) IBOutlet NSImageView* fLocationImageView;
@property(nonatomic) IBOutlet NSTextField* fNameField;
@property(nonatomic) IBOutlet NSTextField* fLocationField;
@property(nonatomic) NSSwitch* fStartCheck;
@property(nonatomic) IBOutlet NSPopUpButton* fGroupPopUp;
@property(nonatomic) IBOutlet NSPopUpButton* fPriorityPopUp;

@property(nonatomic, readonly) Controller* fController;

@property(nonatomic) NSString* fDestination;

@property(nonatomic) NSInteger fGroupValue;
@property(nonatomic) TorrentDeterminationType fGroupDeterminationType;

@end

@implementation AddMagnetWindowController

- (instancetype)initWithTorrent:(Torrent*)torrent destination:(NSString*)path controller:(Controller*)controller
{
    if ((self = [super initWithWindowNibName:@"AddMagnetWindow"]))
    {
        _torrent = torrent;
        _fDestination = path.stringByExpandingTildeInPath;

        _fController = controller;

        _fGroupValue = torrent.groupValue;
        _fGroupDeterminationType = TorrentDeterminationAutomatic;
    }
    return self;
}

- (void)loadWindow
{
    GlassPanel* panel = [[GlassPanel alloc] initWithSize:NSMakeSize(540, 248) style:NSGlassEffectViewStyleRegular];
    panel.cancelHandler = ^{
        [self cancelAdd:nil];
    };
    self.window = panel;

    self.fNameField = [Theme labelWithFont:[Theme displayFontOfSize:22] color:Theme.bright];
    NSTextField* note = [Theme labelWithFont:[NSFont systemFontOfSize:13] color:Theme.foreground];
    note.stringValue = NSLocalizedString(@"Magnet link. The file list arrives once it connects.", "Add magnet -> note");

    self.fLocationImageView = [[NSImageView alloc] initWithFrame:NSZeroRect];
    self.fLocationImageView.translatesAutoresizingMaskIntoConstraints = NO;
    self.fLocationField = [Theme labelWithFont:[NSFont systemFontOfSize:14] color:Theme.bright];
    NSButton* change = [NSButton buttonWithTitle:NSLocalizedString(@"Change", "Add magnet -> button") target:self action:@selector(setDestination:)];
    change.bezelStyle = NSBezelStyleGlass;
    change.translatesAutoresizingMaskIntoConstraints = NO;

    NSTextField* start = [Theme labelWithFont:[NSFont systemFontOfSize:14] color:Theme.bright];
    start.stringValue = NSLocalizedString(@"Start right away", "Add magnet -> switch");
    self.fStartCheck = [[NSSwitch alloc] initWithFrame:NSZeroRect];
    self.fStartCheck.translatesAutoresizingMaskIntoConstraints = NO;

    NSButton* cancel = [NSButton buttonWithTitle:NSLocalizedString(@"Cancel", "Add magnet -> button") target:self action:@selector(cancelAdd:)];
    cancel.bezelStyle = NSBezelStyleGlass;
    cancel.controlSize = NSControlSizeLarge;
    cancel.translatesAutoresizingMaskIntoConstraints = NO;

    NSButton* get = [NSButton buttonWithTitle:NSLocalizedString(@"Get", "Add magnet -> button") target:self action:@selector(add:)];
    get.bezelStyle = NSBezelStyleGlass;
    get.bezelColor = Theme.accent;
    get.controlSize = NSControlSizeLarge;
    get.keyEquivalent = @"\r";
    get.translatesAutoresizingMaskIntoConstraints = NO;

    // Groups and priority stay out of the way, but the controller still reads them.
    self.fGroupPopUp = [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
    self.fPriorityPopUp = [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
    [self.fPriorityPopUp addItemsWithTitles:@[ @"High", @"Normal", @"Low" ]];

    NSView* root = panel.root;
    for (NSView* view in @[ self.fNameField, note, self.fLocationImageView, self.fLocationField, change, start, self.fStartCheck, cancel, get ])
    {
        [root addSubview:view];
    }

    [NSLayoutConstraint activateConstraints:@[
        [self.fNameField.leadingAnchor constraintEqualToAnchor:root.leadingAnchor constant:28],
        [self.fNameField.trailingAnchor constraintEqualToAnchor:root.trailingAnchor constant:-28],
        [self.fNameField.topAnchor constraintEqualToAnchor:root.topAnchor constant:26],
        [note.leadingAnchor constraintEqualToAnchor:self.fNameField.leadingAnchor],
        [note.topAnchor constraintEqualToAnchor:self.fNameField.bottomAnchor constant:4],
        [self.fLocationImageView.leadingAnchor constraintEqualToAnchor:self.fNameField.leadingAnchor],
        [self.fLocationImageView.topAnchor constraintEqualToAnchor:note.bottomAnchor constant:20],
        [self.fLocationImageView.widthAnchor constraintEqualToConstant:22],
        [self.fLocationImageView.heightAnchor constraintEqualToConstant:22],
        [self.fLocationField.leadingAnchor constraintEqualToAnchor:self.fLocationImageView.trailingAnchor constant:10],
        [self.fLocationField.centerYAnchor constraintEqualToAnchor:self.fLocationImageView.centerYAnchor],
        [self.fLocationField.trailingAnchor constraintLessThanOrEqualToAnchor:change.leadingAnchor constant:-12],
        [change.trailingAnchor constraintEqualToAnchor:root.trailingAnchor constant:-28],
        [change.centerYAnchor constraintEqualToAnchor:self.fLocationImageView.centerYAnchor],
        [start.leadingAnchor constraintEqualToAnchor:self.fNameField.leadingAnchor],
        [start.topAnchor constraintEqualToAnchor:self.fLocationImageView.bottomAnchor constant:18],
        [self.fStartCheck.trailingAnchor constraintEqualToAnchor:root.trailingAnchor constant:-28],
        [self.fStartCheck.centerYAnchor constraintEqualToAnchor:start.centerYAnchor],
        [get.trailingAnchor constraintEqualToAnchor:root.trailingAnchor constant:-22],
        [get.bottomAnchor constraintEqualToAnchor:root.bottomAnchor constant:-20],
        [get.widthAnchor constraintGreaterThanOrEqualToConstant:76],
        [cancel.trailingAnchor constraintEqualToAnchor:get.leadingAnchor constant:-10],
        [cancel.bottomAnchor constraintEqualToAnchor:root.bottomAnchor constant:-20],
    ]];

    [self awakeFromNib];
    [panel placeOverMainWindow];
}

- (void)awakeFromNib
{
    [super awakeFromNib];
    [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(updateGroupMenu:) name:@"UpdateGroups" object:nil];

    NSString* name = self.torrent.name;
    self.window.title = name;
    self.fNameField.stringValue = name;
    self.fNameField.toolTip = name;

    //disable fullscreen support
    self.window.collectionBehavior = NSWindowCollectionBehaviorFullScreenNone;

    [self setGroupsMenu];
    [self.fGroupPopUp selectItemWithTag:self.fGroupValue];

    PopupPriority priorityIndex;
    switch (self.torrent.priority)
    {
    case TR_PRI_HIGH:
        priorityIndex = PopupPriorityHigh;
        break;
    case TR_PRI_NORMAL:
        priorityIndex = PopupPriorityNormal;
        break;
    case TR_PRI_LOW:
        priorityIndex = PopupPriorityLow;
        break;
    default:
        NSAssert1(NO, @"Unknown priority for adding torrent: %d", self.torrent.priority);
        priorityIndex = PopupPriorityNormal;
    }
    [self.fPriorityPopUp selectItemAtIndex:priorityIndex];

    self.fStartCheck.state = [NSUserDefaults.standardUserDefaults boolForKey:@"AutoStartDownload"] ? NSControlStateValueOn :
                                                                                                     NSControlStateValueOff;

    if (self.fDestination)
    {
        [self setDestinationPath:self.fDestination determinationType:TorrentDeterminationAutomatic];
    }
    else
    {
        self.fLocationField.stringValue = @"";
        self.fLocationImageView.image = nil;
    }
}

- (void)windowDidLoad
{
    //if there is no destination, prompt for one right away
    if (!self.fDestination)
    {
        [self setDestination:nil];
    }
}

- (void)setDestination:(id)sender
{
    NSOpenPanel* panel = [NSOpenPanel openPanel];

    panel.prompt = NSLocalizedString(@"Select", "Open torrent -> prompt");
    panel.allowsMultipleSelection = NO;
    panel.canChooseFiles = NO;
    panel.canChooseDirectories = YES;
    panel.canCreateDirectories = YES;

    panel.message = [NSString stringWithFormat:NSLocalizedString(@"Select the download folder for \"%@\"", "Add -> select destination folder"),
                                               self.torrent.name];

    [panel beginSheetModalForWindow:self.window completionHandler:^(NSInteger result) {
        if (result == NSModalResponseOK)
        {
            [self setDestinationPath:panel.URLs[0].path determinationType:TorrentDeterminationUserSpecified];
        }
        else
        {
            if (!self.fDestination)
            {
                [self performSelectorOnMainThread:@selector(cancelAdd:) withObject:nil waitUntilDone:NO];
            }
        }
    }];
}

- (void)add:(id)sender
{
    if ([self.fDestination.lastPathComponent isEqualToString:self.torrent.name] &&
        [NSUserDefaults.standardUserDefaults boolForKey:@"WarningFolderDataSameName"])
    {
        NSAlert* alert = [[NSAlert alloc] init];
        alert.messageText = NSLocalizedString(@"The destination directory and root data directory have the same name.", "Add torrent -> same name -> title");
        alert.informativeText = NSLocalizedString(
            @"If you are attempting to use already existing data,"
             " the root data directory should be inside the destination directory.",
            "Add torrent -> same name -> message");
        alert.alertStyle = NSAlertStyleWarning;
        [alert addButtonWithTitle:NSLocalizedString(@"Cancel", "Add torrent -> same name -> button")];
        [alert addButtonWithTitle:NSLocalizedString(@"Add", "Add torrent -> same name -> button")];
        alert.showsSuppressionButton = YES;

        [alert beginSheetModalForWindow:self.window completionHandler:^(NSModalResponse returnCode) {
            if (alert.suppressionButton.state == NSControlStateValueOn)
            {
                [NSUserDefaults.standardUserDefaults setBool:NO forKey:@"WarningFolderDataSameName"];
            }

            if (returnCode == NSAlertSecondButtonReturn)
            {
                [self performSelectorOnMainThread:@selector(confirmAdd) withObject:nil waitUntilDone:NO];
            }
        }];
    }
    else
    {
        [self confirmAdd];
    }
}

- (void)cancelAdd:(id)sender
{
    // A borderless panel has no close button for performClose: to press.
    [self windowShouldClose:self.window];
    [self close];
}

//only called on cancel
- (BOOL)windowShouldClose:(id)window
{
    [self.fController askOpenMagnetConfirmed:self add:NO];
    return YES;
}

- (void)changePriority:(id)sender
{
    tr_priority_t priority;
    switch ([sender indexOfSelectedItem])
    {
    case PopupPriorityHigh:
        priority = TR_PRI_HIGH;
        break;
    case PopupPriorityNormal:
        priority = TR_PRI_NORMAL;
        break;
    case PopupPriorityLow:
        priority = TR_PRI_LOW;
        break;
    default:
        NSAssert1(NO, @"Unknown priority tag for adding torrent: %ld", [sender tag]);
        priority = TR_PRI_NORMAL;
    }
    self.torrent.priority = priority;
}

- (void)updateGroupMenu:(NSNotification*)notification
{
    [self setGroupsMenu];
    if (![self.fGroupPopUp selectItemWithTag:self.fGroupValue])
    {
        self.fGroupValue = -1;
        self.fGroupDeterminationType = TorrentDeterminationAutomatic;
        [self.fGroupPopUp selectItemWithTag:self.fGroupValue];
    }
}

#pragma mark - Private

- (void)confirmAdd
{
    [self.torrent setGroupValue:self.fGroupValue determinationType:self.fGroupDeterminationType];

    if (self.fStartCheck.state == NSControlStateValueOn)
    {
        [self.torrent startMagnetTransferAfterMetaDownload];
    }

    [self close];
    [self.fController askOpenMagnetConfirmed:self add:YES];
}

- (void)setDestinationPath:(NSString*)destination determinationType:(TorrentDeterminationType)determinationType
{
    destination = destination.stringByExpandingTildeInPath;
    if (!self.fDestination || ![self.fDestination isEqualToString:destination])
    {
        self.fDestination = destination;

        [self.torrent changeDownloadFolderBeforeUsing:self.fDestination determinationType:determinationType];
    }

    self.fLocationField.stringValue = self.fDestination.stringByAbbreviatingWithTildeInPath;
    self.fLocationField.toolTip = self.fDestination;

    ExpandedPathToIconTransformer* iconTransformer = [[ExpandedPathToIconTransformer alloc] init];
    self.fLocationImageView.image = [iconTransformer transformedValue:self.fDestination];
}

- (void)setGroupsMenu
{
    NSMenu* groupMenu = [GroupsController.groups groupMenuWithTarget:self action:@selector(changeGroupValue:) isSmall:NO];
    self.fGroupPopUp.menu = groupMenu;
}

- (void)changeGroupValue:(id)sender
{
    NSInteger previousGroup = self.fGroupValue;
    self.fGroupValue = [sender tag];
    self.fGroupDeterminationType = TorrentDeterminationUserSpecified;

    if ([GroupsController.groups usesCustomDownloadLocationForIndex:self.fGroupValue])
    {
        [self setDestinationPath:[GroupsController.groups customDownloadLocationForIndex:self.fGroupValue]
               determinationType:TorrentDeterminationAutomatic];
    }
    else if ([self.fDestination isEqualToString:[GroupsController.groups customDownloadLocationForIndex:previousGroup]])
    {
        [self setDestinationPath:[NSUserDefaults.standardUserDefaults stringForKey:@"DownloadFolder"]
               determinationType:TorrentDeterminationAutomatic];
    }
}

@end
