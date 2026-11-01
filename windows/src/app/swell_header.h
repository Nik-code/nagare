#pragma once

#include <array>
#include <functional>

#include "engine/engine.h"
#include "ui/widgets.h"

namespace app
{

/// The rounded tile at the top of the window: sky, two seas that follow the session's speeds, the
/// wordmark, the speeds on a glass tile and the toolbar pill.
class SwellHeader final : public ui::View
{
public:
    static constexpr float kHeight = 254.0F;

    SwellHeader();
    ~SwellHeader() override;

    /// Call once per refresh. Eases towards the new sea, and does nothing at all if it hasn't moved.
    void refresh(double downBytesPerSecond, double upBytesPerSecond);
    /// Whether the swell is still easing, which is the only time this view asks for frames.
    [[nodiscard]] bool easing() const { return timer_ != 0; }
    /// Stops easing and jumps to where it was heading, for when nothing can be seen.
    void settle();
    /// Lets go of the art, so Theme::trim can really free it while nothing is visible.
    void releaseArt();

    /// Where the curves come from. The tests swap this to hold the sea still.
    std::function<bool(engine::SwellData&)> swellSource;
    /// Whether anyone can see the sea moving. A hook, so a test window behind others can still ease.
    std::function<bool()> watched;

    std::function<void()> onSearch;
    std::function<void()> onLink;
    std::function<void()> onInfo;
    /// Gets the point below the `...` button, in window coordinates.
    std::function<void(D2D1_POINT_2F)> onMore;

    void layout() override;
    void draw(ID2D1DeviceContext* context) override;
    bool onMouse(ui::MouseEvent const& event) override;
    ui::View* hitTest(D2D1_POINT_2F point) override;

private:
    class Stats;
    class Toolbar;
    class CaptionPill;

    using Curve = std::array<float, engine::kSwellPoints>;

    void step();
    void scheduleFrame();
    void stopEasing();
    void buildPaths(ID2D1DeviceContext* context);
    ComPtr<ID2D1PathGeometry> path(ID2D1DeviceContext* context, Curve const& curve, bool closed) const;

    Stats* stats_ = nullptr;
    Toolbar* toolbar_ = nullptr;
    CaptionPill* captionPill_ = nullptr;
    float wordmarkLeft_ = 0.0F;
    float wordmarkTop_ = 0.0F;

    // Heights as a share of the tile, measured from the bottom edge as the Mac does.
    Curve down_{};
    Curve up_{};
    Curve downFrom_{};
    Curve upFrom_{};
    Curve downTo_{};
    Curve upTo_{};

    bool seeded_ = false;
    bool stillDrawn_ = false;
    bool pathsDirty_ = true;
    int timer_ = 0;
    ULONGLONG easeStart_ = 0;
    double frameDue_ = 0.0;
    double frameMs_ = 0.0;

    ComPtr<ID2D1PathGeometry> downArea_;
    ComPtr<ID2D1PathGeometry> downLine_;
    ComPtr<ID2D1PathGeometry> upArea_;
    ComPtr<ID2D1PathGeometry> upLine_;
    ComPtr<ID2D1BitmapBrush1> artBrush_;
    ComPtr<ID2D1StrokeStyle> crestStroke_;
    ComPtr<IDWriteTextLayout> wordmark_;
};

} // namespace app
