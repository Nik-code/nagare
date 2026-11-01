#include <algorithm>
#include <ctime>
#include <string>
#include <vector>

#include "app/app.h"
#include "app/details_common.h"
#include "ui/widgets_internal.h"

namespace app::details
{

namespace
{

using namespace ui;

constexpr float kListTop = 6.0F;
constexpr float kBarGap = 10.0F;
constexpr float kBarBottom = 16.0F;
constexpr float kButtonGap = 6.0F;

// InfoTrackersViewController.mm
constexpr float kTierRowHeight = 30.0F;
constexpr float kTrackerRowHeight = 54.0F;
constexpr float kHostTop = 9.0F;
constexpr float kHostHeight = 18.0F;
constexpr float kStatusTop = 27.0F;
constexpr float kStatusHeight = 16.0F;
constexpr float kTierBottom = 4.0F;
constexpr float kTierTextHeight = 16.0F;
constexpr float kSwarmGap = 12.0F;

// ThemedRowView, moved out with the margin so the hairlines still start just inside the text.
constexpr float kHairlineInset = 28.0F;
constexpr float kSelectionInset = 12.0F;
constexpr float kSelectionInsetY = 3.0F;
constexpr float kSelectionRadius = 12.0F;

constexpr float kEditInsetX = 10.0F; // the field's own padding, so the text doesn't jump
constexpr float kEditHeight = 24.0F;
constexpr float kEditTop = 3.0F;

constexpr DWORD kConfirmMs = 4000;

/// A tier heading, or one tracker with its lines already formatted.
struct Row
{
    bool isTier = false;
    std::wstring title;
    std::wstring status;
    std::wstring swarm;
    std::wstring announce;
    std::wstring hoverStatus;
    bool selected = false;
    float top = 0.0F;
    float height = 0.0F;

    bool sameAs(Row const& other) const
    {
        // Not hoverStatus: its countdown changes every second and only one row can be showing it.
        return isTier == other.isTier && title == other.title && status == other.status && swarm == other.swarm && announce == other.announce;
    }
};

std::wstring announceStatus(engine::TrackerInfo const& tracker)
{
    std::wstring const date = tracker.hasAnnounced && tracker.lastAnnounceTime > 0 ? dateText(tracker.lastAnnounceTime) : L"N/A";
    if (tracker.hasAnnounced && tracker.lastAnnounceTimedOut)
    {
        return L"Announce timed out: " + date;
    }
    if (tracker.hasAnnounced && !tracker.lastAnnounceSucceeded)
    {
        if (tracker.lastAnnounceResult.empty())
        {
            return L"Announce error: " + date;
        }
        return L"Announce error: " + tracker.lastAnnounceResult + L" - " + date;
    }
    std::wstring text = L"Last Announce: " + date;
    if (tracker.hasAnnounced && tracker.lastAnnounceSucceeded && tracker.lastAnnouncePeers > 0)
    {
        text += tracker.lastAnnouncePeers == 1 ? L" (got 1 peer)" : L" (got " + std::to_wstring(tracker.lastAnnouncePeers) + L" peers)";
    }
    return text;
}

std::wstring nextAnnounce(engine::TrackerInfo const& tracker)
{
    switch (tracker.announceState)
    {
    case engine::TrackerState::Active:
        return L"Announce in progress…";
    case engine::TrackerState::Waiting:
        return L"Next announce in " + durationText(tracker.nextAnnounceTime - std::time(nullptr));
    case engine::TrackerState::Queued:
        return L"Announce is queued…";
    case engine::TrackerState::Inactive:
        break;
    }
    return tracker.isBackup ? L"Tracker will be used as a backup" : L"Announce not scheduled";
}

std::wstring scrapeStatus(engine::TrackerInfo const& tracker)
{
    std::wstring const date = tracker.hasScraped && tracker.lastScrapeTime > 0 ? dateText(tracker.lastScrapeTime) : L"N/A";
    if (tracker.hasScraped && !tracker.lastScrapeSucceeded)
    {
        if (tracker.lastScrapeResult.empty())
        {
            return L"Scrape error: " + date;
        }
        return L"Scrape error: " + tracker.lastScrapeResult + L" - " + date;
    }
    return L"Last Scrape: " + date;
}

/// Tier headings and trackers have different heights, which ListView's fixed rows can't do.
class TrackerList final : public Scrollable
{
public:
    TrackerList()
    {
        takesFocus_ = true;
        clipsChildren_ = true;
        attachBar();
        edit_ = add<TextField>();
        edit_->setHidden(true);
        edit_->setTextStyle(kBodyMedium);
        edit_->onSubmit = [this] { endEdit(true); };
        edit_->onCancel = [this] { endEdit(false); };
        edit_->onBlur = [this] { endEdit(true); };
    }

