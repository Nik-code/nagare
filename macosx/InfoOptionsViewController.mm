// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "InfoOptionsViewController.h"
#import "NSStringAdditions.h"
#import "Theme.h"
#import "Torrent.h"
#import "Utils.h"

typedef NS_ENUM(NSInteger, OptionPopupType) {
    OptionPopupTypeGlobal = 0,
    OptionPopupTypeNoLimit = 1,
    OptionPopupTypeLimit = 2,
};

typedef NS_ENUM(NSUInteger, OptionPopupPriority) {
    OptionPopupPriorityHigh = 0,
    OptionPopupPriorityNormal = 1,
    OptionPopupPriorityLow = 2,
};

static NSInteger const kInvalidValue = -99;

static CGFloat const kRowHeight = 52.0;

@interface InfoOptionsViewController ()

@property(nonatomic, copy) NSArray<Torrent*>* fTorrents;

@property(nonatomic) BOOL fSet;

@property(nonatomic) NSPopUpButton* fPriorityPopUp;
@property(nonatomic) NSPopUpButton* fRatioPopUp;
@property(nonatomic) NSPopUpButton* fIdlePopUp;
@property(nonatomic) NSSwitch* fUploadLimitCheck;
@property(nonatomic) NSSwitch* fDownloadLimitCheck;
@property(nonatomic) NSSwitch* fGlobalLimitCheck;
@property(nonatomic) NSSwitch* fRemoveSeedingCompleteCheck;
@property(nonatomic) NSTextField* fUploadLimitField;
@property(nonatomic) NSTextField* fDownloadLimitField;
@property(nonatomic) NSTextField* fRatioLimitField;
@property(nonatomic) NSTextField* fIdleLimitField;
@property(nonatomic) NSTextField* fIdleLimitLabel;
@property(nonatomic) NSTextField* fRatioLimitGlobalLabel;
@property(nonatomic) NSTextField* fIdleLimitGlobalLabel;
@property(nonatomic) NSTextField* fPeersConnectField;

@property(nonatomic, copy) NSString* fInitialString;

@end

@implementation InfoOptionsViewController

- (instancetype)init
{
    if ((self = [super initWithNibName:nil bundle:nil]))
    {
        self.title = NSLocalizedString(@"Options", "Inspector view -> title");
    }

    return self;
}

- (NSSwitch*)switchWithAction:(SEL)action
{
    NSSwitch* toggle = [[NSSwitch alloc] initWithFrame:NSZeroRect];
    toggle.target = self;
    toggle.action = action;
    return toggle;
}

