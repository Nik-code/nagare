#include "ui/widgets.h"
#include "ui/widgets_internal.h"

namespace ui
{

namespace
{

using namespace detail;

constexpr float kBarWidth = 12.0F;
constexpr float kThumbWidth = 6.0F;
constexpr float kThumbHoverWidth = 8.0F;
constexpr float kThumbRight = 3.0F;
constexpr float kThumbMinHeight = 32.0F;
constexpr float kThumbInset = 3.0F;
constexpr DWORD kFadeInMs = 120;
constexpr DWORD kFadeOutMs = 240;
constexpr DWORD kHideDelayMs = 900;

constexpr DWORD kWheelEaseMs = 140;
constexpr float kPixelsPerLine = 16.0F;

constexpr float kRowRadius = 12.0F; // ThemedRowView
constexpr float kRowSelectionInsetY = 3.0F;

float wheelStep(float viewport)
{
    UINT lines = 3;
    SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0);
    return lines == WHEEL_PAGESCROLL ? viewport : static_cast<float>(lines) * kPixelsPerLine;
}

} // namespace

ScrollBar::ScrollBar()
{
    interactive_ = true;
    takesFocus_ = false;
}

ScrollBar::~ScrollBar()
{
    if (hideTimer_ != 0)
    {
        Window::get().cancel(hideTimer_);
    }
}

void ScrollBar::setRange(float offset, float viewport, float content)
{
    if (offset == offset_ && viewport == viewport_ && content == content_)
    {
        return;
    }
    bool const showing = alpha_ > 0.0F;
    if (showing)
    {
        invalidateThumb();
    }
    offset_ = offset;
    viewport_ = viewport;
    content_ = content;
    if (showing)
    {
        invalidateThumb();
    }
}

void ScrollBar::invalidateThumb()
{
    // The whole bar would drag every list row into each fade frame, so only touch what moved.
    auto const origin = absoluteOrigin();
    D2D1_RECT_F const thumb = thumbRect();
    Window::get().invalidate(D2D1::RectF(origin.x, origin.y + thumb.top, origin.x + width(), origin.y + thumb.bottom));
}

void ScrollBar::fadeTo(float alpha, DWORD durationMs)
{
    if (alpha == fadeTarget_)
    {
        return;
    }
    fadeTarget_ = alpha;
    float const from = alpha_;
    fade_.run(
        durationMs,
        [this, from, alpha](float t)
        {
            alpha_ = lerp(from, alpha, t);
            invalidateThumb();
        });
}

void ScrollBar::flash()
{
    if (!needed())
    {
        return;
    }
    fadeTo(1.0F, kFadeInMs);
    if (hideTimer_ != 0)
    {
        Window::get().cancel(hideTimer_);
        hideTimer_ = 0;
    }
    if (!hovered_ && !dragging_)
    {
        hideTimer_ = Window::get().after(
            kHideDelayMs,
            [this]
            {
                hideTimer_ = 0;
                fadeTo(0.0F, kFadeOutMs);
            });
    }
}

D2D1_RECT_F ScrollBar::thumbRect() const
{
    float const track = height() - kThumbInset * 2.0F;
    float const thumbHeight = std::max(kThumbMinHeight, track * viewport_ / std::max(1.0F, content_));
    float const range = content_ - viewport_;
    float const top = kThumbInset + (range > 0.0F ? (track - thumbHeight) * offset_ / range : 0.0F);
    float const thumbWidth = hovered_ || dragging_ ? kThumbHoverWidth : kThumbWidth;
    return D2D1::RectF(width() - kThumbRight - thumbWidth, top, width() - kThumbRight, top + thumbHeight);
}

View* ScrollBar::hitTest(D2D1_POINT_2F point)
{
    // Once faded out the bar must not steal clicks from the content below it.
    return alpha_ > 0.0F && needed() ? View::hitTest(point) : nullptr;
}

bool ScrollBar::onMouse(MouseEvent const& event)
{
    switch (event.kind)
    {
    case MouseKind::Enter:
        hovered_ = true;
        flash();
        invalidateThumb();
        return false;
    case MouseKind::Leave:
        hovered_ = false;
        flash();
        invalidateThumb();
        return false;
    case MouseKind::Down:
    case MouseKind::DoubleClick:
    {
        D2D1_RECT_F const thumb = thumbRect();
        bool const onThumb = event.point.y >= thumb.top && event.point.y < thumb.bottom;
        // A click on the track jumps there and keeps dragging from the thumb's middle.
        dragGrab_ = onThumb ? event.point.y - thumb.top : (thumb.bottom - thumb.top) / 2.0F;
        dragging_ = true;
        flash();
        if (onThumb)
        {
            invalidateThumb();
            return true;
        }
    }
        [[fallthrough]];
    case MouseKind::Move:
    {
        if (!dragging_)
        {
            return false;
        }
        D2D1_RECT_F const thumb = thumbRect();
        float const travel = height() - kThumbInset * 2.0F - (thumb.bottom - thumb.top);
        float const position = event.point.y - dragGrab_ - kThumbInset;
        if (travel > 0.0F && onScroll)
        {
            onScroll(position / travel * (content_ - viewport_));
        }
        return true;
    }
    case MouseKind::Up:
        dragging_ = false;
        flash();
        invalidateThumb();
        return true;
    default:
        return false;
    }
}

