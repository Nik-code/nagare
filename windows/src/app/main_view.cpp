#include "app/main_view.h"

#include <shlwapi.h>

#include <algorithm>
#include <cmath>
#include <cwctype>

#include "app/main_drop.h"
#include "ui/theme.h"

namespace app
{

namespace
{

using engine::TransferInfo;
using engine::TransferState;
using ui::Theme;
using ui::Window;

// Controller.mm
constexpr float kHeaderInset = 12.0F;
constexpr float kFilterInset = 24.0F;
constexpr float kFilterGap = 12.0F;
constexpr float kListGap = 8.0F;
constexpr float kEmptyHeight = 28.0F;
constexpr float kTotalWidth = 240.0F;
constexpr ui::TextStyle kTotalStyle{ 13.0F, DWRITE_FONT_WEIGHT_NORMAL, false };
constexpr ui::TextStyle kEmptyStyle{ 17.0F, DWRITE_FONT_WEIGHT_MEDIUM, true };
constexpr UINT kArtStep = 64;

constexpr int kStateSortPaused = 0;
constexpr int kStateSortWaiting = 1;
constexpr int kStateSortSeeding = 10;
constexpr int kStateSortDownloading = 20;

struct FilterName
{
    TransferFilter filter;
    wchar_t const* key;
    wchar_t const* title;
};

constexpr FilterName kFilterNames[] = {
    { TransferFilter::All, L"None", L"All" },
    { TransferFilter::Download, L"Download", L"Downloading" },
    { TransferFilter::Seed, L"Seed", L"Seeding" },
    { TransferFilter::Pause, L"Pause", L"Paused" },
    { TransferFilter::Error, L"Error", L"Problems" },
};

struct SortName
{
    TransferSort sort;
    wchar_t const* key;
};

constexpr SortName kSortNames[] = {
    { TransferSort::Order, L"Order" },
    { TransferSort::Activity, L"Activity" },
    { TransferSort::Date, L"Date" },
    { TransferSort::Name, L"Name" },
    { TransferSort::Progress, L"Progress" },
    { TransferSort::Eta, L"ETA" },
    { TransferSort::Size, L"Size" },
    { TransferSort::State, L"State" },
    { TransferSort::Tracker, L"Tracker" },
};

template<typename T>
int order(T a, T b)
{
    return a < b ? -1 : b < a ? 1 : 0;
}

int stateKey(TransferInfo const& info)
{
    switch (info.state)
    {
    case TransferState::Seeding:
        return kStateSortSeeding;
    case TransferState::Download:
    case TransferState::Fetching:
    case TransferState::Verifying:
        return kStateSortDownloading;
    default:
        return info.isWaiting ? kStateSortWaiting : kStateSortPaused;
    }
}

float seedShare(TransferInfo const& info)
{
    return info.ratioStop > 0.0 ? static_cast<float>(std::min(1.0, static_cast<double>(info.ratio) / info.ratioStop)) : 0.0F;
}

/// The pill a transfer counts under, the same way the engine counts its session stats.
TransferFilter bucket(TransferInfo const& info)
{
    switch (info.state)
    {
    case TransferState::Seeding:
        return TransferFilter::Seed;
    case TransferState::Download:
    case TransferState::Fetching:
        return TransferFilter::Download;
    case TransferState::Verifying:
        return info.verifyProgress > 0.0F ? TransferFilter::Download : TransferFilter::Pause;
    case TransferState::Problem:
        return TransferFilter::Error;
    default:
        return TransferFilter::Pause;
    }
}

} // namespace

MainView::MainView()
{
    App& app = App::get();
    engine::AppPrefs const& prefs = app.engine().prefs();

    for (FilterName const& name : kFilterNames)
    {
        if (prefs.filter == name.key)
        {
            filter_ = name.filter;
        }
    }
    for (SortName const& name : kSortNames)
    {
        if (prefs.sort == name.key)
        {
            sort_ = name.sort;
        }
    }
    reverse_ = prefs.sortReverse;

    header_ = add<SwellHeader>();
    header_->onSearch = [] { openSearchPanel(); };
    header_->onLink = [] { openLinkPanel(); };
    header_->onInfo = [this] { showInfo(); };
    header_->onMore = [this](D2D1_POINT_2F point) { ui::PopupMenu::show(moreMenu(), point); };

    pills_ = add<ui::PillBar>();
    filters_ = { TransferFilter::All, TransferFilter::Download, TransferFilter::Seed, TransferFilter::Pause };
    pills_->setPills({ { L"All", -1 }, { L"Downloading", -1 }, { L"Seeding", -1 }, { L"Paused", -1 } });
    pills_->onChange = [this](int index)
    {
        if (index >= 0 && index < static_cast<int>(filters_.size()))
        {
            setFilter(filters_[static_cast<size_t>(index)]);
        }
    };

    total_ = add<ui::Label>(std::wstring{}, kTotalStyle);
    total_->setAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);

