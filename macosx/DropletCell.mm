// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "DropletCell.h"
#import "Controller.h"
#import "DropletView.h"
#import "NSStringAdditions.h"
#import "Theme.h"
#import "Torrent.h"
#import "TorrentTableView.h"

@interface DropletCell ()
@property(nonatomic) NSTextField* fNameField;
@property(nonatomic) NSTextField* fStateField;
@property(nonatomic) NSTextField* fDetailField;
@property(nonatomic) NSTextField* fSpeedField;
@property(nonatomic) NSButton* fControlButton;
@property(nonatomic) NSButton* fRemoveButton;
@property(nonatomic) NSTrackingArea* fTrackingArea;
@property(nonatomic, weak) Torrent* fTorrent;
@property(nonatomic, weak) TorrentTableView* fTable;
@property(nonatomic) BOOL fHovering;
@end

@implementation DropletCell

- (instancetype)initWithFrame:(NSRect)frame
{
    if ((self = [super initWithFrame:frame]))
    {
        _fNameField = [Theme labelWithFont:[NSFont systemFontOfSize:15 weight:NSFontWeightMedium] color:Theme.bright];
        _fStateField = [Theme labelWithFont:[NSFont systemFontOfSize:13] color:Theme.foreground];
        _fDetailField = [Theme labelWithFont:[NSFont systemFontOfSize:12] color:Theme.foreground];
        _fDetailField.lineBreakMode = NSLineBreakByTruncatingTail;

        NSStackView* text = [NSStackView stackViewWithViews:@[ _fNameField, _fStateField, _fDetailField ]];
        text.orientation = NSUserInterfaceLayoutOrientationVertical;
        text.alignment = NSLayoutAttributeLeading;
        text.spacing = 3;
        text.translatesAutoresizingMaskIntoConstraints = NO;
        [self addSubview:text];

        _dropletView = [[DropletView alloc] initWithFrame:NSMakeRect(0, 0, 600, 68)];
        _dropletView.translatesAutoresizingMaskIntoConstraints = NO;
        [self addSubview:_dropletView];

        _fSpeedField = [Theme labelWithFont:[NSFont monospacedDigitSystemFontOfSize:14 weight:NSFontWeightRegular] color:Theme.bright];
        _fSpeedField.alignment = NSTextAlignmentRight;
        [self addSubview:_fSpeedField];

        _fControlButton = [Theme iconButton:@"Pause" target:self action:@selector(toggleControl:)];
        _fControlButton.contentTintColor = Theme.bright;
        _fRemoveButton = [Theme iconButton:@"Close" target:self action:@selector(removeTransfer:)];
        _fRemoveButton.toolTip = NSLocalizedString(@"Remove", "Torrent row -> remove button");
        _fControlButton.hidden = YES;
        _fRemoveButton.hidden = YES;
        [self addSubview:_fControlButton];
        [self addSubview:_fRemoveButton];

        [NSLayoutConstraint activateConstraints:@[
            [text.leadingAnchor constraintEqualToAnchor:self.leadingAnchor constant:24],
            [text.centerYAnchor constraintEqualToAnchor:self.centerYAnchor],
            [text.widthAnchor constraintEqualToConstant:260],
            [_fNameField.widthAnchor constraintEqualToAnchor:text.widthAnchor],
            [_fStateField.widthAnchor constraintEqualToAnchor:text.widthAnchor],
            [_fDetailField.widthAnchor constraintEqualToAnchor:text.widthAnchor],

            [_dropletView.leadingAnchor constraintEqualToAnchor:text.trailingAnchor constant:22],
            [_dropletView.trailingAnchor constraintEqualToAnchor:self.trailingAnchor constant:-136],
            [_dropletView.centerYAnchor constraintEqualToAnchor:self.centerYAnchor],
            [_dropletView.heightAnchor constraintEqualToConstant:68],

            [_fSpeedField.trailingAnchor constraintEqualToAnchor:self.trailingAnchor constant:-24],
            [_fSpeedField.centerYAnchor constraintEqualToAnchor:self.centerYAnchor],
            [_fSpeedField.leadingAnchor constraintEqualToAnchor:_dropletView.trailingAnchor constant:8],

            [_fRemoveButton.trailingAnchor constraintEqualToAnchor:self.trailingAnchor constant:-24],
            [_fRemoveButton.centerYAnchor constraintEqualToAnchor:self.centerYAnchor],
            [_fRemoveButton.widthAnchor constraintEqualToConstant:30],
            [_fRemoveButton.heightAnchor constraintEqualToConstant:30],
            [_fControlButton.trailingAnchor constraintEqualToAnchor:_fRemoveButton.leadingAnchor constant:-6],
            [_fControlButton.centerYAnchor constraintEqualToAnchor:self.centerYAnchor],
            [_fControlButton.widthAnchor constraintEqualToConstant:30],
            [_fControlButton.heightAnchor constraintEqualToConstant:30],
        ]];
    }
    return self;
}

