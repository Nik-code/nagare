// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "AddWindowController.h"
#import "Controller.h"
#import "FileOutlineController.h"
#import "FileOutlineView.h"
#import "GlassPanel.h"
#import "NSStringAdditions.h"
#import "Theme.h"
#import "Torrent.h"

static NSTimeInterval const kUpdateSeconds = 1.0;

static CGFloat const kPanelWidth = 560.0;
static CGFloat const kMargin = 28.0;
static CGFloat const kListHeight = 252.0;
static CGFloat const kRowHeight = 50.0;

@interface AddWindowController ()<NSTextFieldDelegate>

@property(nonatomic, readonly) BOOL fMagnet;

@property(nonatomic) NSTextField* fNameField;
@property(nonatomic) NSTextField* fStatusField;
@property(nonatomic) NSTextField* fLocationField;
@property(nonatomic) NSSwitch* fStartSwitch;
@property(nonatomic) NSSwitch* fTrashSwitch;
@property(nonatomic) NSButton* fCheckAllButton;
@property(nonatomic) NSButton* fUncheckAllButton;
@property(nonatomic) NSButton* fGetButton;

@property(nonatomic) FileOutlineController* fFileController;

@property(nonatomic, readonly) Controller* fController;

@property(nonatomic, copy) NSString* fOriginalName;
@property(nonatomic, copy) NSString* fDestination;
@property(nonatomic, readonly) NSString* fTorrentFile;
@property(nonatomic) BOOL fLockDestination;

@property(nonatomic, readonly) BOOL fDeleteTorrentEnableInitially;
@property(nonatomic, readonly) BOOL fCanToggleDelete;

@property(nonatomic, weak) NSTimer* fTimer;

@end

@implementation AddWindowController

- (instancetype)initWithTorrent:(Torrent*)torrent
                          destination:(NSString*)path
                      lockDestination:(BOOL)lockDestination
                           controller:(Controller*)controller
                          torrentFile:(NSString*)torrentFile
    deleteTorrentCheckEnableInitially:(BOOL)deleteTorrent
                      canToggleDelete:(BOOL)canToggleDelete
{
    if ((self = [super initWithWindow:nil]))
    {
        _torrent = torrent;
        _fDestination = path.stringByExpandingTildeInPath;
        _fLockDestination = lockDestination;
        _fController = controller;
        _fTorrentFile = torrentFile.stringByExpandingTildeInPath;
        _fDeleteTorrentEnableInitially = deleteTorrent;
        _fCanToggleDelete = canToggleDelete;
        [self buildPanel];
    }
    return self;
}

- (instancetype)initWithMagnet:(Torrent*)torrent destination:(NSString*)path controller:(Controller*)controller
{
    if ((self = [super initWithWindow:nil]))
    {
        _torrent = torrent;
        _fMagnet = YES;
        _fDestination = path.stringByExpandingTildeInPath;
        _fController = controller;
        [self buildPanel];
    }
    return self;
}

- (void)dealloc
{
    [_fTimer invalidate];
    [NSNotificationCenter.defaultCenter removeObserver:self];
}

