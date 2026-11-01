#include <windows.h>

#include <dwmapi.h>
#include <shellapi.h>
#include <shobjidl.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cwchar>
#include <optional>

#include "app/app.h"
#include "app/settings_internal.h"
#include "app/settings_panel.h"

namespace app
{

namespace
{

using namespace ui;

constexpr float kPanelWidth = 680.0F;
constexpr float kPanelHeight = 800.0F;
constexpr float kSidePadding = 32.0F;
constexpr float kTitleTop = 26.0F;
constexpr float kTitleHeight = 36.0F;
constexpr float kPillsTop = 70.0F;
constexpr float kBodyTop = 118.0F;
constexpr float kBodyBottomPadding = 14.0F;
constexpr float kContentEndPadding = 10.0F;
constexpr float kCloseSize = 28.0F;
constexpr float kCloseIconSize = 18.0F;

constexpr float kRowHeight = 60.0F;
constexpr float kHeadingHeight = 24.0F;
constexpr float kHeadingGapAbove = 26.0F;
constexpr float kHeadingGapBelow = 8.0F;
constexpr float kControlGap = 10.0F;
constexpr float kTextGap = 16.0F;
constexpr float kMaxTextShare = 0.55F;
constexpr float kMinFlexibleWidth = 80.0F;
constexpr float kRowTitleHeight = 19.0F;
constexpr float kRowHintHeight = 16.0F;
constexpr float kRowTextSpacing = 2.0F;
constexpr float kRowTextSlack = 3.0F;

constexpr float kNumberFieldWidth = 64.0F;
constexpr float kRatioFieldWidth = 52.0F;
constexpr float kUnitHeight = 20.0F;

constexpr int kMinutesPerHour = 60;
constexpr int kHoursPerDay = 24;
constexpr double kMinRatio = 0.1;
constexpr double kRatioTolerance = 0.0001;

// Long enough to stay out of the way of someone typing, short enough to feel immediate.
constexpr DWORD kDebounceMs = 450;

constexpr TextStyle kPanelTitle{ 26.0F, DWRITE_FONT_WEIGHT_MEDIUM, true };
constexpr TextStyle kHeading{ 17.0F, DWRITE_FONT_WEIGHT_MEDIUM, true };
constexpr TextStyle kRowTitle{ 14.0F, DWRITE_FONT_WEIGHT_MEDIUM, false };
constexpr TextStyle kRowHint{ 12.0F, DWRITE_FONT_WEIGHT_NORMAL, false };
constexpr TextStyle kUnit{ 13.0F, DWRITE_FONT_WEIGHT_NORMAL, false };

struct SectionName
{
    wchar_t const* flag;
    wchar_t const* pill;
};

constexpr std::array<SectionName, 5> kSectionNames{ {
    { L"general", L"General" },
    { L"transfers", L"Transfers" },
    { L"speed", L"Speed" },
    { L"sources", L"Search sources" },
    { L"advanced", L"Advanced" },
} };

constexpr wchar_t const* kNoFolder = L"Not set";

bool isDigit(wchar_t character)
{
    return character >= L'0' && character <= L'9';
}

std::optional<long long> parseWhole(std::wstring const& text)
{
    wchar_t* end = nullptr;
    long long const value = std::wcstoll(text.c_str(), &end, 10);
    return !text.empty() && *end == L'\0' ? std::optional(value) : std::nullopt;
}

std::optional<double> parseRatio(std::wstring const& text)
{
    wchar_t* end = nullptr;
    double const value = std::wcstod(text.c_str(), &end);
    return !text.empty() && *end == L'\0' ? std::optional(std::max(kMinRatio, value)) : std::nullopt;
}

std::wstring ratioText(double ratio)
{
    wchar_t text[32];
    swprintf_s(text, L"%.1f", ratio);
    return text;
}

std::wstring timeText(int minutes)
{
    wchar_t text[16];
    swprintf_s(text, L"%02d:%02d", minutes / kMinutesPerHour, minutes % kMinutesPerHour);
    return text;
}

std::optional<int> parseTime(std::wstring const& text)
{
    int hours = 0;
    int minutes = 0;
    wchar_t tail = 0;
    if (swscanf_s(text.c_str(), L"%d:%d%c", &hours, &minutes, &tail, 1) != 2)
    {
        return std::nullopt;
    }
    if (hours < 0 || hours >= kHoursPerDay || minutes < 0 || minutes >= kMinutesPerHour)
    {
        return std::nullopt;
    }
    return hours * kMinutesPerHour + minutes;
}

std::wstring showFolderDialog(std::wstring const& current)
{
    ComPtr<IFileOpenDialog> dialog;
    if (FAILED(CoCreateInstance(__uuidof(FileOpenDialog), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog))))
    {
        return {};
    }
    FILEOPENDIALOGOPTIONS options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    dialog->SetOkButtonLabel(L"Select");
    if (!current.empty())
    {
        ComPtr<IShellItem> folder;
        if (SUCCEEDED(SHCreateItemFromParsingName(current.c_str(), nullptr, IID_PPV_ARGS(&folder))))
        {
            dialog->SetFolder(folder.Get());
        }
    }
    if (FAILED(dialog->Show(Window::get().handle())))
    {
        return {};
    }
    ComPtr<IShellItem> chosen;
    wchar_t* path = nullptr;
    if (FAILED(dialog->GetResult(&chosen)) || FAILED(chosen->GetDisplayName(SIGDN_FILESYSPATH, &path)))
    {
        return {};
    }
    std::wstring result = path;
    CoTaskMemFree(path);
    return result;
}

std::wstring chooseFolder(std::wstring const& current)
{
    auto const& pick = settingsHooks().pickFolder;
    return pick ? pick(current) : showFolderDialog(current);
}

/// --section <name> and --scroll <dips> reach any part of the panel for a screenshot.
void applyCommandLine(SettingsPanel* panel)
{
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    for (int i = 1; i + 1 < argc; ++i)
    {
        std::wstring_view const flag = argv[i];
        if (flag == L"--section")
        {
            for (size_t index = 0; index < kSectionNames.size(); ++index)
            {
                if (std::wstring_view(argv[i + 1]) == kSectionNames[index].flag)
                {
                    panel->showSection(static_cast<SettingsSection>(index));
                }
            }
        }
        else if (flag == L"--scroll")
        {
            panel->scroller()->scrollTo(static_cast<float>(std::wcstod(argv[i + 1], nullptr)));
        }
    }
    LocalFree(argv);
}

} // namespace