void ScrollBar::draw(ID2D1DeviceContext* context)
{
    if (alpha_ <= 0.0F || !needed())
    {
        return;
    }
    D2D1_RECT_F const thumb = thumbRect();
    float const strength = hovered_ || dragging_ ? 0.62F : 0.42F;
    fillRounded(context, thumb, (thumb.right - thumb.left) / 2.0F, withAlpha(Theme::get().colors().foreground, strength * alpha_));
}

Scrollable::Scrollable()
{
    interactive_ = true;
    takesFocus_ = false;
}

void Scrollable::attachBar()
{
    bar_ = add<ScrollBar>();
    bar_->onScroll = [this](float offset) { scrollTo(offset); };
}

float Scrollable::maxScroll() const
{
    return std::max(0.0F, contentHeight_ - height());
}

void Scrollable::apply(float offset)
{
    float const clamped = snapToPixel(std::clamp(offset, 0.0F, maxScroll()));
    if (clamped != offset_)
    {
        offset_ = clamped;
        bar_->setRange(offset_, height(), contentHeight_);
        scrolled();
    }
}

void Scrollable::scrollTo(float offset, bool animated)
{
    target_ = std::clamp(offset, 0.0F, maxScroll());
    bar_->flash();
    if (!animated)
    {
        ease_.cancel();
        apply(target_);
        return;
    }
    float const from = offset_;
    float const to = target_;
    ease_.run(kWheelEaseMs, [this, from, to](float t) { apply(lerp(from, to, easeOut(t))); });
}

void Scrollable::setContentHeight(float height)
{
    contentHeight_ = height;
    float const limit = maxScroll();
    if (offset_ > limit)
    {
        ease_.cancel();
        apply(limit);
    }
    bar_->setRange(offset_, this->height(), contentHeight_);
}

void Scrollable::layout()
{
    bar_->setFrame(D2D1::RectF(width() - kBarWidth, 0.0F, width(), height()));
    apply(offset_);
    bar_->setRange(offset_, height(), contentHeight_);
}

bool Scrollable::onMouse(MouseEvent const& event)
{
    if (event.kind == MouseKind::Move)
    {
        bar_->flash();
        return false;
    }
    if (event.kind == MouseKind::Wheel && maxScroll() > 0.0F)
    {
        float const from = ease_.running() ? target_ : offset_;
        scrollTo(from - event.wheelDelta * wheelStep(height()), true);
        return true;
    }
    return false;
}

class ScrollView::Viewport : public View
{
public:
    explicit Viewport(ScrollView const* owner)
        : owner_(owner)
    {
        clipsChildren_ = true;
    }

    D2D1_POINT_2F childOffset() const override { return D2D1::Point2F(0.0F, -owner_->scrollOffset()); }

private:
    ScrollView const* owner_;
};

ScrollView::ScrollView()
{
    viewport_ = add<Viewport>(this);
    content_ = viewport_->add<View>();
    attachBar();
}

void ScrollView::replaceContent(std::unique_ptr<View> view)
{
    viewport_->removeChild(content_);
    content_ = view.get();
    viewport_->addChild(std::move(view));
    layout();
}

void ScrollView::setContentHeight(float height)
{
    Scrollable::setContentHeight(height);
    layout();
}

void ScrollView::layout()
{
    viewport_->setFrame(bounds());
    content_->setFrame(D2D1::RectF(0.0F, 0.0F, width(), std::max(height(), contentHeight())));
    Scrollable::layout();
}

void ScrollView::scrolled()
{
    viewport_->invalidate();
}

ListView::ListView()
{
    takesFocus_ = true;
    attachBar();
}

void ListView::updateExtent()
{
    setContentHeight(static_cast<float>(rowCount_) * rowHeight_);
    invalidate();
}