- (void)buildPanel
{
    BOOL const showFiles = !self.fMagnet && self.torrent.folder;
    BOOL const showTrash = self.fTorrentFile != nil && self.fCanToggleDelete;

    // Magnet names come straight from the link, where spaces are often plus signs.
    NSString* name = self.torrent.name;
    self.fOriginalName = self.fMagnet ? [name stringByReplacingOccurrencesOfString:@"+" withString:@" "] : name;

    self.fNameField = [Theme labelWithFont:[Theme displayFontOfSize:22] color:Theme.bright];
    self.fNameField.stringValue = self.fOriginalName;
    self.fNameField.toolTip = self.fOriginalName;
    self.fNameField.editable = YES;
    self.fNameField.selectable = YES;
    self.fNameField.focusRingType = NSFocusRingTypeNone;
    self.fNameField.lineBreakMode = NSLineBreakByTruncatingMiddle;
    self.fNameField.cell.scrollable = YES;
    self.fNameField.delegate = self;

    NSImageView* pencil = [NSImageView imageViewWithImage:[Theme icon:@"Pencil"]];
    pencil.contentTintColor = Theme.foreground;
    pencil.toolTip = NSLocalizedString(@"Click the name to rename", "Add -> rename hint");
    pencil.translatesAutoresizingMaskIntoConstraints = NO;

    NSView* nameRow = [[NSView alloc] initWithFrame:NSZeroRect];
    nameRow.translatesAutoresizingMaskIntoConstraints = NO;
    [nameRow addSubview:self.fNameField];
    [nameRow addSubview:pencil];
    [NSLayoutConstraint activateConstraints:@[
        [self.fNameField.leadingAnchor constraintEqualToAnchor:nameRow.leadingAnchor],
        [self.fNameField.topAnchor constraintEqualToAnchor:nameRow.topAnchor],
        [self.fNameField.bottomAnchor constraintEqualToAnchor:nameRow.bottomAnchor],
        [self.fNameField.trailingAnchor constraintEqualToAnchor:pencil.leadingAnchor constant:-10],
        [pencil.trailingAnchor constraintEqualToAnchor:nameRow.trailingAnchor],
        [pencil.centerYAnchor constraintEqualToAnchor:nameRow.centerYAnchor],
        [pencil.widthAnchor constraintEqualToConstant:18],
        [pencil.heightAnchor constraintEqualToConstant:18],
    ]];

    self.fStatusField = [Theme labelWithFont:[NSFont systemFontOfSize:13] color:Theme.foreground];

    NSMutableArray<NSView*>* views = [NSMutableArray arrayWithArray:@[ nameRow, self.fStatusField ]];

    if (showFiles)
    {
        self.fFileController = [[FileOutlineController alloc] initWithPriorities:NO];
        self.fFileController.doubleClickRenames = YES;
        self.fFileController.torrent = self.torrent;
        [self.fFileController.scrollView.heightAnchor constraintEqualToConstant:kListHeight].active = YES;

        self.fCheckAllButton = [Theme glassButton:NSLocalizedString(@"All", "Add -> check all") target:self action:@selector(checkAll:)];
        self.fUncheckAllButton = [Theme glassButton:NSLocalizedString(@"None", "Add -> check all") target:self action:@selector(uncheckAll:)];
        self.fCheckAllButton.controlSize = NSControlSizeSmall;
        self.fUncheckAllButton.controlSize = NSControlSizeSmall;

        NSTextField* title = [Theme labelWithFont:[NSFont systemFontOfSize:14 weight:NSFontWeightMedium] color:Theme.bright];
        title.stringValue = NSLocalizedString(@"Files", "Add -> file list");
        NSView* spacer = [[NSView alloc] initWithFrame:NSZeroRect];
        [spacer setContentHuggingPriority:NSLayoutPriorityDefaultLow forOrientation:NSLayoutConstraintOrientationHorizontal];
        NSStackView* header = [NSStackView stackViewWithViews:@[ title, spacer, self.fCheckAllButton, self.fUncheckAllButton ]];
        header.spacing = 6;

        [views addObject:header];
        [views addObject:self.fFileController.scrollView];
    }

    self.fLocationField = [Theme hintField:@""];
    self.fLocationField.lineBreakMode = NSLineBreakByTruncatingMiddle;
    [views addObject:[Theme rowWithTitle:NSLocalizedString(@"Save to", "Add -> row") hintField:self.fLocationField
                                 control:[Theme glassButton:NSLocalizedString(@"Change", "Add -> button") target:self action:@selector(setDestination:)]
                                  height:kRowHeight + 6]];

    self.fStartSwitch = [[NSSwitch alloc] initWithFrame:NSZeroRect];
    self.fStartSwitch.state = [NSUserDefaults.standardUserDefaults boolForKey:@"AutoStartDownload"] ? NSControlStateValueOn : NSControlStateValueOff;
    [views addObject:[Theme rowWithTitle:NSLocalizedString(@"Start right away", "Add -> row") hintField:nil control:self.fStartSwitch height:kRowHeight]];

    if (showTrash)
    {
        self.fTrashSwitch = [[NSSwitch alloc] initWithFrame:NSZeroRect];
        self.fTrashSwitch.state = self.fDeleteTorrentEnableInitially ? NSControlStateValueOn : NSControlStateValueOff;
        [views addObject:[Theme rowWithTitle:NSLocalizedString(@"Move the .torrent file to the Trash", "Add -> row") hintField:nil
                                     control:self.fTrashSwitch
                                      height:kRowHeight]];
    }

    NSButton* cancel = [Theme glassButton:NSLocalizedString(@"Cancel", "Add -> button") target:self action:@selector(cancelAdd:)];
    cancel.controlSize = NSControlSizeLarge;
    cancel.keyEquivalent = @"\033";
    self.fGetButton = [Theme glassButton:NSLocalizedString(@"Get", "Add -> button") target:self action:@selector(add:)];
    self.fGetButton.bezelColor = Theme.accent;
    self.fGetButton.controlSize = NSControlSizeLarge;
    self.fGetButton.keyEquivalent = @"\r";
    [self.fGetButton.widthAnchor constraintGreaterThanOrEqualToConstant:84].active = YES;

    NSView* spacer = [[NSView alloc] initWithFrame:NSZeroRect];
    [spacer setContentHuggingPriority:NSLayoutPriorityDefaultLow forOrientation:NSLayoutConstraintOrientationHorizontal];
    NSStackView* buttons = [NSStackView stackViewWithViews:@[ spacer, cancel, self.fGetButton ]];
    buttons.spacing = 10;
    [views addObject:buttons];

    NSStackView* column = [NSStackView stackViewWithViews:views];
    column.orientation = NSUserInterfaceLayoutOrientationVertical;
    column.alignment = NSLayoutAttributeLeading;
    column.spacing = 0;
    column.translatesAutoresizingMaskIntoConstraints = NO;
    for (NSView* view in views)
    {
        [view.widthAnchor constraintEqualToConstant:kPanelWidth - 2.0 * kMargin].active = YES;
    }
    [column setCustomSpacing:4 afterView:nameRow];
    [column setCustomSpacing:18 afterView:self.fStatusField];
    [column setCustomSpacing:10 afterView:views[views.count - 2]];
    if (showFiles)
    {
        [column setCustomSpacing:8 afterView:views[2]];
        [column setCustomSpacing:10 afterView:self.fFileController.scrollView];
    }

    CGFloat const height = column.fittingSize.height + 26.0 + 22.0;
    GlassPanel* panel = [[GlassPanel alloc] initWithSize:NSMakeSize(kPanelWidth, height) style:NSGlassEffectViewStyleRegular];
    __weak AddWindowController* weakSelf = self;
    panel.cancelHandler = ^{
        [weakSelf cancelAdd:nil];
    };
    panel.title = self.fOriginalName;
    panel.collectionBehavior = NSWindowCollectionBehaviorFullScreenNone;
    panel.initialFirstResponder = self.fGetButton;

    [panel.root addSubview:column];
    [NSLayoutConstraint activateConstraints:@[
        [column.leadingAnchor constraintEqualToAnchor:panel.root.leadingAnchor constant:kMargin],
        [column.topAnchor constraintEqualToAnchor:panel.root.topAnchor constant:26],
    ]];
    self.window = panel;

    if (self.fDestination != nil)
    {
        [self setDestinationPath:self.fDestination
               determinationType:self.fLockDestination ? TorrentDeterminationUserSpecified : TorrentDeterminationAutomatic];
    }

    if (self.fMagnet)
    {
        self.fStatusField.stringValue = NSLocalizedString(@"Magnet link. The file list arrives once it connects.", "Add -> note");
    }
    else
    {
        [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(updateStatus) name:@"TorrentFileCheckChange" object:self.torrent];
        self.fTimer = [NSTimer scheduledTimerWithTimeInterval:kUpdateSeconds target:self selector:@selector(updateFiles) userInfo:nil repeats:YES];
        [self updateFiles];
    }
}