    std::function<void()> onSelectionChanged;
    std::function<void()> onDelete;
    std::function<void()> onPaste;
    std::function<void(std::wstring const&, std::wstring const&)> onEdited;
    std::function<void(D2D1_POINT_2F)> onRightClick;

    std::vector<Row> const& rows() const { return rows_; }
    bool editing() const { return editRow_ >= 0; }

    /// Takes the new rows and repaints only if something showing changed. Selection follows the
    /// announce address.
    void update(std::vector<Row>& next)
    {
        float top = 0.0F;
        for (Row& row : next)
        {
            row.height = row.isTier ? kTierRowHeight : kTrackerRowHeight;
            row.top = top;
            top += row.height;
            row.selected = false;
            if (!row.isTier)
            {
                auto const same = std::find_if(rows_.begin(), rows_.end(), [&row](Row const& old) { return !old.isTier && old.announce == row.announce; });
                row.selected = same != rows_.end() && same->selected;
            }
        }
        bool const same = next.size() == rows_.size() && std::equal(next.begin(), next.end(), rows_.begin(), [](Row const& a, Row const& b) { return a.sameAs(b); });
        bool const selectionLost = selectedCount() != std::count_if(next.begin(), next.end(), [](Row const& row) { return row.selected; });
        bool const hoverMoved = same && hover_ >= 0 && hover_ < static_cast<int>(next.size()) && next[static_cast<size_t>(hover_)].hoverStatus != rows_[static_cast<size_t>(hover_)].hoverStatus;
        rows_.swap(next);
        if (same && !selectionLost)
        {
            if (hoverMoved)
            {
                invalidate();
            }
            return;
        }
        setContentHeight(top);
        hover_ = hover_ < static_cast<int>(rows_.size()) ? hover_ : -1;
        cursor_ = cursor_ < static_cast<int>(rows_.size()) ? cursor_ : -1;
        invalidate();
        placeEdit();
        if (selectionLost && onSelectionChanged)
        {
            onSelectionChanged();
        }
    }

    [[nodiscard]] int selectedCount() const
    {
        return static_cast<int>(std::count_if(rows_.begin(), rows_.end(), [](Row const& row) { return row.selected; }));
    }

    [[nodiscard]] std::vector<std::wstring> selectedAnnounces() const
    {
        std::vector<std::wstring> announces;
        for (Row const& row : rows_)
        {
            if (row.selected)
            {
                announces.push_back(row.announce);
            }
        }
        return announces;
    }

    void clearSelection()
    {
        for (Row& row : rows_)
        {
            row.selected = false;
        }
        invalidate();
        selectionChanged();
    }

    void beginEdit(int row)
    {
        if (row < 0 || rows_[static_cast<size_t>(row)].isTier || editing())
        {
            return;
        }
        editRow_ = row;
        editOriginal_ = rows_[static_cast<size_t>(row)].announce;
        edit_->setText(editOriginal_);
        edit_->selectAll();
        placeEdit();
        edit_->setHidden(false);
        Window::get().focus(edit_);
        invalidate();
    }