- (NSPopUpButton*)popUpWithTitles:(NSArray<NSString*>*)titles action:(SEL)action
{
    NSPopUpButton* popUp = [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
    [popUp addItemsWithTitles:titles];
    popUp.target = self;
    popUp.action = action;
    return popUp;
}

- (NSFormatter*)formatterWithDecimals:(NSUInteger)decimals
{
    NSNumberFormatter* formatter = [[NSNumberFormatter alloc] init];
    formatter.numberStyle = decimals > 0 ? NSNumberFormatterDecimalStyle : NSNumberFormatterNoStyle;
    formatter.minimum = @0;
    formatter.minimumFractionDigits = decimals;
    formatter.maximumFractionDigits = decimals;
    return formatter;
}

- (NSStackView*)controls:(NSArray<NSView*>*)views
{
    NSStackView* stack = [NSStackView stackViewWithViews:views];
    stack.spacing = 8;
    return stack;
}

- (NSView*)limitControlWithField:(NSTextField*)field unit:(NSString*)unit toggle:(NSSwitch*)toggle
{
    NSTextField* label = [Theme labelWithFont:[NSFont systemFontOfSize:12] color:Theme.foreground];
    label.stringValue = unit;
    return [self controls:@[ field, label, toggle ]];
}

- (void)loadView
{
    NSString* speedUnit = NSLocalizedString(@"KB/s", "Inspector -> options -> unit");
    NSArray<NSString*>* limitTitles = @[
        NSLocalizedString(@"Global setting", "Inspector -> options"),
        NSLocalizedString(@"Don't stop", "Inspector -> options"),
        NSLocalizedString(@"Custom", "Inspector -> options"),
    ];

    self.fPriorityPopUp = [self popUpWithTitles:@[
        NSLocalizedString(@"High", "Inspector -> options"),
        NSLocalizedString(@"Normal", "Inspector -> options"),
        NSLocalizedString(@"Low", "Inspector -> options"),
    ]
                                         action:@selector(setPriority:)];

    self.fDownloadLimitCheck = [self switchWithAction:@selector(setUseSpeedLimit:)];
    self.fDownloadLimitField = [Theme valueField:64 formatter:[self formatterWithDecimals:0] target:self action:@selector(setSpeedLimit:)];
    self.fUploadLimitCheck = [self switchWithAction:@selector(setUseSpeedLimit:)];
    self.fUploadLimitField = [Theme valueField:64 formatter:[self formatterWithDecimals:0] target:self action:@selector(setSpeedLimit:)];
    self.fGlobalLimitCheck = [self switchWithAction:@selector(setUseGlobalSpeedLimit:)];

    self.fRatioPopUp = [self popUpWithTitles:limitTitles action:@selector(setRatioSetting:)];
    self.fRatioLimitField = [Theme valueField:56 formatter:[self formatterWithDecimals:2] target:self action:@selector(setRatioLimit:)];
    self.fRatioLimitGlobalLabel = [Theme hintField:@""];

    self.fIdlePopUp = [self popUpWithTitles:limitTitles action:@selector(setIdleSetting:)];
    self.fIdleLimitField = [Theme valueField:56 formatter:[self formatterWithDecimals:0] target:self action:@selector(setIdleLimit:)];
    self.fIdleLimitLabel = [Theme hintField:NSLocalizedString(@"minutes", "Inspector -> options -> unit")];
    self.fIdleLimitGlobalLabel = [Theme hintField:@""];

    self.fRemoveSeedingCompleteCheck = [self switchWithAction:@selector(setRemoveWhenSeedingCompletes:)];

    self.fPeersConnectField = [Theme valueField:64 formatter:[self formatterWithDecimals:0] target:self action:@selector(setPeersConnectLimit:)];

    NSTextField* peersLabel = [Theme hintField:NSLocalizedString(@"peers", "Inspector -> options -> unit")];

    NSArray<NSView*>* rows = @[
        [Theme rowWithTitle:NSLocalizedString(@"Priority", "Inspector -> options") hintField:nil control:self.fPriorityPopUp height:kRowHeight],
        [Theme rowWithTitle:NSLocalizedString(@"Limit download", "Inspector -> options") hintField:nil
                    control:[self limitControlWithField:self.fDownloadLimitField unit:speedUnit toggle:self.fDownloadLimitCheck]
                     height:kRowHeight],
        [Theme rowWithTitle:NSLocalizedString(@"Limit upload", "Inspector -> options") hintField:nil
                    control:[self limitControlWithField:self.fUploadLimitField unit:speedUnit toggle:self.fUploadLimitCheck]
                     height:kRowHeight],
        [Theme rowWithTitle:NSLocalizedString(@"Stay within the global limits", "Inspector -> options") hintField:nil control:self.fGlobalLimitCheck height:kRowHeight],
        [Theme rowWithTitle:NSLocalizedString(@"Stop seeding at ratio", "Inspector -> options") hintField:nil
                    control:[self controls:@[ self.fRatioLimitGlobalLabel, self.fRatioLimitField, self.fRatioPopUp ]]
                     height:kRowHeight],
        [Theme rowWithTitle:NSLocalizedString(@"Stop seeding when idle", "Inspector -> options") hintField:nil
                    control:[self controls:@[ self.fIdleLimitGlobalLabel, self.fIdleLimitField, self.fIdleLimitLabel, self.fIdlePopUp ]]
                     height:kRowHeight],
        [Theme rowWithTitle:NSLocalizedString(@"Remove from the list when done", "Inspector -> options") hintField:nil control:self.fRemoveSeedingCompleteCheck height:kRowHeight],
        [Theme rowWithTitle:NSLocalizedString(@"Maximum connections", "Inspector -> options") hintField:nil
                    control:[self controls:@[ self.fPeersConnectField, peersLabel ]]
                     height:kRowHeight],
    ];

    NSStackView* stack = [NSStackView stackViewWithViews:rows];
    stack.orientation = NSUserInterfaceLayoutOrientationVertical;
    stack.alignment = NSLayoutAttributeLeading;
    stack.spacing = 0;
    stack.translatesAutoresizingMaskIntoConstraints = NO;
    for (NSView* row in rows)
    {
        [row.widthAnchor constraintEqualToAnchor:stack.widthAnchor].active = YES;
    }

    NSView* view = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 540, 480)];
    [view addSubview:stack];
    [NSLayoutConstraint activateConstraints:@[
        [stack.leadingAnchor constraintEqualToAnchor:view.leadingAnchor constant:20],
        [stack.trailingAnchor constraintEqualToAnchor:view.trailingAnchor constant:-20],
        [stack.topAnchor constraintEqualToAnchor:view.topAnchor constant:10],
    ]];
    self.view = view;

    [self setGlobalLabels];

    [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(setGlobalLabels) name:@"UpdateGlobalOptions" object:nil];
    [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(updateOptionsNotification:)
                                               name:@"UpdateOptionsNotification"
                                             object:nil];
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

    self.fSet = YES;
}

