// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#include <libtransmission/transmission.h>
#include <libtransmission/utils.h>

#import "FileNameCellView.h"
#import "FileListNode.h"
#import "NSStringAdditions.h"
#import "Theme.h"
#import "Torrent.h"

@interface FileNameCellView ()<NSTextFieldDelegate>
@property(nonatomic, weak) NSImageView* iconView;
@property(nonatomic, weak) NSTextField* nameField;
@property(nonatomic, weak) NSTextField* statusField;
@property(nonatomic, weak) NSButton* renameButton;
@property(nonatomic) NSTrackingArea* trackingArea;
@property(nonatomic) BOOL renaming;
@end

@implementation FileNameCellView

- (instancetype)initWithFrame:(NSRect)frameRect
{
    if ((self = [super initWithFrame:frameRect]))
    {
        NSImageView* iconView = [[NSImageView alloc] initWithFrame:NSZeroRect];
        iconView.translatesAutoresizingMaskIntoConstraints = NO;
        iconView.imageScaling = NSImageScaleProportionallyDown;
        iconView.contentTintColor = Theme.foreground;
        [self addSubview:iconView];
        _iconView = iconView;

        NSTextField* nameField = [Theme labelWithFont:[NSFont systemFontOfSize:13] color:Theme.bright];
        nameField.lineBreakMode = NSLineBreakByTruncatingMiddle;
        nameField.focusRingType = NSFocusRingTypeNone;
        nameField.delegate = self;
        _nameField = nameField;
        self.textField = nameField;

        NSTextField* statusField = [Theme labelWithFont:[NSFont systemFontOfSize:11] color:Theme.foreground];
        _statusField = statusField;

        NSStackView* text = [NSStackView stackViewWithViews:@[ nameField, statusField ]];
        text.orientation = NSUserInterfaceLayoutOrientationVertical;
        text.alignment = NSLayoutAttributeLeading;
        text.spacing = 1;
        text.translatesAutoresizingMaskIntoConstraints = NO;
        [self addSubview:text];

        NSButton* renameButton = [Theme iconButton:@"Pencil" target:self action:@selector(beginRename)];
        renameButton.toolTip = NSLocalizedString(@"Rename", "File list -> rename button");
        renameButton.hidden = YES;
        [self addSubview:renameButton];
        _renameButton = renameButton;

        [NSLayoutConstraint activateConstraints:@[
            [iconView.leadingAnchor constraintEqualToAnchor:self.leadingAnchor constant:2],
            [iconView.centerYAnchor constraintEqualToAnchor:self.centerYAnchor],
            [iconView.widthAnchor constraintEqualToConstant:18],
            [iconView.heightAnchor constraintEqualToConstant:18],
            [text.leadingAnchor constraintEqualToAnchor:iconView.trailingAnchor constant:10],
            [text.centerYAnchor constraintEqualToAnchor:self.centerYAnchor],
            [text.trailingAnchor constraintEqualToAnchor:renameButton.leadingAnchor constant:-6],
            [nameField.widthAnchor constraintEqualToAnchor:text.widthAnchor],
            [statusField.widthAnchor constraintEqualToAnchor:text.widthAnchor],
            [renameButton.trailingAnchor constraintEqualToAnchor:self.trailingAnchor constant:-4],
            [renameButton.centerYAnchor constraintEqualToAnchor:self.centerYAnchor],
            [renameButton.widthAnchor constraintEqualToConstant:24],
            [renameButton.heightAnchor constraintEqualToConstant:24],
        ]];
    }
    return self;
}

- (void)setNode:(FileListNode*)node
{
    _node = node;
    [self updateDisplay];
}

- (void)updateDisplay
{
    FileListNode* node = self.node;
    if (node == nil || self.renaming)
    {
        return;
    }

    self.iconView.image = [Theme icon:node.isFolder ? @"Folder" : @"File"];
    self.nameField.stringValue = node.name;

    Torrent* torrent = node.torrent;
    CGFloat const progress = [torrent fileProgress:node];
    NSString* size = [NSString stringForFileSize:node.size];
    self.statusField.stringValue = progress > 0.0 ?
        [NSString stringWithFormat:NSLocalizedString(@"%@ of %@", "Inspector -> Files tab -> file status string"),
                                   [NSString percentString:progress longDecimals:YES],
                                   size] :
        size;

    BOOL const wanted = [torrent checkForFiles:node.indexes] != NSControlStateValueOff;
    self.nameField.textColor = wanted ? Theme.bright : Theme.foreground;

    self.toolTip = [torrent fileLocation:node] ?: [node.path stringByAppendingPathComponent:node.name];
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
    self.renameButton.hidden = self.renaming;
}

- (void)mouseExited:(NSEvent*)event
{
    self.renameButton.hidden = YES;
}

- (void)prepareForReuse
{
    [super prepareForReuse];
    self.renameButton.hidden = YES;
}

#pragma mark - Renaming

- (void)beginRename
{
    FileListNode* node = self.node;
    if (node == nil || self.renaming)
    {
        return;
    }

    [self.delegate fileCellWillRename:self];
    self.renaming = YES;
    self.renameButton.hidden = YES;
    self.nameField.editable = YES;
    self.nameField.selectable = YES;
    [self.window makeFirstResponder:self.nameField];

    NSString* name = self.nameField.stringValue;
    NSUInteger const stem = node.isFolder ? name.length : name.stringByDeletingPathExtension.length;
    self.nameField.currentEditor.selectedRange = NSMakeRange(0, stem);
}

- (void)finishRenameWithName:(NSString*)name
{
    if (!self.renaming)
    {
        return;
    }

    self.renaming = NO;
    self.nameField.editable = NO;
    self.nameField.selectable = NO;

    NSString* trimmed = [name stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet];
    BOOL const changed = trimmed.length > 0 && ![trimmed isEqualToString:self.node.name];
    [self.delegate fileCell:self didFinishRenamingTo:changed ? trimmed : nil];
    [self updateDisplay];
}

- (void)controlTextDidEndEditing:(NSNotification*)notification
{
    [self finishRenameWithName:self.nameField.stringValue];
}

- (BOOL)control:(NSControl*)control textView:(NSTextView*)textView doCommandBySelector:(SEL)commandSelector
{
    if (commandSelector == @selector(cancelOperation:))
    {
        [self.nameField abortEditing];
        [self finishRenameWithName:nil];
        return YES;
    }
    return NO;
}

@end
