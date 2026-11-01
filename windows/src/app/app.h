#pragma once

#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "engine/engine.h"
#include "ui/view.h"

namespace app
{

/// Owns the engine and the refresh rhythm. Everything here runs on the UI thread.
class App
{
public:
    static App& get();

    /// Starts the engine and builds the main view. Call after the window exists. An empty folder
    /// means the usual place.
    bool start(std::wstring const& configDir = {});
    /// Saves and closes the engine. The window is already hidden by now. Safe to call twice.
    void shutdown();

    engine::Engine& engine() { return *engine_; }

    /// The latest snapshot, refreshed once a second while visible and every five seconds when hidden.
    std::vector<engine::TransferInfo> const& transfers() const { return *transfers_; }

    /// Runs after each refresh and after anything that changed transfers. Remove it in the destructor.
    int addRefreshListener(std::function<void()> listener);
    void removeRefreshListener(int id);
    /// Refreshes now instead of waiting for the next tick, for use after a user action.
    void refreshNow();

    /// Runs for every engine event, after the transfers have refreshed.
    int addEventListener(std::function<void(engine::Event const&)> listener);
    void removeEventListener(int id);

    /// Opens a magnet link or a .torrent path, as sent by the command line or another launch.
    void open(std::wstring const& pathOrMagnet);
    /// Opens .torrent files and magnet links, one Add panel at a time.
    void openPaths(std::vector<std::wstring> const& paths);
    void openMagnet(std::wstring const& uri);
    void openPreview(engine::AddPreview preview);
    /// The Add panel calls this when it closes, so the next queued item can open.
    void addPanelClosed();

private:
    void tick();
    void applyTheme();
    void showNextAdd();

    std::unique_ptr<engine::Engine> engine_;
    std::vector<engine::TransferInfo> const* transfers_ = nullptr;
    std::vector<std::pair<int, std::function<void()>>> refreshListeners_;
    std::vector<std::pair<int, std::function<void(engine::Event const&)>>> eventListeners_;
    std::deque<engine::AddPreview> addQueue_;
    bool addPanelOpen_ = false;
    bool shutDown_ = false;
    int nextListener_ = 1;
    int timer_ = 0;
    bool wasVisible_ = true;
};

// One entry point per screen, each owned by its own file.

/// main_view.cpp: the header, pills and transfer list.
std::unique_ptr<ui::View> createMainView();
/// add_panel.cpp: the one Add panel for torrents and magnets. Must call App::addPanelClosed when closed.
void openAddPanel(engine::AddPreview preview);
/// link_panel.cpp: paste a magnet link.
void openLinkPanel();
/// search_panel.cpp
void openSearchPanel();
/// Opens Search and runs `query`, for checking live sources from the command line.
void openSearchPanelWith(std::wstring const& query);
/// details_panel.cpp: Overview, Files, Peers, Trackers and Options for one transfer.
void openDetailsPanel(int transferId);
/// settings_panel.cpp
void openSettingsPanel();

/// For the build checks: add, link, search, details, settings, default or quit.
void openScreen(std::wstring_view name);

} // namespace app
