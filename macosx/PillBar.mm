// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "PillBar.h"
#import "Theme.h"

static CGFloat const kBarHeight = 36.0;
static CGFloat const kInset = 3.0;
static CGFloat const kHorizontalPadding = 14.0;

static BOOL isDarkAppearance()
{
    return [NSAppearance.currentDrawingAppearance bestMatchFromAppearancesWithNames:@[ NSAppearanceNameAqua, NSAppearanceNameDarkAqua ]] ==
        NSAppearanceNameDarkAqua;
}

@interface PillButton : NSButton
@property(nonatomic) BOOL fSelected;
@end

@implementation PillButton

- (NSFont*)pillFont
{
    return [NSFont systemFontOfSize:13 weight:self.fSelected ? NSFontWeightMedium : NSFontWeightRegular];
}

- (NSSize)intrinsicContentSize
{
    NSSize const text = [self.title sizeWithAttributes:@{ NSFontAttributeName : [NSFont systemFontOfSize:13 weight:NSFontWeightMedium] }];
    return NSMakeSize(ceil(text.width) + kHorizontalPadding * 2.0, kBarHeight - kInset * 2.0);
}

- (void)drawRect:(NSRect)dirtyRect
{
    if (self.fSelected)
    {
        BOOL const dark = isDarkAppearance();
        NSBezierPath* pill = [NSBezierPath bezierPathWithRoundedRect:self.bounds xRadius:NSHeight(self.bounds) / 2.0 yRadius:NSHeight(self.bounds) / 2.0];
        NSColor* fill = dark ? [NSColor colorWithSRGBRed:1 green:0.941 blue:0.863 alpha:0.15] : [NSColor colorWithSRGBRed:0.984 green:0.973 blue:0.945 alpha:1];
        [fill setFill];
        [pill fill];
    }

    NSDictionary* attributes = @{
        NSFontAttributeName : self.pillFont,
        NSForegroundColorAttributeName : self.fSelected ? Theme.bright : Theme.foreground,
    };
    NSSize const size = [self.title sizeWithAttributes:attributes];
    [self.title drawAtPoint:NSMakePoint((NSWidth(self.bounds) - size.width) / 2.0, (NSHeight(self.bounds) - size.height) / 2.0) withAttributes:attributes];
}

@end

@interface PillBar ()
@property(nonatomic, weak) id fTarget;
@property(nonatomic) SEL fAction;
@property(nonatomic) NSStackView* fStack;
@end

@implementation PillBar

- (instancetype)initWithLabels:(NSArray<NSString*>*)labels target:(id)target action:(SEL)action
{
    if ((self = [super initWithFrame:NSMakeRect(0, 0, 300, kBarHeight)]))
    {
        _fTarget = target;
        _fAction = action;
        self.translatesAutoresizingMaskIntoConstraints = NO;

        _fStack = [[NSStackView alloc] initWithFrame:NSZeroRect];
        _fStack.spacing = 2;
        _fStack.translatesAutoresizingMaskIntoConstraints = NO;
        [self addSubview:_fStack];
        [NSLayoutConstraint activateConstraints:@[
            [_fStack.leadingAnchor constraintEqualToAnchor:self.leadingAnchor constant:kInset],
            [_fStack.trailingAnchor constraintEqualToAnchor:self.trailingAnchor constant:-kInset],
            [_fStack.centerYAnchor constraintEqualToAnchor:self.centerYAnchor],
            [self.heightAnchor constraintEqualToConstant:kBarHeight],
        ]];
        self.labels = labels;
    }
    return self;
}

- (void)setLabels:(NSArray<NSString*>*)labels
{
    _labels = [labels copy];

    NSArray<__kindof NSView*>* buttons = self.fStack.arrangedSubviews;
    for (NSUInteger index = buttons.count; index > labels.count; --index)
    {
        [buttons[index - 1] removeFromSuperview];
    }
    for (NSUInteger index = 0; index < labels.count; ++index)
    {
        PillButton* button;
        if (index < self.fStack.arrangedSubviews.count)
        {
            button = self.fStack.arrangedSubviews[index];
        }
        else
        {
            button = [PillButton buttonWithTitle:@"" target:self action:@selector(pillClicked:)];
            button.bordered = NO;
            button.tag = index;
            [self.fStack addArrangedSubview:button];
        }
        if (![button.title isEqualToString:labels[index]])
        {
            button.title = labels[index];
            [button invalidateIntrinsicContentSize];
            button.needsDisplay = YES;
        }
    }
    self.selectedSegment = MIN(self.selectedSegment, static_cast<NSInteger>(labels.count) - 1);
}

- (void)setSelectedSegment:(NSInteger)selectedSegment
{
    _selectedSegment = selectedSegment;
    [self.fStack.arrangedSubviews enumerateObjectsUsingBlock:^(__kindof NSView* view, NSUInteger index, BOOL*) {
        PillButton* button = view;
        BOOL const selected = static_cast<NSInteger>(index) == selectedSegment;
        if (button.fSelected != selected)
        {
            button.fSelected = selected;
            button.needsDisplay = YES;
        }
    }];
}

- (void)pillClicked:(NSButton*)sender
{
    self.selectedSegment = sender.tag;
    [NSApp sendAction:self.fAction to:self.fTarget from:self];
}

- (void)drawRect:(NSRect)dirtyRect
{
    BOOL const dark = isDarkAppearance();
    NSColor* track = dark ? [NSColor colorWithSRGBRed:1 green:0.941 blue:0.863 alpha:0.07] : [NSColor colorWithSRGBRed:0.353 green:0.275 blue:0.157 alpha:0.08];
    [track setFill];
    CGFloat const radius = NSHeight(self.bounds) / 2.0;
    [[NSBezierPath bezierPathWithRoundedRect:self.bounds xRadius:radius yRadius:radius] fill];
}

@end
