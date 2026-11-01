#include "app/app.h"

#include <dwmapi.h>

#include <algorithm>

#include "app/add_panel.h"
#include "app/shell_integration.h"
#include "ui/theme.h"
#include "ui/window.h"

namespace app
{

namespace
{

constexpr DWORD kVisibleIntervalMs = 1000;
constexpr DWORD kHiddenIntervalMs = 5000;

} // namespace

App& App::get()
{
    static App app;
    return app;
}

bool App::start(std::wstring const& configDir)
{
    ui::Window& window = ui::Window::get();
    engine_ = engine::createEngineFromCommandLine();

    window.onMessage = [this](UINT message, WPARAM wparam, LPARAM lparam)
    {
        if (message != engine::kDefaultNotifyMessage)
        {
            return shell::handleMessage(message, wparam, lparam);
        }
        if (!shutDown_)
        {
            engine_->pump();
        }
        return true;
    };

    engine::StartOptions options;
    options.configDir = configDir;
    options.notifyWindow = window.handle();
    options.onEvent = [this](engine::Event const& event)
    {
        refreshNow();
        for (auto const& [id, listener] : std::vector(eventListeners_))
        {
            listener(event);
        }
    };
    if (!engine_->start(options))
    {
        return false;
    }

    transfers_ = &engine_->refresh();
    applyTheme();
    window.setRoot(createMainView());

    window.onFilesDropped = [this](std::vector<std::wstring> const& paths) { openPaths(paths); };
    window.onOpenRequest = [this, &window](std::wstring const& text)
    {
        window.bringForward();
        if (!text.empty())
        {
            open(text);
        }
    };
    window.onShown = [this] { refreshNow(); };

    if (!engine_->prefs().qbittorrentImported)
    {
        engine_->importFromQBittorrent();
    }

    timer_ = window.every(kVisibleIntervalMs, [this] { tick(); });
    return true;
}

void App::shutdown()
{
    if (engine_ != nullptr && !shutDown_)
    {
        shutDown_ = true;
        ui::Window::get().cancel(timer_);
        engine_->shutdown();
    }
}

void App::applyTheme()
{
    ui::Theme::get().setMode(static_cast<ui::ThemeMode>(engine_->prefs().themeMode));
    BOOL dark = ui::Theme::get().dark();
    DwmSetWindowAttribute(ui::Window::get().handle(), DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
}

void App::tick()
{
    ui::Window& window = ui::Window::get();
    bool const visible = window.visible();
    if (visible != wasVisible_)
    {
        wasVisible_ = visible;
        window.cancel(timer_);
        timer_ = window.every(visible ? kVisibleIntervalMs : kHiddenIntervalMs, [this] { tick(); });
    }
    refreshNow();
}

void App::refreshNow()
{
    if (shutDown_)
    {
        return;
    }
    transfers_ = &engine_->refresh();
    for (auto const& [id, listener] : std::vector(refreshListeners_))
    {
        listener();
    }
}

int App::addRefreshListener(std::function<void()> listener)
{
    int const id = nextListener_++;
    refreshListeners_.emplace_back(id, std::move(listener));
    return id;
}

void App::removeRefreshListener(int id)
{
    std::erase_if(refreshListeners_, [id](auto const& entry) { return entry.first == id; });
}

int App::addEventListener(std::function<void(engine::Event const&)> listener)
{
    int const id = nextListener_++;
    eventListeners_.emplace_back(id, std::move(listener));
    return id;
}

void App::removeEventListener(int id)
{
    std::erase_if(eventListeners_, [id](auto const& entry) { return entry.first == id; });
}

void App::open(std::wstring const& pathOrMagnet)
{
    if (_wcsnicmp(pathOrMagnet.c_str(), L"magnet:", 7) == 0)
    {
        openMagnet(pathOrMagnet);
    }
    else
    {
        openPaths({ pathOrMagnet });
    }
}

void App::openPaths(std::vector<std::wstring> const& paths)
{
    for (std::wstring const& path : paths)
    {
        auto preview = engine_->parseTorrentFile(path);
        if (preview.valid)
        {
            preview.torrentFile = path;
            addQueue_.push_back(std::move(preview));
        }
    }
    showNextAdd();
}

void App::openMagnet(std::wstring const& uri)
{
    auto preview = engine_->parseMagnet(uri);
    if (preview.valid)
    {
        openPreview(std::move(preview));
    }
}

void App::openPreview(engine::AddPreview preview)
{
    addQueue_.push_back(std::move(preview));
    showNextAdd();
}

void App::addPanelClosed()
{
    addPanelOpen_ = false;
    showNextAdd();
}

void App::showNextAdd()
{
    if (addPanelOpen_ || addQueue_.empty())
    {
        return;
    }
    addPanelOpen_ = true;
    auto preview = std::move(addQueue_.front());
    addQueue_.pop_front();
    openAddPanel(std::move(preview));
}

void openScreen(std::wstring_view name)
{
    if (name == L"add")
    {
        App::get().openMagnet(L"magnet:?xt=urn:btih:08ada5a7a6183aae1e09d831df6748d566095a10&dn=Blender+Open+Movies");
    }
    else if (name == L"add-files" || name == L"search-results")
    {
        openPanelDemo(name);
    }
    else if (name == L"link")
    {
        openLinkPanel();
    }
    else if (name == L"search")
    {
        openSearchPanel();
    }
    else if (name == L"settings")
    {
        openSettingsPanel();
    }
    else if (name == L"details" && !App::get().transfers().empty())
    {
        openDetailsPanel(App::get().transfers().front().id);
    }
    else if (name == L"default")
    {
        shell::showDefaultPrompt();
    }
    else if (name == L"quit")
    {
        shell::showQuitPrompt();
    }
}

} // namespace app
