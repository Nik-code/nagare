// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#include <libtransmission/transmission.h>
#include <libtransmission/utils.h>

#import "InfoPeersViewController.h"
#import "NSStringAdditions.h"
#import "Theme.h"
#import "Torrent.h"

static CGFloat const kRowHeight = 50.0;

/// How much of the transfer a peer has, as a thin line along the bottom of its row.
@interface PeerProgressLine : NSView
@property(nonatomic) CGFloat progress;
@property(nonatomic) BOOL seed;
@end

@implementation PeerProgressLine

- (void)drawRect:(NSRect)dirtyRect
{
    [Theme.border setFill];
    NSRectFill(self.bounds);
    [[(self.seed ? Theme.seed : Theme.accent) colorWithAlphaComponent:0.45] setFill];
    NSRectFill(NSMakeRect(0, 0, NSWidth(self.bounds) * self.progress, NSHeight(self.bounds)));
}

@end

/// One peer or web seed: who it is, how much it has, and what's moving.
@interface PeerCell : NSTableCellView
- (void)configureWithPeer:(NSDictionary*)peer;
- (void)configureWithWebSeed:(NSDictionary*)webSeed;
@end

@interface PeerCell ()
@property(nonatomic) NSImageView* fLock;
@property(nonatomic) NSTextField* fAddressField;
@property(nonatomic) NSTextField* fDetailField;
@property(nonatomic) NSTextField* fDownField;
@property(nonatomic) NSTextField* fUpField;
@property(nonatomic) PeerProgressLine* fLine;
@end

@implementation PeerCell

- (instancetype)initWithFrame:(NSRect)frame
{
    if ((self = [super initWithFrame:frame]))
    {
        _fAddressField = [Theme labelWithFont:[NSFont monospacedDigitSystemFontOfSize:13 weight:NSFontWeightRegular] color:Theme.bright];
        _fAddressField.lineBreakMode = NSLineBreakByTruncatingMiddle;
        _fDetailField = [Theme labelWithFont:[NSFont systemFontOfSize:12] color:Theme.foreground];
        _fDownField = [Theme labelWithFont:[NSFont monospacedDigitSystemFontOfSize:12 weight:NSFontWeightRegular] color:Theme.accent];
        _fDownField.alignment = NSTextAlignmentRight;
        _fUpField = [Theme labelWithFont:[NSFont monospacedDigitSystemFontOfSize:12 weight:NSFontWeightRegular] color:Theme.seed];
        _fUpField.alignment = NSTextAlignmentRight;

        _fLock = [NSImageView imageViewWithImage:[Theme icon:@"Lock"]];
        _fLock.contentTintColor = Theme.foreground;
        _fLock.toolTip = NSLocalizedString(@"Encrypted Connection", "inspector -> peer table -> header tool tip");
        _fLock.translatesAutoresizingMaskIntoConstraints = NO;
        [self addSubview:_fLock];

        NSStackView* who = [NSStackView stackViewWithViews:@[ _fAddressField, _fDetailField ]];
        who.orientation = NSUserInterfaceLayoutOrientationVertical;
        who.alignment = NSLayoutAttributeLeading;
        who.spacing = 1;
        who.translatesAutoresizingMaskIntoConstraints = NO;
        [self addSubview:who];

        NSStackView* rates = [NSStackView stackViewWithViews:@[ _fDownField, _fUpField ]];
        rates.orientation = NSUserInterfaceLayoutOrientationVertical;
        rates.alignment = NSLayoutAttributeTrailing;
        rates.spacing = 1;
        rates.translatesAutoresizingMaskIntoConstraints = NO;
        [self addSubview:rates];

        _fLine = [[PeerProgressLine alloc] initWithFrame:NSZeroRect];
        _fLine.translatesAutoresizingMaskIntoConstraints = NO;
        [self addSubview:_fLine];

        [NSLayoutConstraint activateConstraints:@[
            [who.leadingAnchor constraintEqualToAnchor:self.leadingAnchor constant:20],
            [who.centerYAnchor constraintEqualToAnchor:self.centerYAnchor],
            [who.trailingAnchor constraintLessThanOrEqualToAnchor:_fLock.leadingAnchor constant:-8],
            [_fLock.trailingAnchor constraintEqualToAnchor:rates.leadingAnchor constant:-12],
            [_fLock.centerYAnchor constraintEqualToAnchor:self.centerYAnchor],
            [_fLock.widthAnchor constraintEqualToConstant:14],
            [_fLock.heightAnchor constraintEqualToConstant:14],
            [rates.trailingAnchor constraintEqualToAnchor:self.trailingAnchor constant:-20],
            [rates.centerYAnchor constraintEqualToAnchor:self.centerYAnchor],
            [rates.widthAnchor constraintEqualToConstant:96],
            [_fLine.leadingAnchor constraintEqualToAnchor:self.leadingAnchor constant:20],
            [_fLine.trailingAnchor constraintEqualToAnchor:self.trailingAnchor constant:-20],
            [_fLine.bottomAnchor constraintEqualToAnchor:self.bottomAnchor],
            [_fLine.heightAnchor constraintEqualToConstant:1],
        ]];
    }
    return self;
}