- (void)updateOptions
{
    if (self.fTorrents.count == 0)
    {
        return;
    }

    //get bandwidth info
    NSEnumerator* enumerator = [self.fTorrents objectEnumerator];
    Torrent* torrent = [enumerator nextObject]; //first torrent

    NSInteger uploadUseSpeedLimit = [torrent usesSpeedLimit:YES] ? NSControlStateValueOn : NSControlStateValueOff;
    NSUInteger uploadSpeedLimit = [torrent speedLimit:YES];
    BOOL multipleUploadSpeedLimits = NO;
    NSInteger downloadUseSpeedLimit = [torrent usesSpeedLimit:NO] ? NSControlStateValueOn : NSControlStateValueOff;
    NSUInteger downloadSpeedLimit = [torrent speedLimit:NO];
    BOOL multipleDownloadSpeedLimits = NO;
    NSInteger globalUseSpeedLimit = torrent.usesGlobalSpeedLimit ? NSControlStateValueOn : NSControlStateValueOff;

    while ((torrent = [enumerator nextObject]) &&
           (uploadUseSpeedLimit != NSControlStateValueMixed || !multipleUploadSpeedLimits || downloadUseSpeedLimit != NSControlStateValueMixed ||
            !multipleDownloadSpeedLimits || globalUseSpeedLimit != NSControlStateValueMixed))
    {
        if (uploadUseSpeedLimit != NSControlStateValueMixed &&
            uploadUseSpeedLimit != ([torrent usesSpeedLimit:YES] ? NSControlStateValueOn : NSControlStateValueOff))
        {
            uploadUseSpeedLimit = NSControlStateValueMixed;
        }

        if (!multipleUploadSpeedLimits && uploadSpeedLimit != [torrent speedLimit:YES])
        {
            multipleUploadSpeedLimits = YES;
        }

        if (downloadUseSpeedLimit != NSControlStateValueMixed &&
            downloadUseSpeedLimit != ([torrent usesSpeedLimit:NO] ? NSControlStateValueOn : NSControlStateValueOff))
        {
            downloadUseSpeedLimit = NSControlStateValueMixed;
        }

        if (!multipleDownloadSpeedLimits && downloadSpeedLimit != [torrent speedLimit:NO])
        {
            multipleDownloadSpeedLimits = YES;
        }

        if (globalUseSpeedLimit != NSControlStateValueMixed &&
            globalUseSpeedLimit != (torrent.usesGlobalSpeedLimit ? NSControlStateValueOn : NSControlStateValueOff))
        {
            globalUseSpeedLimit = NSControlStateValueMixed;
        }
    }

    //set upload view
    self.fUploadLimitCheck.state = uploadUseSpeedLimit;
    self.fUploadLimitCheck.enabled = YES;

    self.fUploadLimitField.enabled = uploadUseSpeedLimit == NSControlStateValueOn;
    if (!multipleUploadSpeedLimits)
    {
        self.fUploadLimitField.integerValue = uploadSpeedLimit;
    }
    else
    {
        self.fUploadLimitField.stringValue = @"";
    }

    //set download view
    self.fDownloadLimitCheck.state = downloadUseSpeedLimit;
    self.fDownloadLimitCheck.enabled = YES;

    self.fDownloadLimitField.enabled = downloadUseSpeedLimit == NSControlStateValueOn;
    if (!multipleDownloadSpeedLimits)
    {
        self.fDownloadLimitField.integerValue = downloadSpeedLimit;
    }
    else
    {
        self.fDownloadLimitField.stringValue = @"";
    }

    //set global check
    self.fGlobalLimitCheck.state = globalUseSpeedLimit;
    self.fGlobalLimitCheck.enabled = YES;

    //get ratio and idle info
    enumerator = [self.fTorrents objectEnumerator];
    torrent = [enumerator nextObject]; //first torrent

    NSInteger checkRatio = torrent.ratioSetting;
    NSInteger checkIdle = torrent.idleSetting;
    NSInteger removeWhenFinishSeeding = torrent.removeWhenFinishSeeding ? NSControlStateValueOn : NSControlStateValueOff;
    CGFloat ratioLimit = torrent.ratioLimit;
    BOOL multipleRatioLimits = NO;
    NSUInteger idleLimit = torrent.idleLimitMinutes;
    BOOL multipleIdleLimits = NO;

    while ((torrent = [enumerator nextObject]) &&
           (checkRatio != kInvalidValue || !multipleRatioLimits || checkIdle != kInvalidValue || !multipleIdleLimits))
    {
        if (checkRatio != kInvalidValue && checkRatio != torrent.ratioSetting)
        {
            checkRatio = kInvalidValue;
        }

        if (!multipleRatioLimits && !isRatioEqual(ratioLimit, torrent.ratioLimit))
        {
            multipleRatioLimits = YES;
        }

        if (checkIdle != kInvalidValue && checkIdle != torrent.idleSetting)
        {
            checkIdle = kInvalidValue;
        }

        if (!multipleIdleLimits && idleLimit != torrent.idleLimitMinutes)
        {
            multipleIdleLimits = YES;
        }

        if (removeWhenFinishSeeding != NSControlStateValueMixed &&
            removeWhenFinishSeeding != (torrent.removeWhenFinishSeeding ? NSControlStateValueOn : NSControlStateValueOff))
        {
            removeWhenFinishSeeding = NSControlStateValueMixed;
        }
    }

    //set ratio view
    NSInteger index;
    if (checkRatio == TR_RATIOLIMIT_SINGLE)
    {
        index = OptionPopupTypeLimit;
    }
    else if (checkRatio == TR_RATIOLIMIT_UNLIMITED)
    {
        index = OptionPopupTypeNoLimit;
    }
    else if (checkRatio == TR_RATIOLIMIT_GLOBAL)
    {
        index = OptionPopupTypeGlobal;
    }
    else
    {
        index = -1;
    }
    [self.fRatioPopUp selectItemAtIndex:index];
    self.fRatioPopUp.enabled = YES;

    self.fRatioLimitField.hidden = checkRatio != TR_RATIOLIMIT_SINGLE;
    if (!multipleRatioLimits)
    {
        self.fRatioLimitField.floatValue = ratioLimit;
    }
    else
    {
        self.fRatioLimitField.stringValue = @"";
    }

    self.fRatioLimitGlobalLabel.hidden = checkRatio != TR_RATIOLIMIT_GLOBAL;

    //set idle view
    if (checkIdle == TR_IDLELIMIT_SINGLE)
    {
        index = OptionPopupTypeLimit;
    }
    else if (checkIdle == TR_IDLELIMIT_UNLIMITED)
    {
        index = OptionPopupTypeNoLimit;
    }
    else if (checkIdle == TR_IDLELIMIT_GLOBAL)
    {
        index = OptionPopupTypeGlobal;
    }
    else
    {
        index = -1;
    }
    [self.fIdlePopUp selectItemAtIndex:index];
    self.fIdlePopUp.enabled = YES;

    self.fIdleLimitField.hidden = checkIdle != TR_IDLELIMIT_SINGLE;
    if (!multipleIdleLimits)
    {
        self.fIdleLimitField.integerValue = idleLimit;
    }
    else
    {
        self.fIdleLimitField.stringValue = @"";
    }
    self.fIdleLimitLabel.hidden = checkIdle != TR_IDLELIMIT_SINGLE;

    self.fIdleLimitGlobalLabel.hidden = checkIdle != TR_IDLELIMIT_GLOBAL;

    //set remove transfer when seeding finishes
    self.fRemoveSeedingCompleteCheck.state = removeWhenFinishSeeding;
    self.fRemoveSeedingCompleteCheck.enabled = YES;

    //get priority info
    enumerator = [self.fTorrents objectEnumerator];
    torrent = [enumerator nextObject]; //first torrent

    NSInteger priority = torrent.priority;

    while ((torrent = [enumerator nextObject]) && priority != kInvalidValue)
    {
        if (priority != torrent.priority)
        {
            priority = kInvalidValue;
        }
    }

    //set priority view
    if (priority == TR_PRI_HIGH)
    {
        index = OptionPopupPriorityHigh;
    }
    else if (priority == TR_PRI_NORMAL)
    {
        index = OptionPopupPriorityNormal;
    }
    else if (priority == TR_PRI_LOW)
    {
        index = OptionPopupPriorityLow;
    }
    else
    {
        index = -1;
    }
    [self.fPriorityPopUp selectItemAtIndex:index];
    self.fPriorityPopUp.enabled = YES;

    //get peer info
    enumerator = [self.fTorrents objectEnumerator];
    torrent = [enumerator nextObject]; //first torrent

    NSInteger maxPeers = torrent.maxPeerConnect;

    while ((torrent = [enumerator nextObject]))
    {
        if (maxPeers != torrent.maxPeerConnect)
        {
            maxPeers = kInvalidValue;
            break;
        }
    }

    //set peer view
    self.fPeersConnectField.enabled = YES;
    if (maxPeers != kInvalidValue)
    {
        self.fPeersConnectField.integerValue = maxPeers;
    }
    else
    {
        self.fPeersConnectField.stringValue = @"";
    }
}

