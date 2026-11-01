#pragma once

#include <span>
#include <string_view>
#include <vector>

#include "engine/engine.h"
#include "gfx/renderer.h"

namespace app
{

/// Draws the droplet field: the row art, rounded, with the piece dots over it. Dots go through one
/// sprite batch and a small atlas of baked circles, so a row costs one draw call however wide it is.
class DropletPainter
{
public:
    static constexpr float kHeight = 68.0F;

    /// Same sum as updateWithTorrent in DropletView.
    [[nodiscard]] static int columnsFor(float width);
    /// The art DropletView picks for a state. Plain downloads alternate by name.
    [[nodiscard]] static wchar_t const* artFor(engine::TransferState state, std::wstring_view name);

    /// `batch` belongs to the caller's row, because a batch must not change while a frame is pending.
    void paint(
        ID2D1DeviceContext* context,
        D2D1_RECT_F rect,
        std::wstring_view art,
        engine::TransferState state,
        std::span<engine::PieceDot const> dots,
        int columns,
        ComPtr<ID2D1SpriteBatch>& batch);

    /// Lets go of the art, so Theme::trim can really free it while nothing is visible.
    void releaseArt() { artBrush_.Reset(); }

private:
    void paintDots(
        ID2D1DeviceContext* context,
        D2D1_RECT_F rect,
        engine::TransferState state,
        std::span<engine::PieceDot const> dots,
        int columns,
        ComPtr<ID2D1SpriteBatch>& batch);
    void buildAtlas(ID2D1DeviceContext* context, float scale);

    ComPtr<ID2D1BitmapBrush1> artBrush_;
    ComPtr<ID2D1Bitmap> atlas_;
    float atlasScale_ = 0.0F;
    UINT cell_ = 0;
    UINT sizes_ = 0;

    // Kept between rows, so painting one never allocates.
    std::vector<D2D1_RECT_F> destinations_;
    std::vector<D2D1_RECT_U> sources_;
    std::vector<D2D1_COLOR_F> colors_;
};

} // namespace app
