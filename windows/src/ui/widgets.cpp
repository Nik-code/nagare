#include "ui/widgets.h"

#include "ui/widgets_internal.h"

namespace ui
{

namespace
{

using namespace detail;

constexpr float kButtonRadius = 7.0F;
constexpr float kButtonPadding = 14.0F;
constexpr float kButtonIconSize = 16.0F;
constexpr float kButtonIconGap = 6.0F;
constexpr float kDisabledAlpha = 0.55F;
constexpr float kHoverMix = 0.12F;
constexpr float kPressedMix = 0.18F;

constexpr float kSwitchInset = 3.0F;
constexpr float kSwitchThumbWidth = 36.0F;

constexpr float kCheckboxRadius = 5.0F;
constexpr float kCheckboxIcon = 14.0F;
constexpr float kCheckDashWidth = 8.0F;

// PillBar.mm
constexpr float kPillInset = 3.0F;
constexpr float kPillPadding = 14.0F;
constexpr float kPillSpacing = 2.0F;
constexpr TextStyle kPillRegular{ 13.0F, DWRITE_FONT_WEIGHT_NORMAL, false };
constexpr TextStyle kPillMedium{ 13.0F, DWRITE_FONT_WEIGHT_MEDIUM, false };

constexpr float kDropdownRadius = 7.0F;
constexpr float kDropdownLeft = 10.0F;
constexpr float kDropdownChevron = 22.0F;
constexpr float kDropdownMenuGap = 4.0F;
constexpr float kDropdownChevronSize = 7.0F;
constexpr float kDropdownChevronGap = 3.5F;
constexpr TextStyle kDropdownText{ 13.0F, DWRITE_FONT_WEIGHT_NORMAL, false };

constexpr float kChevronStroke = 1.4F;

ID2D1StrokeStyle* roundStroke(ID2D1DeviceContext* context)
{
    static ComPtr<ID2D1StrokeStyle> style;
    if (style == nullptr)
    {
        ComPtr<ID2D1Factory> factory;
        context->GetFactory(&factory);
        D2D1_STROKE_STYLE_PROPERTIES const properties = D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_ROUND);
        factory->CreateStrokeStyle(properties, nullptr, 0, &style);
    }
    return style.Get();
}

D2D1_COLOR_F faded(D2D1_COLOR_F color, bool enabled)
{
    // Theme::icon ignores alpha, so dimming has to happen against the surface.
    return enabled ? color : mix(color, Theme::get().colors().surface, 1.0F - kDisabledAlpha);
}

} // namespace

namespace detail
{

D2D1_COLOR_F controlFill(bool hovered, bool pressed)
{
    if (Theme::get().dark())
    {
        return cream(pressed ? 0.06F : hovered ? 0.15F : 0.10F);
    }
    return withAlpha(Theme::get().colors().field, pressed ? 0.45F : hovered ? 0.95F : 0.72F);
}

D2D1_COLOR_F controlHairline()
{
    return Theme::get().dark() ? cream(0.10F) : hex(0x5a4628, 0.14F);
}

void drawChevron(ID2D1DeviceContext* context, D2D1_POINT_2F centre, float size, Chevron direction, D2D1_COLOR_F color)
{
    float const half = size / 2.0F;
    auto* brush = Theme::get().brush(color);
    auto* stroke = roundStroke(context);
    D2D1_POINT_2F first{};
    D2D1_POINT_2F tip{};
    D2D1_POINT_2F last{};
    switch (direction)
    {
    case Chevron::Up:
        first = D2D1::Point2F(centre.x - half, centre.y + half / 2.0F);
        tip = D2D1::Point2F(centre.x, centre.y - half / 2.0F);
        last = D2D1::Point2F(centre.x + half, centre.y + half / 2.0F);
        break;
    case Chevron::Down:
        first = D2D1::Point2F(centre.x - half, centre.y - half / 2.0F);
        tip = D2D1::Point2F(centre.x, centre.y + half / 2.0F);
        last = D2D1::Point2F(centre.x + half, centre.y - half / 2.0F);
        break;
    case Chevron::Right:
        first = D2D1::Point2F(centre.x - half / 2.0F, centre.y - half);
        tip = D2D1::Point2F(centre.x + half / 2.0F, centre.y);
        last = D2D1::Point2F(centre.x - half / 2.0F, centre.y + half);
        break;
    }
    context->DrawLine(first, tip, brush, kChevronStroke, stroke);
    context->DrawLine(tip, last, brush, kChevronStroke, stroke);
}

} // namespace detail