- (void)setUseSpeedLimit:(id)sender
{
    BOOL const upload = sender == self.fUploadLimitCheck;

    BOOL const limit = ((NSSwitch*)sender).state == NSControlStateValueOn;

    for (Torrent* torrent in self.fTorrents)
    {
        [torrent setUseSpeedLimit:limit upload:upload];
    }

    NSTextField* field = upload ? self.fUploadLimitField : self.fDownloadLimitField;
    field.enabled = limit;
    if (limit)
    {
        [field selectText:self];
        [self.view.window makeKeyAndOrderFront:self];
    }

    [NSNotificationCenter.defaultCenter postNotificationName:@"UpdateOptionsNotification" object:self];
}

- (void)setUseGlobalSpeedLimit:(id)sender
{
    BOOL const limit = ((NSSwitch*)sender).state == NSControlStateValueOn;

    for (Torrent* torrent in self.fTorrents)
    {
        torrent.usesGlobalSpeedLimit = limit;
    }

    [NSNotificationCenter.defaultCenter postNotificationName:@"UpdateOptionsNotification" object:self];
}

- (void)setSpeedLimit:(id)sender
{
    BOOL const upload = sender == self.fUploadLimitField;
    NSInteger const limit = [sender intValue];

    for (Torrent* torrent in self.fTorrents)
    {
        [torrent setSpeedLimit:limit upload:upload];
    }

    [NSNotificationCenter.defaultCenter postNotificationName:@"UpdateOptionsNotification" object:self];
}