SettingsHooks& settingsHooks()
{
    static SettingsHooks hooks;
    return hooks;
}

SettingsRow::SettingsRow(std::wstring rowTitle, std::wstring rowHint)
    : title_(std::move(rowTitle))
    , hint_(std::move(rowHint))
{
}

void SettingsRow::setHint(std::wstring_view text)
{
    if (text != hint_)
    {
        hint_ = text;
        layout();
        invalidate();
    }
}

void SettingsRow::layout()
{
    Theme& theme = Theme::get();
    float const rowWidth = width();
    float fixed = 0.0F;
    for (size_t i = 0; i < slots_.size(); ++i)
    {
        fixed += slots_[i].width + (i > 0 ? kControlGap : 0.0F);
    }

    float const titleWidth = theme.measure(title_, kRowTitle).width;
    float const hintWidth = hint_.empty() ? 0.0F : theme.measure(hint_, kRowHint).width;
    float const textWidth = std::min(std::max(titleWidth, hintWidth), rowWidth * kMaxTextShare);
    float const room = std::max(kMinFlexibleWidth, rowWidth - textWidth - kTextGap - fixed);

    float right = rowWidth;
    for (auto slot = slots_.rbegin(); slot != slots_.rend(); ++slot)
    {
        float const slotWidth = slot->flexible ? std::min(std::ceil(static_cast<Label*>(slot->view)->preferredWidth()), room) : slot->width;
        float const top = std::round((height() - slot->height) / 2.0F);
        slot->view->setFrame(D2D1::RectF(right - slotWidth, top, right, top + slot->height));
        right -= slotWidth + kControlGap;
    }
    textRight_ = slots_.empty() ? rowWidth : right + kControlGap - kTextGap;
}

