#include "app/transfer_row_list.h"

#include <algorithm>
#include <cmath>

#include "app/app.h"
#include "ui/widgets_internal.h"

namespace app
{

namespace
{

using engine::TransferId;

constexpr float kHairlineInset = 24.0F;
constexpr float kSelectionInset = 12.0F;

} // namespace

TransferList::TransferList()
{
    setRowHeight(kRowHeight);
    setRowInsets(kHairlineInset, kSelectionInset);
    drawRow = [this](ID2D1DeviceContext* context, int row, D2D1_RECT_F rect, ui::RowState state) { paint(context, row, rect, state); };
    onSelectionChanged = [this]
    {
        if (restoring_)
        {
            return;
        }
        selectedIds_.clear();
        for (int const row : selectedRows())
        {
            selectedIds_.push_back(ids_[static_cast<size_t>(row)]);
        }
        std::sort(selectedIds_.begin(), selectedIds_.end());
    };
}

TransferId TransferList::idAt(int row) const
{
    return row >= 0 && row < static_cast<int>(ids_.size()) ? ids_[static_cast<size_t>(row)] : engine::kNoTransfer;
}

int TransferList::rowOf(TransferId id) const
{
    auto const found = std::find(ids_.begin(), ids_.end(), id);
    return found != ids_.end() ? static_cast<int>(found - ids_.begin()) : -1;
}

void TransferList::restoreSelection()
{
    restoring_ = true;
    clearSelection();
    size_t kept = 0;
    for (int row = 0; row < static_cast<int>(ids_.size()) && !selectedIds_.empty(); ++row)
    {
        if (std::binary_search(selectedIds_.begin(), selectedIds_.end(), ids_[static_cast<size_t>(row)]))
        {
            setRowSelected(row, true);
            ++kept;
        }
    }
    restoring_ = false;

    // A row the filter hid leaves the selection too, or Delete would remove something out of sight.
    if (kept != selectedIds_.size())
    {
        selectedIds_.clear();
        for (int const row : selectedRows())
        {
            selectedIds_.push_back(ids_[static_cast<size_t>(row)]);
        }
        std::sort(selectedIds_.begin(), selectedIds_.end());
    }
}

void TransferList::selectIds(std::vector<TransferId> ids)
{
    std::sort(ids.begin(), ids.end());
    selectedIds_ = std::move(ids);
    restoreSelection();
}

void TransferList::show(std::vector<int>& rows)
{
    auto const& transfers = App::get().transfers();
    bool same = rows.size() == ids_.size();
    for (size_t i = 0; same && i < rows.size(); ++i)
    {
        same = transfers[static_cast<size_t>(rows[i])].id == ids_[i];
    }
    rows_.swap(rows);
    if (same)
    {
        return;
    }

    ids_.resize(rows_.size());
    for (size_t i = 0; i < rows_.size(); ++i)
    {
        ids_[i] = transfers[static_cast<size_t>(rows_[i])].id;
    }

    restoring_ = true;
    setRowCount(static_cast<int>(rows_.size()));
    restoring_ = false;
    restoreSelection();
    invalidate();
}

int TransferList::columnsNow() const
{
    D2D1_RECT_F const field = RowGeometry::droplet(D2D1::RectF(0.0F, 0.0F, width(), kRowHeight));
    return DropletPainter::columnsFor(field.right - field.left);
}

RowSlot& TransferList::slotFor(int row)
{
    return slots_[static_cast<size_t>(row) % slots_.size()];
}

bool TransferList::update(RowSlot& slot, int row, int columns)
{
    // While hidden the list skips refreshes, so the first frame back can see an older snapshot's rows.
    auto const& transfers = App::get().transfers();
    auto const index = static_cast<size_t>(rows_[static_cast<size_t>(row)]);
    if (index >= transfers.size() || transfers[index].id != ids_[static_cast<size_t>(row)])
    {
        return false;
    }

    auto const& info = transfers[index];
    bool const isNew = slot.id != info.id;
    bool changed = isNew || slot.state != info.state;

    // The one thing a row needs that the snapshot lacks, and only an uploading row can have it.
    int givingBackTo = 0;
    if (info.state == engine::TransferState::Seeding && info.upBytesPerSecond > 0.0)
    {
        givingBackTo = App::get().engine().details(info.id).peersGettingFromUs;
    }

    fillRowText(info, givingBackTo, scratch_);
    ++counters_.textBuilds;
    if (isNew || scratch_ != slot.text)
    {
        changed = true;
        bool const newName = isNew || scratch_.name != slot.text.name;
        bool const newState = isNew || scratch_.state != slot.text.state;
        std::swap(scratch_, slot.text);
        if (newName)
        {
            slot.shownName = middleTrimmed(slot.text.name, RowGeometry::kNameStyle, RowGeometry::kNameWidth);
        }
        if (newState)
        {
            slot.stateWidth = ui::Theme::get().measure(slot.text.state, RowGeometry::kStateStyle).width;
        }
    }

    slot.id = info.id;
    slot.state = info.state;
    size_t const count = static_cast<size_t>(columns) * engine::kDefaultDotRows;
    if (slot.dots.size() != count)
    {
        slot.dots.assign(count, engine::PieceDot{});
        changed = true;
    }
    slot.columns = columns;

    ++counters_.dotCalls;
    bool const dotsMoved = App::get().engine().pieceDots(info.id, count, slot.dots.data());
    // The demo engine calls every update a change, so look at the dots themselves before repainting.
    if ((dotsMoved || changed) && slot.shown != slot.dots)
    {
        slot.shown = slot.dots;
        changed = true;
    }
    return changed;
}

void TransferList::refreshVisible()
{
    if (slots_.empty() || rowCount() == 0)
    {
        return;
    }
    int const columns = columnsNow();
    int const first = static_cast<int>(scrollOffset() / kRowHeight);
    int const last = std::min(rowCount(), static_cast<int>(std::ceil((scrollOffset() + height()) / kRowHeight)));
    for (int row = first; row < last; ++row)
    {
        if (update(slotFor(row), row, columns))
        {
            ++counters_.rowsReloaded;
            reloadRow(row);
        }
    }
}

void TransferList::layout()
{
    ListView::layout();
    // One extra, since a scrolled list shows part of one row more than fits.
    auto const wanted = static_cast<size_t>(std::ceil(height() / kRowHeight)) + 1U;
    if (slots_.size() != wanted)
    {
        slots_.clear();
        slots_.resize(wanted);
    }
}

void TransferList::paint(ID2D1DeviceContext* context, int row, D2D1_RECT_F rect, ui::RowState state)
{
    ++counters_.rowsDrawn;
    RowSlot& slot = slotFor(row);
    int const columns = columnsNow();
    if (slot.id != idAt(row) || slot.columns != columns)
    {
        update(slot, row, columns);
        if (slot.id != idAt(row))
        {
            return;
        }
    }
    bool const lit = row == buttonRow_;
    paintRow(context, slot, rect, state, lit ? hoveredButton_ : RowButton::None, lit && pressedButton_ != RowButton::None, painter_);
}

void TransferList::hoverButton(int row, RowButton button)
{
    if (row == buttonRow_ && button == hoveredButton_)
    {
        return;
    }
    int const previous = buttonRow_;
    buttonRow_ = row;
    hoveredButton_ = button;
    reloadRow(previous);
    reloadRow(row);
}

bool TransferList::onMouse(ui::MouseEvent const& event)
{
    using ui::MouseKind;
    using ui::detail::contains;

    auto const buttonAt = [this](int row, D2D1_POINT_2F point)
    {
        if (row < 0)
        {
            return RowButton::None;
        }
        D2D1_RECT_F const rect = rowRect(row);
        if (contains(RowGeometry::button(rect, RowButton::Control), point))
        {
            return RowButton::Control;
        }
        return contains(RowGeometry::button(rect, RowButton::Remove), point) ? RowButton::Remove : RowButton::None;
    };

    switch (event.kind)
    {
    case MouseKind::Enter:
    case MouseKind::Move:
    {
        int const row = rowAt(event.point);
        hoverButton(row, buttonAt(row, event.point));
        break;
    }
    case MouseKind::Leave:
        hoverButton(-1, RowButton::None);
        break;
    case MouseKind::Down:
    case MouseKind::DoubleClick:
    {
        int const row = rowAt(event.point);
        RowButton const button = buttonAt(row, event.point);
        if (button != RowButton::None)
        {
            pressedButton_ = button;
            pressedId_ = idAt(row);
            reloadRow(row);
            return true;
        }
        break;
    }
    case MouseKind::Up:
        if (pressedButton_ != RowButton::None)
        {
            int const row = rowAt(event.point);
            RowButton const button = pressedButton_;
            TransferId const id = pressedId_;
            pressedButton_ = RowButton::None;
            pressedId_ = engine::kNoTransfer;
            reloadRow(row);
            if (idAt(row) == id && buttonAt(row, event.point) == button)
            {
                auto const& callback = button == RowButton::Control ? onToggle : onRemove;
                if (callback)
                {
                    callback(id);
                }
            }
            return true;
        }
        break;
    case MouseKind::RightClick:
        if (rowAt(event.point) < 0)
        {
            clearSelection();
            if (onEmptyRightClick)
            {
                auto const origin = absoluteOrigin();
                onEmptyRightClick(D2D1::Point2F(origin.x + event.point.x, origin.y + event.point.y));
            }
            return true;
        }
        break;
    default:
        break;
    }
    return ListView::onMouse(event);
}

} // namespace app
