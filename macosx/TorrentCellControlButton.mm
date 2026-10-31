// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "TorrentCellControlButton.h"
#import "TorrentTableView.h"
#import "Torrent.h"
#import "TorrentCell.h"
#import "Theme.h"

@interface TorrentCellControlButton ()
@property(nonatomic) NSTrackingArea* fTrackingArea;
@property(nonatomic, copy) NSString* controlImageSuffix;
@property(nonatomic, weak) IBOutlet TorrentCell* torrentCell;
@property(nonatomic, readonly) TorrentTableView* torrentTableView;
@end

@implementation TorrentCellControlButton

- (TorrentTableView*)torrentTableView
{
    return self.torrentCell.fTorrentTableView;
}

- (void)awakeFromNib
{
    [super awakeFromNib];

    self.controlImageSuffix = @"Off";
    [self updateImage];
}

- (void)resetImage
{
    self.controlImageSuffix = @"Off";
    [self updateImage];
}

- (void)mouseEntered:(NSEvent*)event
{
    [super mouseEntered:event];
    self.controlImageSuffix = @"Hover";
    [self updateImage];

    [self.torrentTableView hoverEventBeganForView:self];
}

- (void)mouseExited:(NSEvent*)event
{
    [super mouseExited:event];
    self.controlImageSuffix = @"Off";
    [self updateImage];

    [self.torrentTableView hoverEventEndedForView:self];
}

- (void)mouseDown:(NSEvent*)event
{
    //when filterbar is shown, we need to remove focus otherwise action fails
    [self.window makeFirstResponder:self.torrentTableView];

    [super mouseDown:event];
    self.controlImageSuffix = @"On";
    [self updateImage];

    [self.torrentTableView hoverEventEndedForView:self];
}

- (void)updateImage
{
    NSString* symbol = @"play.fill";
    Torrent* torrent = [self.torrentTableView itemAtRow:[self.torrentTableView rowForView:self]];
    if (torrent.active || (torrent.waitingToStart && !(NSApp.currentEvent.modifierFlags & NSEventModifierFlagOption)))
    {
        symbol = @"pause.fill";
    }
    else if (NSApp.currentEvent.modifierFlags & NSEventModifierFlagOption)
    {
        symbol = @"forward.fill";
    }

    NSColor* color = Theme.foreground;
    if ([self.controlImageSuffix isEqualToString:@"Hover"])
    {
        color = Theme.bright;
    }
    else if ([self.controlImageSuffix isEqualToString:@"On"])
    {
        color = Theme.accent;
    }
    self.image = [Theme symbol:symbol color:color size:11.0];
}

- (void)updateTrackingAreas
{
    if (self.fTrackingArea != nil)
    {
        [self removeTrackingArea:self.fTrackingArea];
    }

    NSTrackingAreaOptions opts = (NSTrackingMouseEnteredAndExited | NSTrackingActiveAlways);
    self.fTrackingArea = [[NSTrackingArea alloc] initWithRect:self.bounds options:opts owner:self userInfo:nil];
    [self addTrackingArea:self.fTrackingArea];
}

@end
