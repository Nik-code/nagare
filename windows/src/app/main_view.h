#pragma once

#include <map>
#include <string>
#include <vector>

#include "app/app.h"
#include "app/swell_header.h"
#include "app/transfer_row_list.h"
#include "ui/widgets.h"

namespace app
{

class DropTarget;
class DropOverlay;

enum class TransferFilter
{
    All,
    Download,
    Seed,
    Pause,
    Error,
};

enum class TransferSort
{
    Order,
    Activity,
    Date,
    Name,
    Progress,
    Eta,
    Size,
    State,
    Tracker,
};

/// The window's one screen: header, filter pills and the transfer list.
class MainView final : public ui::View
{
public:
    MainView();
    ~MainView() override;

    void layout() override;
    void draw(ID2D1DeviceContext* context) override;
    bool onKey(UINT key, bool down) override;

    // What the tests drive.

    [[nodiscard]] TransferList* list() const { return list_; }
    [[nodiscard]] SwellHeader* header() const { return header_; }
    [[nodiscard]] ui::PillBar* pills() const { return pills_; }
    [[nodiscard]] ui::Label* totalLabel() const { return total_; }
    [[nodiscard]] ui::Label* emptyLabel() const { return empty_; }
    [[nodiscard]] DropOverlay* overlay() const { return overlay_; }
    [[nodiscard]] DropTarget* dropTarget() const { return dropTarget_; }
    [[nodiscard]] TransferFilter filter() const { return filter_; }
    void setFilter(TransferFilter filter);
    void setSort(TransferSort sort);

private:
    void refresh();
    void update();
    void updatePills(engine::SessionStats const& stats);
    void rebuildRows();
    void updateTotal(engine::SessionStats const& stats);
    [[nodiscard]] bool passes(engine::TransferInfo const& info) const;
    [[nodiscard]] int compare(engine::TransferInfo const& a, engine::TransferInfo const& b) const;
    [[nodiscard]] bool sortsBefore(int a, int b) const;
    void cycleFilter(bool forward);
    void persistView();

    // main_actions.cpp

    using Ids = std::vector<engine::TransferId>;

    [[nodiscard]] engine::TransferInfo const* find(engine::TransferId id) const;
    [[nodiscard]] Ids selection() const { return list_->selectedIds(); }
    [[nodiscard]] Ids idsWhere(bool (*test)(engine::TransferInfo const&)) const;
    [[nodiscard]] bool anySelected(bool (*test)(engine::TransferInfo const&)) const;

    void pause(Ids const& ids);
    void resume(Ids const& ids);
    void resumeNow(Ids const& ids);
    void toggle(Ids const& ids);
    void verify(Ids const& ids);
    void reannounce(Ids const& ids);
    void remove(Ids const& ids, bool deleteData);
    void removeNow(Ids const& ids, bool deleteData);
    void removeCompleted();
    void moveInQueue(engine::QueueMove move);
    void setGroup(int group);
    void openFolder(Ids const& ids);
    void reveal(Ids const& ids);
    void copyMagnets(Ids const& ids);
    void moveData(Ids const& ids);
    void saveTorrentFiles(Ids const& ids);
    void renameSelected();
    void paste();
    void openTorrentFiles();
    void toggleSpeedLimit();
    void showInfo();
    void doubleClicked(int row);

    [[nodiscard]] std::vector<ui::MenuItem> rowMenu();
    [[nodiscard]] std::vector<ui::MenuItem> emptyMenu();
    [[nodiscard]] std::vector<ui::MenuItem> moreMenu();
    [[nodiscard]] ui::MenuItem sortMenu();

    SwellHeader* header_ = nullptr;
    ui::PillBar* pills_ = nullptr;
    ui::Label* total_ = nullptr;
    TransferList* list_ = nullptr;
    ui::Label* empty_ = nullptr;
    DropOverlay* overlay_ = nullptr;

    int listener_ = 0;
    std::vector<int> rows_;
    std::vector<TransferFilter> filters_;
    TransferFilter filter_ = TransferFilter::All;
    TransferSort sort_ = TransferSort::Order;
    bool reverse_ = false;
    bool useGroups_ = false;
    bool stale_ = false;
    std::map<engine::TransferId, std::wstring> trackerKeys_;
    DropTarget* dropTarget_ = nullptr;
};

/// main_view_test.cpp: the --main-test checks, run in a live --demo session. Exits with the failure count.
void runMainViewTests();

} // namespace app
