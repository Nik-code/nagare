#include "app/details_panel.h"

#include <shellapi.h>

#include <algorithm>
#include <string_view>

#include "app/app.h"
#include "app/details_files.h"

namespace app
{

namespace details
{

namespace
{

using namespace ui;

constexpr float kPanelWidth = 640.0F;
constexpr float kPanelHeight = 720.0F;
constexpr float kNameTop = 20.0F;
constexpr float kNameHeight = 32.0F;
constexpr float kSummaryTop = 52.0F;
constexpr float kSummaryHeight = 18.0F;
constexpr float kPillsTop = 76.0F;
constexpr float kContentTop = 112.0F;
// The Mac's window is opaque, so its dense lists never fight what's below. Glass this much heavier
// keeps them legible over a busy transfer list while the sheet still reads as glass.
constexpr float kTintScale = 2.2F;

constexpr TextStyle kName{ 24.0F, DWRITE_FONT_WEIGHT_MEDIUM, true };

// Survives closing, as the Mac's InspectorSelected does.
Tab gLastTab = Tab::Overview;

constexpr std::array<std::wstring_view, kTabCount> kTabNames{ L"overview", L"files", L"peers", L"trackers", L"options" };

/// --tab for the build checks, honoured by the first panel of a run.
void readTabFromCommandLine(Tab& tab)
{
    static bool used = false;
    if (used)
    {
        return;
    }
    used = true;

    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    for (int i = 1; i + 1 < argc; ++i)
    {
        if (std::wstring_view(argv[i]) != L"--tab")
        {
            continue;
        }
        for (size_t index = 0; index < kTabNames.size(); ++index)
        {
            if (kTabNames[index] == argv[i + 1])
            {
                tab = static_cast<Tab>(index);
            }
        }
    }
    LocalFree(argv);
}

} // namespace

DetailsPanel::DetailsPanel(engine::TransferId id)
    : Panel(kPanelWidth, kPanelHeight)
    , id_(id)
{
    name_ = add<Label>(L"", kName, ColorRole::Bright);
    name_->setTrim(Trim::Middle);
    summary_ = add<Label>(L"", kBody, ColorRole::Foreground);

    pills_ = add<PillBar>();
    pills_->setPills({ { L"Overview" }, { L"Files" }, { L"Peers" }, { L"Trackers" }, { L"Options" } });
    pills_->onChange = [this](int index) { showTab(static_cast<Tab>(index)); };

    // Whatever is half typed goes in, as it does when the Mac's fields lose focus.
    shouldClose = []
    {
        Window::get().focus(nullptr);
        return true;
    };

    App& app = App::get();
    refreshListener_ = app.addRefreshListener([this] { onRefresh(); });
    eventListener_ = app.addEventListener([this](engine::Event const& event) { onEvent(event); });

    readFileCount();
    if (auto const* info = findInfo(); info != nullptr)
    {
        updateHeader(*info);
    }

    Tab tab = gLastTab;
    readTabFromCommandLine(tab);
    showTab(tab);
}

DetailsPanel::~DetailsPanel()
{
    App& app = App::get();
    app.removeRefreshListener(refreshListener_);
    app.removeEventListener(eventListener_);
}

engine::TransferInfo const* DetailsPanel::findInfo() const
{
    auto const& transfers = App::get().transfers();
    auto const found = std::lower_bound(transfers.begin(), transfers.end(), id_, [](engine::TransferInfo const& info, engine::TransferId id) { return info.id < id; });
    return found != transfers.end() && found->id == id_ ? &*found : nullptr;
}

void DetailsPanel::readFileCount()
{
    fileCount_ = App::get().engine().details(id_).fileCount;
}

void DetailsPanel::updateHeader(engine::TransferInfo const& info)
{
    name_->setText(info.name);

    if (info.isMagnet)
    {
        summary_->setText(L"Magnetized transfer");
        return;
    }
    TextBuffer buffer;
    std::wstring text;
    if (info.isFolder)
    {
        text = fileCount_ == 1 ? L"1 file" : std::to_wstring(fileCount_) + L" files";
        text += L", ";
    }
    text += fileSizeText(info.totalBytes, buffer);
    summary_->setText(text);
}

void DetailsPanel::showTab(Tab tab)
{
    // Commits whatever field had the keyboard before its tab goes out of sight.
    Window::get().focus(nullptr);

    auto const index = static_cast<size_t>(tab);
    if (tabs_[index] == nullptr)
    {
        std::unique_ptr<TabView> view;
        switch (tab)
        {
        case Tab::Overview:
            view = createOverviewTab(id_);
            break;
        case Tab::Files:
            view = std::make_unique<FilesTab>(id_);
            break;
        case Tab::Peers:
            view = createPeersTab(id_);
            break;
        case Tab::Trackers:
            view = createTrackersTab(id_);
            break;
        case Tab::Options:
            view = createOptionsTab(id_);
            break;
        }
        tabs_[index] = view.get();
        view->setHidden(true);
        addChild(std::move(view));
        tabs_[index]->setFrame(D2D1::RectF(0.0F, kContentTop, width(), height()));
    }

    for (TabView* view : tabs_)
    {
        if (view != nullptr)
        {
            view->setHidden(view != tabs_[index]);
        }
    }
    current_ = tab;
    gLastTab = tab;
    pills_->setSelected(static_cast<int>(index), laidOut_);

    // The first tab is chosen before the panel has a size, so layoutContent shows it instead.
    if (auto const* info = findInfo(); info != nullptr && laidOut_)
    {
        tabs_[index]->shown(*info);
    }
}

void DetailsPanel::onRefresh()
{
    if (closing() || !Window::get().visible())
    {
        return;
    }
    auto const* info = findInfo();
    if (info == nullptr)
    {
        // The transfer was removed, so there is nothing left to describe.
        close();
        return;
    }
    updateHeader(*info);
    auto const index = static_cast<size_t>(current_);
    ++refreshes_[index];
    tabs_[index]->refresh(*info);
}

void DetailsPanel::onEvent(engine::Event const& event)
{
    if (event.type != engine::EventType::MetadataReceived || event.id != id_ || closing())
    {
        return;
    }
    readFileCount();
    for (TabView* view : tabs_)
    {
        if (view != nullptr)
        {
            view->metadataChanged();
        }
    }
    if (auto const* info = findInfo(); info != nullptr)
    {
        updateHeader(*info);
        tabs_[static_cast<size_t>(current_)]->shown(*info);
    }
}

bool DetailsPanel::onKey(UINT key, bool down)
{
    if (key == VK_TAB && (GetKeyState(VK_CONTROL) & 0x8000) != 0)
    {
        if (down)
        {
            int const step = (GetKeyState(VK_SHIFT) & 0x8000) != 0 ? kTabCount - 1 : 1;
            showTab(static_cast<Tab>((static_cast<int>(current_) + step) % kTabCount));
        }
        return true;
    }
    return Panel::onKey(key, down);
}

void DetailsPanel::layoutContent()
{
    float const contentWidth = width() - kMargin * 2.0F;
    name_->setFrame(D2D1::RectF(kMargin, kNameTop, kMargin + contentWidth, kNameTop + kNameHeight));
    summary_->setFrame(D2D1::RectF(kMargin, kSummaryTop, kMargin + contentWidth, kSummaryTop + kSummaryHeight));
    pills_->setFrame(D2D1::RectF(kMargin, kPillsTop, kMargin + std::min(pills_->preferredWidth(), contentWidth), kPillsTop + PillBar::kHeight));
    for (TabView* view : tabs_)
    {
        if (view != nullptr)
        {
            view->setFrame(D2D1::RectF(0.0F, kContentTop, width(), height()));
        }
    }
    if (!laidOut_)
    {
        laidOut_ = true;
        if (auto const* info = findInfo(); info != nullptr)
        {
            tabs_[static_cast<size_t>(current_)]->shown(*info);
        }
    }
}

void DetailsPanel::adjustGlass(GlassShape& shape) const
{
    Panel::adjustGlass(shape);
    shape.tint.a = std::min(1.0F, shape.tint.a * kTintScale);
}

} // namespace details

void openDetailsPanel(int transferId)
{
    ui::Panel::open<details::DetailsPanel>(transferId);
}

} // namespace app
