#include "ui/view.h"

#include <algorithm>

#include "ui/window.h"

namespace ui
{

namespace
{

// Painting is single threaded and draw() can't nest, so one slot is enough.
D2D1_RECT_F gPaintDirty{};

bool intersects(D2D1_RECT_F const& a, D2D1_RECT_F const& b)
{
    return a.left < b.right && b.left < a.right && a.top < b.bottom && b.top < a.bottom;
}

D2D1_RECT_F intersection(D2D1_RECT_F const& a, D2D1_RECT_F const& b)
{
    return D2D1::RectF(std::max(a.left, b.left), std::max(a.top, b.top), std::min(a.right, b.right), std::min(a.bottom, b.bottom));
}

} // namespace

void View::setFrame(D2D1_RECT_F frame)
{
    bool const moved = frame.left != frame_.left || frame.top != frame_.top || frame.right != frame_.right || frame.bottom != frame_.bottom;
    bool const resized = frame.right - frame.left != width() || frame.bottom - frame.top != height();
    if (!moved)
    {
        return;
    }
    invalidate();
    frame_ = frame;
    invalidate();
    if (resized)
    {
        layout();
    }
}

D2D1_POINT_2F View::absoluteOrigin() const
{
    D2D1_POINT_2F point = D2D1::Point2F(frame_.left, frame_.top);
    for (View const* ancestor = parent_; ancestor != nullptr; ancestor = ancestor->parent_)
    {
        auto const offset = ancestor->childOffset();
        point.x += ancestor->frame_.left + offset.x;
        point.y += ancestor->frame_.top + offset.y;
    }
    return point;
}

void View::addChild(std::unique_ptr<View> child)
{
    child->parent_ = this;
    children_.push_back(std::move(child));
    invalidate();
}

void View::removeChild(View* child)
{
    Window::get().forgetView(child);
    child->invalidate();
    std::erase_if(children_, [child](auto const& candidate) { return candidate.get() == child; });
}

void View::clearChildren()
{
    for (auto const& child : children_)
    {
        Window::get().forgetView(child.get());
    }
    children_.clear();
    invalidate();
}

void View::setHidden(bool hidden)
{
    if (hidden != hidden_)
    {
        hidden_ = hidden;
        // A hidden view has to repaint what's behind it, which uses the same rect.
        bool const saved = hidden_;
        hidden_ = false;
        invalidate();
        hidden_ = saved;
    }
}

void View::setOpacity(float opacity)
{
    if (opacity != opacity_)
    {
        opacity_ = opacity;
        invalidate();
    }
}

D2D1_RECT_F View::localDirty() const
{
    auto const origin = absoluteOrigin();
    return D2D1::RectF(gPaintDirty.left - origin.x, gPaintDirty.top - origin.y, gPaintDirty.right - origin.x, gPaintDirty.bottom - origin.y);
}

void View::invalidate()
{
    if (hidden_)
    {
        return;
    }
    auto const origin = absoluteOrigin();
    Window::get().invalidate(D2D1::RectF(origin.x, origin.y, origin.x + width(), origin.y + height()));
}

View* View::hitTest(D2D1_POINT_2F point)
{
    if (hidden_ || point.x < 0.0F || point.y < 0.0F || point.x >= width() || point.y >= height())
    {
        return nullptr;
    }
    auto const offset = childOffset();
    for (auto it = children_.rbegin(); it != children_.rend(); ++it)
    {
        View* child = it->get();
        auto const& frame = child->frame();
        if (View* hit = child->hitTest(D2D1::Point2F(point.x - offset.x - frame.left, point.y - offset.y - frame.top)))
        {
            return hit;
        }
    }
    return interactive_ ? this : nullptr;
}

void View::paint(ID2D1DeviceContext* context, D2D1_POINT_2F origin, D2D1_RECT_F dirty, bool floatingPass, bool inheritedFloating)
{
    if (hidden_)
    {
        return;
    }
    D2D1_RECT_F const absolute = D2D1::RectF(origin.x, origin.y, origin.x + width(), origin.y + height());
    if (!intersects(absolute, dirty))
    {
        return;
    }

    if (opacity_ <= 0.0F)
    {
        return;
    }

    bool const fades = opacity_ < 1.0F;
    if (fades)
    {
        context->SetTransform(D2D1::Matrix3x2F::Identity());
        context->PushLayer(D2D1::LayerParameters1(absolute, nullptr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE, D2D1::IdentityMatrix(), opacity_), nullptr);
    }

    bool const isFloating = inheritedFloating || floating_;
    context->SetTransform(D2D1::Matrix3x2F::Translation(origin.x, origin.y));
    if (isFloating == floatingPass)
    {
        gPaintDirty = dirty;
        draw(context);
    }

    if (!children_.empty())
    {
        D2D1_RECT_F inner = dirty;
        if (clipsChildren_)
        {
            context->SetTransform(D2D1::Matrix3x2F::Identity());
            context->PushAxisAlignedClip(absolute, D2D1_ANTIALIAS_MODE_ALIASED);
            inner = intersection(dirty, absolute);
        }
        auto const offset = childOffset();
        for (auto const& child : children_)
        {
            auto const& frame = child->frame();
            child->paint(context, D2D1::Point2F(origin.x + offset.x + frame.left, origin.y + offset.y + frame.top), inner, floatingPass, isFloating);
        }
        if (clipsChildren_)
        {
            context->SetTransform(D2D1::Matrix3x2F::Identity());
            context->PopAxisAlignedClip();
        }
    }

    if (fades)
    {
        context->SetTransform(D2D1::Matrix3x2F::Identity());
        context->PopLayer();
    }
}

void View::collectGlass(std::vector<GlassShape>& shapes, D2D1_POINT_2F origin) const
{
    if (hidden_)
    {
        return;
    }
    if (glass_.has_value())
    {
        GlassShape shape = *glass_;
        shape.rect = D2D1::RectF(origin.x, origin.y, origin.x + width(), origin.y + height());
        adjustGlass(shape);
        shapes.push_back(shape);
    }
    auto const offset = childOffset();
    for (auto const& child : children_)
    {
        auto const& frame = child->frame();
        child->collectGlass(shapes, D2D1::Point2F(origin.x + offset.x + frame.left, origin.y + offset.y + frame.top));
    }
}

} // namespace ui