    list_ = add<TransferList>();
    list_->onToggle = [this](engine::TransferId id) { toggle({ id }); };
    list_->onRemove = [this](engine::TransferId id) { remove({ id }, false); };
    list_->onDelete = [this]
    {
        bool const shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        remove(selection(), shift);
    };
    list_->onEnter = [this] { showInfo(); };
    list_->onDoubleClick = [this](int row) { doubleClicked(row); };
    list_->onRightClick = [this](int, D2D1_POINT_2F point) { ui::PopupMenu::show(rowMenu(), point); };
    list_->onEmptyRightClick = [this](D2D1_POINT_2F point) { ui::PopupMenu::show(emptyMenu(), point); };

    empty_ = add<ui::Label>(L"Drop a torrent here, or search for one", kEmptyStyle);
    empty_->setAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    empty_->setHidden(true);

    overlay_ = add<DropOverlay>();
    overlay_->setHidden(true);
    dropTarget_ = registerDropTarget(overlay_);

    listener_ = app.addRefreshListener([this] { refresh(); });
    update();
}

MainView::~MainView()
{
    App::get().removeRefreshListener(listener_);
    releaseDropTarget(dropTarget_);
}

void MainView::layout()
{
    float const w = width();
    float const h = height();
    float const headerBottom = kHeaderInset + SwellHeader::kHeight;
    header_->setFrame(D2D1::RectF(kHeaderInset, kHeaderInset, w - kHeaderInset, headerBottom));

    float const filterTop = headerBottom + kFilterGap;
    float const filterBottom = filterTop + ui::PillBar::kHeight;
    pills_->setFrame(D2D1::RectF(kFilterInset, filterTop, kFilterInset + pills_->preferredWidth(), filterBottom));
    total_->setFrame(D2D1::RectF(w - kFilterInset - kTotalWidth, filterTop, w - kFilterInset, filterBottom));

    float const listTop = filterBottom + kListGap;
    list_->setFrame(D2D1::RectF(0.0F, listTop, w, h));

    float const emptyTop = std::round((listTop + h - kEmptyHeight) / 2.0F);
    empty_->setFrame(D2D1::RectF(0.0F, emptyTop, w, emptyTop + kEmptyHeight));

    overlay_->setFrame(bounds());
}

void MainView::draw(ID2D1DeviceContext* context)
{
    float const scale = Window::get().renderer().dpi() / 96.0F;
    auto const rounded = [](float dips)
    {
        auto const pixels = static_cast<UINT>(std::ceil(dips));
        return (pixels + kArtStep - 1U) / kArtStep * kArtStep;
    };

    UINT const pixelWidth = rounded(width() * scale);
    UINT const pixelHeight = rounded(height() * scale);
    auto* art = Theme::get().art(L"ArtSurface", pixelWidth, pixelHeight);
    if (art != nullptr)
    {
        float const w = static_cast<float>(pixelWidth) / scale;
        float const h = static_cast<float>(pixelHeight) / scale;
        float const left = (width() - w) / 2.0F;
        float const top = (height() - h) / 2.0F;
        context->DrawBitmap(art, D2D1::RectF(left, top, left + w, top + h));
    }

    // Rows skip their updates while hidden, so catch up as soon as a frame can show them.
    if (stale_)
    {
        stale_ = false;
        Window::get().after(0, [] { App::get().refreshNow(); });
    }
}

bool MainView::passes(TransferInfo const& info) const
{
    return filter_ == TransferFilter::All || bucket(info) == filter_;
}

