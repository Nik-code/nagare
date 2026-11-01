#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "engine/engine.h"
#include "ui/widgets.h"

namespace app
{

/// Which files of a preview are wanted and what they will be called. Nothing exists in the session
/// until Get, so this stands in for the Mac's paused Torrent.
class AddFileList
{
public:
    explicit AddFileList(engine::FileTree tree);

    std::vector<engine::FileNode> const& nodes() const { return nodes_; }
    size_t fileCount() const { return wanted_.size(); }
    uint64_t selectedBytes() const { return selectedBytes_; }
    bool allWanted() const { return wantedFiles_ == fileCount(); }
    bool anyWanted() const { return wantedFiles_ > 0; }
    /// One entry per file index, which is what AddRequest takes.
    std::vector<uint8_t> const& wanted() const { return wanted_; }

    /// Mixed for a folder holding both.
    ui::CheckState checkState(int node) const;
    void setWanted(int node, bool wanted);
    void setAllWanted(bool wanted);

    /// The nodes on show, which is every node not inside a collapsed folder.
    std::vector<int> const& rows() const { return rows_; }
    engine::FileNode const& rowNode(int row) const { return nodes_[static_cast<size_t>(rows_[static_cast<size_t>(row)])]; }
    /// The row showing this node, or -1 inside a collapsed folder.
    int rowOf(int node) const { return rowOf_[static_cast<size_t>(node)]; }
    bool expanded(int node) const { return expanded_[static_cast<size_t>(node)] != 0; }
    void setExpanded(int node, bool expanded);

    /// False if Windows wouldn't take the name, or a neighbour already has it.
    bool rename(int node, std::wstring const& name);
    std::vector<engine::FileRename> renames() const;

private:
    void rollUp();
    void rebuildRows();
    [[nodiscard]] bool nameTaken(int node, std::wstring const& name) const;

    std::vector<engine::FileNode> nodes_;
    std::vector<uint8_t> wanted_;
    std::vector<int> wantedInside_;
    std::vector<uint8_t> expanded_;
    std::vector<int> rows_;
    std::vector<int> rowOf_;
    std::map<int, std::wstring> originalNames_;
    size_t wantedFiles_ = 0;
    uint64_t selectedBytes_ = 0;
};

/// The file tree: a chevron and a checkbox on each row, with rename in place.
class AddFileRows final : public ui::ListView
{
public:
    explicit AddFileRows(AddFileList& files);

    /// Which files are wanted changed.
    std::function<void()> onWantedChanged;

    void beginRename(int row);
    bool renaming() const { return editRow_ >= 0; }
    ui::TextField* editor() const { return editor_; }

    // In window coordinates, for the self test.
    D2D1_POINT_2F checkPoint(int row) const;
    D2D1_POINT_2F chevronPoint(int row) const;
    D2D1_POINT_2F pencilPoint(int row) const;

    bool onMouse(ui::MouseEvent const& event) override;
    bool onKey(UINT key, bool down) override;

protected:
    void scrolled() override;

private:
    enum class Zone
    {
        None,
        Chevron,
        Check,
        Pencil,
    };

    void paintRow(ID2D1DeviceContext* context, int row, D2D1_RECT_F rect, ui::RowState state);
    [[nodiscard]] Zone zoneAt(int row, D2D1_POINT_2F point) const;
    void toggleCheck(int row);
    void setExpanded(int row, bool expanded);
    void setSelectedWanted(bool wanted);
    void wantOnlySelected();
    void wantedChanged();
    void finishRename(bool keep);
    void placeEditor();
    void showMenu(D2D1_POINT_2F point);
    [[nodiscard]] std::vector<int> selectedNodes() const;

    AddFileList& files_;
    ui::TextField* editor_ = nullptr;
    int editRow_ = -1;
    int pressedRow_ = -1;
    Zone pressedZone_ = Zone::None;
};

/// The one Add panel, for torrents and magnets alike. The preview only becomes a transfer on Get.
class AddPanel final : public ui::Panel
{
public:
    struct Parts
    {
        ui::Label* title = nullptr;
        ui::TextField* titleEditor = nullptr;
        ui::IconButton* pencil = nullptr;
        ui::Label* status = nullptr;
        ui::Label* filesTitle = nullptr;
        ui::Button* all = nullptr;
        ui::Button* none = nullptr;
        AddFileRows* rows = nullptr;
        ui::Label* saveTitle = nullptr;
        ui::Label* saveHint = nullptr;
        ui::Button* change = nullptr;
        ui::Label* startTitle = nullptr;
        ui::Switch* start = nullptr;
        ui::Label* trashTitle = nullptr;
        ui::Switch* trash = nullptr;
        ui::Dropdown* priority = nullptr;
        ui::Button* cancel = nullptr;
        ui::Button* get = nullptr;
    };

    explicit AddPanel(engine::AddPreview preview);
    ~AddPanel() override;

    Parts const& parts() const { return parts_; }
    AddFileList const& files() const { return files_; }

    /// What Get asks the engine for, as the panel stands now.
    engine::AddRequest request() const;
    /// Replaces the folder picker, which a self test can't drive.
    std::function<std::optional<std::wstring>()> chooseFolder;

    void beginTitleRename();
    void changeFolder();
    void get();

    void draw(ID2D1DeviceContext* context) override;
    bool onMouse(ui::MouseEvent const& event) override;
    bool onKey(UINT key, bool down) override;

protected:
    void layoutContent() override;

private:
    void build();
    void finishTitleRename(bool keep);
    void updateStatus();
    void showProblem(std::wstring const& text);
    void setDestination(std::wstring folder);
    [[nodiscard]] std::vector<std::wstring> warnings() const;

    bool const showFiles_;
    engine::AddPreview preview_;
    AddFileList files_;
    Parts parts_;
    std::wstring originalName_;
    std::wstring name_;
    std::wstring destination_;
    std::vector<std::wstring> acknowledged_;
    std::vector<float> rules_;
    bool renamingTitle_ = false;
};

/// True while the Add panel or its duplicate notice is up, which Link and Search don't replace.
bool addPanelShowing();

/// add_panel_test.cpp: drives the panels on the demo engine and quits with the failure count.
void runPanelsTest();
/// add_panel_test.cpp: `add-files` and `search-results`, the scenes in add.png and search.png.
void openPanelDemo(std::wstring_view name);

} // namespace app
