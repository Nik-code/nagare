#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "app/widget_gallery.h"
#include "ui/widgets.h"

namespace app
{

namespace
{

using namespace ui;

constexpr float kTop = 64.0F;
constexpr float kGap = 14.0F;
constexpr float kMargin = 28.0F;
constexpr float kPad = 18.0F;
constexpr float kTitleHeight = 34.0F;
constexpr DWORD kBarShotDelayMs = 900;

constexpr TextStyle kHeading{ 13.0F, DWRITE_FONT_WEIGHT_MEDIUM, false };
constexpr TextStyle kRowTitle{ 14.0F, DWRITE_FONT_WEIGHT_MEDIUM, false };
constexpr TextStyle kRowDetail{ 12.0F, DWRITE_FONT_WEIGHT_NORMAL, false };
constexpr TextStyle kPanelTitle{ 22.0F, DWRITE_FONT_WEIGHT_MEDIUM, true };
constexpr TextStyle kBody{ 13.0F, DWRITE_FONT_WEIGHT_NORMAL, false };

/// A frosted tile with a heading, standing in for the header and toolbar tiles.
class Section : public View
{
public:
    explicit Section(std::wstring title)
        : title_(std::move(title))
    {
        floating_ = true;
        glass_ = Theme::get().tileGlass();
    }

    void draw(ID2D1DeviceContext* context) override
    {
        Theme::get().text(context, title_, kHeading, D2D1::RectF(kPad, 12.0F, width() - kPad, kTitleHeight), Theme::get().colors().foreground);
    }

private:
    std::wstring title_;
};

/// Floating views place their children by hand, so a helper keeps the rows readable.
template<typename T>
T* place(T* view, float left, float top, float width, float height)
{
    view->setFrame(D2D1::RectF(left, top, left + width, top + height));
    return view;
}

class GalleryRoot : public View
{
public:
    GalleryRoot()
    {
        buildButtons();
        buildToggles();
        buildPills();
        buildFields();
        buildPickers();
        buildScroll();
        buildList();
    }

    void draw(ID2D1DeviceContext* context) override
    {
        auto const size = context->GetPixelSize();
        if (auto* art = Theme::get().art(L"ArtSurface", size.width, size.height); art != nullptr)
        {
            context->DrawBitmap(art, bounds());
        }
    }

    void layout() override
    {
        float const columnWidth = (width() - kMargin * 2.0F - kGap * 2.0F) / 3.0F;
        float const second = kMargin + columnWidth + kGap;
        float const third = second + columnWidth + kGap;

        place(buttons_, kMargin, kTop, columnWidth, 196.0F);
        place(toggles_, kMargin, kTop + 210.0F, columnWidth, 150.0F);
        place(pickers_, kMargin, kTop + 374.0F, columnWidth, 300.0F);
        place(pills_, second, kTop, columnWidth, 104.0F);
        place(fields_, second, kTop + 118.0F, columnWidth, 286.0F);
        place(scroll_, second, kTop + 418.0F, columnWidth, 256.0F);
        place(list_, third, kTop + 24.0F, columnWidth, 650.0F);
        place(listTitle_, third + 12.0F, kTop, columnWidth, 20.0F);
    }

