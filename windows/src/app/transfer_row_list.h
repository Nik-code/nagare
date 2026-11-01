#pragma once

#include <functional>
#include <vector>

#include "app/transfer_row.h"
#include "ui/widgets.h"

namespace app
{

/// Counts of real work, so the tests can hold the list to its budget.
struct ListCounters
{
    uint64_t rowsDrawn = 0;
    uint64_t rowsReloaded = 0;
    uint64_t textBuilds = 0;
    uint64_t dotCalls = 0;
};

/// The transfer list. It keeps selection by transfer, not by position, because sorting moves rows
/// under it, and it does per-row work for visible rows only.
class TransferList final : public ui::ListView
{
public:
    static constexpr float kRowHeight = 88.0F;

    TransferList();

    /// `rows` holds indexes into App::transfers(), in display order. Swapped in, so the caller's
    /// vector comes back holding the old ones for reuse.
    void show(std::vector<int>& rows);
    /// Brings visible rows up to date and repaints only the ones that changed.
    void refreshVisible();

    [[nodiscard]] std::vector<engine::TransferId> const& selectedIds() const { return selectedIds_; }
    [[nodiscard]] engine::TransferId idAt(int row) const;
    [[nodiscard]] int rowOf(engine::TransferId id) const;
    void selectIds(std::vector<engine::TransferId> ids);
    [[nodiscard]] int slotCount() const { return static_cast<int>(slots_.size()); }
    void releaseArt() { painter_.releaseArt(); }

    [[nodiscard]] ListCounters const& counters() const { return counters_; }

    std::function<void(engine::TransferId)> onToggle;
    std::function<void(engine::TransferId)> onRemove;
    /// Right click below the rows, with the point in window coordinates.
    std::function<void(D2D1_POINT_2F)> onEmptyRightClick;

    void layout() override;
    bool onMouse(ui::MouseEvent const& event) override;

private:
    RowSlot& slotFor(int row);
    bool update(RowSlot& slot, int row, int columns);
    [[nodiscard]] int columnsNow() const;
    void paint(ID2D1DeviceContext* context, int row, D2D1_RECT_F rect, ui::RowState state);
    void hoverButton(int row, RowButton button);
    void restoreSelection();

    std::vector<int> rows_;
    std::vector<engine::TransferId> ids_;
    std::vector<engine::TransferId> selectedIds_;
    std::vector<RowSlot> slots_;
    RowText scratch_;
    DropletPainter painter_;
    ListCounters counters_;
    bool restoring_ = false;

    int buttonRow_ = -1;
    RowButton hoveredButton_ = RowButton::None;
    RowButton pressedButton_ = RowButton::None;
    engine::TransferId pressedId_ = engine::kNoTransfer;
};

} // namespace app
