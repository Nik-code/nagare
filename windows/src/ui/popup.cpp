#include "ui/widgets.h"
#include "ui/widgets_internal.h"

namespace ui
{

namespace
{

using namespace detail;

constexpr float kRadius = 12.0F;
constexpr float kPadding = 6.0F;
constexpr float kRowHeight = 30.0F;
constexpr float kSeparatorHeight = 9.0F;
constexpr float kHighlightRadius = 8.0F;
constexpr float kTextLeft = 12.0F;
constexpr float kLeadingColumn = 26.0F;
constexpr float kTextRight = 16.0F;
constexpr float kSubmenuColumn = 16.0F;
constexpr float kIconSize = 16.0F;
constexpr float kCheckSize = 14.0F;
constexpr float kChevronSize = 8.0F;
constexpr float kChevronRight = 11.0F;
constexpr float kWindowMargin = 8.0F;
constexpr float kSubmenuOverlap = 4.0F;
constexpr TextStyle kText{ 13.0F, DWRITE_FONT_WEIGHT_NORMAL, false };

constexpr float kShadowPad = 28.0F;
constexpr float kShadowBlur = 10.0F;
constexpr float kShadowDrop = 6.0F;
constexpr float kShadowAlpha = 0.34F;

} // namespace

MenuItem MenuItem::line()
{
    MenuItem item;
    item.separator = true;
    return item;
}

PopupMenu::PopupMenu(std::vector<MenuItem> items, D2D1_POINT_2F anchor, float minWidth)
    : items_(std::move(items))
    , anchor_(anchor)
{
    interactive_ = true;
    takesFocus_ = false;
    floating_ = true;

    Theme& theme = Theme::get();
    float widest = 0.0F;
    bool hasSubmenu = false;
    float menuHeight = kPadding * 2.0F;
    for (MenuItem const& item : items_)
    {
        menuHeight += item.separator ? kSeparatorHeight : kRowHeight;
        if (!item.separator)
        {
            widest = std::max(widest, theme.measure(item.title, kText).width);
            hasLeadingColumn_ = hasLeadingColumn_ || item.checked || !item.icon.empty();
            hasSubmenu = hasSubmenu || !item.submenu.empty();
        }
    }
    float const menuWidth = std::max(minWidth, std::ceil(widest) + kPadding * 2.0F + kTextLeft + kTextRight + (hasLeadingColumn_ ? kLeadingColumn : 0.0F) + (hasSubmenu ? kSubmenuColumn : 0.0F));

    float const windowWidth = Window::get().width();
    float const windowHeight = Window::get().height();
    float const left = std::clamp(anchor.x, kWindowMargin, std::max(kWindowMargin, windowWidth - menuWidth - kWindowMargin));
    float top = anchor.y;
    if (top + menuHeight > windowHeight - kWindowMargin)
    {
        float const above = anchor.y - menuHeight;
        top = above >= kWindowMargin ? above : std::max(kWindowMargin, windowHeight - menuHeight - kWindowMargin);
    }
    setFrame(D2D1::RectF(left, top, left + menuWidth, top + menuHeight));
}

PopupMenu* PopupMenu::show(std::vector<MenuItem> items, D2D1_POINT_2F anchor, float minWidth)
{
    auto menu = std::make_unique<PopupMenu>(std::move(items), anchor, minWidth);
    PopupMenu* raw = menu.get();
    Window::get().setPopup(std::move(menu));
    return raw;
}

float PopupMenu::rowTop(int index) const
{
    float top = kPadding;
    for (int i = 0; i < index; ++i)
    {
        top += items_[static_cast<size_t>(i)].separator ? kSeparatorHeight : kRowHeight;
    }
    return top;
}

float PopupMenu::rowHeight(int index) const
{
    return items_[static_cast<size_t>(index)].separator ? kSeparatorHeight : kRowHeight;
}

int PopupMenu::rowAt(D2D1_POINT_2F point) const
{
    if (point.x < 0.0F || point.x >= width())
    {
        return -1;
    }
    float top = kPadding;
    for (int i = 0; i < static_cast<int>(items_.size()); ++i)
    {
        float const bottom = top + rowHeight(i);
        if (point.y >= top && point.y < bottom)
        {
            return i;
        }
        top = bottom;
    }
    return -1;
}

bool PopupMenu::selectable(int index) const
{
    if (index < 0 || index >= static_cast<int>(items_.size()))
    {
        return false;
    }
    MenuItem const& item = items_[static_cast<size_t>(index)];
    return !item.separator && item.enabled;
}

void PopupMenu::highlight(int index)
{
    if (index != highlight_)
    {
        highlight_ = index;
        invalidate();
    }
}

void PopupMenu::step(int direction)
{
    int const count = static_cast<int>(items_.size());
    int index = highlight_;
    if (index < 0)
    {
        index = direction > 0 ? -1 : count;
    }
    for (int tried = 0; tried < count; ++tried)
    {
        index = (index + direction + count) % count;
        if (selectable(index))
        {
            highlight(index);
            return;
        }
    }
}

void PopupMenu::activate(int index)
{
    if (!selectable(index))
    {
        return;
    }
    MenuItem const& item = items_[static_cast<size_t>(index)];
    if (!item.submenu.empty())
    {
        openSubmenu(index, true);
        return;
    }
    auto action = item.action;
    Window::get().setPopup(nullptr);
    if (action)
    {
        action();
    }
}

void PopupMenu::openSubmenu(int index, bool highlightFirst)
{
    if (sub_ == nullptr || subIndex_ != index)
    {
        closeSubmenu();
        auto const origin = absoluteOrigin();
        D2D1_POINT_2F const anchor = D2D1::Point2F(origin.x + width() - kSubmenuOverlap, origin.y + rowTop(index) - kPadding);
        auto menu = std::make_unique<PopupMenu>(items_[static_cast<size_t>(index)].submenu, anchor, 0.0F);

        // The constructor places it in window coordinates; a child's frame is relative to this menu.
        D2D1_RECT_F const placed = menu->frame();
        float const menuWidth = placed.right - placed.left;
        float const left = anchor.x + menuWidth <= Window::get().width() - kWindowMargin ? anchor.x : origin.x - menuWidth + kSubmenuOverlap;
        menu->setFrame(D2D1::RectF(left - origin.x, placed.top - origin.y, left - origin.x + menuWidth, placed.bottom - origin.y));

        sub_ = menu.get();
        subIndex_ = index;
        addChild(std::move(menu));
    }
    if (highlightFirst)
    {
        sub_->step(1);
    }
}

void PopupMenu::closeSubmenu()
{
    if (sub_ != nullptr)
    {
        PopupMenu* closing = sub_;
        sub_ = nullptr;
        subIndex_ = -1;
        removeChild(closing);
        invalidate();
    }
}

View* PopupMenu::hitTest(D2D1_POINT_2F point)
{
    // A submenu hangs outside this view's bounds, which the base test would reject.
    if (sub_ != nullptr)
    {
        auto const& frame = sub_->frame();
        if (View* hit = sub_->hitTest(D2D1::Point2F(point.x - frame.left, point.y - frame.top)))
        {
            return hit;
        }
    }
    return View::hitTest(point);
}

bool PopupMenu::onMouse(MouseEvent const& event)
{
    switch (event.kind)
    {
    case MouseKind::Enter:
    case MouseKind::Move:
    {
        int const row = rowAt(event.point);
        highlight(selectable(row) ? row : -1);
        if (selectable(row) && !items_[static_cast<size_t>(row)].submenu.empty())
        {
            openSubmenu(row, false);
        }
        else if (row >= 0)
        {
            closeSubmenu();
        }
        return true;
    }
    case MouseKind::Leave:
        if (sub_ == nullptr)
        {
            highlight(-1);
        }
        return true;
    case MouseKind::Up:
    case MouseKind::RightClick:
        activate(rowAt(event.point));
        return true;
    default:
        return true;
    }
}

bool PopupMenu::onKey(UINT key, bool down)
{
    if (!down)
    {
        return true;
    }
    if (sub_ != nullptr && sub_->highlighted() >= 0)
    {
        if (key == VK_LEFT || key == VK_ESCAPE)
        {
            closeSubmenu();
            return true;
        }
        return sub_->onKey(key, down);
    }

    switch (key)
    {
    case VK_DOWN:
        step(1);
        return true;
    case VK_UP:
        step(-1);
        return true;
    case VK_HOME:
        highlight(-1);
        step(1);
        return true;
    case VK_END:
        highlight(-1);
        step(-1);
        return true;
    case VK_RIGHT:
        if (selectable(highlight_) && !items_[static_cast<size_t>(highlight_)].submenu.empty())
        {
            openSubmenu(highlight_, true);
        }
        return true;
    case VK_RETURN:
    case VK_SPACE:
        activate(highlight_);
        return true;
    case VK_ESCAPE:
        Window::get().setPopup(nullptr);
        return true;
    default:
        return true;
    }
}

void PopupMenu::buildShadow(ID2D1DeviceContext* context)
{
    // Blurring once into a small bitmap means the open menu costs one bitmap draw per frame.
    D2D1_SIZE_F const size = D2D1::SizeF(width() + kShadowPad * 2.0F, height() + kShadowPad * 2.0F);
    D2D1_PIXEL_FORMAT const format = D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED);