    void beginEditSelected()
    {
        auto const found = std::find_if(rows_.begin(), rows_.end(), [](Row const& row) { return row.selected; });
        if (found != rows_.end())
        {
            beginEdit(static_cast<int>(found - rows_.begin()));
        }
    }

    bool onMouse(MouseEvent const& event) override
    {
        switch (event.kind)
        {
        case MouseKind::Enter:
        case MouseKind::Move:
            setHover(rowAt(event.point));
            return Scrollable::onMouse(event);
        case MouseKind::Leave:
            setHover(-1);
            return false;
        case MouseKind::Down:
            press(rowAt(event.point), event);
            return true;
        case MouseKind::DoubleClick:
            beginEdit(rowAt(event.point));
            return true;
        case MouseKind::RightClick:
        {
            int const row = rowAt(event.point);
            if (row >= 0 && !rows_[static_cast<size_t>(row)].selected)
            {
                selectOnly(row);
            }
            if (onRightClick)
            {
                auto const origin = absoluteOrigin();
                onRightClick(D2D1::Point2F(origin.x + event.point.x, origin.y + event.point.y));
            }
            return true;
        }
        case MouseKind::Up:
            return true;
        default:
            return Scrollable::onMouse(event);
        }
    }

    bool onKey(UINT key, bool down) override
    {
        if (!down)
        {
            return false;
        }
        bool const control = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        switch (key)
        {
        case VK_DELETE:
            if (onDelete)
            {
                onDelete();
            }
            return true;
        case VK_UP:
        case VK_DOWN:
            step(key == VK_UP ? -1 : 1);
            return true;
        case 'A':
            if (control)
            {
                for (Row& row : rows_)
                {
                    row.selected = !row.isTier;
                }
                invalidate();
                selectionChanged();
                return true;
            }
            return false;
        case 'C':
            if (control)
            {
                copySelected();
                return true;
            }
            return false;
        case 'V':
            if (control && onPaste)
            {
                onPaste();
                return true;
            }
            return false;
        default:
            return false;
        }
    }

    void copySelected() const
    {
        std::wstring text;
        for (Row const& row : rows_)
        {
            if (row.selected)
            {
                text += text.empty() ? L"" : L"\r\n";
                text += row.announce;
            }
        }
        if (!text.empty())
        {
            copyText(text);
        }
    }