void SettingsRow::draw(ID2D1DeviceContext* context)
{
    Theme& theme = Theme::get();
    Palette const& colors = theme.colors();
    float const pixel = 96.0F / Window::get().renderer().dpi();
    context->FillRectangle(D2D1::RectF(0.0F, 0.0F, width(), pixel), theme.brush(colors.border));

    float const stack = hint_.empty() ? kRowTitleHeight : kRowTitleHeight + kRowTextSpacing + kRowHintHeight;
    float const top = std::round((height() - stack) / 2.0F);
    theme.text(context, title_, kRowTitle, D2D1::RectF(0.0F, top, textRight_, top + kRowTitleHeight + kRowTextSlack), colors.bright);
    if (!hint_.empty())
    {
        float const hintTop = top + kRowTitleHeight + kRowTextSpacing;
        theme.text(context, hint_, kRowHint, D2D1::RectF(0.0F, hintTop, textRight_, hintTop + kRowHintHeight + kRowTextSlack), colors.foreground);
    }
}

SettingsRow* SettingsBody::addRow(std::wstring title, std::wstring hint)
{
    auto* row = add<SettingsRow>(std::move(title), std::move(hint));
    entries_.push_back({ row, bottom_, kRowHeight });
    bottom_ += kRowHeight;
    lastWasRow_ = true;
    return row;
}

void SettingsBody::addHeading(std::wstring title)
{
    if (lastWasRow_)
    {
        bottom_ += kHeadingGapAbove;
    }
    auto* label = add<Label>(std::move(title), kHeading, ColorRole::Bright);
    entries_.push_back({ label, bottom_, kHeadingHeight });
    bottom_ += kHeadingHeight + kHeadingGapBelow;
    lastWasRow_ = false;
}

void SettingsBody::clear()
{
    clearChildren();
    entries_.clear();
    bottom_ = 0.0F;
    lastWasRow_ = false;
}

void SettingsBody::layout()
{
    float const right = std::max(kSidePadding, width() - kSidePadding);
    for (Entry const& entry : entries_)
    {
        entry.view->setFrame(D2D1::RectF(kSidePadding, entry.top, right, entry.top + entry.height));
    }
}

SettingsPanel::SettingsPanel(SettingsSection section)
    : Panel(kPanelWidth, kPanelHeight)
{
    title_ = add<Label>(L"Settings", kPanelTitle, ColorRole::Bright);

    close_ = add<IconButton>(L"Close");
    close_->setIconSize(kCloseIconSize);
    close_->onClick = [this]
    {
        commitEdits();
        close();
    };

    std::vector<Pill> pills;
    for (SectionName const& name : kSectionNames)
    {
        pills.push_back({ name.pill });
    }
    sections_ = add<PillBar>();
    sections_->setPills(std::move(pills));
    sections_->onChange = [this](int index) { showSection(static_cast<SettingsSection>(index)); };

    scroll_ = add<ScrollView>();
    body_ = scroll_->setContent<SettingsBody>();

    shouldClose = [this]
    {
        commitEdits();
        return true;
    };

    // The schedule can flip slow mode while the panel is open.
    eventListener_ = App::get().addEventListener(
        [this](engine::Event const& event)
        {
            if (event.type == engine::EventType::AltSpeedChanged && slowMode_ != nullptr)
            {
                slowMode_->setOn(event.flag, true);
            }
        });

    showSection(section);
}

SettingsPanel::~SettingsPanel()
{
    // Safe at exit too: the window drops its panel before App::shutdown closes the engine.
    commitEdits();
    App::get().removeEventListener(eventListener_);
}