void ListView::setRowCount(int count)
{
    count = std::max(0, count);
    if (count == rowCount_)
    {
        return;
    }
    bool const shrinking = count < rowCount_;
    rowCount_ = count;
    selected_.resize(static_cast<size_t>(count), false);
    if (shrinking)
    {
        bool const hadSelection = selectedCount_ > 0;
        selectedCount_ = static_cast<int>(std::count(selected_.begin(), selected_.end(), true));
        anchor_ = anchor_ < count ? anchor_ : -1;
        cursor_ = cursor_ < count ? cursor_ : -1;
        hover_ = hover_ < count ? hover_ : -1;
        if (hadSelection && selectedCount_ == 0 && onSelectionChanged)
        {
            onSelectionChanged();
        }
    }
    updateExtent();
}

void ListView::setRowHeight(float height)
{
    rowHeight_ = height;
    updateExtent();
}

void ListView::setRowInsets(float hairlineInset, float selectionInset)
{
    hairlineInset_ = hairlineInset;
    selectionInset_ = selectionInset;
    invalidate();
}

D2D1_RECT_F ListView::rowRect(int index) const
{
    float const top = static_cast<float>(index) * rowHeight_ - scrollOffset();
    return D2D1::RectF(0.0F, top, width(), top + rowHeight_);
}

int ListView::rowAt(D2D1_POINT_2F point) const
{
    if (point.x < 0.0F || point.x >= width() || point.y < 0.0F || point.y >= height())
    {
        return -1;
    }
    int const row = static_cast<int>((point.y + scrollOffset()) / rowHeight_);
    return row < rowCount_ ? row : -1;
}

void ListView::reloadRow(int index)
{
    if (index < 0 || index >= rowCount_)
    {
        return;
    }
    D2D1_RECT_F const row = rowRect(index);
    float const top = std::max(0.0F, row.top);
    float const bottom = std::min(height(), row.bottom);
    if (top >= bottom)
    {
        return;
    }
    auto const origin = absoluteOrigin();
    Window::get().invalidate(D2D1::RectF(origin.x, origin.y + top, origin.x + width(), origin.y + bottom));
}

bool ListView::isSelected(int index) const
{
    return index >= 0 && index < rowCount_ && selected_[static_cast<size_t>(index)];
}

std::vector<int> ListView::selectedRows() const
{
    std::vector<int> rows;
    rows.reserve(static_cast<size_t>(selectedCount_));
    for (int i = 0; i < rowCount_ && static_cast<int>(rows.size()) < selectedCount_; ++i)
    {
        if (selected_[static_cast<size_t>(i)])
        {
            rows.push_back(i);
        }
    }
    return rows;
}

void ListView::selectionChanged()
{
    invalidate();
    if (onSelectionChanged)
    {
        onSelectionChanged();
    }
}

void ListView::selectRange(int from, int to)
{
    std::fill(selected_.begin(), selected_.end(), false);
    int const low = std::min(from, to);
    int const high = std::max(from, to);
    for (int i = low; i <= high; ++i)
    {
        selected_[static_cast<size_t>(i)] = true;
    }
    selectedCount_ = high - low + 1;
    selectionChanged();
}

void ListView::selectRow(int index, bool extend)
{
    if (index < 0 || index >= rowCount_)
    {
        return;
    }
    if (extend && anchor_ >= 0)
    {
        selectRange(anchor_, index);
    }
    else
    {
        anchor_ = index;
        selectRange(index, index);
    }
    cursor_ = index;
}

void ListView::setRowSelected(int index, bool selected)
{
    if (index < 0 || index >= rowCount_ || isSelected(index) == selected)
    {
        return;
    }
    selected_[static_cast<size_t>(index)] = selected;
    selectedCount_ += selected ? 1 : -1;
    anchor_ = index;
    cursor_ = index;
    selectionChanged();
}

void ListView::selectAll()
{
    if (rowCount_ > 0)
    {
        selectRange(0, rowCount_ - 1);
    }
}

void ListView::clearSelection()
{
    anchor_ = -1;
    cursor_ = -1;
    if (selectedCount_ > 0)
    {
        std::fill(selected_.begin(), selected_.end(), false);
        selectedCount_ = 0;
        selectionChanged();
    }
}

void ListView::revealRow(int index)
{
    if (index < 0 || index >= rowCount_)
    {
        return;
    }
    float const top = static_cast<float>(index) * rowHeight_;
    if (top < scrollOffset())
    {
        scrollTo(top);
    }
    else if (top + rowHeight_ > scrollOffset() + height())
    {
        scrollTo(top + rowHeight_ - height());
    }
}

void ListView::moveCursor(int row, bool extend)
{
    if (rowCount_ == 0)
    {
        return;
    }
    row = std::clamp(row, 0, rowCount_ - 1);
    selectRow(row, extend);
    revealRow(row);
}