+ (NSString*)timeLeft:(time_t)eta
{
    if (eta <= 0)
    {
        return nil;
    }
    static NSDateComponentsFormatter* sFormatter = ^{
        NSDateComponentsFormatter* formatter = [[NSDateComponentsFormatter alloc] init];
        formatter.unitsStyle = NSDateComponentsFormatterUnitsStyleShort;
        formatter.maximumUnitCount = 1;
        formatter.allowedUnits = NSCalendarUnitDay | NSCalendarUnitHour | NSCalendarUnitMinute | NSCalendarUnitSecond;
        return formatter;
    }();
    return [NSString stringWithFormat:NSLocalizedString(@"%@ left", "Torrent row detail"), [sFormatter stringFromTimeInterval:eta]];
}

- (void)configureWithTorrent:(Torrent*)torrent table:(TorrentTableView*)table
{
    self.fTorrent = torrent;
    self.fTable = table;
    self.objectValue = torrent;
    self.fHovering = NO;
    [self updateHover];
    [self refresh];
}

- (void)refresh
{
    Torrent* torrent = self.fTorrent;
    if (torrent == nil)
    {
        return;
    }

    self.fNameField.stringValue = torrent.name;

    NSString* state;
    NSString* suffix = @"";
    NSColor* color = Theme.foreground;
    NSString* detail;
    NSString* speed = @"";

    NSString* have = [NSString stringForFileSize:torrent.haveTotal];
    NSString* total = [NSString stringForFileSize:torrent.size];
    NSString* percent = [NSString stringWithFormat:@"  %.0f%%", torrent.progress * 100.0];

    if (torrent.error)
    {
        state = NSLocalizedString(@"Problem", "Torrent row state");
        color = Theme.danger;
        detail = torrent.errorMessage;
    }
    else if (torrent.checking || torrent.checkingWaiting)
    {
        state = NSLocalizedString(@"Verifying", "Torrent row state");
        color = Theme.accent;
        suffix = torrent.checking ? [NSString stringWithFormat:@"  %.0f%%", torrent.checkingProgress * 100.0] : @"";
        detail = total;
    }
    else if (torrent.magnet)
    {
        state = torrent.active ? NSLocalizedString(@"Fetching details", "Torrent row state") : NSLocalizedString(@"Paused", "Torrent row state");
        color = torrent.active ? Theme.accent : Theme.foreground;
        detail = NSLocalizedString(@"Waiting for the file list", "Torrent row detail");
    }
    else if (torrent.seeding)
    {
        state = NSLocalizedString(@"Seeding", "Torrent row state");
        color = Theme.seed;
        suffix = [NSString stringWithFormat:NSLocalizedString(@"  Ratio %@", "Torrent row state"), [NSString stringForRatio:torrent.ratio]];
        if ([NSUserDefaults.standardUserDefaults boolForKey:@"RatioCheck"])
        {
            CGFloat const goal = [NSUserDefaults.standardUserDefaults floatForKey:@"RatioLimit"];
            suffix = [suffix stringByAppendingFormat:NSLocalizedString(@" of %.1f", "Torrent row state"), goal];
        }
        NSUInteger const peers = torrent.peersGettingFromUs;
        detail = peers > 0 ? [NSString stringWithFormat:NSLocalizedString(@"%@  ·  giving back to %lu peers", "Torrent row detail"), total, peers] : total;
        speed = torrent.uploadRate > 0.0 ? [NSString stringForSpeed:torrent.uploadRate] : @"";
    }
    else if (torrent.active)
    {
        state = NSLocalizedString(@"Downloading", "Torrent row state");
        color = Theme.accent;
        suffix = percent;
        NSString* left = torrent.downloadRate > 0.0 ? [DropletCell timeLeft:torrent.eta] : nil;
        detail = left.length > 0 ? [NSString stringWithFormat:NSLocalizedString(@"%@ of %@  ·  %@", "Torrent row detail"), have, total, left] :
                                   [NSString stringWithFormat:NSLocalizedString(@"%@ of %@", "Torrent row detail"), have, total];
        speed = torrent.downloadRate > 0.0 ? [NSString stringForSpeed:torrent.downloadRate] : @"";
    }
    else if (torrent.allDownloaded)
    {
        state = NSLocalizedString(@"Finished", "Torrent row state");
        detail = total;
    }
    else
    {
        state = torrent.waitingToStart ? NSLocalizedString(@"Queued", "Torrent row state") : NSLocalizedString(@"Paused", "Torrent row state");
        suffix = percent;
        detail = [NSString stringWithFormat:NSLocalizedString(@"%@ of %@", "Torrent row detail"), have, total];
    }

    NSMutableAttributedString* line = [[NSMutableAttributedString alloc]
        initWithString:state
            attributes:@{ NSFontAttributeName : [NSFont systemFontOfSize:13 weight:NSFontWeightMedium], NSForegroundColorAttributeName : color }];
    [line appendAttributedString:[[NSAttributedString alloc] initWithString:suffix attributes:@{
              NSFontAttributeName : [NSFont systemFontOfSize:13],
              NSForegroundColorAttributeName : Theme.foreground
          }]];
    self.fStateField.attributedStringValue = line;
    self.fDetailField.stringValue = detail ?: @"";
    self.fSpeedField.stringValue = speed;

    BOOL const running = torrent.active || torrent.waitingToStart;
    self.fControlButton.image = [Theme icon:running ? @"Pause" : @"Play"];
    self.fControlButton.toolTip = running ? NSLocalizedString(@"Pause", "Torrent row -> control button") :
                                            NSLocalizedString(@"Resume", "Torrent row -> control button");

    [self.dropletView updateWithTorrent:torrent];
}

