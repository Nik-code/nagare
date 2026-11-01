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

enum class SettingsSection
{
    General,
    Transfers,
    Speed,
    Sources,
    Advanced,
};

/// Theme rowWithTitle: title and hint on the left, a hairline on top, controls on the right.
class SettingsRow : public ui::View
{
public:
    SettingsRow(std::wstring rowTitle, std::wstring rowHint);

    std::wstring const& title() const { return title_; }
    std::wstring const& hint() const { return hint_; }
    void setHint(std::wstring_view text);

    /// Puts a child in the row's controls, each to the right of the last.
    template<typename T>
    T* fit(T* view, float viewWidth, float viewHeight)
    {
        slots_.push_back({ view, viewWidth, viewHeight, false });
        return view;
    }
    /// A label as wide as its text, or as wide as the row has left.
    void fitFlexible(ui::Label* label, float viewHeight)
    {
        slots_.push_back({ label, 0.0F, viewHeight, true });
    }

    void layout() override;
    void draw(ID2D1DeviceContext* context) override;

private:
    struct Slot
    {
        ui::View* view = nullptr;
        float width = 0.0F;
        float height = 0.0F;
        bool flexible = false;
    };

    std::wstring title_;
    std::wstring hint_;
    std::vector<Slot> slots_;
    float textRight_ = 0.0F;
};

/// The scrolling column of rows and headings for one section.
class SettingsBody : public ui::View
{
public:
    SettingsRow* addRow(std::wstring title, std::wstring hint);
    void addHeading(std::wstring title);
    void clear();
    float contentHeight() const { return bottom_; }

    void layout() override;

private:
    struct Entry
    {
        ui::View* view = nullptr;
        float top = 0.0F;
        float height = 0.0F;
    };

    std::vector<Entry> entries_;
    float bottom_ = 0.0F;
    bool lastWasRow_ = false;
};

/// The Mac's Settings window as a sheet: the sidebar becomes pills, and each section is rebuilt from
/// the engine when it's shown, so sections out of sight cost nothing.
class SettingsPanel : public ui::Panel
{
public:
    explicit SettingsPanel(SettingsSection section = SettingsSection::General);
    ~SettingsPanel() override;

    void showSection(SettingsSection section);

    /// Commits whatever is still being typed.
    void commitEdits();

    SettingsRow* findRow(std::wstring_view title) const;
    ui::PillBar* sectionPills() const { return sections_; }
    ui::ScrollView* scroller() const { return scroll_; }

protected:
    void layoutContent() override;

private:
    using CommitEdit = std::function<void(bool final)>;
    using Settings = engine::SessionSettings;
    using Prefs = engine::AppPrefs;

    struct NumberRange
    {
        int minimum = 0;
        int maximum = 0;
    };

    /// The two halves of a folder row. Leave `isOn` empty for a row with no switch.
    struct FolderBinding
    {
        std::function<std::wstring()> read;
        std::function<void(std::wstring const&)> write;
        std::function<bool()> isOn;
        std::function<void(bool)> setOn;
    };

    // In settings_sections.cpp, in the Mac's order.
    void buildGeneral();
    void buildTransfers();
    void buildSpeed();
    void buildSchedule();
    void buildSources();
    void buildAdvanced();
    void buildBlocklist();
    void buildImport();

    SettingsRow* switchRow(std::wstring title, std::wstring hint, bool Settings::* field);
    SettingsRow* switchRow(std::wstring title, std::wstring hint, bool Prefs::* field);
    void hookSwitchRow(std::wstring title, std::wstring hint, std::function<bool()> const& get, std::function<void(bool)> set);
    void numberRow(std::wstring title, std::wstring hint, int Settings::* field, NumberRange range, std::wstring_view unit, bool Settings::* toggle);
    void folderRow(std::wstring title, std::wstring hint, FolderBinding binding);

    ui::Switch* addSwitch(SettingsRow* row, bool on, std::function<void(bool)> changed);
    ui::Switch* addSwitch(SettingsRow* row, bool Settings::* field);
    void addNumberField(SettingsRow* row, int Settings::* field, NumberRange range, bool debounced = true);
    void addTimeField(SettingsRow* row, int Settings::* field);
    void addRatioField(SettingsRow* row);
    void addUnit(SettingsRow* row, std::wstring_view unit);
    ui::Button* addButton(SettingsRow* row, std::wstring title, std::function<void()> clicked);

    // Typing applies after a pause, and on Return, on leaving the field or on leaving the panel.
    // `commit(false)` applies what's valid so far; `commit(true)` also tidies the text.
    void bindEdit(ui::TextField* field, CommitEdit commit, bool debounced);
    void editChanged(ui::TextField* field, CommitEdit const& commit, bool debounced);
    void finishEdit(ui::TextField const* field);
    void cancelDebounce();

    void changeSettings(std::function<void(Settings&)> const& change);
    void changePrefs(std::function<void(Prefs&)> const& change);

    void applyAppearance(int mode);
    void testPort();
    void updateBlocklist();
    void refreshBlocklistButton();
    void importAgain();

    Settings current_;

    ui::Label* title_ = nullptr;
    ui::IconButton* close_ = nullptr;
    ui::PillBar* sections_ = nullptr;
    ui::ScrollView* scroll_ = nullptr;
    SettingsBody* body_ = nullptr;

    // Only set while their section is showing.
    ui::Switch* slowMode_ = nullptr;
    SettingsRow* portRow_ = nullptr;
    ui::Button* portTest_ = nullptr;
    SettingsRow* blocklistRow_ = nullptr;
    ui::Button* blocklistUpdate_ = nullptr;
    SettingsRow* importRow_ = nullptr;

    ui::TextField* editing_ = nullptr;
    CommitEdit commitEditing_;
    int debounce_ = 0;
    int eventListener_ = 0;
    bool blocklistBusy_ = false;

    // Engine callbacks can outlive the panel, so they hold this weakly and check it first.
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
};

} // namespace app