bool ListView::handleKey(UINT key, bool shift, bool control)
{
    int const page = std::max(1, static_cast<int>(height() / rowHeight_) - 1);
    switch (key)
    {
    case VK_UP:
        moveCursor(cursor_ < 0 ? 0 : cursor_ - 1, shift);
        return true;
    case VK_DOWN:
        moveCursor(cursor_ + 1, shift);
        return true;
    case VK_HOME:
        moveCursor(0, shift);
        return true;
    case VK_END:
        moveCursor(rowCount_ - 1, shift);
        return true;
    case VK_PRIOR:
        moveCursor(std::max(cursor_, 0) - page, shift);
        return true;
    case VK_NEXT:
        moveCursor(std::max(cursor_, 0) + page, shift);
        return true;
    case VK_DELETE:
        if (onDelete)
        {
            onDelete();
            return true;
        }
        return false;
    case VK_RETURN:
        if (onEnter)
        {
            onEnter();
            return true;
        }
        return false;
    case 'A':
        if (control)
        {
            selectAll();
            return true;
        }
        return false;
    default:
        return false;
    }
}

bool ListView::onKey(UINT key, bool down)
{
    if (!down)
    {
        return false;
    }
    return handleKey(key, (GetKeyState(VK_SHIFT) & 0x8000) != 0, (GetKeyState(VK_CONTROL) & 0x8000) != 0);
}

void ListView::updateHover(int row)
{
    if (row == hover_)
    {
        return;
    }
    int const previous = hover_;
    hover_ = row;
    reloadRow(previous);
    reloadRow(row);
}

void ListView::scrolled()
{
    invalidate();
    if (pointerInside_)
    {
        updateHover(rowAt(pointer_));
    }
}

bool ListView::onMouse(MouseEvent const& event)
{
    switch (event.kind)
    {
    case MouseKind::Enter:
    case MouseKind::Move:
        pointer_ = event.point;
        pointerInside_ = true;
        updateHover(rowAt(event.point));
        return Scrollable::onMouse(event);
    case MouseKind::Leave:
        pointerInside_ = false;
        updateHover(-1);
        return false;
    case MouseKind::Down:
    {
        int const row = rowAt(event.point);
        if (row < 0)
        {
            clearSelection();
        }
        else if (event.control)
        {
            setRowSelected(row, !isSelected(row));
        }
        else
        {
            selectRow(row, event.shift);
        }
        return true;
    }
    case MouseKind::DoubleClick:
    {
        int const row = rowAt(event.point);
        if (row >= 0 && onDoubleClick)
        {
            onDoubleClick(row);
        }
        return true;
    }
    case MouseKind::RightClick:
    {
        int const row = rowAt(event.point);
        if (row >= 0 && !isSelected(row))
        {
            selectRow(row);
        }
        if (row >= 0 && onRightClick)
        {
            auto const origin = absoluteOrigin();
            onRightClick(row, D2D1::Point2F(origin.x + event.point.x, origin.y + event.point.y));
        }
        return true;
    }
    case MouseKind::Up:
        return true;
    default:
        return Scrollable::onMouse(event);
    }
}

void ListView::draw(ID2D1DeviceContext* context)
{
    if (rowCount_ == 0)
    {
        return;
    }
    Theme& theme = Theme::get();
    Palette const& colors = theme.colors();

    D2D1_RECT_F const dirty = localDirty();
    float const top = std::max(0.0F, dirty.top) + scrollOffset();
    float const bottom = std::min(height(), dirty.bottom) + scrollOffset();
    if (top >= bottom)
    {
        return;
    }
    int const first = std::clamp(static_cast<int>(top / rowHeight_), 0, rowCount_ - 1);
    int const last = std::clamp(static_cast<int>(std::ceil(bottom / rowHeight_)), first + 1, rowCount_);

    context->PushAxisAlignedClip(bounds(), D2D1_ANTIALIAS_MODE_ALIASED);
    float const pixel = hairline();
    for (int i = first; i < last; ++i)
    {
        D2D1_RECT_F const row = rowRect(i);
        RowState const state{ i == hover_, selected_[static_cast<size_t>(i)] };

        if (hairlineInset_ >= 0.0F)
        {
            D2D1_RECT_F const line = D2D1::RectF(hairlineInset_, row.bottom - pixel, row.right - hairlineInset_, row.bottom);
            context->FillRectangle(line, theme.brush(colors.border));
        }
        if (state.selected || state.hover)
        {
            D2D1_RECT_F const fill = inset(row, selectionInset_, kRowSelectionInsetY);
            fillRounded(context, fill, kRowRadius, state.selected ? colors.selected : colors.hover);
        }
        if (drawRow)
        {
            drawRow(context, i, row, state);
        }
    }
    context->PopAxisAlignedClip();
}

} // namespace ui