- (void)setDown:(NSNumber*)down up:(NSNumber*)up
{
    self.fDownField.stringValue = down != nil ? [@"↓ " stringByAppendingString:[NSString stringForSpeed:down.doubleValue]] : @"";
    self.fDownField.hidden = down == nil;
    self.fUpField.stringValue = up != nil ? [@"↑ " stringByAppendingString:[NSString stringForSpeed:up.doubleValue]] : @"";
    self.fUpField.hidden = up == nil;
}

- (void)configureWithPeer:(NSDictionary*)peer
{
    self.fAddressField.stringValue = peer[@"IP"];

    BOOL const seed = [peer[@"Seed"] boolValue];
    CGFloat const progress = [peer[@"Progress"] floatValue];
    NSString* held = seed ? NSLocalizedString(@"Seed", "Inspector -> Peers tab -> row") : [NSString percentString:progress longDecimals:NO];
    NSString* client = peer[@"Client"];
    self.fDetailField.stringValue = client.length > 0 ? [NSString stringWithFormat:@"%@  ·  %@", held, client] : held;

    self.fLock.hidden = ![peer[@"Encryption"] boolValue];
    [self setDown:peer[@"DL From Rate"] up:peer[@"UL To Rate"]];

    self.fLine.hidden = NO;
    self.fLine.seed = seed;
    self.fLine.progress = progress;
    self.fLine.needsDisplay = YES;
}

- (void)configureWithWebSeed:(NSDictionary*)webSeed
{
    self.fAddressField.stringValue = webSeed[@"Address"];
    self.fDetailField.stringValue = NSLocalizedString(@"Web seed", "Inspector -> Peers tab -> row");
    self.fLock.hidden = YES;
    [self setDown:webSeed[@"DL From Rate"] up:nil];
    self.fLine.hidden = YES;
}

@end

@interface InfoPeersViewController ()<NSTableViewDataSource, NSTableViewDelegate>

@property(nonatomic, copy) NSArray<Torrent*>* fTorrents;

@property(nonatomic) BOOL fSet;

@property(nonatomic) NSMutableArray<NSDictionary*>* fPeers;
@property(nonatomic) NSMutableArray<NSDictionary*>* fWebSeeds;

@property(nonatomic) NSTableView* fPeerTable;
@property(nonatomic) NSArray<NSTextField*>* fStatValues;
@property(nonatomic) NSTextField* fSourcesField;

@end

@implementation InfoPeersViewController

- (instancetype)init
{
    if ((self = [super initWithNibName:nil bundle:nil]))
    {
        self.title = NSLocalizedString(@"Peers", "Inspector view -> title");
    }

    return self;
}