- (void)updateHover
{
    self.fControlButton.hidden = !self.fHovering;
    self.fRemoveButton.hidden = !self.fHovering;
    self.fSpeedField.hidden = self.fHovering;
}

- (void)updateTrackingAreas
{
    [super updateTrackingAreas];
    if (self.fTrackingArea != nil)
    {
        [self removeTrackingArea:self.fTrackingArea];
    }
    self.fTrackingArea = [[NSTrackingArea alloc] initWithRect:NSZeroRect
                                                      options:NSTrackingMouseEnteredAndExited | NSTrackingActiveInKeyWindow | NSTrackingInVisibleRect
                                                        owner:self
                                                     userInfo:nil];
    [self addTrackingArea:self.fTrackingArea];
}

- (void)mouseEntered:(NSEvent*)event
{
    self.fHovering = YES;
    [self updateHover];
}

- (void)mouseExited:(NSEvent*)event
{
    self.fHovering = NO;
    [self updateHover];
}

- (void)toggleControl:(id)sender
{
    [self.fTable toggleControlForTorrent:sender];
    [self refresh];
}

- (void)removeTransfer:(id)sender
{
    Torrent* torrent = self.fTorrent;
    if (torrent != nil)
    {
        [(Controller*)NSApp.delegate removeTorrents:@[ torrent ] deleteData:NO];
    }
}

@end