void SettingsPanel::layoutContent()
{
    float const contentRight = width() - kSidePadding;
    title_->setFrame(D2D1::RectF(kSidePadding, kTitleTop, contentRight - kCloseSize, kTitleTop + kTitleHeight));
    close_->setFrame(D2D1::RectF(contentRight - kCloseSize, kTitleTop + 4.0F, contentRight, kTitleTop + 4.0F + kCloseSize));
    float const pillsWidth = std::min(sections_->preferredWidth(), contentRight - kSidePadding);
    sections_->setFrame(D2D1::RectF(kSidePadding, kPillsTop, kSidePadding + pillsWidth, kPillsTop + PillBar::kHeight));
    scroll_->setFrame(D2D1::RectF(0.0F, kBodyTop, width(), height() - kBodyBottomPadding));
}

void SettingsPanel::showSection(SettingsSection section)
{
    commitEdits();
    // Not while opening: the window is still remembering what has focus below the panel.
    if (Window::get().panel() == this)
    {
        Window::get().focus(nullptr);
    }

    sections_->setSelected(static_cast<int>(section));
    slowMode_ = nullptr;
    portRow_ = nullptr;
    portTest_ = nullptr;
    blocklistRow_ = nullptr;
    blocklistUpdate_ = nullptr;
    importRow_ = nullptr;

    body_->clear();
    current_ = App::get().engine().settings();
    switch (section)
    {
    case SettingsSection::General:
        buildGeneral();
        break;
    case SettingsSection::Transfers:
        buildTransfers();
        break;
    case SettingsSection::Speed:
        buildSpeed();
        break;
    case SettingsSection::Sources:
        buildSources();
        break;
    case SettingsSection::Advanced:
        buildAdvanced();
        break;
    }

    scroll_->scrollTo(0.0F);
    scroll_->setContentHeight(body_->contentHeight() + kContentEndPadding);
    body_->layout();
    scroll_->invalidate();
}

SettingsRow* SettingsPanel::findRow(std::wstring_view title) const
{
    for (auto const& child : body_->children())
    {
        if (auto* row = dynamic_cast<SettingsRow*>(child.get()); row != nullptr && row->title() == title)
        {
            return row;
        }
    }
    return nullptr;
}

void SettingsPanel::changeSettings(std::function<void(Settings&)> const& change)
{
    // Read fresh, so that a slow mode the schedule just flipped isn't overwritten with an old value.
    auto& engine = App::get().engine();
    auto next = engine.settings();
    change(next);
    engine.applySettings(next);
    current_ = std::move(next);
}

void SettingsPanel::changePrefs(std::function<void(Prefs&)> const& change)
{
    auto& engine = App::get().engine();
    auto next = engine.prefs();
    change(next);
    engine.setPrefs(next);
}

