// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "FilePriorityCellView.h"
#import "FileListNode.h"
#import "Theme.h"
#import "Torrent.h"

@interface FilePriorityCellView ()
@property(nonatomic, weak) NSButton* button;
@property(nonatomic) NSTrackingArea* trackingArea;
@property(nonatomic) BOOL normal;
@end

@implementation FilePriorityCellView

- (instancetype)initWithFrame:(NSRect)frameRect
{
    if ((self = [super initWithFrame:frameRect]))
    {
        NSButton* button = [Theme iconButton:@"Dots" target:self action:@selector(cyclePriority:)];
        [self addSubview:button];
        _button = button;

        [NSLayoutConstraint activateConstraints:@[
            [button.centerXAnchor constraintEqualToAnchor:self.centerXAnchor],
            [button.centerYAnchor constraintEqualToAnchor:self.centerYAnchor],
            [button.widthAnchor constraintEqualToConstant:24],
            [button.heightAnchor constraintEqualToConstant:24],
        ]];
    }
    return self;
}

- (void)setNode:(FileListNode*)node
{
    _node = node;

    Torrent* torrent = node.torrent;
    NSSet* priorities = [torrent filePrioritiesForIndexes:node.indexes];
    BOOL const high = [priorities containsObject:@(TR_PRI_HIGH)];
    BOOL const low = [priorities containsObject:@(TR_PRI_LOW)];

    NSString* icon = @"Dots";
    NSColor* color = [Theme.foreground colorWithAlphaComponent:0.45];
    NSString* tip = NSLocalizedString(@"Normal priority. Click to change.", "File list -> priority tooltip");
    if (priorities.count > 1)
    {
        tip = NSLocalizedString(@"Mixed priorities. Click to change.", "File list -> priority tooltip");
    }
    else if (high)
    {
        icon = @"Up";
        color = Theme.accent;
        tip = NSLocalizedString(@"High priority. Click to change.", "File list -> priority tooltip");
    }
    else if (low)
    {
        icon = @"Down";
        color = Theme.foreground;
        tip = NSLocalizedString(@"Low priority. Click to change.", "File list -> priority tooltip");
    }

    // Normal is the usual case, so it stays out of sight until the row is hovered.
    self.normal = priorities.count == 1 && !high && !low;
    self.button.alphaValue = self.normal ? 0.0 : 1.0;

    self.button.image = [Theme icon:icon];
    self.button.contentTintColor = color;
    self.button.toolTip = tip;
    self.button.enabled = [torrent canChangeDownloadCheckForFiles:node.indexes];
}

- (void)updateTrackingAreas
{
    [super updateTrackingAreas];
    if (self.trackingArea != nil)
    {
        [self removeTrackingArea:self.trackingArea];
    }
    self.trackingArea = [[NSTrackingArea alloc] initWithRect:NSZeroRect
                                                     options:NSTrackingMouseEnteredAndExited | NSTrackingActiveAlways | NSTrackingInVisibleRect
                                                       owner:self
                                                    userInfo:nil];
    [self addTrackingArea:self.trackingArea];
}

- (void)mouseEntered:(NSEvent*)event
{
    self.button.alphaValue = 1.0;
}

- (void)mouseExited:(NSEvent*)event
{
    self.button.alphaValue = self.normal ? 0.0 : 1.0;
}

- (void)cyclePriority:(id)sender
{
    FileListNode* node = self.node;
    Torrent* torrent = node.torrent;
    NSSet* priorities = [torrent filePrioritiesForIndexes:node.indexes];

    // Normal, then high, then low, and round again.
    tr_priority_t next = TR_PRI_HIGH;
    if (priorities.count == 1)
    {
        next = [priorities containsObject:@(TR_PRI_HIGH)] ? TR_PRI_LOW : [priorities containsObject:@(TR_PRI_LOW)] ? TR_PRI_NORMAL : TR_PRI_HIGH;
    }
    [torrent setFilePriority:next forIndexes:node.indexes];
    self.node = node;
}

@end