int MainView::compare(TransferInfo const& a, TransferInfo const& b) const
{
    int const direction = reverse_ ? -1 : 1;
    auto const byName = [&] { return direction * StrCmpLogicalW(a.name.c_str(), b.name.c_str()); };
    auto const byProgress = [&](int sign)
    {
        if (int const result = order(a.percentDone, b.percentDone); result != 0)
        {
            return sign * result;
        }
        if (int const result = order(seedShare(a), seedShare(b)); result != 0)
        {
            return sign * result;
        }
        return sign * order(a.ratio, b.ratio);
    };

    if (useGroups_ && a.group != b.group)
    {
        // Ungrouped transfers come last, like the Mac's "No Group".
        return a.group < 0 ? 1 : b.group < 0 ? -1 : order(a.group, b.group);
    }

    int result = 0;
    switch (sort_)
    {
    case TransferSort::Order:
        return order(a.queuePosition, b.queuePosition);
    case TransferSort::State:
        result = -direction * order(stateKey(a), stateKey(b));
        if (result == 0)
        {
            result = byProgress(-direction);
        }
        break;
    case TransferSort::Progress:
        result = byProgress(direction);
        break;
    case TransferSort::Eta:
        result = direction * order(a.etaSeconds, b.etaSeconds);
        if (result == 0)
        {
            result = byProgress(direction);
        }
        break;
    case TransferSort::Tracker:
    {
        auto const& first = trackerKeys_.at(a.id);
        auto const& second = trackerKeys_.at(b.id);
        result = direction * StrCmpIW(first.c_str(), second.c_str());
        break;
    }
    case TransferSort::Activity:
        result = direction * order(a.downBytesPerSecond + a.upBytesPerSecond, b.downBytesPerSecond + b.upBytesPerSecond);
        if (result == 0)
        {
            int64_t const first = a.activityDate != 0 ? a.activityDate : a.addedDate;
            int64_t const second = b.activityDate != 0 ? b.activityDate : b.addedDate;
            result = direction * order(first, second);
        }
        break;
    case TransferSort::Date:
        result = direction * order(a.addedDate, b.addedDate);
        break;
    case TransferSort::Size:
        result = direction * order(a.selectedBytes, b.selectedBytes);
        break;
    case TransferSort::Name:
        break;
    }
    return result != 0 ? result : byName();
}

bool MainView::sortsBefore(int a, int b) const
{
    auto const& transfers = App::get().transfers();
    auto const& first = transfers[static_cast<size_t>(a)];
    auto const& second = transfers[static_cast<size_t>(b)];
    int const result = compare(first, second);
    return result != 0 ? result < 0 : first.id < second.id;
}

void MainView::rebuildRows()
{
    auto const& transfers = App::get().transfers();

    if (sort_ == TransferSort::Tracker)
    {
        // Only worked out for this sort, and only once per transfer.
        std::vector<engine::TrackerInfo> trackers;
        for (TransferInfo const& info : transfers)
        {
            if (!trackerKeys_.contains(info.id))
            {
                App::get().engine().trackers(info.id, trackers);
                trackerKeys_[info.id] = trackers.empty() ? std::wstring{} : trackers.front().host;
            }
        }
    }

    rows_.clear();
    for (size_t i = 0; i < transfers.size(); ++i)
    {
        if (passes(transfers[i]))
        {
            rows_.push_back(static_cast<int>(i));
        }
    }

    auto const before = [this](int a, int b) { return sortsBefore(a, b); };
    // Nearly always in order already, and this check is far cheaper than a sort.
    if (!std::is_sorted(rows_.begin(), rows_.end(), before))
    {
        std::sort(rows_.begin(), rows_.end(), before);
    }
    list_->show(rows_);
}

void MainView::updatePills(engine::SessionStats const& stats)
{
    // Problems only get a pill while something has gone wrong.
    bool const showProblems = stats.problems > 0 || filter_ == TransferFilter::Error;
    bool const hasProblems = filters_.size() > 4;
    if (showProblems != hasProblems)
    {
        std::vector<ui::Pill> pills{ { L"All", -1 }, { L"Downloading", -1 }, { L"Seeding", -1 }, { L"Paused", -1 } };
        filters_ = { TransferFilter::All, TransferFilter::Download, TransferFilter::Seed, TransferFilter::Pause };
        if (showProblems)
        {
            pills.push_back({ L"Problems", -1 });
            filters_.push_back(TransferFilter::Error);
        }
        pills_->setPills(std::move(pills));
    }

    auto const shown = [](int count) { return count > 0 ? count : -1; };
    pills_->setCount(1, shown(stats.downloading));
    pills_->setCount(2, shown(stats.seeding));
    pills_->setCount(3, shown(stats.paused));
    if (showProblems)
    {
        pills_->setCount(4, shown(stats.problems));
    }

    auto const selected = std::find(filters_.begin(), filters_.end(), filter_);
    pills_->setSelected(selected != filters_.end() ? static_cast<int>(selected - filters_.begin()) : 0);

    float const right = pills_->frame().left + pills_->preferredWidth();
    if (right != pills_->frame().right)
    {
        auto frame = pills_->frame();
        frame.right = right;
        pills_->setFrame(frame);
    }
}

