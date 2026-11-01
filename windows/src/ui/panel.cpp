#include "ui/widgets.h"
#include "ui/widgets_internal.h"

namespace ui
{

namespace
{

using namespace detail;

constexpr float kStartScale = 0.97F;
constexpr float kSideMargin = 24.0F;
constexpr float kBottomMargin = 24.0F;

} // namespace

Panel::Panel(float panelWidth, float panelHeight)
    : panelWidth_(panelWidth)
    , panelHeight_(panelHeight)
{
    interactive_ = true;
    takesFocus_ = false;
    floating_ = true;
    glass_ = Theme::get().panelGlass();
    opacity_ = 0.0F;
}

void Panel::present(std::unique_ptr<Panel> panel)
{
    Panel* raw = panel.get();
    Window::get().setPanel(std::move(panel));
    raw->animateTo(1.0F, {});
}

void Panel::animateTo(float target, std::function<void()> done)
{
    float const from = progress_;
    animation_.run(
        kAnimationMs,
        [this, from, target](float t)
        {
            progress_ = lerp(from, target, easeOut(t));
            setOpacity(progress_);
        },
        std::move(done));
}

void Panel::close()
{
    if (closing_)
    {
        return;
    }
    closing_ = true;
    animateTo(
        0.0F,
        [this]
        {
            auto closed = std::move(onClosed);
            // A replacement may already be up, and this panel's last tick must not close that one.
            if (Window::get().panel() == this)
            {
                Window::get().setPanel(nullptr);
            }
            if (closed)
            {
                closed();
            }
        });
}

void Panel::requestClose()
{
    if (!shouldClose || shouldClose())
    {
        close();
    }
}

void Panel::setPanelSize(float panelWidth, float panelHeight)
{
    panelWidth_ = panelWidth;
    panelHeight_ = panelHeight;
    layout();
}

void Panel::layout()
{
    float const windowWidth = Window::get().width();
    float const windowHeight = Window::get().height();
    float const sheetWidth = std::min(panelWidth_, windowWidth - kSideMargin * 2.0F);
    float const sheetHeight = std::min(panelHeight_, windowHeight - kTopMargin - kBottomMargin);
    float const left = std::round((windowWidth - sheetWidth) / 2.0F);

    // A size change comes back through here from setFrame, so content is only laid out once.
    bool const resized = sheetWidth != width() || sheetHeight != height();
    setFrame(D2D1::RectF(left, kTopMargin, left + sheetWidth, kTopMargin + sheetHeight));
    if (!resized)
    {
        layoutContent();
    }
}

View* Panel::hitTest(D2D1_POINT_2F point)
{
    // Owning every point is what keeps the views below from seeing the mouse at all.
    View* hit = View::hitTest(point);
    return hit != nullptr ? hit : this;
}

bool Panel::onMouse(MouseEvent const& event)
{
    bool const outside = !contains(bounds(), event.point);
    if (outside && closesOnOutsideClick_ && (isPress(event.kind) || event.kind == MouseKind::RightClick))
    {
        requestClose();
    }
    return true;
}

bool Panel::onKey(UINT key, bool down)
{
    if (key == VK_ESCAPE)
    {
        if (down)
        {
            requestClose();
        }
        return true;
    }
    return false;
}

void Panel::adjustGlass(GlassShape& shape) const
{
    // Read fresh each frame so an open panel follows a theme change.
    GlassShape glass = Theme::get().panelGlass();
    float const scale = lerp(kStartScale, 1.0F, progress_);
    float const cx = (shape.rect.left + shape.rect.right) / 2.0F;
    float const cy = (shape.rect.top + shape.rect.bottom) / 2.0F;
    float const halfWidth = (shape.rect.right - shape.rect.left) * scale / 2.0F;
    float const halfHeight = (shape.rect.bottom - shape.rect.top) * scale / 2.0F;
    glass.rect = D2D1::RectF(cx - halfWidth, cy - halfHeight, cx + halfWidth, cy + halfHeight);

    // The shader has no opacity, but a sheet with no bend, tint, rim or frost is invisible.
    glass.refraction *= progress_;
    glass.tint.a *= progress_;
    glass.rimLight *= progress_;
    glass.rimDark *= progress_;
    glass.shadow *= progress_;
    glass.clarity = lerp(1.0F, glass.clarity, progress_);
    glass.saturation = lerp(1.0F, glass.saturation, progress_);
    shape = glass;
}

} // namespace ui
