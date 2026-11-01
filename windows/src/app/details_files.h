#pragma once

#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "app/details_common.h"

namespace app::details
{

/// The file tree and what's visible of it. Holds no view, so it can be tested with any tree.
class FileModel
{
public:
    /// Keeps which folders are open, matched by path.
    void setTree(engine::FileTree tree);
    engine::FileTree& tree() { return tree_; }

    /// Node index per row, always ascending. Only open folders contribute, or the matches of a filter.
    std::vector<int> const& rows() const { return rows_; }
    engine::FileNode const& nodeAt(int row) const { return tree_.nodes[static_cast<size_t>(rows_[static_cast<size_t>(row)])]; }

    bool isExpanded(int node) const { return expanded_[static_cast<size_t>(node)] != 0; }
    void setExpanded(int node, bool expanded);
    /// Folders and everything inside them, as an option-click does on the Mac.
    void setSubtreeExpanded(int node, bool expanded);

    /// The Mac's filter: every word must appear in a file's name, ignoring case and accents. Folders
    /// drop out and the matching files list flat.
    void setFilter(std::wstring_view text);
    bool filtering() const { return !terms_.empty(); }

    /// The row a node sits on, or -1 when a closed folder or the filter hides it.
    [[nodiscard]] int rowOf(int node) const;

    /// Every file under these rows, each once.
    [[nodiscard]] std::vector<int> fileIndexes(std::span<int const> rows) const;

    /// A folder's path changed: open folders keep their state across the rename.
    void renameExpanded(std::wstring const& from, std::wstring const& to);

private:
    void rebuild();
    [[nodiscard]] bool matches(engine::FileNode const& node) const;

    engine::FileTree tree_;
    std::vector<uint8_t> expanded_;
    std::vector<int> rows_;
    std::vector<std::wstring> terms_;
    std::vector<std::wstring> openPaths_;
};

/// Parts of a row, for hit testing and for --details-test.
enum class FileZone
{
    None,
    Disclosure,
    Rename,
    Priority,
    Check,
};

class FileList;

class FilesTab final : public TabView
{
public:
    explicit FilesTab(engine::TransferId id);

    void refresh(engine::TransferInfo const& info) override;
    void shown(engine::TransferInfo const& info) override;
    void metadataChanged() override;
    void layout() override;

    /// For --details-test: a tree no demo transfer has, and direct access to what's on screen.
    void adoptTree(engine::FileTree tree);
    FileModel& model() { return model_; }
    ui::ListView* list() const;
    ui::TextField* filterField() const { return filter_; }
    ui::TextField* renameField() const;
    /// The middle of a part of a row, in window coordinates.
    [[nodiscard]] D2D1_POINT_2F zoneCentre(int row, FileZone zone) const;
    void beginRename(int row);

private:
    friend class FileList;

    void load();
    /// Runs a change that moves rows, keeping the selection on the same nodes.
    void reshape(std::function<void()> const& change);
    void applyFilter(std::wstring const& text);
    void updateTree();
    void updateButtons(engine::TransferInfo const& info);

    void toggleWanted(int row, bool everyFile);
    void cyclePriority(int row);
    void toggleExpanded(int row, bool wholeSubtree);
    void setAllWanted(bool wanted);
    void setSelectedWanted(bool wanted);
    void setOnlySelectedWanted();
    void setSelectedPriority(engine::Priority priority);
    void revealSelected();
    void openSelected();
    void showMenu(D2D1_POINT_2F point);
    void finishRename(std::wstring const& node, std::wstring const& name);
    void changed();

    [[nodiscard]] std::vector<int> selectedFiles() const;
    [[nodiscard]] std::wstring locationOf(engine::FileNode const& node, std::wstring const& folder) const;
    [[nodiscard]] std::vector<std::wstring> selectedLocations() const;

    engine::TransferId id_;
    FileModel model_;
    FileList* list_ = nullptr;
    ui::TextField* filter_ = nullptr;
    ui::Button* all_ = nullptr;
    ui::Button* none_ = nullptr;

    bool loaded_ = false;
    bool stale_ = true;
    bool isFolder_ = true;
    uint64_t lastHave_ = 0;

    struct Seen
    {
        uint64_t have = 0;
        engine::Mixed wanted = engine::Mixed::On;
        engine::Priority priority = engine::Priority::Normal;
        bool mixedPriority = false;
        bool canChange = true;
    };
    std::vector<Seen> seen_;

    // Rename callbacks can arrive after the panel has gone.
    std::shared_ptr<FilesTab*> self_;
};

} // namespace app::details
