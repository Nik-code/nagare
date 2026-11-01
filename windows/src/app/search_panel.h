#pragma once

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "engine/engine.h"
#include "ui/widgets.h"

namespace app
{

/// One result, with what the row shows worked out once. Titles and names come from the network, so
/// they're cleaned on arrival and only ever reach the screen as plain text.
struct SearchRow
{
    engine::SearchResult result;
    /// The source and the size, as the row's second line.
    std::wstring detail;
    std::wstring seeds;
    /// How many health dots to fill, or -1 to draw none.
    int health = -1;
};

/// No control characters or bidi overrides, and a sane length.
std::wstring plainResultText(std::wstring_view text);

/// Result rows with the Mac's look: its own selection, and a Get pill on the selected row.
class SearchResultList final : public ui::ListView
{
public:
    SearchResultList();

    void setRows(std::vector<SearchRow> rows);
    std::vector<SearchRow>& rows() { return rows_; }
    int selected() const { return selected_; }
    void select(int row);
    void moveSelection(int delta);

    ui::Button* getButton() const { return get_; }

    /// Double click and the pill.
    std::function<void()> onGet;

    void layout() override;
    bool onMouse(ui::MouseEvent const& event) override;

protected:
    void scrolled() override;

private:
    void paintRow(ID2D1DeviceContext* context, int row, D2D1_RECT_F rect);
    void placeGet();

    std::vector<SearchRow> rows_;
    ui::Button* get_ = nullptr;
    int selected_ = -1;
};

class SearchPanel final : public ui::Panel
{
public:
    SearchPanel();
    ~SearchPanel() override;

    ui::TextField* query() const { return query_; }
    ui::Dropdown* sources() const { return sources_; }
    SearchResultList* results() const { return list_; }
    ui::Label* status() const { return status_; }

    /// Replaces opening the sources folder in Explorer, for the self test.
    std::function<void(std::wstring const&)> openFolder;

    void search();
    void getSelected();
    /// True from a search starting until it finishes or is cancelled, whichever panel asked.
    static bool searching();

    bool onKey(UINT key, bool down) override;

protected:
    void layoutContent() override;

private:
    void reloadSources();
    void stopSearch();
    void sourceChanged(int index);
    void showOutcome(engine::SearchOutcome const& outcome);
    void showStatus(std::wstring const& text, ui::ColorRole role = ui::ColorRole::Foreground);

    ui::TextField* query_ = nullptr;
    ui::Dropdown* sources_ = nullptr;
    SearchResultList* list_ = nullptr;
    ui::Label* status_ = nullptr;
    ui::Label* hint_ = nullptr;
    int generation_ = 0;
    bool queryChanged_ = false;
    bool fetching_ = false;
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
};

} // namespace app
