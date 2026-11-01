#pragma once

#include <array>
#include <memory>
#include <string>

#include "app/details_common.h"
#include "ui/widgets.h"

namespace app::details
{

/// Overview, Files, Peers, Trackers and Options for one transfer, as InfoWindowController.
class DetailsPanel final : public ui::Panel
{
public:
    explicit DetailsPanel(engine::TransferId id);
    ~DetailsPanel() override;

    engine::TransferId transfer() const { return id_; }
    Tab currentTab() const { return current_; }
    void showTab(Tab tab);
    /// The tab's view once it has been shown, built the first time it is asked for.
    TabView* tabView(Tab tab) const { return tabs_[static_cast<size_t>(tab)]; }
    ui::PillBar* pills() const { return pills_; }

    /// How many times each tab has been refreshed, for --details-test.
    int refreshCount(Tab tab) const { return refreshes_[static_cast<size_t>(tab)]; }

    bool onKey(UINT key, bool down) override;

protected:
    void layoutContent() override;
    void adjustGlass(GlassShape& shape) const override;

private:
    void onRefresh();
    void onEvent(engine::Event const& event);
    void updateHeader(engine::TransferInfo const& info);
    void readFileCount();
    [[nodiscard]] engine::TransferInfo const* findInfo() const;

    engine::TransferId id_ = engine::kNoTransfer;
    ui::Label* name_ = nullptr;
    ui::Label* summary_ = nullptr;
    ui::PillBar* pills_ = nullptr;
    std::array<TabView*, kTabCount> tabs_{};
    std::array<int, kTabCount> refreshes_{};
    Tab current_ = Tab::Overview;
    size_t fileCount_ = 0;
    bool laidOut_ = false;
    int refreshListener_ = 0;
    int eventListener_ = 0;
};

} // namespace app::details
