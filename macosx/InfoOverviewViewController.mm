// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "InfoOverviewViewController.h"
#import "DropletView.h"
#import "NSStringAdditions.h"
#import "Theme.h"
#import "Torrent.h"

static CGFloat const kViewHeight = 480.0;
static CGFloat const kDropletsHeight = 236.0;

@interface InfoOverviewViewController ()
@property(nonatomic, copy) NSArray<Torrent*>* fTorrents;
@property(nonatomic) DropletView* fDropletView;
@property(nonatomic) NSArray<NSTextField*>* fStatValues;
@property(nonatomic) NSTextField* fLocationField;
@property(nonatomic) NSTextField* fPiecesField;
@property(nonatomic) NSTextField* fHashField;
@end

@implementation InfoOverviewViewController

- (instancetype)init
{
    if ((self = [super initWithNibName:nil bundle:nil]))
    {
        self.title = NSLocalizedString(@"Overview", "Inspector -> tab");
    }
    return self;
}

- (NSView*)factWithTitle:(NSString*)title value:(NSTextField*)value
{
    NSTextField* label = [Theme labelWithFont:[NSFont systemFontOfSize:13] color:Theme.foreground];
    label.stringValue = title;
    [label.widthAnchor constraintEqualToConstant:76].active = YES;
    value.selectable = YES;
    [value setContentCompressionResistancePriority:NSLayoutPriorityDefaultLow forOrientation:NSLayoutConstraintOrientationHorizontal];
    NSStackView* stack = [NSStackView stackViewWithViews:@[ label, value ]];
    stack.spacing = 8;
    return stack;
}

- (void)loadView
{
    NSView* view = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 520, kViewHeight)];
    view.translatesAutoresizingMaskIntoConstraints = NO;

    self.fDropletView = [[DropletView alloc] initWithFrame:NSMakeRect(0, 0, 500, kDropletsHeight)];
    self.fDropletView.dotRows = 20;
    self.fDropletView.tracksNewPieces = NO;
    self.fDropletView.translatesAutoresizingMaskIntoConstraints = NO;
    [view addSubview:self.fDropletView];

    NSMutableArray<NSTextField*>* values = [NSMutableArray array];
    NSMutableArray<NSView*>* stats = [NSMutableArray array];
    NSArray<NSString*>* titles = @[
        NSLocalizedString(@"Have", "Overview -> stat"),
        NSLocalizedString(@"Seeds", "Overview -> stat"),
        NSLocalizedString(@"Peers", "Overview -> stat"),
        NSLocalizedString(@"Down", "Overview -> stat"),
        NSLocalizedString(@"Up", "Overview -> stat"),
    ];
    for (NSString* title in titles)
    {
        NSTextField* value = [Theme labelWithFont:[Theme displayFontOfSize:19] color:Theme.bright];
        [values addObject:value];
        [stats addObject:[Theme statWithTitle:title value:value]];
    }
    self.fStatValues = values;

    NSStackView* row = [NSStackView stackViewWithViews:stats];
    row.distribution = NSStackViewDistributionFillEqually;
    row.translatesAutoresizingMaskIntoConstraints = NO;
    [view addSubview:row];

    self.fLocationField = [Theme labelWithFont:[NSFont systemFontOfSize:13] color:Theme.bright];
    self.fPiecesField = [Theme labelWithFont:[NSFont systemFontOfSize:13] color:Theme.bright];
    self.fHashField = [Theme labelWithFont:[NSFont monospacedSystemFontOfSize:12 weight:NSFontWeightRegular] color:Theme.bright];
    NSStackView* facts = [NSStackView stackViewWithViews:@[
        [self factWithTitle:NSLocalizedString(@"Saved to", "Overview -> fact") value:self.fLocationField],
        [self factWithTitle:NSLocalizedString(@"Pieces", "Overview -> fact") value:self.fPiecesField],
        [self factWithTitle:NSLocalizedString(@"Hash", "Overview -> fact") value:self.fHashField],
    ]];
    facts.orientation = NSUserInterfaceLayoutOrientationVertical;
    facts.alignment = NSLayoutAttributeLeading;
    facts.spacing = 8;
    facts.translatesAutoresizingMaskIntoConstraints = NO;
    [view addSubview:facts];

    [NSLayoutConstraint activateConstraints:@[
        [self.fDropletView.leadingAnchor constraintEqualToAnchor:view.leadingAnchor constant:20],
        [self.fDropletView.trailingAnchor constraintEqualToAnchor:view.trailingAnchor constant:-20],
        [self.fDropletView.topAnchor constraintEqualToAnchor:view.topAnchor constant:14],
        [self.fDropletView.heightAnchor constraintEqualToConstant:kDropletsHeight],
        [row.leadingAnchor constraintEqualToAnchor:view.leadingAnchor constant:20],
        [row.trailingAnchor constraintEqualToAnchor:view.trailingAnchor constant:-20],
        [row.topAnchor constraintEqualToAnchor:self.fDropletView.bottomAnchor constant:20],
        [facts.leadingAnchor constraintEqualToAnchor:view.leadingAnchor constant:20],
        [facts.trailingAnchor constraintEqualToAnchor:view.trailingAnchor constant:-20],
        [facts.topAnchor constraintEqualToAnchor:row.bottomAnchor constant:22],
    ]];

    self.view = view;
}

- (void)setInfoForTorrents:(NSArray<Torrent*>*)torrents
{
    self.fTorrents = torrents;
    [self view];
    [self updateInfo];
}

- (void)updateInfo
{
    NSArray<Torrent*>* torrents = self.fTorrents;
    Torrent* only = torrents.count == 1 ? torrents[0] : nil;

    for (NSView* view in self.view.subviews)
    {
        view.hidden = torrents.count == 0;
    }
    if (torrents.count == 0)
    {
        return;
    }

    uint64_t have = 0;
    NSUInteger connected = 0;
    NSInteger seeds = -1;
    NSInteger peers = -1;
    CGFloat down = 0.0;
    CGFloat up = 0.0;
    for (Torrent* torrent in torrents)
    {
        have += torrent.haveTotal;
        connected += torrent.totalPeersConnected;
        if (torrent.swarmSeeders >= 0)
        {
            seeds = MAX(seeds, 0) + torrent.swarmSeeders;
        }
        if (torrent.swarmLeechers >= 0)
        {
            peers = MAX(peers, 0) + torrent.swarmLeechers;
        }
        down += torrent.downloadRate;
        up += torrent.uploadRate;
    }

    self.fStatValues[0].stringValue = [NSString stringForFileSize:have];
    self.fStatValues[1].stringValue = seeds >= 0 ? [NSString stringWithFormat:@"%ld", seeds] : @"?";
    self.fStatValues[2].stringValue = peers >= 0 ? [NSString stringWithFormat:@"%ld", peers] : [NSString stringWithFormat:@"%lu", connected];
    self.fStatValues[3].stringValue = [NSString stringForSpeed:down];
    self.fStatValues[4].stringValue = [NSString stringForSpeed:up];

    [self.fDropletView updateWithTorrent:only];
    self.fDropletView.hidden = only == nil;

    self.fLocationField.stringValue = (only.dataLocation ?: only.currentDirectory).stringByAbbreviatingWithTildeInPath ?: @"";
    self.fPiecesField.stringValue = only != nil ? [NSString stringWithFormat:@"%ld × %@", only.pieceCount, [NSString stringForFileSize:only.pieceSize]] : @"";
    self.fHashField.stringValue = only.hashString ?: @"";
}

@end