void MainView::updateTotal(engine::SessionStats const& stats)
{
    std::wstring text = stats.all == 1 ? L"1 transfer" : countText(stats.all) + L" transfers";
    if (filter_ != TransferFilter::All)
    {
        text = countText(list_->rowCount()) + L" of " + text;
    }
    total_->setText(text);

    bool const showEmpty = stats.all == 0;
    if (empty_->hidden() == showEmpty)
    {
        empty_->setHidden(!showEmpty);
    }
}

void MainView::refresh()
{
    if (!Window::get().visible())
    {
        if (!stale_)
        {
            // The window trimmed when it hid, but these brushes were still holding on to two bitmaps.
            header_->releaseArt();
            list_->releaseArt();
            Window::get().renderer().trim();
        }
        stale_ = true;
        return;
    }
    update();
}

void MainView::update()
{
    auto const stats = App::get().engine().sessionStats();
    updatePills(stats);
    rebuildRows();
    updateTotal(stats);
    list_->refreshVisible();
    header_->refresh(stats.downBytesPerSecond, stats.upBytesPerSecond);
}

void MainView::persistView()
{
    engine::AppPrefs prefs = App::get().engine().prefs();
    for (FilterName const& name : kFilterNames)
    {
        if (name.filter == filter_)
        {
            prefs.filter = name.key;
        }
    }
    for (SortName const& name : kSortNames)
    {
        if (name.sort == sort_)
        {
            prefs.sort = name.key;
        }
    }
    prefs.sortReverse = reverse_;
    App::get().engine().setPrefs(prefs);
}

void MainView::setFilter(TransferFilter filter)
{
    if (filter == filter_)
    {
        return;
    }
    filter_ = filter;
    persistView();
    refresh();
}

void MainView::setSort(TransferSort sort)
{
    if (sort == sort_)
    {
        return;
    }
    sort_ = sort;
    trackerKeys_.clear();
    persistView();
    refresh();
}

void MainView::cycleFilter(bool forward)
{
    auto const at = std::find(filters_.begin(), filters_.end(), filter_);
    auto const index = static_cast<size_t>(at - filters_.begin());
    size_t const count = filters_.size();
    setFilter(filters_[(index + (forward ? 1U : count - 1U)) % count]);
}

bool MainView::onKey(UINT key, bool down)
{
    if (!down)
    {
        return false;
    }
    bool const control = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    bool const shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;

    // Keys first reach here from the list when it has focus, and need routing to it when it doesn't.
    if (Window::get().focused() != list_ && list_->handleKey(key, shift, control))
    {
        return true;
    }

    if (control)
    {
        switch (key)
        {
        case 'F':
        case 'K':
            openSearchPanel();
            return true;
        case 'L':
            if (shift)
            {
                toggleSpeedLimit();
            }
            else
            {
                openLinkPanel();
            }
            return true;
        case 'U':
            openLinkPanel();
            return true;
        case 'I':
            showInfo();
            return true;
        case 'O':
            openTorrentFiles();
            return true;
        case 'V':
            paste();
            return true;
        case 'C':
            copyMagnets(selection());
            return true;
        case 'R':
            reveal(selection());
            return true;
        case 'Q':
            PostMessageW(Window::get().handle(), WM_CLOSE, 0, 0);
            return true;
        case VK_OEM_COMMA:
            openSettingsPanel();
            return true;
        case VK_OEM_PERIOD:
            pause(shift ? idsWhere([](TransferInfo const&) { return true; }) : selection());
            return true;
        case VK_OEM_2:
            resume(shift ? idsWhere([](TransferInfo const& info) { return !info.isFinishedSeeding; }) : selection());
            return true;
        case VK_TAB:
            cycleFilter(!shift);
            return true;
        default:
            return false;
        }
    }

    switch (key)
    {
    case VK_SPACE:
        toggle(selection());
        return true;
    case VK_ESCAPE:
        list_->clearSelection();
        return true;
    default:
        return false;
    }
}

std::unique_ptr<ui::View> createMainView()
{
    return std::make_unique<MainView>();
}

} // namespace app
