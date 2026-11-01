#pragma once

#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "gfx/renderer.h"

namespace ui
{

enum class MouseKind
{
    Enter,
    Leave,
    Move,
    Down,
    Up,
    DoubleClick,
    RightClick,
    Wheel,
};

/// `point` is in the receiving view's own coordinates.
struct MouseEvent
{
    MouseKind kind = MouseKind::Move;
    D2D1_POINT_2F point{};
    float wheelDelta = 0.0F; // in lines, positive scrolls up
    bool shift = false;
    bool control = false;
};

/// All sizes are device-independent pixels; a child's frame is relative to its parent.
class View
{
public:
    virtual ~View() = default;

    D2D1_RECT_F const& frame() const { return frame_; }
    void setFrame(D2D1_RECT_F frame);
    float width() const { return frame_.right - frame_.left; }
    float height() const { return frame_.bottom - frame_.top; }
    D2D1_RECT_F bounds() const { return D2D1::RectF(0.0F, 0.0F, width(), height()); }

    /// This view's origin in window coordinates.
    D2D1_POINT_2F absoluteOrigin() const;

    View* parent() const { return parent_; }
    std::vector<std::unique_ptr<View>> const& children() const { return children_; }

    template<typename T, typename... Args>
    T* add(Args&&... args)
    {
        auto child = std::make_unique<T>(std::forward<Args>(args)...);
        T* raw = child.get();
        addChild(std::move(child));
        return raw;
    }
    void addChild(std::unique_ptr<View> child);
    void removeChild(View* child);
    void clearChildren();

    bool hidden() const { return hidden_; }
    void setHidden(bool hidden);

    /// Draws this view and its children after the glass, so they sit on top of it.
    void setFloating(bool floating) { floating_ = floating; }

    /// Fades this view and its children together. Only costs a layer while below 1.
    void setOpacity(float opacity);
    float opacity() const { return opacity_; }

    /// Whether a click moves keyboard focus here. Plain controls leave it where it was.
    bool takesFocus() const { return takesFocus_; }

    /// Repaints this view on the next frame. Nothing else repaints, so call it whenever state changes.
    void invalidate();

    /// Positions children. Runs after a size change.
    virtual void layout() {}
    /// Draws this view in local coordinates, children on top.
    virtual void draw(ID2D1DeviceContext*) {}

    virtual bool onMouse(MouseEvent const&) { return false; }
    virtual bool onKey(UINT, bool /*down*/) { return false; }
    virtual bool onChar(wchar_t) { return false; }
    virtual void onFocus(bool) {}
    /// Called on the focused view when the window itself gains or loses activation.
    virtual void onWindowActive(bool) {}
    /// Called on the focused view just before the input method starts composing.
    virtual void onImeStart() {}
    /// Called when a frame is about to be shown again after the app was hidden or trimmed.
    virtual HCURSOR cursor() const { return nullptr; }

    virtual View* hitTest(D2D1_POINT_2F point);

    /// Shifts children, which is all a scroll view needs.
    virtual D2D1_POINT_2F childOffset() const { return D2D1::Point2F(0.0F, 0.0F); }

    void paint(ID2D1DeviceContext* context, D2D1_POINT_2F origin, D2D1_RECT_F dirty, bool floatingPass, bool inheritedFloating);
    void collectGlass(std::vector<GlassShape>& shapes, D2D1_POINT_2F origin) const;

protected:
    /// The part of this view being repainted, in local coordinates. Only valid inside draw().
    D2D1_RECT_F localDirty() const;
    /// Lets a view animate its sheet of glass. `shape.rect` is already set from the frame.
    virtual void adjustGlass(GlassShape&) const {}

    /// Receives mouse events and stops the window being dragged from here.
    bool interactive_ = false;
    /// Drawn after the glass, so it sits on top of it. Children inherit this.
    bool floating_ = false;
    bool clipsChildren_ = false;
    bool takesFocus_ = true;
    float opacity_ = 1.0F;
    /// A sheet of glass filling this view. Its rect is set from the frame each frame.
    std::optional<GlassShape> glass_;

private:
    View* parent_ = nullptr;
    D2D1_RECT_F frame_{};
    bool hidden_ = false;
    std::vector<std::unique_ptr<View>> children_;
};

} // namespace ui
