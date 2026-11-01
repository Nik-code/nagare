#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "app/droplet.h"
#include "engine/engine.h"
#include "ui/widgets.h"

namespace app
{

/// NSByteCountFormatter's file style: "Zero KB", "125.9 MB".
[[nodiscard]] std::wstring sizeText(uint64_t bytes);
/// stringForSpeed: "966.7 KB/s", "16.61 MB/s".
[[nodiscard]] std::wstring speedText(double bytesPerSecond);
/// "5,000", the way localizedStringWithFormat groups a count.
[[nodiscard]] std::wstring countText(int count);

/// What refresh in DropletCell works out. Compared whole, so a row only repaints if a character moved.
struct RowText
{
    std::wstring name;
    std::wstring state;
    std::wstring suffix;
    std::wstring detail;
    std::wstring speed;
    std::wstring swarm;
    ui::ColorRole stateRole = ui::ColorRole::Foreground;
    bool running = false;

    bool operator==(RowText const&) const = default;
};

/// Reuses `out`'s strings. `givingBackTo` is the number of peers we are uploading to.
void fillRowText(engine::TransferInfo const& info, int givingBackTo, RowText& out);

/// The Mac truncates names in the middle, so both ends of a release name stay readable.
[[nodiscard]] std::wstring middleTrimmed(std::wstring_view text, ui::TextStyle style, float width);

enum class RowButton
{
    None,
    Control,
    Remove,
};

/// A row's cached text and dots. There is one per visible row, however long the list is.
struct RowSlot
{
    engine::TransferId id = engine::kNoTransfer;
    RowText text;
    std::wstring shownName;
    float stateWidth = 0.0F;
    engine::TransferState state = engine::TransferState::None;
    int columns = 0;
    std::vector<engine::PieceDot> dots;
    std::vector<engine::PieceDot> shown;
    ComPtr<ID2D1SpriteBatch> batch;
};

/// DropletCell's layout, in the list's coordinates.
struct RowGeometry
{
    static constexpr float kNameWidth = 260.0F;
    static constexpr ui::TextStyle kNameStyle{ 15.0F, DWRITE_FONT_WEIGHT_MEDIUM, false };
    static constexpr ui::TextStyle kStateStyle{ 13.0F, DWRITE_FONT_WEIGHT_MEDIUM, false };

    [[nodiscard]] static D2D1_RECT_F droplet(D2D1_RECT_F row);
    [[nodiscard]] static D2D1_RECT_F button(D2D1_RECT_F row, RowButton button);
};

void paintRow(ID2D1DeviceContext* context, RowSlot& slot, D2D1_RECT_F row, ui::RowState state, RowButton hovered, bool pressed, DropletPainter& painter);

} // namespace app