D2D1_COLOR_F roleColor(ColorRole role)
{
    Palette const& colors = Theme::get().colors();
    switch (role)
    {
    case ColorRole::Bright:
        return colors.bright;
    case ColorRole::Muted:
        return withAlpha(colors.foreground, 0.72F);
    case ColorRole::Accent:
        return colors.accent;
    case ColorRole::Seed:
        return colors.seed;
    case ColorRole::Danger:
        return colors.danger;
    case ColorRole::Foreground:
        break;
    }
    return colors.foreground;
}

Animator::~Animator()
{
    cancel();
}

void Animator::run(DWORD durationMs, std::function<void(float)> step, std::function<void()> done)
{
    cancel();
    id_ = Window::get().animate(
        durationMs,
        std::move(step),
        [this, finished = std::move(done)]
        {
            id_ = 0;
            if (finished)
            {
                finished();
            }
        });
}

void Animator::cancel()
{
    if (id_ != 0)
    {
        Window::get().cancel(id_);
        id_ = 0;
    }
}

Label::Label(std::wstring text, TextStyle style, ColorRole role)
    : text_(std::move(text))
    , style_(style)
    , role_(role)
{
}

void Label::setText(std::wstring_view text)
{
    if (text != text_)
    {
        text_ = text;
        invalidate();
    }
}

void Label::setTextStyle(TextStyle style)
{
    if (style != style_)
    {
        style_ = style;
        invalidate();
    }
}

void Label::setRole(ColorRole role)
{
    if (role != role_)
    {
        role_ = role;
        invalidate();
    }
}

void Label::setAlignment(DWRITE_TEXT_ALIGNMENT alignment)
{
    alignment_ = alignment;
    invalidate();
}

void Label::setVerticalAlignment(DWRITE_PARAGRAPH_ALIGNMENT alignment)
{
    vertical_ = alignment;
    invalidate();
}

void Label::setTrim(Trim trim)
{
    trim_ = trim;
    invalidate();
}

float Label::preferredWidth() const
{
    return std::ceil(Theme::get().measure(text_, style_).width);
}

float Label::preferredHeight() const
{
    return std::ceil(Theme::get().measure(L"Ag", style_).height);
}

void Label::draw(ID2D1DeviceContext* context)
{
    Theme::get().text(context, text_, style_, bounds(), roleColor(role_), alignment_, trim_, vertical_);
}

Control::Control()
{
    interactive_ = true;
    takesFocus_ = false;
}

void Control::setEnabled(bool enabled)
{
    if (enabled != enabled_)
    {
        enabled_ = enabled;
        pressed_ = false;
        tracking_ = false;
        invalidate();
    }
}

bool Control::onMouse(MouseEvent const& event)
{
    auto const setState = [this](bool& state, bool value)
    {
        if (state != value)
        {
            state = value;
            invalidate();
        }
    };

    switch (event.kind)
    {
    case MouseKind::Enter:
        setState(hovered_, true);
        return false;
    case MouseKind::Leave:
        setState(hovered_, false);
        return false;
    case MouseKind::Move:
        if (tracking_)
        {
            setState(pressed_, contains(bounds(), event.point));
        }
        return false;
    case MouseKind::Down:
    case MouseKind::DoubleClick:
        if (enabled_)
        {
            tracking_ = true;
            setState(pressed_, true);
        }
        return true;
    case MouseKind::Up:
    {
        bool const clicked = tracking_ && pressed_ && enabled_;
        tracking_ = false;
        setState(pressed_, false);
        if (clicked)
        {
            activate();
        }
        return true;
    }
    default:
        return false;
    }
}

Button::Button(std::wstring title, ButtonStyle style, std::function<void()> onClick)
    : onClick(std::move(onClick))
    , title_(std::move(title))
    , style_(style)
    , capsule_(style == ButtonStyle::Accent)
{
}