- (void)setRatioSetting:(id)sender
{
    NSInteger setting;
    BOOL single = NO;
    switch ([sender indexOfSelectedItem])
    {
    case OptionPopupTypeLimit:
        setting = TR_RATIOLIMIT_SINGLE;
        single = YES;
        break;
    case OptionPopupTypeNoLimit:
        setting = TR_RATIOLIMIT_UNLIMITED;
        break;
    case OptionPopupTypeGlobal:
        setting = TR_RATIOLIMIT_GLOBAL;
        break;
    default:
        NSAssert1(NO, @"Unknown option selected in ratio popup: %ld", [sender indexOfSelectedItem]);
        return;
    }

    for (Torrent* torrent in self.fTorrents)
    {
        torrent.ratioSetting = static_cast<tr_ratiolimit>(setting);
    }

    self.fRatioLimitField.hidden = !single;
    if (single)
    {
        [self.fRatioLimitField selectText:self];
        [self.view.window makeKeyAndOrderFront:self];
    }

    self.fRatioLimitGlobalLabel.hidden = setting != TR_RATIOLIMIT_GLOBAL;

    [NSNotificationCenter.defaultCenter postNotificationName:@"UpdateOptionsNotification" object:self];
}

- (void)setRatioLimit:(id)sender
{
    CGFloat const limit = [sender floatValue];

    for (Torrent* torrent in self.fTorrents)
    {
        torrent.ratioLimit = limit;
    }

    [NSNotificationCenter.defaultCenter postNotificationName:@"UpdateOptionsNotification" object:self];
}