    /// For --states: stands in for a pointer and a keyboard that a screenshot can't show.
    void applyStates()
    {
        MouseEvent enter;
        enter.kind = MouseKind::Enter;
        hoverButton_->onMouse(enter);
        hoverAccent_->onMouse(enter);
        hoverIcon_->onMouse(enter);
        hoverDropdown_->onMouse(enter);
        Window::get().focus(queryField_);
        queryField_->selectAll();
        list_->selectRow(2);
        list_->setRowSelected(3, true);
        list_->setRowSelected(5, true);

        // Just before the capture, so the bars are still on screen when the frame is taken.
        Window::get().after(
            kBarShotDelayMs,
            [this]
            {
                scrollView_->scrollTo(160.0F);
                list_->scrollTo(30.0F);
            });
    }

private:
    void buildButtons()
    {
        buttons_ = add<Section>(L"Buttons");
        float const first = 46.0F;
        hoverButton_ = place(buttons_->add<Button>(L"Cancel"), kPad, first, 84.0F, Button::kHeight);
        hoverAccent_ = place(buttons_->add<Button>(L"Get", ButtonStyle::Accent), kPad + 92.0F, first, 84.0F, Button::kHeight);
        place(buttons_->add<Button>(L"Change"), kPad + 184.0F, first, 76.0F, Button::kHeight);

        float const second = first + 38.0F;
        auto* plain = place(buttons_->add<Button>(L"Plain", ButtonStyle::Plain), kPad, second, 70.0F, Button::kHeight);
        plain->setCapsule(true);
        auto* off = place(buttons_->add<Button>(L"Disabled"), kPad + 78.0F, second, 84.0F, Button::kHeight);
        off->setEnabled(false);
        auto* accentOff = place(buttons_->add<Button>(L"Get", ButtonStyle::Accent), kPad + 170.0F, second, 70.0F, Button::kHeight);
        accentOff->setEnabled(false);

        float const third = second + 38.0F;
        auto* icon = buttons_->add<Button>(L"Add link");
        icon->setIcon(L"Link");
        place(icon, kPad, third, icon->preferredWidth(), Button::kHeight);
        hoverIcon_ = place(buttons_->add<IconButton>(L"Search"), kPad + 120.0F, third - 1.0F, IconButton::kSize, IconButton::kSize);
        place(buttons_->add<IconButton>(L"Link"), kPad + 156.0F, third - 1.0F, IconButton::kSize, IconButton::kSize);
        place(buttons_->add<IconButton>(L"Info"), kPad + 192.0F, third - 1.0F, IconButton::kSize, IconButton::kSize);
        auto* disabled = place(buttons_->add<IconButton>(L"Settings"), kPad + 228.0F, third - 1.0F, IconButton::kSize, IconButton::kSize);
        disabled->setEnabled(false);
    }

    void buildToggles()
    {
        toggles_ = add<Section>(L"Switches and checkboxes");
        auto* on = place(toggles_->add<Switch>(), kPad, 44.0F, Switch::kWidth, Switch::kHeight);
        on->setOn(true);
        place(toggles_->add<Switch>(), kPad + 72.0F, 44.0F, Switch::kWidth, Switch::kHeight);
        auto* offOn = place(toggles_->add<Switch>(), kPad + 144.0F, 44.0F, Switch::kWidth, Switch::kHeight);
        offOn->setOn(true);
        offOn->setEnabled(false);

        float const row = 92.0F;
        place(toggles_->add<Checkbox>(), kPad, row, Checkbox::kSize, Checkbox::kSize);
        auto* checked = place(toggles_->add<Checkbox>(), kPad + 32.0F, row, Checkbox::kSize, Checkbox::kSize);
        checked->setState(CheckState::On);
        auto* mixed = place(toggles_->add<Checkbox>(), kPad + 64.0F, row, Checkbox::kSize, Checkbox::kSize);
        mixed->setState(CheckState::Mixed);
        auto* grey = place(toggles_->add<Checkbox>(), kPad + 96.0F, row, Checkbox::kSize, Checkbox::kSize);
        grey->setState(CheckState::On);
        grey->setEnabled(false);
    }

    void buildPills()
    {
        pills_ = add<Section>(L"Pill bar");
        auto* bar = pills_->add<PillBar>();
        bar->setPills({ { L"All" }, { L"Downloading", 4 }, { L"Seeding" }, { L"Paused" } });
        place(bar, kPad, 46.0F, bar->preferredWidth(), PillBar::kHeight);
    }

    void buildFields()
    {
        fields_ = add<Section>(L"Text fields");
        float const fieldWidth = 330.0F;
        float top = 44.0F;

        queryField_ = place(fields_->add<TextField>(), kPad, top, fieldWidth, TextField::kHeight);
        queryField_->setFieldStyle(FieldStyle::Capsule);
        queryField_->setIcon(L"Search");
        queryField_->setText(L"Sintel.2010.1080p");
        top += 40.0F;

        auto* filter = place(fields_->add<TextField>(), kPad, top, fieldWidth, TextField::kHeight);
        filter->setFieldStyle(FieldStyle::Capsule);
        filter->setIcon(L"Filter");
        filter->setPlaceholder(L"Filter");
        top += 40.0F;

        auto* cjk = place(fields_->add<TextField>(), kPad, top, fieldWidth, TextField::kHeight);
        cjk->setText(L"千と千尋の神隠し 漢字 한글 テスト");
        top += 40.0F;

        auto* value = place(fields_->add<TextField>(), kPad, top, 74.0F, TextField::kHeight);
        value->setAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
        value->setText(L"100");
        value->setFilter([](wchar_t c) { return c >= L'0' && c <= L'9'; });

        auto* disabled = place(fields_->add<TextField>(), kPad + 86.0F, top, 74.0F, TextField::kHeight);
        disabled->setAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
        disabled->setText(L"50");
        disabled->setEnabled(false);
        top += 42.0F;

        auto* plain = place(fields_->add<TextField>(), kPad, top, fieldWidth, 34.0F);
        plain->setFieldStyle(FieldStyle::Plain);
        plain->setTextStyle(TextStyle{ 21.0F, DWRITE_FONT_WEIGHT_NORMAL, false });
        plain->setPlaceholder(L"Search the web");
    }