    ComPtr<ID2D1BitmapRenderTarget> shapeTarget;
    if (FAILED(context->CreateCompatibleRenderTarget(&size, nullptr, &format, D2D1_COMPATIBLE_RENDER_TARGET_OPTIONS_NONE, &shapeTarget)))
    {
        return;
    }
    ComPtr<ID2D1SolidColorBrush> solid;
    shapeTarget->CreateSolidColorBrush(hex(0x000000), &solid);
    shapeTarget->BeginDraw();
    shapeTarget->Clear(D2D1::ColorF(0.0F, 0.0F, 0.0F, 0.0F));
    shapeTarget->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(kShadowPad, kShadowPad, kShadowPad + width(), kShadowPad + height()), kRadius, kRadius), solid.Get());
    shapeTarget->EndDraw();
    ComPtr<ID2D1Bitmap> shape;
    shapeTarget->GetBitmap(&shape);

    ComPtr<ID2D1BitmapRenderTarget> blurTarget;
    if (FAILED(context->CreateCompatibleRenderTarget(&size, nullptr, &format, D2D1_COMPATIBLE_RENDER_TARGET_OPTIONS_NONE, &blurTarget)))
    {
        return;
    }
    ComPtr<ID2D1DeviceContext> blurContext;
    if (FAILED(blurTarget.As(&blurContext)))
    {
        return;
    }
    ComPtr<ID2D1Effect> effect;
    if (FAILED(blurContext->CreateEffect(CLSID_D2D1Shadow, &effect)))
    {
        return;
    }
    effect->SetInput(0, shape.Get());
    effect->SetValue(D2D1_SHADOW_PROP_BLUR_STANDARD_DEVIATION, kShadowBlur);
    effect->SetValue(D2D1_SHADOW_PROP_COLOR, D2D1::Vector4F(0.0F, 0.0F, 0.0F, kShadowAlpha));

    blurContext->BeginDraw();
    blurContext->Clear(D2D1::ColorF(0.0F, 0.0F, 0.0F, 0.0F));
    blurContext->DrawImage(effect.Get());
    blurContext->EndDraw();
    blurTarget->GetBitmap(&shadow_);
}