void SettingsPanel::applyAppearance(int mode)
{
    changePrefs([mode](Prefs& next) { next.themeMode = mode; });

    Window& window = Window::get();
    Theme::get().setMode(static_cast<ThemeMode>(mode));
    BOOL dark = Theme::get().dark();
    DwmSetWindowAttribute(window.handle(), DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    window.invalidateAll();
}

Switch* SettingsPanel::addSwitch(SettingsRow* row, bool on, std::function<void(bool)> changed)
{
    auto* toggle = row->fit(row->add<Switch>(), Switch::kWidth, Switch::kHeight);
    toggle->setOn(on);
    toggle->onChange = std::move(changed);
    return toggle;
}

Switch* SettingsPanel::addSwitch(SettingsRow* row, bool Settings::* field)
{
    return addSwitch(row, current_.*field, [this, field](bool on) { changeSettings([field, on](Settings& next) { next.*field = on; }); });
}

SettingsRow* SettingsPanel::switchRow(std::wstring title, std::wstring hint, bool Settings::* field)
{
    auto* row = body_->addRow(std::move(title), std::move(hint));
    addSwitch(row, field);
    return row;
}

SettingsRow* SettingsPanel::switchRow(std::wstring title, std::wstring hint, bool Prefs::* field)
{
    auto* row = body_->addRow(std::move(title), std::move(hint));
    addSwitch(row, App::get().engine().prefs().*field, [this, field](bool on) { changePrefs([field, on](Prefs& next) { next.*field = on; }); });
    return row;
}

void SettingsPanel::hookSwitchRow(std::wstring title, std::wstring hint, std::function<bool()> const& get, std::function<void(bool)> set)
{
    auto* row = body_->addRow(std::move(title), std::move(hint));
    bool const available = get && set;
    auto* toggle = addSwitch(row, get && get(), std::move(set));
    toggle->setEnabled(available);
}

void SettingsPanel::addUnit(SettingsRow* row, std::wstring_view unit)
{
    auto* label = row->add<Label>(std::wstring(unit), kUnit, ColorRole::Foreground);
    row->fit(label, std::ceil(label->preferredWidth()), kUnitHeight);
}

Button* SettingsPanel::addButton(SettingsRow* row, std::wstring title, std::function<void()> clicked)
{
    auto* button = row->add<Button>(std::move(title), ButtonStyle::Glass, std::move(clicked));
    return row->fit(button, button->preferredWidth(), Button::kHeight);
}

void SettingsPanel::bindEdit(TextField* field, CommitEdit commit, bool debounced)
{
    field->onChange = [this, field, commit, debounced](std::wstring const&) { editChanged(field, commit, debounced); };
    field->onSubmit = [this, field] { finishEdit(field); };
    field->onBlur = [this, field] { finishEdit(field); };
}

void SettingsPanel::editChanged(TextField* field, CommitEdit const& commit, bool debounced)
{
    if (editing_ != field)
    {
        commitEdits();
        editing_ = field;
        commitEditing_ = commit;
    }
    cancelDebounce();
    if (debounced)
    {
        debounce_ = Window::get().after(
            kDebounceMs,
            [this]
            {
                debounce_ = 0;
                commitEditing_(false);
            });
    }
}

void SettingsPanel::finishEdit(TextField const* field)
{
    if (editing_ == field)
    {
        commitEdits();
    }
}

void SettingsPanel::cancelDebounce()
{
    if (debounce_ != 0)
    {
        Window::get().cancel(debounce_);
        debounce_ = 0;
    }
}

void SettingsPanel::commitEdits()
{
    cancelDebounce();
    if (commitEditing_)
    {
        auto const commit = std::move(commitEditing_);
        commitEditing_ = nullptr;
        editing_ = nullptr;
        commit(true);
    }
}

void SettingsPanel::addNumberField(SettingsRow* row, int Settings::* field, NumberRange range, bool debounced)
{
    auto* text = row->fit(row->add<TextField>(), kNumberFieldWidth, TextField::kHeight);
    text->setAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
    text->setFilter(isDigit);
    text->setText(std::to_wstring(current_.*field));
    bindEdit(
        text,
        [this, text, field, range](bool final)
        {
            if (auto const typed = parseWhole(text->text()))
            {
                int const value = static_cast<int>(std::clamp<long long>(*typed, range.minimum, range.maximum));
                if (value != current_.*field)
                {
                    changeSettings([field, value](Settings& next) { next.*field = value; });
                }
            }
            if (final)
            {
                text->setText(std::to_wstring(current_.*field));
            }
        },
        debounced);
}

void SettingsPanel::addTimeField(SettingsRow* row, int Settings::* field)
{
    auto* text = row->fit(row->add<TextField>(), kNumberFieldWidth, TextField::kHeight);
    text->setAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
    text->setFilter([](wchar_t character) { return isDigit(character) || character == L':'; });
    text->setText(timeText(current_.*field));
    bindEdit(
        text,
        [this, text, field](bool final)
        {
            auto const typed = parseTime(text->text());
            if (typed && *typed != current_.*field)
            {
                changeSettings([field, value = *typed](Settings& next) { next.*field = value; });
            }
            if (final)
            {
                text->setText(timeText(current_.*field));
            }
        },
        true);
}

void SettingsPanel::addRatioField(SettingsRow* row)
{
    auto* text = row->fit(row->add<TextField>(), kRatioFieldWidth, TextField::kHeight);
    text->setAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
    text->setFilter([](wchar_t character) { return isDigit(character) || character == L'.'; });
    text->setText(ratioText(current_.ratioLimit));
    bindEdit(
        text,
        [this, text](bool final)
        {
            auto const typed = parseRatio(text->text());
            if (typed && std::abs(*typed - current_.ratioLimit) > kRatioTolerance)
            {
                changeSettings([goal = *typed](Settings& next) { next.ratioLimit = goal; });
            }
            if (final)
            {
                text->setText(ratioText(current_.ratioLimit));
            }
        },
        true);
}

void SettingsPanel::numberRow(std::wstring title, std::wstring hint, int Settings::* field, NumberRange range, std::wstring_view unit, bool Settings::* toggle)
{
    auto* row = body_->addRow(std::move(title), std::move(hint));
    addNumberField(row, field, range);
    if (!unit.empty())
    {
        addUnit(row, unit);
    }
    if (toggle != nullptr)
    {
        addSwitch(row, toggle);
    }
}

void SettingsPanel::folderRow(std::wstring title, std::wstring hint, FolderBinding binding)
{
    auto* row = body_->addRow(std::move(title), std::move(hint));

    std::wstring const folder = binding.read();
    auto* path = row->add<Label>(folder.empty() ? kNoFolder : folder, kUnit, folder.empty() ? ColorRole::Muted : ColorRole::Bright);
    path->setTrim(Trim::Middle);
    path->setAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
    row->fitFlexible(path, kUnitHeight);

    auto const choose = [row, path, read = binding.read, write = binding.write]
    {
        std::wstring const chosen = chooseFolder(read());
        if (chosen.empty())
        {
            return false;
        }
        write(chosen);
        path->setText(chosen);
        path->setRole(ColorRole::Bright);
        row->layout();
        return true;
    };
    addButton(row, L"Change", [choose] { choose(); });

    if (binding.isOn)
    {
        auto* toggle = addSwitch(row, binding.isOn(), {});
        // As on the Mac, turning on with no folder asks for one, and backing out turns off again.
        toggle->onChange = [toggle, read = binding.read, setOn = binding.setOn, choose](bool on)
        {
            if (on && read().empty() && !choose())
            {
                toggle->setOn(false, true);
                return;
            }
            setOn(on);
        };
    }
}

void SettingsPanel::testPort()
{
    commitEdits();
    portRow_->setHint(L"Checking the port");
    portTest_->setEnabled(false);
    App::get().engine().testPort(
        [this, alive = std::weak_ptr<bool>(alive_)](bool ok, std::wstring const& error)
        {
            if (alive.expired() || portRow_ == nullptr)
            {
                return;
            }
            portTest_->setEnabled(true);
            portRow_->setHint(!error.empty() ? L"Port check site is down" : ok ? L"Port is open" : L"Port is closed");
        });
}

void SettingsPanel::refreshBlocklistButton()
{
    if (blocklistUpdate_ != nullptr)
    {
        blocklistUpdate_->setEnabled(!blocklistBusy_ && current_.blocklistOn && !current_.blocklistUrl.empty());
    }
}

void SettingsPanel::updateBlocklist()
{
    commitEdits();
    blocklistBusy_ = true;
    refreshBlocklistButton();
    blocklistRow_->setHint(L"Downloading");
    App::get().engine().updateBlocklist(
        [this, alive = std::weak_ptr<bool>(alive_)](bool ok, std::wstring const& error)
        {
            if (alive.expired())
            {
                return;
            }
            blocklistBusy_ = false;
            if (blocklistRow_ == nullptr)
            {
                return;
            }
            refreshBlocklistButton();
            if (!ok)
            {
                blocklistRow_->setHint(error.empty() ? L"The blocklist couldn't be updated" : error);
                return;
            }
            wchar_t text[64];
            swprintf_s(text, L"%zu IP address rules in list", App::get().engine().blocklistRuleCount());
            blocklistRow_->setHint(text);
        });
}

void SettingsPanel::importAgain()
{
    size_t const added = App::get().engine().importFromQBittorrent();
    App::get().refreshNow();
    if (added == 0)
    {
        importRow_->setHint(L"Nothing new to add");
        return;
    }
    wchar_t text[64];
    swprintf_s(text, L"Added %zu, paused", added);
    importRow_->setHint(text);
}

void openSettingsPanel()
{
    applyCommandLine(Panel::open<SettingsPanel>());
}

} // namespace app