- (void)loadView
{
    NSView* view = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 540, 480)];

    NSMutableArray<NSTextField*>* values = [NSMutableArray array];
    NSMutableArray<NSView*>* stats = [NSMutableArray array];
    NSArray<NSString*>* titles = @[
        NSLocalizedString(@"Connected", "Inspector -> Peers tab -> stat"),
        NSLocalizedString(@"Sending to you", "Inspector -> Peers tab -> stat"),
        NSLocalizedString(@"Receiving from you", "Inspector -> Peers tab -> stat"),
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

    self.fSourcesField = [Theme hintField:@""];
    self.fSourcesField.lineBreakMode = NSLineBreakByTruncatingTail;
    [view addSubview:self.fSourcesField];

    self.fPeerTable = [[NSTableView alloc] initWithFrame:NSMakeRect(0, 0, 540, 300)];
    [Theme styleList:self.fPeerTable rowHeight:kRowHeight];
    NSTableColumn* column = [[NSTableColumn alloc] initWithIdentifier:@"Peer"];
    column.width = 540;
    column.resizingMask = NSTableColumnAutoresizingMask;
    [self.fPeerTable addTableColumn:column];
    self.fPeerTable.dataSource = self;
    self.fPeerTable.delegate = self;

    NSScrollView* list = [Theme scrollViewWithDocument:self.fPeerTable];
    [view addSubview:list];

    [NSLayoutConstraint activateConstraints:@[
        [row.leadingAnchor constraintEqualToAnchor:view.leadingAnchor constant:20],
        [row.trailingAnchor constraintEqualToAnchor:view.trailingAnchor constant:-20],
        [row.topAnchor constraintEqualToAnchor:view.topAnchor constant:14],
        [self.fSourcesField.leadingAnchor constraintEqualToAnchor:view.leadingAnchor constant:20],
        [self.fSourcesField.trailingAnchor constraintEqualToAnchor:view.trailingAnchor constant:-20],
        [self.fSourcesField.topAnchor constraintEqualToAnchor:row.bottomAnchor constant:10],
        [list.leadingAnchor constraintEqualToAnchor:view.leadingAnchor],
        [list.trailingAnchor constraintEqualToAnchor:view.trailingAnchor],
        [list.topAnchor constraintEqualToAnchor:self.fSourcesField.bottomAnchor constant:8],
        [list.bottomAnchor constraintEqualToAnchor:view.bottomAnchor],
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

    if (!self.fPeers)
    {
        self.fPeers = [[NSMutableArray alloc] init];
    }
    else
    {
        [self.fPeers removeAllObjects];
    }

    if (!self.fWebSeeds)
    {
        self.fWebSeeds = [[NSMutableArray alloc] init];
    }
    else
    {
        [self.fWebSeeds removeAllObjects];
    }

    NSUInteger connected = 0;
    NSUInteger tracker = 0;
    NSUInteger incoming = 0;
    NSUInteger cache = 0;
    NSUInteger lpd = 0;
    NSUInteger pex = 0;
    NSUInteger dht = 0;
    NSUInteger ltep = 0;
    NSUInteger toUs = 0;
    NSUInteger fromUs = 0;
    NSUInteger knownTracker = 0;
    NSUInteger knownIncoming = 0;
    NSUInteger knownCache = 0;
    NSUInteger knownLpd = 0;
    NSUInteger knownPex = 0;
    NSUInteger knownDht = 0;
    NSUInteger knownLtep = 0;
    BOOL anyActive = false;
    for (Torrent* torrent in self.fTorrents)
    {
        if (torrent.webSeedCount > 0)
        {
            [self.fWebSeeds addObjectsFromArray:torrent.webSeeds];
        }

        if (torrent.active)
        {
            anyActive = YES;
            [self.fPeers addObjectsFromArray:torrent.peers];

            NSUInteger const connectedThis = torrent.totalPeersConnected;
            if (connectedThis > 0)
            {
                connected += torrent.totalPeersConnected;
                tracker += torrent.totalPeersTracker;
                incoming += torrent.totalPeersIncoming;
                cache += torrent.totalPeersCache;
                lpd += torrent.totalPeersLocal;
                pex += torrent.totalPeersPex;
                dht += torrent.totalPeersDHT;
                ltep += torrent.totalPeersLTEP;

                toUs += torrent.peersSendingToUs;
                fromUs += torrent.peersGettingFromUs;
            }
        }
        knownTracker += torrent.totalKnownPeersTracker;
        knownIncoming += torrent.totalKnownPeersIncoming;
        knownCache += torrent.totalKnownPeersCache;
        knownLpd += torrent.totalKnownPeersLocal;
        knownPex += torrent.totalKnownPeersPex;
        knownDht += torrent.totalKnownPeersDHT;
        knownLtep += torrent.totalKnownPeersLTEP;
    }

    [self.fPeers sortUsingComparator:^NSComparisonResult(NSDictionary* a, NSDictionary* b) {
        return [self comparePeer:a toPeer:b];
    }];
    [self.fPeerTable reloadData];

    self.fStatValues[0].stringValue = [NSString stringWithFormat:@"%lu", connected];
    self.fStatValues[1].stringValue = [NSString stringWithFormat:@"%lu", toUs];
    self.fStatValues[2].stringValue = [NSString stringWithFormat:@"%lu", fromUs];

    if (!anyActive)
    {
        self.fSourcesField.stringValue = self.fTorrents.count == 1 ? NSLocalizedString(@"Transfer Not Active", "Inspector -> Peers tab -> peers") :
                                                                     NSLocalizedString(@"Transfers Not Active", "Inspector -> Peers tab -> peers");
    }
    else if (connected > 0)
    {
        self.fSourcesField.stringValue = [NSString stringWithFormat:NSLocalizedString(@"Found through %@", "Inspector -> Peers tab -> peers"),
                                                                    [self connectedTextFrom:tracker:incoming:cache:lpd:pex:dht:ltep]];
    }
    else
    {
        self.fSourcesField.stringValue = NSLocalizedString(@"Looking for peers", "Inspector -> Peers tab -> peers");
    }

    auto totalKnown = knownTracker + knownIncoming + knownCache + knownLpd + knownPex + knownDht + knownLtep;
    NSString* knownText = [self connectedTextFrom:knownTracker:knownIncoming:knownCache:knownLpd:knownPex:knownDht:knownLtep];
    if (totalKnown <= 1)
    {
        self.fSourcesField.toolTip = [NSLocalizedString(@"Known:", "Inspector -> Peers tab -> peers")
            stringByAppendingFormat:@" %@", totalKnown > 0 ? knownText : @"0"];
    }
    else
    {
        self.fSourcesField.toolTip = [[NSString
            localizedStringWithFormat:NSLocalizedString(@"%lu Known:", "Inspector -> Peers tab -> peers"), totalKnown]
            stringByAppendingFormat:@" %@", knownText];
    }
}

// clang-format off: compatibility with unnamed arguments
- (NSString*)connectedTextFrom:(NSUInteger)tracker
                              :(NSUInteger)incoming
                              :(NSUInteger)cache
                              :(NSUInteger)lpd
                              :(NSUInteger)pex
                              :(NSUInteger)dht
                              :(NSUInteger)ltep
// clang-format on
{
    NSMutableArray* fromComponents = [NSMutableArray arrayWithCapacity:7];
    if (tracker > 0)
    {
        [fromComponents
            addObject:[NSString localizedStringWithFormat:NSLocalizedString(@"%lu tracker", "Inspector -> Peers tab -> peers"), tracker]];
    }
    if (incoming > 0)
    {
        [fromComponents
            addObject:[NSString localizedStringWithFormat:NSLocalizedString(@"%lu incoming", "Inspector -> Peers tab -> peers"), incoming]];
    }
    if (cache > 0)
    {
        [fromComponents
            addObject:[NSString localizedStringWithFormat:NSLocalizedString(@"%lu cache", "Inspector -> Peers tab -> peers"), cache]];
    }
    if (lpd > 0)
    {
        [fromComponents
            addObject:[NSString localizedStringWithFormat:NSLocalizedString(@"%lu local discovery", "Inspector -> Peers tab -> peers"), lpd]];
    }
    if (pex > 0)
    {
        [fromComponents
            addObject:[NSString localizedStringWithFormat:NSLocalizedString(@"%lu PEX", "Inspector -> Peers tab -> peers"), pex]];
    }
    if (dht > 0)
    {
        [fromComponents
            addObject:[NSString localizedStringWithFormat:NSLocalizedString(@"%lu DHT", "Inspector -> Peers tab -> peers"), dht]];
    }
    if (ltep > 0)
    {
        [fromComponents
            addObject:[NSString localizedStringWithFormat:NSLocalizedString(@"%lu LTEP", "Inspector -> Peers tab -> peers"), ltep]];
    }

    return [fromComponents componentsJoinedByString:@", "];
}

- (void)clearView
{
    self.fPeers = nil;
    self.fWebSeeds = nil;
}

- (NSInteger)numberOfRowsInTableView:(NSTableView*)tableView
{
    return self.fPeers.count + self.fWebSeeds.count;
}

- (NSView*)tableView:(NSTableView*)tableView viewForTableColumn:(NSTableColumn*)tableColumn row:(NSInteger)row
{
    PeerCell* cell = [tableView makeViewWithIdentifier:@"PeerCell" owner:self];
    if (cell == nil)
    {
        cell = [[PeerCell alloc] initWithFrame:NSZeroRect];
        cell.identifier = @"PeerCell";
    }

    NSInteger const peers = self.fPeers.count;
    if (row < peers)
    {
        NSDictionary* peer = self.fPeers[row];
        [cell configureWithPeer:peer];
        cell.toolTip = [self toolTipForPeer:peer];
    }
    else
    {
        NSDictionary* webSeed = self.fWebSeeds[row - peers];
        [cell configureWithWebSeed:webSeed];
        cell.toolTip = self.fTorrents.count > 1 ? webSeed[@"Name"] : nil;
    }
    return cell;
}

- (NSTableRowView*)tableView:(NSTableView*)tableView rowViewForRow:(NSInteger)row
{
    return [[ThemedRowView alloc] initWithHairlineInset:-1 selectionInset:12];
}

- (BOOL)tableView:(NSTableView*)tableView shouldSelectRow:(NSInteger)row
{
    return NO;
}

- (NSComparisonResult)comparePeer:(NSDictionary*)first toPeer:(NSDictionary*)second
{
    // Busiest first, then whoever has the most, so the list holds still between updates.
    double const firstRate = [first[@"DL From Rate"] doubleValue] + [first[@"UL To Rate"] doubleValue];
    double const secondRate = [second[@"DL From Rate"] doubleValue] + [second[@"UL To Rate"] doubleValue];
    if (firstRate != secondRate)
    {
        return firstRate > secondRate ? NSOrderedAscending : NSOrderedDescending;
    }

    float const firstProgress = [first[@"Progress"] floatValue];
    float const secondProgress = [second[@"Progress"] floatValue];
    if (firstProgress != secondProgress)
    {
        return firstProgress > secondProgress ? NSOrderedAscending : NSOrderedDescending;
    }

    return [first[@"IP"] compareNumeric:second[@"IP"]];
}

- (NSString*)toolTipForPeer:(NSDictionary*)peer
{
    BOOL const multiple = self.fTorrents.count > 1;

    NSMutableArray* components = [NSMutableArray arrayWithCapacity:multiple ? 6 : 5];

    if (multiple)
    {
        [components addObject:peer[@"Name"]];
    }

    CGFloat const progress = [peer[@"Progress"] floatValue];
    NSString* progressString = [NSString stringWithFormat:NSLocalizedString(@"Progress: %@", "Inspector -> Peers tab -> table row tooltip"),
                                                          [NSString percentString:progress longDecimals:NO]];
    if (progress < 1.0 && [peer[@"Seed"] boolValue])
    {
        progressString = [progressString
            stringByAppendingFormat:@" (%@)", NSLocalizedString(@"Partial Seed", "Inspector -> Peers tab -> table row tooltip")];
    }
    [components addObject:progressString];

    NSString* protocolString = [peer[@"uTP"] boolValue] ? @"\u00b5TP" : @"TCP";
    if ([peer[@"Encryption"] boolValue])
    {
        protocolString = [protocolString
            stringByAppendingFormat:@" (%@)", NSLocalizedString(@"encrypted", "Inspector -> Peers tab -> table row tooltip")];
    }
    [components addObject:[NSString stringWithFormat:NSLocalizedString(@"Protocol: %@", "Inspector -> Peers tab -> table row tooltip"),
                                                     protocolString]];

    NSString* portString;
    NSInteger port;
    if ((port = [peer[@"Port"] intValue]) > 0)
    {
        portString = [NSString stringWithFormat:@"%ld", port];
    }
    else
    {
        portString = NSLocalizedString(@"N/A", "Inspector -> Peers tab -> table row tooltip");
    }
    [components addObject:[NSString stringWithFormat:NSLocalizedString(@"Port: %@", "Inspector -> Peers tab -> table row tooltip"),
                                                     portString]];

    NSInteger const peerFrom = [peer[@"From"] integerValue];
    switch (peerFrom)
    {
    case TR_PEER_FROM_TRACKER:
        [components addObject:NSLocalizedString(@"From: tracker", "Inspector -> Peers tab -> table row tooltip")];
        break;
    case TR_PEER_FROM_INCOMING:
        [components addObject:NSLocalizedString(@"From: incoming connection", "Inspector -> Peers tab -> table row tooltip")];
        break;
    case TR_PEER_FROM_RESUME:
        [components addObject:NSLocalizedString(@"From: cache", "Inspector -> Peers tab -> table row tooltip")];
        break;
    case TR_PEER_FROM_LPD:
        [components addObject:NSLocalizedString(@"From: local peer discovery", "Inspector -> Peers tab -> table row tooltip")];
        break;
    case TR_PEER_FROM_PEX:
        [components addObject:NSLocalizedString(@"From: peer exchange", "Inspector -> Peers tab -> table row tooltip")];
        break;
    case TR_PEER_FROM_DHT:
        [components addObject:NSLocalizedString(@"From: distributed hash table", "Inspector -> Peers tab -> table row tooltip")];
        break;
    case TR_PEER_FROM_LTEP:
        [components addObject:NSLocalizedString(@"From: libtorrent extension protocol handshake", "Inspector -> Peers tab -> table row tooltip")];
        break;
    default:
        NSAssert1(NO, @"Peer from unknown source: %ld", peerFrom);
    }

    //determine status strings from flags
    NSMutableArray* statusArray = [NSMutableArray arrayWithCapacity:6];
    NSString* flags = peer[@"Flags"];

    if ([flags rangeOfString:@"D"].location != NSNotFound)
    {
        [statusArray addObject:NSLocalizedString(@"Currently downloading (interested and not choked)", "Inspector -> peer -> status")];
    }
    if ([flags rangeOfString:@"d"].location != NSNotFound)
    {
        [statusArray addObject:NSLocalizedString(
                                   @"You want to download, but peer does not want to send (interested and choked)",
                                   "Inspector -> peer -> status")];
    }
    if ([flags rangeOfString:@"U"].location != NSNotFound)
    {
        [statusArray addObject:NSLocalizedString(@"Currently uploading (interested and not choked)", "Inspector -> peer -> status")];
    }
    if ([flags rangeOfString:@"u"].location != NSNotFound)
    {
        [statusArray addObject:NSLocalizedString(@"Peer wants you to upload, but you do not want to (interested and choked)", "Inspector -> peer -> status")];
    }
    if ([flags rangeOfString:@"K"].location != NSNotFound)
    {
        [statusArray addObject:NSLocalizedString(@"Peer is unchoking you, but you are not interested", "Inspector -> peer -> status")];
    }
    if ([flags rangeOfString:@"?"].location != NSNotFound)
    {
        [statusArray addObject:NSLocalizedString(@"You unchoked the peer, but the peer is not interested", "Inspector -> peer -> status")];
    }

    if (statusArray.count > 0)
    {
        NSString* statusStrings = [statusArray componentsJoinedByString:@"\n\n"];
        [components addObject:[@"\n" stringByAppendingString:statusStrings]];
    }

    return [components componentsJoinedByString:@"\n"];
}

#pragma mark - Private

- (void)setupInfo
{
    [self view];

    if (self.fTorrents.count == 0)
    {
        self.fPeers = nil;
        self.fWebSeeds = nil;
        [self.fPeerTable reloadData];

        for (NSTextField* value in self.fStatValues)
        {
            value.stringValue = @"0";
        }
        self.fSourcesField.stringValue = @"";
    }

    self.fSet = YES;
}

@end