    void buildPickers()
    {
        pickers_ = add<Section>(L"Dropdown, menu and labels");
        hoverDropdown_ = place(pickers_->add<Dropdown>(std::vector<std::wstring>{ L"All sources", L"Internet Archive", L"Public Domain Torrents" }), kPad, 44.0F, 150.0F, Dropdown::kHeight);
        auto* priority = pickers_->add<Dropdown>(std::vector<std::wstring>{ L"High", L"Normal", L"Low" }, 1);
        place(priority, kPad + 162.0F, 44.0F, priority->preferredWidth(), Dropdown::kHeight);

        auto* menuButton = place(pickers_->add<Button>(L"Open menu"), kPad, 84.0F, 104.0F, Button::kHeight);
        menuButton->onClick = [menuButton]
        {
            auto const origin = menuButton->absoluteOrigin();
            PopupMenu::show(sampleMenu(), D2D1::Point2F(origin.x, origin.y + menuButton->height() + 4.0F));
        };
        auto* panelButton = place(pickers_->add<Button>(L"Open panel"), kPad + 112.0F, 84.0F, 104.0F, Button::kHeight);
        panelButton->onClick = [] { openSamplePanel(); };

        float top = 126.0F;
        auto const addLabel = [&](std::wstring text, TextStyle style, ColorRole role)
        {
            auto* label = pickers_->add<Label>(std::move(text), style, role);
            place(label, kPad, top, 330.0F, 22.0F);
            top += 24.0F;
            return label;
        };
        addLabel(L"Bright medium label", kRowTitle, ColorRole::Bright);
        addLabel(L"Foreground label", kBody, ColorRole::Foreground);
        addLabel(L"Muted hint", kBody, ColorRole::Muted);
        addLabel(L"Downloading, in the accent", kBody, ColorRole::Accent);
        addLabel(L"Seeding, in the seed colour", kBody, ColorRole::Seed);
        auto* trimmed = addLabel(L"A very long name that has to trim in the middle so both of its ends stay readable.mkv", kBody, ColorRole::Danger);
        trimmed->setTrim(Trim::Middle);
    }

    void buildScroll()
    {
        scroll_ = add<Section>(L"Scroll view");
        auto* view = place(scroll_->add<ScrollView>(), 4.0F, kTitleHeight, 362.0F, 214.0F);
        scrollView_ = view;
        float top = 4.0F;
        for (int i = 1; i <= 24; ++i)
        {
            auto* label = view->content()->add<Label>(L"Row " + std::to_wstring(i), kBody, ColorRole::Bright);
            place(label, 16.0F, top, 180.0F, 24.0F);
            if (i % 3 == 0)
            {
                place(view->content()->add<Switch>(), 278.0F, top - 2.0F, Switch::kWidth, Switch::kHeight);
                top += 8.0F;
            }
            top += 30.0F;
        }
        view->setContentHeight(top);
    }

    void buildList()
    {
        listTitle_ = add<Label>(L"List view, 5,000 rows on the surface", kHeading, ColorRole::Foreground);
        list_ = add<ListView>();
        list_->setRowHeight(58.0F);
        list_->setRowCount(5000);
        list_->drawRow = [](ID2D1DeviceContext* context, int index, D2D1_RECT_F rect, RowState)
        {
            Theme& theme = Theme::get();
            // A stack buffer, because this runs once per visible row per frame.
            wchar_t line[64];
            int const length = swprintf_s(line, L"Transfer %d", index + 1);
            D2D1_RECT_F const title = D2D1::RectF(rect.left + 24.0F, rect.top + 10.0F, rect.right - 24.0F, rect.top + 30.0F);
            theme.text(context, std::wstring_view(line, static_cast<size_t>(length)), kRowTitle, title, theme.colors().bright);
            D2D1_RECT_F const detail = D2D1::RectF(rect.left + 24.0F, rect.top + 30.0F, rect.right - 24.0F, rect.top + 48.0F);
            theme.text(context, index % 2 == 0 ? L"Downloading" : L"Seeding", kRowDetail, detail, index % 2 == 0 ? theme.colors().accent : theme.colors().seed);
        };
    }