- (void)setIdleSetting:(id)sender
{
    NSInteger setting;
    BOOL single = NO;
    switch ([sender indexOfSelectedItem])
    {
    case OptionPopupTypeLimit:
        setting = TR_IDLELIMIT_SINGLE;
        single = YES;
        break;
    case OptionPopupTypeNoLimit:
        setting = TR_IDLELIMIT_UNLIMITED;
        break;
    case OptionPopupTypeGlobal:
        setting = TR_IDLELIMIT_GLOBAL;
        break;
    default:
        NSAssert1(NO, @"Unknown option selected in idle popup: %ld", [sender indexOfSelectedItem]);
        return;
    }

    for (Torrent* torrent in self.fTorrents)
    {
        torrent.idleSetting = static_cast<tr_idlelimit>(setting);
    }

    self.fIdleLimitField.hidden = !single;
    self.fIdleLimitLabel.hidden = !single;
    if (single)
    {
        [self.fIdleLimitField selectText:self];
        [self.view.window makeKeyAndOrderFront:self];
    }

    self.fIdleLimitGlobalLabel.hidden = setting != TR_IDLELIMIT_GLOBAL;

    [NSNotificationCenter.defaultCenter postNotificationName:@"UpdateOptionsNotification" object:self];
}

- (void)setIdleLimit:(id)sender
{
    NSUInteger const limit = [sender integerValue];

    for (Torrent* torrent in self.fTorrents)
    {
        torrent.idleLimitMinutes = limit;
    }

    [NSNotificationCenter.defaultCenter postNotificationName:@"UpdateOptionsNotification" object:self];
}

- (IBAction)setRemoveWhenSeedingCompletes:(id)sender
{
    BOOL const enable = ((NSSwitch*)sender).state == NSControlStateValueOn;

    for (Torrent* torrent in self.fTorrents)
    {
        torrent.removeWhenFinishSeeding = enable;
    }

    [NSNotificationCenter.defaultCenter postNotificationName:@"UpdateOptionsNotification" object:self];
}

- (void)setPriority:(id)sender
{
    tr_priority_t priority;
    switch ([sender indexOfSelectedItem])
    {
    case OptionPopupPriorityHigh:
        priority = TR_PRI_HIGH;
        break;
    case OptionPopupPriorityNormal:
        priority = TR_PRI_NORMAL;
        break;
    case OptionPopupPriorityLow:
        priority = TR_PRI_LOW;
        break;
    default:
        NSAssert1(NO, @"Unknown option selected in priority popup: %ld", [sender indexOfSelectedItem]);
        return;
    }

    for (Torrent* torrent in self.fTorrents)
    {
        torrent.priority = priority;
    }

    [NSNotificationCenter.defaultCenter postNotificationName:@"UpdateUI" object:nil];

    [NSNotificationCenter.defaultCenter postNotificationName:@"UpdateOptionsNotification" object:self];
}

