#include "app/search_panel.h"

#include <shellapi.h>

#include <algorithm>
#include <cmath>

#include "app/add_panel.h"
#include "app/app.h"
#include "app/details_common.h"
#include "app/link_panel.h"
#include "ui/widgets_internal.h"

namespace app
{

namespace
{

using namespace ui;
using details::fileSizeText;
using details::refuse;
using details::TextBuffer;
using details::trimmed;

// The numbers in SearchWindowController.mm.
constexpr float kPanelWidth = 840.0F;
constexpr float kPanelHeight = 520.0F;
constexpr float kRowHeight = 66.0F;
constexpr int kHealthDots = 6;
constexpr float kHealthStep = 11.0F;
constexpr float kHealthDot = 7.0F;
constexpr float kHealthWidth = kHealthDots * kHealthStep;
constexpr float kDotOn = 0.9F;
constexpr float kDotOff = 0.18F;
constexpr double kHealthScale = 0.8;

constexpr float kFieldLeft = 26.0F;
constexpr float kFieldTop = 26.0F;
constexpr float kFieldHeight = 25.0F;
constexpr float kIconSize = 22.0F;
constexpr float kIconGap = 12.0F;
constexpr float kSourcesGap = 12.0F;
constexpr float kSourcesInset = 22.0F;
constexpr float kSourcesMinWidth = 144.0F;
constexpr float kListInset = 12.0F;
constexpr float kListTopGap = 18.0F;
constexpr float kListBottomGap = 10.0F;
constexpr float kFooterInset = 28.0F;
constexpr float kFooterBottom = 16.0F;
constexpr float kFooterHeight = 16.0F;
constexpr float kFooterGap = 16.0F;
constexpr float kHintWidth = 220.0F;

constexpr float kRowLeft = 16.0F;
constexpr float kGetWidth = 64.0F;
constexpr float kGetInset = 14.0F;
constexpr float kSeedsWidth = 70.0F;
constexpr float kSeedsGap = 14.0F;
constexpr float kHealthGap = 8.0F;
constexpr float kTextGap = 18.0F;
constexpr float kTitleTop = 13.0F;
constexpr float kTitleHeight = 21.0F;
constexpr float kDetailTop = 35.0F;
constexpr float kDetailHeight = 18.0F;
constexpr float kSelectionRadius = 16.0F;
constexpr float kSelectionInset = 1.0F;
constexpr float kSelectionAlpha = 0.6F;

constexpr TextStyle kQueryText{ 21.0F, DWRITE_FONT_WEIGHT_NORMAL, false };
constexpr TextStyle kTitleText{ 15.0F, DWRITE_FONT_WEIGHT_MEDIUM, false };
constexpr TextStyle kDetailText{ 13.0F, DWRITE_FONT_WEIGHT_NORMAL, false };

constexpr size_t kMaxResultChars = 400;

constexpr wchar_t const* kDefaultStatus = L"Type and press Return to search";

/// What the Mac gets for free by keeping its one search window: reopening shows the last search.
struct SearchMemory
{
    std::wstring query;
    std::vector<SearchRow> rows;
    std::wstring status = kDefaultStatus;
    int selected = -1;
    int source = 0;
    int sourceCount = 0;
};

SearchMemory gMemory;
bool gSearching = false;

class LeadingIcon final : public View
{
public:
    void draw(ID2D1DeviceContext* context) override { Theme::get().icon(context, L"Search", bounds(), Theme::get().colors().bright); }
};

SearchRow makeRow(engine::SearchResult const& result)
{
    SearchRow row;
    row.result = result;
    row.result.title = plainResultText(result.title);
    row.detail = plainResultText(result.sourceName);
    if (result.size > 0U)
    {
        TextBuffer buffer{};
        row.detail += L"  ·  ";
        row.detail += fileSizeText(result.size, buffer);
    }
    if (result.seeds >= 0)
    {
        row.seeds = std::to_wstring(result.seeds) + (result.seeds == 1 ? L" seed" : L" seeds");
        // Each dot is roughly a doubling, so a healthy swarm fills the row.
        row.health = result.seeds > 0 ? std::min(kHealthDots, 1 + static_cast<int>(std::log2(static_cast<double>(result.seeds)) * kHealthScale)) : 0;
    }
    return row;
}

} // namespace

std::wstring plainResultText(std::wstring_view text)
{
    std::wstring clean;
    clean.reserve(std::min(text.size(), kMaxResultChars));
    for (wchar_t const c : text)
    {
        if (clean.size() == kMaxResultChars)
        {
            break;
        }
        bool const control = c < 0x20 || (c >= 0x7F && c < 0xA0);
        bool const bidi = (c >= 0x202A && c <= 0x202E) || (c >= 0x2066 && c <= 0x2069) || c == 0x200E || c == 0x200F || c == 0x061C;
        if (control)
        {
            clean.push_back(L' ');
        }
        else if (!bidi)
        {
            clean.push_back(c);
        }
    }
    return trimmed(clean);
}

SearchResultList::SearchResultList()
{
    clipsChildren_ = true;
    // Typing carries on in the query however the list is used, as Up, Down and Return need it to.
    takesFocus_ = false;
    setRowHeight(kRowHeight);
    setRowInsets(-1.0F, 0.0F);
    drawRow = [this](ID2D1DeviceContext* context, int row, D2D1_RECT_F rect, RowState) { paintRow(context, row, rect); };

    get_ = add<Button>(
        L"Get",
        ButtonStyle::Accent,
        [this]
        {
            if (onGet)
            {
                onGet();
            }
        });
    get_->setHidden(true);
}

void SearchResultList::setRows(std::vector<SearchRow> rows)
{
    rows_ = std::move(rows);
    selected_ = -1;
    setRowCount(static_cast<int>(rows_.size()));
    if (scrollOffset() > 0.0F)
    {
        scrollTo(0.0F);
    }
    reloadData();
    placeGet();
}

void SearchResultList::select(int row)
{
    if (row == selected_ || row < 0 || row >= rowCount())
    {
        return;
    }
    int const previous = selected_;
    selected_ = row;
    reloadRow(previous);
    reloadRow(row);
    placeGet();
}

void SearchResultList::moveSelection(int delta)
{
    if (rowCount() == 0)
    {
        return;
    }
    int const row = std::clamp(selected_ + delta, 0, rowCount() - 1);
    select(row);
    revealRow(row);
}

void SearchResultList::layout()
{
    ListView::layout();
    placeGet();
}

void SearchResultList::scrolled()
{
    ListView::scrolled();
    placeGet();
}

void SearchResultList::placeGet()
{
    get_->setHidden(selected_ < 0);
    if (selected_ < 0)
    {
        return;
    }
    D2D1_RECT_F const row = rowRect(selected_);
    float const top = row.top + (kRowHeight - Button::kHeight) / 2.0F;
    float const right = width() - kGetInset;
    get_->setFrame(D2D1::RectF(right - kGetWidth, top, right, top + Button::kHeight));
}

bool SearchResultList::onMouse(MouseEvent const& event)
{
    // The selection is this class's own, because the Mac's has no hover and a softer shape than
    // ListView draws, so ListView's press and hover handling stays out of the way.
    switch (event.kind)
    {
    case MouseKind::Down:
        select(rowAt(event.point));
        return true;
    case MouseKind::DoubleClick:
        select(rowAt(event.point));
        if (rowAt(event.point) >= 0 && onGet)
        {
            onGet();
        }
        return true;
    case MouseKind::Up:
    case MouseKind::RightClick:
        return true;
    default:
        return Scrollable::onMouse(event);
    }
}

void SearchResultList::paintRow(ID2D1DeviceContext* context, int row, D2D1_RECT_F rect)
{
    Theme& theme = Theme::get();
    Palette const& colors = theme.colors();
    SearchRow const& entry = rows_[static_cast<size_t>(row)];

    if (row == selected_)
    {
        detail::fillRounded(context, detail::inset(rect, 0.0F, kSelectionInset), kSelectionRadius, withAlpha(colors.field, kSelectionAlpha));
    }

    float const getLeft = rect.right - kGetInset - kGetWidth;
    float const seedsRight = getLeft - kSeedsGap;
    float const healthRight = seedsRight - kSeedsWidth - kHealthGap;
    float const healthLeft = healthRight - kHealthWidth;
    float const textRight = healthLeft - kTextGap;
    float const left = rect.left + kRowLeft;

    theme.text(context, entry.result.title, kTitleText, D2D1::RectF(left, rect.top + kTitleTop, textRight, rect.top + kTitleTop + kTitleHeight), colors.bright);
    theme.text(context, entry.detail, kDetailText, D2D1::RectF(left, rect.top + kDetailTop, textRight, rect.top + kDetailTop + kDetailHeight), colors.foreground);

    if (entry.health < 0)
    {
        return;
    }
    float const midY = (rect.top + rect.bottom) / 2.0F;
    D2D1_RECT_F const seeds = D2D1::RectF(seedsRight - kSeedsWidth, rect.top, seedsRight, rect.bottom);
    theme.text(context, entry.seeds, kDetailText, seeds, colors.foreground, DWRITE_TEXT_ALIGNMENT_TRAILING, Trim::None, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    for (int dot = 0; dot < kHealthDots; ++dot)
    {
        float const centre = healthLeft + static_cast<float>(dot) * kHealthStep + kHealthDot / 2.0F;
        auto const ellipse = D2D1::Ellipse(D2D1::Point2F(centre, midY), kHealthDot / 2.0F, kHealthDot / 2.0F);
        context->FillEllipse(ellipse, theme.brush(withAlpha(colors.bright, dot < entry.health ? kDotOn : kDotOff)));
    }
}

SearchPanel::SearchPanel()
    : Panel(kPanelWidth, kPanelHeight)
{
    add<LeadingIcon>()->setFrame(D2D1::RectF(kFieldLeft, kFieldTop + (kFieldHeight - kIconSize) / 2.0F, kFieldLeft + kIconSize, kFieldTop + (kFieldHeight + kIconSize) / 2.0F));

    query_ = add<TextField>();
    query_->setFieldStyle(FieldStyle::Plain);
    query_->setTextStyle(kQueryText);
    query_->setPlaceholder(L"Search for a torrent");
    query_->onChange = [this](std::wstring const&) { queryChanged_ = true; };
    query_->onSubmit = [this]
    {
        if (list_->rowCount() > 0 && !queryChanged_)
        {
            getSelected();
        }
        else
        {
            search();
        }
    };

    sources_ = add<Dropdown>();
    sources_->onChange = [this](int index) { sourceChanged(index); };

    list_ = add<SearchResultList>();
    list_->onGet = [this] { getSelected(); };

    status_ = add<Label>(gMemory.status, kDetailText, ColorRole::Foreground);
    hint_ = add<Label>(L"Return to get, Esc to close", kDetailText, ColorRole::Foreground);
    hint_->setAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);

    openFolder = [](std::wstring const& folder) { ShellExecuteW(Window::get().handle(), L"open", folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL); };

    query_->setText(gMemory.query);
    reloadSources();
    list_->setRows(std::move(gMemory.rows));
    gMemory.rows.clear();
    list_->select(gMemory.selected);

    // Now, not when the animation ends, or results could land in a panel that's on its way out.
    // A panel that's replaced skips this and stops in the destructor.
    shouldClose = [this]
    {
        stopSearch();
        return true;
    };
}

SearchPanel::~SearchPanel()
{
    stopSearch();
    // A fetch carries on without this panel, so reopening shows what was here before it.
    gMemory.status = fetching_ ? gMemory.status : status_->text();
    gMemory.query = query_->text();
    gMemory.rows = std::move(list_->rows());
    gMemory.selected = list_->selected();
    gMemory.source = sources_->selected();
}

bool SearchPanel::searching()
{
    return gSearching;
}

void SearchPanel::stopSearch()
{
    if (gSearching)
    {
        gSearching = false;
        ++generation_;
        App::get().engine().cancelSearch();
        status_->setText(kDefaultStatus);
    }
}

void SearchPanel::reloadSources()
{
    std::vector<std::wstring> items{ L"All sources" };
    auto const names = App::get().engine().searchSourceNames();
    for (std::wstring const& name : names)
    {
        items.push_back(plainResultText(name));
    }
    items.emplace_back(L"Add sources…");

    // A source added or removed since last time moves the others, so the old choice means nothing.
    bool const unchanged = gMemory.sourceCount == static_cast<int>(names.size());
    gMemory.sourceCount = static_cast<int>(names.size());
    sources_->setItems(std::move(items));
    sources_->setSelected(unchanged ? gMemory.source : 0);
}

void SearchPanel::sourceChanged(int index)
{
    if (index == gMemory.sourceCount + 1)
    {
        sources_->setSelected(0);
        openFolder(App::get().engine().searchSourcesFolder());
        return;
    }
    if (!query_->text().empty())
    {
        search();
    }
}

void SearchPanel::showStatus(std::wstring const& text, ColorRole role)
{
    status_->setRole(role);
    status_->setText(text);
}

void SearchPanel::search()
{
    std::wstring const query = trimmed(query_->text());
    if (query.empty() || closing())
    {
        return;
    }

    queryChanged_ = false;
    int const generation = ++generation_;
    list_->setRows({});
    showStatus(L"Searching…");

    gSearching = true;
    App::get().engine().search(
        query,
        sources_->selected() - 1,
        [this, generation, alive = std::weak_ptr<bool>(alive_)](engine::SearchOutcome const& outcome)
        {
            if (!alive.expired() && generation == generation_ && !outcome.cancelled)
            {
                gSearching = false;
                showOutcome(outcome);
            }
        });
}

void SearchPanel::showOutcome(engine::SearchOutcome const& outcome)
{
    std::vector<SearchRow> rows;
    rows.reserve(outcome.results.size());
    for (engine::SearchResult const& result : outcome.results)
    {
        rows.push_back(makeRow(result));
    }
    size_t const count = rows.size();
    list_->setRows(std::move(rows));
    list_->select(0);

    if (outcome.responded == 0 && outcome.asked > 0)
    {
        showStatus(L"Couldn't reach the sources");
    }
    else if (count == 0)
    {
        showStatus(L"Nothing found");
    }
    else
    {
        showStatus(std::to_wstring(count) + L" results from " + std::to_wstring(outcome.responded) + L" of " + std::to_wstring(outcome.asked) + L" sources");
    }
}

void SearchPanel::getSelected()
{
    int const row = list_->selected();
    if (row < 0 || fetching_ || closing())
    {
        return;
    }
    engine::SearchResult const& result = list_->rows()[static_cast<size_t>(row)].result;
    fetching_ = true;
    gMemory.status = status_->text();
    showStatus(L"Fetching…");
    openLink(
        result.link,
        [this, title = result.title, alive = std::weak_ptr<bool>(alive_)](std::wstring const& error)
        {
            if (alive.expired())
            {
                return;
            }
            fetching_ = false;
            if (error.empty())
            {
                showStatus(L"Added " + title);
                return;
            }
            showStatus(error, ColorRole::Danger);
            refuse();
        });
}

bool SearchPanel::onKey(UINT key, bool down)
{
    if (key == VK_UP || key == VK_DOWN)
    {
        if (down)
        {
            list_->moveSelection(key == VK_DOWN ? 1 : -1);
        }
        return true;
    }
    return Panel::onKey(key, down);
}

void SearchPanel::layoutContent()
{
    float const dropdownWidth = std::max(sources_->preferredWidth(), kSourcesMinWidth);
    float const dropdownLeft = width() - kSourcesInset - dropdownWidth;
    float const fieldLeft = kFieldLeft + kIconSize + kIconGap;
    float const fieldBottom = kFieldTop + kFieldHeight;
    query_->setFrame(D2D1::RectF(fieldLeft, kFieldTop, dropdownLeft - kSourcesGap, fieldBottom));
    float const dropdownTop = kFieldTop + (kFieldHeight - Dropdown::kHeight) / 2.0F;
    sources_->setFrame(D2D1::RectF(dropdownLeft, dropdownTop, dropdownLeft + dropdownWidth, dropdownTop + Dropdown::kHeight));

    float const footerBottom = height() - kFooterBottom;
    float const footerTop = footerBottom - kFooterHeight;
    list_->setFrame(D2D1::RectF(kListInset, fieldBottom + kListTopGap, width() - kListInset, footerTop - kListBottomGap));

    float const hintLeft = width() - kFooterInset - kHintWidth;
    hint_->setFrame(D2D1::RectF(hintLeft, footerTop, hintLeft + kHintWidth, footerBottom));
    status_->setFrame(D2D1::RectF(kFooterInset, footerTop, hintLeft - kFooterGap, footerBottom));
}

void openSearchPanelWith(std::wstring const& query)
{
    openSearchPanel();
    if (auto* panel = dynamic_cast<SearchPanel*>(Window::get().panel()))
    {
        panel->query()->setText(query);
        panel->search();
    }
}

void openSearchPanel()
{
    Window& window = Window::get();
    if (addPanelShowing())
    {
        return;
    }
    auto* panel = dynamic_cast<SearchPanel*>(window.panel());
    if (panel == nullptr || panel->closing())
    {
        panel = Panel::open<SearchPanel>();
    }
    window.focus(panel->query());
    panel->query()->selectAll();
}

} // namespace app