    static std::vector<MenuItem> sampleMenu()
    {
        std::vector<MenuItem> menu;
        MenuItem pause;
        pause.title = L"Pause";
        pause.icon = L"Pause";
        menu.push_back(pause);
        MenuItem start;
        start.title = L"Start now";
        start.icon = L"Play";
        start.enabled = false;
        menu.push_back(start);
        menu.push_back(MenuItem::line());
        MenuItem sort;
        sort.title = L"Sort by";
        for (wchar_t const* name : { L"Name", L"Progress", L"Activity" })
        {
            MenuItem entry;
            entry.title = name;
            entry.checked = std::wstring_view(name) == L"Activity";
            sort.submenu.push_back(entry);
        }
        menu.push_back(sort);
        MenuItem show;
        show.title = L"Show the remaining time";
        show.checked = true;
        menu.push_back(show);
        menu.push_back(MenuItem::line());
        MenuItem remove;
        remove.title = L"Remove from list";
        remove.icon = L"Trash";
        menu.push_back(remove);
        return menu;
    }

public:
    static void showSampleMenu()
    {
        PopupMenu::show(sampleMenu(), D2D1::Point2F(420.0F, 200.0F));
    }

    static void openSamplePanel();

private:
    Section* buttons_ = nullptr;
    Section* toggles_ = nullptr;
    Section* pills_ = nullptr;
    Section* fields_ = nullptr;
    Section* pickers_ = nullptr;
    Section* scroll_ = nullptr;
    Label* listTitle_ = nullptr;
    ListView* list_ = nullptr;
    ScrollView* scrollView_ = nullptr;
    Button* hoverButton_ = nullptr;
    Button* hoverAccent_ = nullptr;
    IconButton* hoverIcon_ = nullptr;
    Dropdown* hoverDropdown_ = nullptr;
    TextField* queryField_ = nullptr;
};

/// The add panel from docs/nagare/add.png, built only out of widgets.
class SamplePanel : public Panel
{
public:
    SamplePanel()
        : Panel(560.0F, 610.0F)
    {
        title_ = add<Label>(L"Blender Open Movies", kPanelTitle, ColorRole::Bright);
        pencil_ = add<IconButton>(L"Pencil");
        pencil_->setIconSize(18.0F);
        status_ = add<Label>(L"5 files  ·  10.5 MB  ·  10.5 MB selected", kBody, ColorRole::Foreground);
        files_ = add<Label>(L"Files", kRowTitle, ColorRole::Bright);
        all_ = add<Button>(L"All");
        none_ = add<Button>(L"None");

        rename_ = add<TextField>();
        rename_->setText(L"ブレンダー オープンムービー");

        list_ = add<ListView>();
        list_->setRowHeight(44.0F);
        list_->setRowCount(40);
        list_->setRowInsets(16.0F, 4.0F);
        list_->drawRow = [](ID2D1DeviceContext* context, int index, D2D1_RECT_F rect, RowState state)
        {
            Theme& theme = Theme::get();
            float const midY = (rect.top + rect.bottom) / 2.0F;
            theme.icon(context, index % 3 == 0 ? L"Folder" : L"File", D2D1::RectF(rect.left + 16.0F, midY - 9.0F, rect.left + 34.0F, midY + 9.0F), theme.colors().foreground);
            wchar_t line[48];
            int const length = swprintf_s(line, L"File %d.mkv", index + 1);
            theme.text(context, std::wstring_view(line, static_cast<size_t>(length)), kBody, D2D1::RectF(rect.left + 44.0F, rect.top + 6.0F, rect.right - 60.0F, rect.top + 25.0F), theme.colors().bright);
            theme.text(context, L"4.2 MB", kRowDetail, D2D1::RectF(rect.left + 44.0F, rect.top + 24.0F, rect.right - 60.0F, rect.top + 40.0F), theme.colors().foreground);
            float const boxLeft = rect.right - 34.0F;
            CheckState const check = index % 5 == 2 ? CheckState::Mixed : index % 5 == 4 ? CheckState::Off : CheckState::On;
            Checkbox::paint(context, D2D1::RectF(boxLeft, midY - 9.0F, boxLeft + Checkbox::kSize, midY + 9.0F), check, true, state.hover);
        };

        saveTitle_ = add<Label>(L"Save to", kRowTitle, ColorRole::Bright);
        saveHint_ = add<Label>(L"~/Movies", kRowDetail, ColorRole::Foreground);
        change_ = add<Button>(L"Change");
        startLabel_ = add<Label>(L"Start right away", kRowTitle, ColorRole::Bright);
        start_ = add<Switch>();
        start_->setOn(true);
        trashLabel_ = add<Label>(L"Move the .torrent file to the Trash", kRowTitle, ColorRole::Bright);
        trash_ = add<Switch>();
        source_ = add<Dropdown>(std::vector<std::wstring>{ L"Normal priority", L"High priority", L"Low priority" });
        cancel_ = add<Button>(L"Cancel");
        cancel_->setCapsule(true);
        cancel_->onClick = [this] { close(); };
        get_ = add<Button>(L"Get", ButtonStyle::Accent);
        get_->onClick = [this] { close(); };
    }