- (void)setPeersConnectLimit:(id)sender
{
    NSInteger limit = [sender intValue];

    for (Torrent* torrent in self.fTorrents)
    {
        torrent.maxPeerConnect = limit;
    }

    [NSNotificationCenter.defaultCenter postNotificationName:@"UpdateOptionsNotification" object:self];
}

- (BOOL)control:(NSControl*)control textShouldBeginEditing:(NSText*)fieldEditor
{
    self.fInitialString = control.stringValue;

    return YES;
}

- (BOOL)control:(NSControl*)control didFailToFormatString:(NSString*)string errorDescription:(NSString*)error
{
    NSBeep();
    if (self.fInitialString)
    {
        control.stringValue = self.fInitialString;
        self.fInitialString = nil;
    }
    return NO;
}

#pragma mark - Private

- (void)setupInfo
{
    [self view];

    if (self.fTorrents.count == 0)
    {
        self.fUploadLimitCheck.enabled = NO;
        self.fUploadLimitCheck.state = NSControlStateValueOff;
        self.fUploadLimitField.enabled = NO;
        self.fUploadLimitField.stringValue = @"";

        self.fDownloadLimitCheck.enabled = NO;
        self.fDownloadLimitCheck.state = NSControlStateValueOff;
        self.fDownloadLimitField.enabled = NO;
        self.fDownloadLimitField.stringValue = @"";

        self.fGlobalLimitCheck.enabled = NO;
        self.fGlobalLimitCheck.state = NSControlStateValueOff;

        self.fPriorityPopUp.enabled = NO;
        [self.fPriorityPopUp selectItemAtIndex:-1];

        self.fRatioPopUp.enabled = NO;
        [self.fRatioPopUp selectItemAtIndex:-1];
        self.fRatioLimitField.hidden = YES;
        self.fRatioLimitField.stringValue = @"";
        self.fRatioLimitGlobalLabel.hidden = YES;

        self.fIdlePopUp.enabled = NO;
        [self.fIdlePopUp selectItemAtIndex:-1];
        self.fIdleLimitField.hidden = YES;
        self.fIdleLimitField.stringValue = @"";
        self.fIdleLimitLabel.hidden = YES;
        self.fIdleLimitGlobalLabel.hidden = YES;

        self.fRemoveSeedingCompleteCheck.enabled = NO;
        self.fRemoveSeedingCompleteCheck.state = NSControlStateValueOff;

        self.fPeersConnectField.enabled = NO;
        self.fPeersConnectField.stringValue = @"";
    }
    else
    {
        [self updateOptions];
    }
}

- (void)setGlobalLabels
{
    NSString* global = [NSUserDefaults.standardUserDefaults boolForKey:@"RatioCheck"] ?
        [NSString stringForRatio:[NSUserDefaults.standardUserDefaults floatForKey:@"RatioLimit"]] :
        NSLocalizedString(@"disabled", "Info options -> global setting");
    self.fRatioLimitGlobalLabel.stringValue = global;

    //idle field
    NSString* globalIdle;
    if ([NSUserDefaults.standardUserDefaults boolForKey:@"IdleLimitCheck"])
    {
        NSInteger const globalMin = [NSUserDefaults.standardUserDefaults integerForKey:@"IdleLimitMinutes"];
        globalIdle = globalMin == 1 ?
            NSLocalizedString(@"1 minute", "Info options -> global setting") :
            [NSString localizedStringWithFormat:NSLocalizedString(@"%ld minutes", "Info options -> global setting"), globalMin];
    }
    else
    {
        globalIdle = NSLocalizedString(@"disabled", "Info options -> global setting");
    }
    self.fIdleLimitGlobalLabel.stringValue = globalIdle;
}

- (void)updateOptionsNotification:(NSNotification*)notification
{
    if (notification.object != self)
    {
        [self updateOptions];
    }
}

@end