- (void)showWindow:(id)sender
{
    [(GlassPanel*)self.window placeOverMainWindow];
    [super showWindow:sender];

    //if there is no destination, prompt for one right away
    if (self.fDestination == nil)
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
                                               self.fNameField.stringValue];

    [panel beginSheetModalForWindow:self.window completionHandler:^(NSInteger result) {
        if (result == NSModalResponseOK)
        {
            self.fLockDestination = YES;
            [self setDestinationPath:panel.URLs[0].path determinationType:TorrentDeterminationUserSpecified];

            // The new folder may already hold some of this.
            if (!self.fMagnet)
            {
                [self.torrent resetCache];
                [self updateFiles];
            }
        }
        else if (self.fDestination == nil)
        {
            [self performSelectorOnMainThread:@selector(cancelAdd:) withObject:nil waitUntilDone:NO];
        }
    }];
}

- (void)add:(id)sender
{
    [self.window makeFirstResponder:nil];

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
                [self performSelectorOnMainThread:@selector(renameThenConfirm) withObject:nil waitUntilDone:NO];
            }
        }];
    }
    else
    {
        [self renameThenConfirm];
    }
}

- (void)cancelAdd:(id)sender
{
    [self.fTimer invalidate];
    self.fFileController.torrent = nil; //avoid a crash when window tries to update

    [self close];
    [self.fController askOpenConfirmed:self add:NO];
}