void Button::setTitle(std::wstring_view title)
{
    if (title != title_)
    {
        title_ = title;
        titleWidth_ = -1.0F;
        invalidate();
    }
}

void Button::setStyle(ButtonStyle style)
{
    style_ = style;
    invalidate();
}

void Button::setIcon(std::wstring name)
{
    icon_ = std::move(name);
    invalidate();
}

void Button::setCapsule(bool capsule)
{
    capsule_ = capsule;
    invalidate();
}

float Button::titleWidth() const
{
    if (titleWidth_ < 0.0F)
    {
        titleWidth_ = title_.empty() ? 0.0F : std::ceil(Theme::get().measure(title_, kPillRegular).width);
    }
    return titleWidth_;
}

float Button::preferredWidth() const
{
    float const icon = icon_.empty() ? 0.0F : kButtonIconSize + (title_.empty() ? 0.0F : kButtonIconGap);
    return titleWidth() + icon + kButtonPadding * 2.0F;
}

void Button::activate()
{
    if (onClick)
    {
        onClick();
    }
}

void Button::draw(ID2D1DeviceContext* context)
{
    Theme& theme = Theme::get();
    Palette const& colors = theme.colors();
    float const radius = capsule_ ? height() / 2.0F : kButtonRadius;
    bool const active = enabled();

    D2D1_COLOR_F content = colors.bright;
    switch (style_)
    {
    case ButtonStyle::Glass:
        fillRounded(context, bounds(), radius, active ? controlFill(isHovered(), isPressed()) : controlFill(false, true));
        strokeRounded(context, bounds(), radius, controlHairline());
        break;
    case ButtonStyle::Accent:
    {
        D2D1_COLOR_F fill = colors.accent;
        if (isPressed())
        {
            fill = mix(fill, colors.surface, kPressedMix);
        }
        else if (isHovered())
        {
            fill = mix(fill, colors.bright, kHoverMix);
        }
        fillRounded(context, bounds(), radius, active ? fill : withAlpha(fill, 0.4F));
        content = colors.field;
        break;
    }
    case ButtonStyle::Plain:
        if (active && (isHovered() || isPressed()))
        {
            fillRounded(context, bounds(), radius, controlFill(true, isPressed()));
        }
        break;
    }
    if (!active && style_ != ButtonStyle::Accent)
    {
        content = faded(content, false);
    }

    float const iconWidth = icon_.empty() ? 0.0F : kButtonIconSize + (title_.empty() ? 0.0F : kButtonIconGap);
    float left = (width() - titleWidth() - iconWidth) / 2.0F;
    if (!icon_.empty())
    {
        float const top = (height() - kButtonIconSize) / 2.0F;
        theme.icon(context, icon_, D2D1::RectF(left, top, left + kButtonIconSize, top + kButtonIconSize), content);
        left += iconWidth;
    }
    theme.text(context, title_, kPillRegular, D2D1::RectF(std::max(0.0F, left), 0.0F, width(), height()), content, DWRITE_TEXT_ALIGNMENT_LEADING, Trim::End, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
}

IconButton::IconButton(std::wstring name, std::function<void()> onClick)
    : onClick(std::move(onClick))
    , name_(std::move(name))
{
}

void IconButton::setIcon(std::wstring name)
{
    name_ = std::move(name);
    invalidate();
}

void IconButton::setIconSize(float size)
{
    iconSize_ = size;
    invalidate();
}

void IconButton::setRole(ColorRole role)
{
    role_ = role;
    invalidate();
}

void IconButton::activate()
{
    if (onClick)
    {
        onClick();
    }
}

void IconButton::draw(ID2D1DeviceContext* context)
{
    Theme& theme = Theme::get();
    bool const active = enabled();
    if (active && (isHovered() || isPressed()))
    {
        float const radius = std::min(width(), height()) / 2.0F;
        fillRounded(context, bounds(), radius, withAlpha(theme.colors().bright, isPressed() ? 0.06F : 0.10F));
    }
    float const left = (width() - iconSize_) / 2.0F;
    float const top = (height() - iconSize_) / 2.0F;
    D2D1_COLOR_F const color = active && isHovered() ? theme.colors().bright : roleColor(role_);
    theme.icon(context, name_, D2D1::RectF(left, top, left + iconSize_, top + iconSize_), faded(color, active));
}

void Switch::setOn(bool on, bool animated)
{
    if (on == on_)
    {
        return;
    }
    on_ = on;
    float const to = on ? 1.0F : 0.0F;
    if (!animated)
    {
        slide_.cancel();
        position_ = to;
        invalidate();
        return;
    }
    float const from = position_;
    slide_.run(
        kShortAnimationMs,
        [this, from, to](float t)
        {
            position_ = lerp(from, to, easeOut(t));
            invalidate();
        });
}

void Switch::activate()
{
    setOn(!on_, true);
    if (onChange)
    {
        onChange(on_);
    }
}

void Switch::draw(ID2D1DeviceContext* context)
{
    Theme& theme = Theme::get();
    Palette const& colors = theme.colors();
    float const radius = height() / 2.0F;

    D2D1_COLOR_F const off = withAlpha(colors.foreground, theme.dark() ? 0.22F : 0.16F);
    D2D1_COLOR_F track = mix(off, colors.accent, position_);
    if (!enabled())
    {
        track.a *= 0.5F;
    }
    fillRounded(context, bounds(), radius, track);

    float const thumbHeight = height() - kSwitchInset * 2.0F;
    float const travel = width() - kSwitchInset * 2.0F - kSwitchThumbWidth;
    float const left = kSwitchInset + travel * position_;
    D2D1_RECT_F const thumb = D2D1::RectF(left, kSwitchInset, left + kSwitchThumbWidth, kSwitchInset + thumbHeight);
    D2D1_COLOR_F const shade = hex(0x000000, 0.12F);
    fillRounded(context, D2D1::RectF(thumb.left, thumb.top + 0.5F, thumb.right, thumb.bottom + 1.0F), thumbHeight / 2.0F, shade);
    fillRounded(context, thumb, thumbHeight / 2.0F, faded(hex(0xffffff), enabled()));
}

void Checkbox::setState(CheckState state)
{
    if (state != state_)
    {
        state_ = state;
        invalidate();
    }
}

void Checkbox::activate()
{
    setState(state_ == CheckState::On ? CheckState::Off : CheckState::On);
    if (onChange)
    {
        onChange(state_);
    }
}

void Checkbox::draw(ID2D1DeviceContext* context)
{
    D2D1_RECT_F const box = D2D1::RectF((width() - kSize) / 2.0F, (height() - kSize) / 2.0F, (width() + kSize) / 2.0F, (height() + kSize) / 2.0F);
    paint(context, box, state_, enabled(), isHovered(), isPressed());
}

void Checkbox::paint(ID2D1DeviceContext* context, D2D1_RECT_F box, CheckState state, bool enabled, bool hovered, bool pressed)
{
    Theme& theme = Theme::get();
    Palette const& colors = theme.colors();

    if (state == CheckState::Off)
    {
        fillRounded(context, box, kCheckboxRadius, enabled ? controlFill(hovered, pressed) : controlFill(false, true));
        strokeRounded(context, box, kCheckboxRadius, withAlpha(colors.foreground, 0.45F));
        return;
    }

    // Disabled checks are the grey ones in the details window's file list.
    D2D1_COLOR_F fill = enabled ? colors.accent : withAlpha(colors.foreground, 0.35F);
    if (enabled && pressed)
    {
        fill = mix(fill, colors.surface, kPressedMix);
    }
    else if (enabled && hovered)
    {
        fill = mix(fill, colors.bright, kHoverMix);
    }
    fillRounded(context, box, kCheckboxRadius, fill);

    D2D1_COLOR_F const mark = enabled ? colors.field : colors.bright;
    float const cx = (box.left + box.right) / 2.0F;
    float const cy = (box.top + box.bottom) / 2.0F;
    if (state == CheckState::Mixed)
    {
        float const half = kCheckDashWidth / 2.0F;
        context->DrawLine(D2D1::Point2F(cx - half, cy), D2D1::Point2F(cx + half, cy), theme.brush(mark), 2.0F, roundStroke(context));
        return;
    }
    float const half = kCheckboxIcon / 2.0F;
    theme.icon(context, L"Check", D2D1::RectF(cx - half, cy - half, cx + half, cy + half), mark);
}

PillBar::PillBar()
{
    interactive_ = true;
    takesFocus_ = false;
}

void PillBar::measureItem(Item& item) const
{
    item.text = item.pill.label;
    if (item.pill.count >= 0)
    {
        item.text += L' ';
        item.text += std::to_wstring(item.pill.count);
    }
    // Always the medium weight, so a pill doesn't change width when it's selected.
    item.width = std::ceil(Theme::get().measure(item.text, kPillMedium).width) + kPillPadding * 2.0F;
}

void PillBar::setPills(std::vector<Pill> pills)
{
    items_.clear();
    items_.reserve(pills.size());
    for (Pill& pill : pills)
    {
        Item item;
        item.pill = std::move(pill);
        measureItem(item);
        items_.push_back(std::move(item));
    }
    selected_ = std::clamp(selected_, 0, std::max(0, static_cast<int>(items_.size()) - 1));
    slideTo(selected_, false);
}

void PillBar::setCount(int index, int count)
{
    if (index < 0 || index >= static_cast<int>(items_.size()) || items_[index].pill.count == count)
    {
        return;
    }
    items_[index].pill.count = count;
    measureItem(items_[index]);
    slideTo(selected_, false);
}

void PillBar::setSelected(int index, bool animated)
{
    if (index < 0 || index >= static_cast<int>(items_.size()) || index == selected_)
    {
        return;
    }
    selected_ = index;
    slideTo(index, animated);
}

float PillBar::preferredWidth() const
{
    float total = kPillInset * 2.0F;
    for (size_t i = 0; i < items_.size(); ++i)
    {
        total += items_[i].width + (i > 0 ? kPillSpacing : 0.0F);
    }
    return total;
}

D2D1_RECT_F PillBar::pillRect(int index) const
{
    float left = kPillInset;
    for (int i = 0; i < index && i < static_cast<int>(items_.size()); ++i)
    {
        left += items_[i].width + kPillSpacing;
    }
    float const pillWidth = index >= 0 && index < static_cast<int>(items_.size()) ? items_[index].width : 0.0F;
    return D2D1::RectF(left, kPillInset, left + pillWidth, height() - kPillInset);
}

void PillBar::slideTo(int index, bool animated)
{
    D2D1_RECT_F const rect = pillRect(index);
    if (!animated)
    {
        slide_.cancel();
        highlightLeft_ = rect.left;
        highlightRight_ = rect.right;
        invalidate();
        return;
    }
    float const fromLeft = highlightLeft_;
    float const fromRight = highlightRight_;
    float const toLeft = rect.left;
    float const toRight = rect.right;
    slide_.run(
        kShortAnimationMs + 30,
        [this, fromLeft, fromRight, toLeft, toRight](float t)
        {
            float const eased = easeOut(t);
            highlightLeft_ = lerp(fromLeft, toLeft, eased);
            highlightRight_ = lerp(fromRight, toRight, eased);
            invalidate();
        });
}

int PillBar::pillAt(D2D1_POINT_2F point) const
{
    for (int i = 0; i < static_cast<int>(items_.size()); ++i)
    {
        D2D1_RECT_F const rect = pillRect(i);
        if (point.x >= rect.left && point.x < rect.right + kPillSpacing)
        {
            return i;
        }
    }
    return -1;
}

bool PillBar::onMouse(MouseEvent const& event)
{
    auto const setHover = [this](int index)
    {
        if (index != hovered_)
        {
            hovered_ = index;
            invalidate();
        }
    };

    switch (event.kind)
    {
    case MouseKind::Enter:
    case MouseKind::Move:
        setHover(pillAt(event.point));
        return false;
    case MouseKind::Leave:
        setHover(-1);
        return false;
    case MouseKind::Down:
    case MouseKind::DoubleClick:
        pressed_ = pillAt(event.point);
        return true;
    case MouseKind::Up:
    {
        int const index = pillAt(event.point);
        bool const clicked = index >= 0 && index == pressed_ && index != selected_;
        pressed_ = -1;
        if (clicked)
        {
            setSelected(index, true);
            if (onChange)
            {
                onChange(index);
            }
        }
        return true;
    }
    default:
        return false;
    }
}

void PillBar::draw(ID2D1DeviceContext* context)
{
    Theme& theme = Theme::get();
    Palette const& colors = theme.colors();
    float const radius = height() / 2.0F;

    fillRounded(context, bounds(), radius, theme.dark() ? cream(0.07F) : hex(0x5a4628, 0.08F));

    if (!items_.empty())
    {
        float const pillRadius = (height() - kPillInset * 2.0F) / 2.0F;
        D2D1_RECT_F const highlight = D2D1::RectF(highlightLeft_, kPillInset, highlightRight_, height() - kPillInset);
        fillRounded(context, highlight, pillRadius, theme.dark() ? cream(0.15F) : colors.field);
    }

    for (int i = 0; i < static_cast<int>(items_.size()); ++i)
    {
        bool const selected = i == selected_;
        theme.text(
            context,
            items_[i].text,
            selected ? kPillMedium : kPillRegular,
            pillRect(i),
            selected || i == hovered_ ? colors.bright : colors.foreground,
            DWRITE_TEXT_ALIGNMENT_CENTER,
            Trim::None,
            DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }
}

Dropdown::Dropdown(std::vector<std::wstring> items, int selected)
    : items_(std::move(items))
    , selected_(selected)
{
}

void Dropdown::setItems(std::vector<std::wstring> items)
{
    items_ = std::move(items);
    selected_ = std::clamp(selected_, 0, std::max(0, static_cast<int>(items_.size()) - 1));
    invalidate();
}

void Dropdown::setSelected(int index)
{
    if (index >= 0 && index < static_cast<int>(items_.size()) && index != selected_)
    {
        selected_ = index;
        invalidate();
    }
}

std::wstring const& Dropdown::title() const
{
    static std::wstring const empty;
    return selected_ < static_cast<int>(items_.size()) ? items_[selected_] : empty;
}

float Dropdown::preferredWidth() const
{
    float widest = 0.0F;
    for (auto const& item : items_)
    {
        widest = std::max(widest, Theme::get().measure(item, kDropdownText).width);
    }
    return std::ceil(widest) + kDropdownLeft + kDropdownChevron;
}

void Dropdown::activate()
{
    std::vector<MenuItem> menu;
    menu.reserve(items_.size());
    for (int i = 0; i < static_cast<int>(items_.size()); ++i)
    {
        MenuItem item;
        item.title = items_[i];
        item.checked = i == selected_;
        item.action = [this, i]
        {
            if (i != selected_)
            {
                setSelected(i);
                if (onChange)
                {
                    onChange(i);
                }
            }
        };
        menu.push_back(std::move(item));
    }
    auto const origin = absoluteOrigin();
    PopupMenu::show(std::move(menu), D2D1::Point2F(origin.x, origin.y + height() + kDropdownMenuGap), width());
}

void Dropdown::draw(ID2D1DeviceContext* context)
{
    Theme& theme = Theme::get();
    bool const active = enabled();
    fillRounded(context, bounds(), kDropdownRadius, active ? controlFill(isHovered(), isPressed()) : controlFill(false, true));
    strokeRounded(context, bounds(), kDropdownRadius, controlHairline());

    D2D1_COLOR_F const color = faded(theme.colors().bright, active);
    D2D1_RECT_F const textRect = D2D1::RectF(kDropdownLeft, 0.0F, width() - kDropdownChevron, height());
    theme.text(context, title(), kDropdownText, textRect, color, DWRITE_TEXT_ALIGNMENT_LEADING, Trim::End, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    float const cx = width() - kDropdownChevron / 2.0F;
    float const cy = height() / 2.0F;
    drawChevron(context, D2D1::Point2F(cx, cy - kDropdownChevronGap), kDropdownChevronSize, Chevron::Up, color);
    drawChevron(context, D2D1::Point2F(cx, cy + kDropdownChevronGap), kDropdownChevronSize, Chevron::Down, color);
}

} // namespace ui
