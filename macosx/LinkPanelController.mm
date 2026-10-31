// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "LinkPanelController.h"
#import "Controller.h"
#import "GlassPanel.h"
#import "Theme.h"

@interface LinkPanelController ()<NSTextFieldDelegate>
@property(nonatomic, weak) Controller* fController;
@property(nonatomic) NSTextField* fField;
@property(nonatomic) NSButton* fAddButton;
@end

@implementation LinkPanelController

- (instancetype)initWithController:(Controller*)controller
{
    GlassPanel* panel = [[GlassPanel alloc] initWithSize:NSMakeSize(560, 150) style:NSGlassEffectViewStyleClear];
    if ((self = [super initWithWindow:panel]))
    {
        _fController = controller;
        panel.closesOnResignKey = YES;

        NSImageView* icon = [NSImageView imageViewWithImage:[Theme icon:@"Link"]];
        icon.contentTintColor = Theme.bright;
        icon.translatesAutoresizingMaskIntoConstraints = NO;

        _fField = [[NSTextField alloc] initWithFrame:NSZeroRect];
        _fField.bordered = NO;
        _fField.drawsBackground = NO;
        _fField.focusRingType = NSFocusRingTypeNone;
        _fField.font = [NSFont systemFontOfSize:20];
        _fField.textColor = Theme.bright;
        _fField.lineBreakMode = NSLineBreakByTruncatingMiddle;
        _fField.placeholderString = NSLocalizedString(@"Paste a magnet or torrent link", "Add link -> field");
        _fField.delegate = self;
        _fField.target = self;
        _fField.action = @selector(add:);
        _fField.translatesAutoresizingMaskIntoConstraints = NO;

        NSButton* cancel = [NSButton buttonWithTitle:NSLocalizedString(@"Cancel", "Add link -> button") target:panel action:@selector(cancelOperation:)];
        cancel.bezelStyle = NSBezelStyleGlass;
        cancel.controlSize = NSControlSizeLarge;
        cancel.translatesAutoresizingMaskIntoConstraints = NO;

        _fAddButton = [NSButton buttonWithTitle:NSLocalizedString(@"Get", "Add link -> button") target:self action:@selector(add:)];
        _fAddButton.bezelStyle = NSBezelStyleGlass;
        _fAddButton.bezelColor = Theme.accent;
        _fAddButton.controlSize = NSControlSizeLarge;
        _fAddButton.translatesAutoresizingMaskIntoConstraints = NO;

        NSView* root = panel.root;
        for (NSView* view in @[ icon, _fField, cancel, _fAddButton ])
        {
            [root addSubview:view];
        }

        [NSLayoutConstraint activateConstraints:@[
            [icon.leadingAnchor constraintEqualToAnchor:root.leadingAnchor constant:26],
            [icon.centerYAnchor constraintEqualToAnchor:_fField.centerYAnchor],
            [icon.widthAnchor constraintEqualToConstant:22],
            [icon.heightAnchor constraintEqualToConstant:22],
            [_fField.leadingAnchor constraintEqualToAnchor:icon.trailingAnchor constant:12],
            [_fField.trailingAnchor constraintEqualToAnchor:root.trailingAnchor constant:-26],
            [_fField.topAnchor constraintEqualToAnchor:root.topAnchor constant:32],
            [_fAddButton.trailingAnchor constraintEqualToAnchor:root.trailingAnchor constant:-22],
            [_fAddButton.bottomAnchor constraintEqualToAnchor:root.bottomAnchor constant:-22],
            [_fAddButton.widthAnchor constraintGreaterThanOrEqualToConstant:76],
            [cancel.trailingAnchor constraintEqualToAnchor:_fAddButton.leadingAnchor constant:-10],
            [cancel.bottomAnchor constraintEqualToAnchor:root.bottomAnchor constant:-22],
        ]];
    }
    return self;
}

- (void)present
{
    GlassPanel* panel = (GlassPanel*)self.window;

    // A copied link is almost always why this was opened.
    NSString* clipboard = [NSPasteboard.generalPasteboard stringForType:NSPasteboardTypeString];
    clipboard = [clipboard stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet];
    BOOL const isLink = [clipboard.lowercaseString hasPrefix:@"magnet:"] || ([clipboard.lowercaseString hasPrefix:@"https://"] && [clipboard.lowercaseString containsString:@".torrent"]);
    self.fField.stringValue = isLink ? clipboard : @"";
    [self updateButton];

    [panel placeOverMainWindow];
    [panel makeKeyAndOrderFront:nil];
    [panel makeFirstResponder:self.fField];
}

- (void)updateButton
{
    self.fAddButton.enabled = self.fField.stringValue.length > 0;
}

- (void)controlTextDidChange:(NSNotification*)notification
{
    [self updateButton];
}

- (void)add:(id)sender
{
    NSString* link = [self.fField.stringValue stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet];
    if (link.length == 0)
    {
        return;
    }
    [self.window orderOut:nil];
    [self.fController openURL:link];
}

@end