    void draw(ID2D1DeviceContext* context) override
    {
        float const y = 408.0F;
        context->FillRectangle(D2D1::RectF(28.0F, y, width() - 28.0F, y + 1.0F), Theme::get().brush(Theme::get().colors().border));
    }

protected:
    void layoutContent() override
    {
        float const right = width() - 28.0F;
        place(title_, 28.0F, 26.0F, 440.0F, 30.0F);
        place(pencil_, right - 26.0F, 26.0F, 26.0F, 26.0F);
        place(status_, 28.0F, 56.0F, 460.0F, 20.0F);
        place(rename_, 28.0F, 86.0F, right - 28.0F, TextField::kHeight);

        place(files_, 28.0F, 128.0F, 120.0F, 24.0F);
        place(none_, right - 56.0F, 128.0F, 56.0F, Button::kSmallHeight);
        place(all_, right - 106.0F, 128.0F, 44.0F, Button::kSmallHeight);
        place(list_, 12.0F, 158.0F, width() - 24.0F, 240.0F);

        place(saveTitle_, 28.0F, 416.0F, 200.0F, 22.0F);
        place(saveHint_, 28.0F, 437.0F, 300.0F, 18.0F);
        place(change_, right - 76.0F, 422.0F, 76.0F, Button::kHeight);

        place(startLabel_, 28.0F, 468.0F, 300.0F, 28.0F);
        place(start_, right - Switch::kWidth, 468.0F, Switch::kWidth, Switch::kHeight);
        place(trashLabel_, 28.0F, 508.0F, 360.0F, 28.0F);
        place(trash_, right - Switch::kWidth, 508.0F, Switch::kWidth, Switch::kHeight);

        place(source_, 28.0F, 556.0F, 170.0F, Dropdown::kHeight);
        place(get_, right - 84.0F, 554.0F, 84.0F, Button::kLargeHeight);
        place(cancel_, right - 176.0F, 554.0F, 84.0F, Button::kLargeHeight);
    }

private:
    Label* title_ = nullptr;
    IconButton* pencil_ = nullptr;
    Label* status_ = nullptr;
    TextField* rename_ = nullptr;
    Label* files_ = nullptr;
    Button* all_ = nullptr;
    Button* none_ = nullptr;
    ListView* list_ = nullptr;
    Label* saveTitle_ = nullptr;
    Label* saveHint_ = nullptr;
    Button* change_ = nullptr;
    Label* startLabel_ = nullptr;
    Switch* start_ = nullptr;
    Label* trashLabel_ = nullptr;
    Switch* trash_ = nullptr;
    Dropdown* source_ = nullptr;
    Button* cancel_ = nullptr;
    Button* get_ = nullptr;
};

void GalleryRoot::openSamplePanel()
{
    Panel::open<SamplePanel>();
}

} // namespace

void showWidgetGallery(GalleryOptions const& options)
{
    Window& window = Window::get();
    auto root = std::make_unique<GalleryRoot>();
    GalleryRoot* gallery = root.get();
    window.setRoot(std::move(root));

    if (options.panel)
    {
        GalleryRoot::openSamplePanel();
    }
    if (options.states)
    {
        gallery->applyStates();
    }
    if (options.menu)
    {
        // Late, because the first activation closes popups and would take this one with it.
        window.after(600, [] { GalleryRoot::showSampleMenu(); });
    }
}

} // namespace app