void PopupMenu::draw(ID2D1DeviceContext* context)
{
    Theme& theme = Theme::get();
    Palette const& colors = theme.colors();

    if (shadow_ == nullptr)
    {
        buildShadow(context);
    }
    if (shadow_ != nullptr)
    {
        D2D1_RECT_F const area = D2D1::RectF(-kShadowPad, -kShadowPad + kShadowDrop, width() + kShadowPad, height() + kShadowPad + kShadowDrop);
        context->DrawBitmap(shadow_.Get(), area);
    }

    fillRounded(context, bounds(), kRadius, colors.surface);
    strokeRounded(context, bounds(), kRadius, colors.border);

    float const textLeft = kPadding + kTextLeft + (hasLeadingColumn_ ? kLeadingColumn : 0.0F);
    float top = kPadding;
    for (int i = 0; i < static_cast<int>(items_.size()); ++i)
    {
        MenuItem const& item = items_[static_cast<size_t>(i)];
        float const rowBottom = top + rowHeight(i);
        if (item.separator)
        {
            float const y = snapToPixel((top + rowBottom) / 2.0F);
            context->FillRectangle(D2D1::RectF(kPadding + kTextLeft, y, width() - kPadding - kTextLeft, y + hairline()), theme.brush(colors.border));
            top = rowBottom;
            continue;
        }

        bool const lit = i == highlight_ || i == subIndex_;
        if (lit)
        {
            fillRounded(context, D2D1::RectF(kPadding, top, width() - kPadding, rowBottom), kHighlightRadius, colors.selected);
        }
        D2D1_COLOR_F const color = item.enabled ? colors.bright : mix(colors.foreground, colors.surface, 0.45F);
        float const cy = (top + rowBottom) / 2.0F;
        float const iconLeft = kPadding + kTextLeft;
        if (item.checked)
        {
            float const half = kCheckSize / 2.0F;
            float const cx = iconLeft + half;
            theme.icon(context, L"Check", D2D1::RectF(cx - half, cy - half, cx + half, cy + half), item.enabled ? colors.accent : color);
        }
        else if (!item.icon.empty())
        {
            float const half = kIconSize / 2.0F;
            theme.icon(context, item.icon, D2D1::RectF(iconLeft, cy - half, iconLeft + kIconSize, cy + half), color);
        }
        theme.text(context, item.title, kText, D2D1::RectF(textLeft, top, width() - kPadding - kTextRight, rowBottom), color, DWRITE_TEXT_ALIGNMENT_LEADING, Trim::End, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        if (!item.submenu.empty())
        {
            drawChevron(context, D2D1::Point2F(width() - kPadding - kChevronRight, cy), kChevronSize, Chevron::Right, color);
        }
        top = rowBottom;
    }
}

} // namespace ui