- (void)checkAll:(id)sender
{
    [self.fFileController checkAll];
    [self updateStatus];
}

- (void)uncheckAll:(id)sender
{
    [self.fFileController uncheckAll];
    [self updateStatus];
}

- (void)updateStatus
{
    Torrent* torrent = self.torrent;
    if (torrent.checking)
    {
        self.fStatusField.stringValue = [NSString stringWithFormat:NSLocalizedString(@"Checking what's already here  %.0f%%", "Add -> status"),
                                                                   torrent.checkingProgress * 100.0];
        return;
    }

    NSString* size = [NSString stringForFileSize:torrent.size];
    if (!torrent.folder)
    {
        self.fStatusField.stringValue = size;
        return;
    }

    //check buttons
    //keep synced with identical code in InfoFileViewController.mm
    NSControlStateValue const filesCheckState = [torrent checkForFiles:[NSIndexSet indexSetWithIndexesInRange:NSMakeRange(0, torrent.fileCount)]];
    self.fCheckAllButton.enabled = filesCheckState != NSControlStateValueOn; //if anything is unchecked
    self.fUncheckAllButton.enabled = !torrent.allDownloaded;                 //if there are any checked files that aren't finished

    NSUInteger const count = torrent.fileCount;
    NSString* files = count == 1 ? NSLocalizedString(@"1 file", "Add -> status") :
                                   [NSString localizedStringWithFormat:NSLocalizedString(@"%lu files", "Add -> status"), count];
    NSString* selected = [NSString stringForFileSize:torrent.totalSizeSelected];
    self.fStatusField.stringValue = [NSString stringWithFormat:NSLocalizedString(@"%@  ·  %@  ·  %@ selected", "Add -> status"), files, size, selected];
}

#pragma mark - Private

- (void)updateFiles
{
    [self.torrent update];
    [self.fFileController reloadVisibleRows];
    [self updateStatus];
}

/// The name in the field can only be applied once, and a bad one keeps the panel open.
- (void)renameThenConfirm
{
    NSString* name = [self.fNameField.stringValue stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet];
    if (name.length == 0 || [name isEqualToString:self.fOriginalName])
    {
        [self confirmAdd];
    }
    else if (self.fMagnet)
    {
        self.torrent.pendingName = name;
        [self confirmAdd];
    }
    else
    {
        [self.torrent renameTorrent:name completionHandler:^(BOOL didRename) {
            if (didRename)
            {
                [self confirmAdd];
            }
            else
            {
                NSBeep();
                [self.window makeFirstResponder:self.fNameField];
            }
        }];
    }
}

- (void)confirmAdd
{
    [self.fTimer invalidate];

    if (self.fTrashSwitch.state == NSControlStateValueOn)
    {
        [Torrent trashFile:self.fTorrentFile error:nil];
    }

    if (self.fStartSwitch.state == NSControlStateValueOn)
    {
        if (self.fMagnet)
        {
            [self.torrent startMagnetTransferAfterMetaDownload];
        }
        else
        {
            [self.torrent startTransfer];
        }
    }

    self.fFileController.torrent = nil; //avoid a crash when window tries to update

    [self close];
    [self.fController askOpenConfirmed:self add:YES];
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
}

@end