    void draw(ID2D1DeviceContext* context) override
    {
        Theme& theme = Theme::get();
        Palette const& colors = theme.colors();
        D2D1_RECT_F const dirty = localDirty();
        float const offset = scrollOffset();
        float const pixel = detail::hairline();

        context->PushAxisAlignedClip(bounds(), D2D1_ANTIALIAS_MODE_ALIASED);
        for (size_t i = 0; i < rows_.size(); ++i)
        {
            Row const& row = rows_[i];
            float const top = row.top - offset;
            float const bottom = top + row.height;
            if (bottom < dirty.top || top > dirty.bottom)
            {
                continue;
            }

            D2D1_RECT_F const rect = D2D1::RectF(0.0F, top, width(), bottom);
            context->FillRectangle(D2D1::RectF(kHairlineInset, bottom - pixel, width() - kHairlineInset, bottom), theme.brush(colors.border));

            bool const hovered = static_cast<int>(i) == hover_ && !row.isTier;
            if (row.selected || hovered)
            {
                detail::fillRounded(context, detail::inset(rect, kSelectionInset, kSelectionInsetY), kSelectionRadius, row.selected ? colors.selected : colors.hover);
            }

            float const left = kMargin;
            float const right = width() - kMargin;
            if (row.isTier)
            {
                D2D1_RECT_F const label = D2D1::RectF(left, bottom - kTierBottom - kTierTextHeight, right, bottom - kTierBottom);
                theme.text(context, row.title, kSmallMedium, label, colors.foreground);
                continue;
            }

            float swarmWidth = 0.0F;
            if (!row.swarm.empty())
            {
                swarmWidth = std::ceil(theme.measure(row.swarm, kSmall).width);
                D2D1_RECT_F const swarm = D2D1::RectF(right - swarmWidth, top + kHostTop, right, top + kHostTop + kHostHeight);
                theme.text(context, row.swarm, kSmall, swarm, colors.foreground, DWRITE_TEXT_ALIGNMENT_TRAILING, Trim::None, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            }
            float const textRight = right - (swarmWidth > 0.0F ? swarmWidth + kSwarmGap : 0.0F);
            if (static_cast<int>(i) != editRow_)
            {
                theme.text(context, hovered ? row.announce : row.title, kBodyMedium, D2D1::RectF(left, top + kHostTop, textRight, top + kHostTop + kHostHeight), colors.bright, DWRITE_TEXT_ALIGNMENT_LEADING, Trim::Middle, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            }
            theme.text(context, hovered ? row.hoverStatus : row.status, kSmall, D2D1::RectF(left, top + kStatusTop, right, top + kStatusTop + kStatusHeight), colors.foreground);
        }
        context->PopAxisAlignedClip();
    }

protected:
    void scrolled() override
    {
        invalidate();
        placeEdit();
    }

private:
    [[nodiscard]] int rowAt(D2D1_POINT_2F point) const
    {
        if (point.x < 0.0F || point.x >= width() || point.y < 0.0F || point.y >= height())
        {
            return -1;
        }
        float const y = point.y + scrollOffset();
        for (size_t i = 0; i < rows_.size(); ++i)
        {
            if (y >= rows_[i].top && y < rows_[i].top + rows_[i].height)
            {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    void setHover(int row)
    {
        if (row != hover_)
        {
            hover_ = row;
            invalidate();
        }
    }

    void selectionChanged()
    {
        if (onSelectionChanged)
        {
            onSelectionChanged();
        }
    }

    void selectOnly(int row)
    {
        for (Row& each : rows_)
        {
            each.selected = false;
        }
        rows_[static_cast<size_t>(row)].selected = true;
        cursor_ = row;
        invalidate();
        selectionChanged();
    }

    void press(int row, MouseEvent const& event)
    {
        if (row < 0 || rows_[static_cast<size_t>(row)].isTier)
        {
            if (row < 0)
            {
                clearSelection();
            }
            return;
        }
        if (event.control)
        {
            rows_[static_cast<size_t>(row)].selected = !rows_[static_cast<size_t>(row)].selected;
            cursor_ = row;
            invalidate();
            selectionChanged();
        }
        else if (event.shift && cursor_ >= 0)
        {
            int const low = std::min(cursor_, row);
            int const high = std::max(cursor_, row);
            for (int i = 0; i < static_cast<int>(rows_.size()); ++i)
            {
                rows_[static_cast<size_t>(i)].selected = i >= low && i <= high && !rows_[static_cast<size_t>(i)].isTier;
            }
            invalidate();
            selectionChanged();
        }
        else
        {
            selectOnly(row);
        }
    }

    void step(int direction)
    {
        int row = cursor_ < 0 ? (direction > 0 ? -1 : static_cast<int>(rows_.size())) : cursor_;
        for (row += direction; row >= 0 && row < static_cast<int>(rows_.size()); row += direction)
        {
            if (!rows_[static_cast<size_t>(row)].isTier)
            {
                selectOnly(row);
                Row const& target = rows_[static_cast<size_t>(row)];
                if (target.top < scrollOffset())
                {
                    scrollTo(target.top);
                }
                else if (target.top + target.height > scrollOffset() + height())
                {
                    scrollTo(target.top + target.height - height());
                }
                return;
            }
        }
    }

    void placeEdit()
    {
        if (editRow_ < 0)
        {
            return;
        }
        // The row can have moved under a refresh, so go by what it was editing.
        auto const found = std::find_if(rows_.begin(), rows_.end(), [this](Row const& row) { return !row.isTier && row.announce == editOriginal_; });
        if (found == rows_.end())
        {
            return;
        }
        editRow_ = static_cast<int>(found - rows_.begin());
        float const top = found->top - scrollOffset() + kEditTop;
        edit_->setFrame(D2D1::RectF(kMargin - kEditInsetX, top, width() - kMargin + kEditInsetX, top + kEditHeight));
    }

    void endEdit(bool commit)
    {
        if (editRow_ < 0)
        {
            return;
        }
        editRow_ = -1;
        edit_->setHidden(true);
        if (Window::get().focused() == edit_)
        {
            Window::get().focus(this);
        }
        invalidate();
        std::wstring const text = trimmed(edit_->text());
        if (commit && !text.empty() && text != editOriginal_ && onEdited)
        {
            onEdited(editOriginal_, text);
        }
    }

    std::vector<Row> rows_;
    TextField* edit_ = nullptr;
    int editRow_ = -1;
    std::wstring editOriginal_;
    int hover_ = -1;
    int cursor_ = -1;
};

class TrackersTab : public TabView
{
public:
    explicit TrackersTab(engine::TransferId id)
        : id_(id)
    {
        list_ = add<TrackerList>();
        list_->onSelectionChanged = [this] { selectionChanged(); };
        list_->onDelete = [this] { removeSelected(); };
        list_->onPaste = [this] { addFrom(pasteText()); };
        list_->onEdited = [this](std::wstring const& from, std::wstring const& to) { edit(from, to); };
        list_->onRightClick = [this](D2D1_POINT_2F point) { showMenu(point); };

        field_ = add<TextField>();
        field_->setPlaceholder(L"Add a tracker address");
        field_->onSubmit = [this] { addFromField(); };

        add_ = add<Button>(L"Add");
        add_->onClick = [this] { addFromField(); };
        remove_ = add<Button>(L"Remove");
        remove_->onClick = [this] { removeSelected(); };
        remove_->setEnabled(false);
    }

    ~TrackersTab() override
    {
        stopConfirming();
    }

    void layout() override
    {
        float const right = width() - kMargin;
        float const fieldBottom = height() - kBarBottom;
        float const fieldTop = fieldBottom - TextField::kHeight;
        float const buttonTop = fieldTop + (TextField::kHeight - Button::kHeight) / 2.0F;

        float x = right - confirmWidth();
        remove_->setFrame(D2D1::RectF(x, buttonTop, right, buttonTop + Button::kHeight));
        float const addRight = x - kButtonGap;
        x = addRight - add_->preferredWidth();
        add_->setFrame(D2D1::RectF(x, buttonTop, addRight, buttonTop + Button::kHeight));
        field_->setFrame(D2D1::RectF(kMargin, fieldTop, x - kBarGap, fieldBottom));
        list_->setFrame(D2D1::RectF(0.0F, kListTop, width(), fieldTop - kBarGap));
    }

    void refresh(engine::TransferInfo const&) override
    {
        App::get().engine().trackers(id_, trackers_);

        size_t rowCount = 0;
        int tier = -1;
        for (auto const& tracker : trackers_)
        {
            rowCount += tracker.tier != tier ? 2 : 1;
            tier = tracker.tier;
        }

        next_.resize(rowCount);
        size_t at = 0;
        tier = -1;
        for (auto const& tracker : trackers_)
        {
            if (tracker.tier != tier)
            {
                tier = tracker.tier;
                Row& heading = next_[at++];
                heading.isTier = true;
                heading.title = L"Tier " + std::to_wstring(tier + 1);
            }
            Row& row = next_[at++];
            row.isTier = false;
            row.title = tracker.host;
            row.announce = tracker.announce;
            row.status = announceStatus(tracker);
            row.hoverStatus = nextAnnounce(tracker);
            row.hoverStatus += L"  ·  ";
            row.hoverStatus += scrapeStatus(tracker);
            row.swarm.clear();
            if (tracker.seeders >= 0 || tracker.leechers >= 0)
            {
                row.swarm = std::to_wstring(std::max(tracker.seeders, 0)) + L" seeds  ·  " + std::to_wstring(std::max(tracker.leechers, 0)) + L" peers";
            }
        }
        list_->update(next_);
    }

private:
    [[nodiscard]] float confirmWidth() const
    {
        return remove_->preferredWidth();
    }

    void selectionChanged()
    {
        stopConfirming();
        remove_->setEnabled(list_->selectedCount() > 0);
    }

    void changed()
    {
        App::get().refreshNow();
    }

    void addFromField()
    {
        std::wstring const text = trimmed(field_->text());
        if (text.empty())
        {
            return;
        }
        if (App::get().engine().addTracker(id_, text))
        {
            field_->setText(L"");
            changed();
        }
        else
        {
            refuse();
        }
    }

    void addFrom(std::wstring const& text)
    {
        bool added = false;
        size_t at = 0;
        while (at <= text.size())
        {
            size_t const end = std::min(text.find_first_of(L"\r\n", at), text.size());
            if (auto const line = trimmed(std::wstring_view(text).substr(at, end - at)); !line.empty())
            {
                added = App::get().engine().addTracker(id_, line) || added;
            }
            at = end + 1;
        }
        if (added)
        {
            changed();
        }
        else
        {
            refuse();
        }
    }

    void edit(std::wstring const& from, std::wstring const& to)
    {
        if (App::get().engine().editTracker(id_, from, to))
        {
            changed();
        }
        else
        {
            refuse();
        }
    }

    /// The Mac asks in an alert, since a tracker can't be got back. Here the button asks instead.
    void removeSelected()
    {
        if (list_->selectedCount() == 0)
        {
            return;
        }
        if (!confirming_)
        {
            confirming_ = true;
            remove_->setTitle(L"Sure?");
            remove_->setStyle(ButtonStyle::Accent);
            layout();
            confirmTimer_ = Window::get().after(
                kConfirmMs,
                [this]
                {
                    confirmTimer_ = 0;
                    stopConfirming();
                });
            return;
        }
        stopConfirming();
        auto const announces = list_->selectedAnnounces();
        list_->clearSelection();
        if (!App::get().engine().removeTrackers(id_, announces))
        {
            refuse();
        }
        changed();
    }

    void stopConfirming()
    {
        if (confirmTimer_ != 0)
        {
            Window::get().cancel(confirmTimer_);
            confirmTimer_ = 0;
        }
        if (confirming_)
        {
            confirming_ = false;
            remove_->setTitle(L"Remove");
            remove_->setStyle(ButtonStyle::Glass);
            layout();
        }
    }

    void showMenu(D2D1_POINT_2F point)
    {
        bool const any = list_->selectedCount() > 0;
        auto const item = [](wchar_t const* title, bool enabled, std::function<void()> action)
        {
            MenuItem entry;
            entry.title = title;
            entry.enabled = enabled;
            entry.action = std::move(action);
            return entry;
        };
        std::vector<MenuItem> menu;
        menu.push_back(item(L"Copy", any, [this] { list_->copySelected(); }));
        menu.push_back(item(L"Paste", IsClipboardFormatAvailable(CF_UNICODETEXT) != FALSE, [this] { addFrom(pasteText()); }));
        menu.push_back(MenuItem::line());
        menu.push_back(item(L"Edit…", list_->selectedCount() == 1, [this] { list_->beginEditSelected(); }));
        menu.push_back(item(L"Remove", any, [this] { removeSelected(); }));
        PopupMenu::show(std::move(menu), point);
    }

    engine::TransferId id_;
    TrackerList* list_ = nullptr;
    TextField* field_ = nullptr;
    Button* add_ = nullptr;
    Button* remove_ = nullptr;
    std::vector<engine::TrackerInfo> trackers_;
    std::vector<Row> next_;
    bool confirming_ = false;
    int confirmTimer_ = 0;
};

} // namespace

std::unique_ptr<TabView> createTrackersTab(engine::TransferId id)
{
    return std::make_unique<TrackersTab>(id);
}

} // namespace app::details
