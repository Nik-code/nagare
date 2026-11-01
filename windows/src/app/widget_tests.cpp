#include <windows.h>

#include <imm.h>

#include <cstdarg>
#include <cstdio>
#include <deque>
#include <functional>
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

constexpr DWORD kSettleMs = 320;

int gFailures = 0;
int gChecks = 0;
FILE* gLog = nullptr;

/// The run's own safety timer is always pending, and nothing else should be.
bool settled()
{
    return Window::get().pendingWork() == 1;
}

void note(char const* format, ...)
{
    if (gLog == nullptr)
    {
        return;
    }
    va_list args;
    va_start(args, format);
    vfprintf(gLog, format, args);
    va_end(args);
    fflush(gLog);
}

void check(bool passed, char const* name)
{
    ++gChecks;
    if (!passed)
    {
        ++gFailures;
    }
    note("%s %s\n", passed ? "ok  " : "FAIL", name);
}

float scaled(float dips)
{
    return dips * Window::get().renderer().dpi() / 96.0F;
}

LPARAM clientPoint(float x, float y)
{
    return MAKELPARAM(static_cast<int>(scaled(x) + 0.5F), static_cast<int>(scaled(y) + 0.5F));
}

void send(UINT message, WPARAM wparam, float x, float y)
{
    SendMessageW(Window::get().handle(), message, wparam, clientPoint(x, y));
}

void moveTo(float x, float y)
{
    send(WM_MOUSEMOVE, 0, x, y);
}

void click(float x, float y)
{
    moveTo(x, y);
    send(WM_LBUTTONDOWN, MK_LBUTTON, x, y);
    send(WM_LBUTTONUP, 0, x, y);
}

void wheel(float x, float y, int notches)
{
    POINT screen{ static_cast<LONG>(scaled(x)), static_cast<LONG>(scaled(y)) };
    ClientToScreen(Window::get().handle(), &screen);
    SendMessageW(Window::get().handle(), WM_MOUSEWHEEL, MAKEWPARAM(0, notches * WHEEL_DELTA), MAKELPARAM(screen.x, screen.y));
}

void key(UINT virtualKey)
{
    SendMessageW(Window::get().handle(), WM_KEYDOWN, virtualKey, 0);
    SendMessageW(Window::get().handle(), WM_KEYUP, virtualKey, 0);
}

void typed(std::wstring_view text)
{
    for (wchar_t const c : text)
    {
        SendMessageW(Window::get().handle(), WM_CHAR, c, 0);
    }
}

MouseEvent mouseEvent(MouseKind kind, float x, float y = 15.0F, bool shift = false)
{
    MouseEvent event;
    event.kind = kind;
    event.point = D2D1::Point2F(x, y);
    event.shift = shift;
    return event;
}

template<typename T>
T* place(T* view, float left, float top, float width, float height)
{
    view->setFrame(D2D1::RectF(left, top, left + width, top + height));
    return view;
}

D2D1_POINT_2F centre(View* view)
{
    auto const origin = view->absoluteOrigin();
    return D2D1::Point2F(origin.x + view->width() / 2.0F, origin.y + view->height() / 2.0F);
}

void clickView(View* view)
{
    auto const point = centre(view);
    click(point.x, point.y);
}

class TestPanel : public Panel
{
public:
    TestPanel()
        : Panel(400.0F, 300.0F)
    {
        field = add<TextField>();
        ok = add<Button>(L"OK");
        menu = add<Dropdown>(std::vector<std::wstring>{ L"One", L"Two" });
    }

    TextField* field = nullptr;
    Button* ok = nullptr;
    Dropdown* menu = nullptr;

protected:
    void layoutContent() override
    {
        place(field, 20.0F, 20.0F, 300.0F, TextField::kHeight);
        place(ok, 20.0F, 60.0F, 80.0F, Button::kHeight);
        place(menu, 20.0F, 100.0F, 120.0F, Dropdown::kHeight);
    }
};

class TestRoot : public View
{
public:
    TestRoot()
    {
        title = add<Label>(L"Title");
        button = add<Button>(L"Button", ButtonStyle::Glass, [this] { ++clicks; });
        icon = add<IconButton>(L"Search", [this] { ++iconClicks; });
        toggle = add<Switch>();
        toggle->onChange = [this](bool on) { lastToggle = on; };
        box = add<Checkbox>();
        pills = add<PillBar>();
        pills->setPills({ { L"All" }, { L"Downloading", 4 }, { L"Seeding" } });
        pills->onChange = [this](int index) { lastPill = index; };
        field = add<TextField>();
        short_ = add<TextField>();
        picker = add<Dropdown>(std::vector<std::wstring>{ L"One", L"Two", L"Three" });
        picker->onChange = [this](int index) { lastPicked = index; };
        scroller = add<ScrollView>();
        scrolledButton = scroller->content()->add<Button>(L"Deep");
        scrolledButton->onClick = [this] { ++deepClicks; };
        list = add<ListView>();
        list->setRowHeight(40.0F);
        list->setRowCount(100000);
        list->drawRow = [this](ID2D1DeviceContext*, int, D2D1_RECT_F, RowState) { ++rowsDrawn; };
        list->onDoubleClick = [this](int row) { doubleClicked = row; };
        list->onEnter = [this] { ++enters; };
        list->onDelete = [this] { ++deletes; };
        list->onRightClick = [this](int row, D2D1_POINT_2F) { rightClicked = row; };
    }

    void draw(ID2D1DeviceContext* context) override
    {
        context->FillRectangle(bounds(), Theme::get().brush(Theme::get().colors().surface));
    }

    void layout() override
    {
        place(title, 20.0F, 60.0F, 200.0F, 20.0F);
        place(button, 20.0F, 90.0F, 100.0F, Button::kHeight);
        place(icon, 130.0F, 90.0F, IconButton::kSize, IconButton::kSize);
        place(toggle, 20.0F, 130.0F, Switch::kWidth, Switch::kHeight);
        place(box, 100.0F, 135.0F, Checkbox::kSize, Checkbox::kSize);
        place(pills, 20.0F, 170.0F, pills->preferredWidth(), PillBar::kHeight);
        place(field, 20.0F, 220.0F, 300.0F, TextField::kHeight);
        place(short_, 20.0F, 260.0F, 100.0F, TextField::kHeight);
        place(picker, 20.0F, 300.0F, 140.0F, Dropdown::kHeight);
        place(scroller, 20.0F, 340.0F, 300.0F, 200.0F);
        place(scrolledButton, 20.0F, 800.0F, 100.0F, Button::kHeight);
        scroller->setContentHeight(1000.0F);
        place(list, 400.0F, 60.0F, 500.0F, 400.0F);
    }

    Label* title = nullptr;
    Button* button = nullptr;
    IconButton* icon = nullptr;
    Switch* toggle = nullptr;
    Checkbox* box = nullptr;
    PillBar* pills = nullptr;
    TextField* field = nullptr;
    TextField* short_ = nullptr;
    Dropdown* picker = nullptr;
    ScrollView* scroller = nullptr;
    Button* scrolledButton = nullptr;
    ListView* list = nullptr;

    int clicks = 0;
    int iconClicks = 0;
    int deepClicks = 0;
    bool lastToggle = false;
    int lastPill = -1;
    int lastPicked = -1;
    int doubleClicked = -1;
    int rightClicked = -1;
    int enters = 0;
    int deletes = 0;
    int rowsDrawn = 0;
};

struct Step
{
    DWORD delay;
    std::function<void()> run;
};

std::deque<Step> gSteps;

void runNext()
{
    if (gSteps.empty())
    {
        note("%d checks, %d failed\n", gChecks, gFailures);
        if (gLog != nullptr)
        {
            fclose(gLog);
        }
        Window::get().quit(gFailures);
        return;
    }
    Step step = std::move(gSteps.front());
    gSteps.pop_front();
    Window::get().after(
        step.delay,
        [run = std::move(step.run)]
        {
            run();
            runNext();
        });
}

void then(DWORD delay, std::function<void()> run)
{
    gSteps.push_back(Step{ delay, std::move(run) });
}

void testBasics(TestRoot* root)
{
    then(50, [root]
    {
        root->title->setText(L"Title");
    });
    then(50, [root]
    {
        check(!Window::get().framePending(), "label: an unchanged text schedules no frame");
        root->title->setText(L"Other");
        check(Window::get().framePending(), "label: a changed text schedules a frame");
    });

    then(50, [root]
    {
        clickView(root->button);
        check(root->clicks == 1, "button: a click fires onClick");

        auto const at = centre(root->button);
        send(WM_LBUTTONDOWN, MK_LBUTTON, at.x, at.y);
        moveTo(at.x + 300.0F, at.y);
        send(WM_LBUTTONUP, 0, at.x + 300.0F, at.y);
        check(root->clicks == 1, "button: releasing outside doesn't click");

        // A quick second click reaches a view as a double click.
        moveTo(at.x, at.y);
        send(WM_LBUTTONDOWN, MK_LBUTTON, at.x, at.y);
        send(WM_LBUTTONUP, 0, at.x, at.y);
        send(WM_LBUTTONDBLCLK, MK_LBUTTON, at.x, at.y);
        send(WM_LBUTTONUP, 0, at.x, at.y);
        check(root->clicks == 3, "button: both halves of a double click count");

        root->button->setEnabled(false);
        clickView(root->button);
        check(root->clicks == 3, "button: disabled ignores clicks");
        root->button->setEnabled(true);

        clickView(root->icon);
        check(root->iconClicks == 1, "icon button: click");

        clickView(root->toggle);
        check(root->toggle->on() && root->lastToggle, "switch: click turns on");
        check(root->toggle->thumbPosition() < 1.0F, "switch: thumb is still sliding");

        check(root->box->state() == CheckState::Off, "checkbox: starts off");
        clickView(root->box);
        check(root->box->state() == CheckState::On, "checkbox: click turns on");
        root->box->setState(CheckState::Mixed);
        clickView(root->box);
        check(root->box->state() == CheckState::On, "checkbox: mixed turns on");
        clickView(root->box);
        check(root->box->state() == CheckState::Off, "checkbox: then off");

        auto const origin = root->pills->absoluteOrigin();
        D2D1_RECT_F const pill = root->pills->pillRect(1);
        click(origin.x + (pill.left + pill.right) / 2.0F, origin.y + 18.0F);
        check(root->lastPill == 1 && root->pills->selected() == 1, "pill bar: click selects and fires");
        root->lastPill = -1;
        click(origin.x + (pill.left + pill.right) / 2.0F, origin.y + 18.0F);
        check(root->lastPill == -1, "pill bar: clicking the selected pill does nothing");
    });
    then(kSettleMs, [root]
    {
        check(root->toggle->thumbPosition() == 1.0F, "switch: thumb finishes");
    });
}

void testTextField(TestRoot* root)
{
    then(50, [root]
    {
        TextField* field = root->field;
        Window& window = Window::get();
        int changes = 0;
        field->onChange = [&changes](std::wstring const&) { ++changes; };

        check(window.focused() == nullptr, "field: nothing is focused yet");
        clickView(field);
        check(window.focused() == field, "field: a click focuses it");
        HWND const handle = window.handle();

        typed(L"hello");
        check(field->text() == L"hello" && field->caret() == 5, "field: typing");
        check(changes == 5, "field: onChange per character");

        typed(L"語\xD83D\xDE00");
        check(field->text() == L"hello語\xD83D\xDE00" && field->caret() == 8, "field: CJK and a surrogate pair");
        key(VK_LEFT);
        check(field->caret() == 6, "field: left steps over the whole pair");
        key(VK_RIGHT);
        check(field->caret() == 8, "field: right steps over the whole pair");
        key(VK_BACK);
        check(field->text() == L"hello語", "field: backspace removes the whole pair");
        key(VK_BACK);
        check(field->text() == L"hello", "field: backspace removes a CJK character");

        typed(L"\xDE00");
        check(field->text() == L"hello", "field: a lone low surrogate is dropped");

        field->handleKey(VK_HOME, false, false);
        check(field->caret() == 0, "field: home");
        field->handleKey(VK_RIGHT, true, false);
        field->handleKey(VK_RIGHT, true, false);
        check(field->anchor() == 0 && field->caret() == 2, "field: shift+right extends");
        field->handleKey(VK_RIGHT, false, false);
        check(field->anchor() == 2 && field->caret() == 2, "field: right collapses to the selection's end");
        field->handleKey(VK_END, false, false);
        check(field->caret() == 5, "field: end");
        field->handleKey(VK_HOME, true, false);
        check(field->anchor() == 5 && field->caret() == 0, "field: shift+home");

        field->setSelection(0, 2);
        field->handleKey('C', false, true);
        field->handleKey(VK_END, false, false);
        field->handleKey('V', false, true);
        check(field->text() == L"hellohe", "field: copy and paste");
        field->handleKey('Z', false, true);
        check(field->text() == L"hello", "field: undo the paste");
        field->handleKey('Y', false, true);
        check(field->text() == L"hellohe", "field: redo");
        field->handleKey('Z', false, true);

        field->handleKey('A', false, true);
        check(field->anchor() == 0 && field->caret() == 5, "field: ctrl+a");
        field->handleKey('X', false, true);
        check(field->text().empty(), "field: cut");
        field->handleKey('V', false, true);
        check(field->text() == L"hello", "field: paste back");

        field->handleKey(VK_HOME, false, false);
        field->handleKey(VK_DELETE, false, false);
        check(field->text() == L"ello", "field: delete");

        field->setText(L"Sintel.2010.1080p 千と千尋");
        check(changes >= 0 && field->caret() == 22, "field: setText moves the caret to the end");
        changes = 0;
        field->setText(L"x");
        check(changes == 0, "field: setText doesn't fire onChange");

        // Word selection, by a real double click at a position found through a real caret walk.
        field->setText(L"Sintel.2010.1080p 千と千尋");
        auto const origin = field->absoluteOrigin();
        float const y = origin.y + 15.0F;
        float const x = origin.x + 10.0F + 12.0F;
        moveTo(x, y);
        send(WM_LBUTTONDOWN, MK_LBUTTON, x, y);
        send(WM_LBUTTONUP, 0, x, y);
        send(WM_LBUTTONDBLCLK, MK_LBUTTON, x, y);
        send(WM_LBUTTONUP, 0, x, y);
        check(field->anchor() == 0 && field->caret() == 6, "field: double click selects one word");

        field->setSelection(18, 18);
        field->onMouse(mouseEvent(MouseKind::DoubleClick, 10.0F + field->width() * 0.95F));
        field->onMouse(mouseEvent(MouseKind::Up, 0.0F));

        field->handleKey(VK_HOME, false, false);
        field->handleKey(VK_RIGHT, false, true);
        check(field->caret() == 6, "field: ctrl+right stops at the word's end");

        // Dragging selects.
        field->onMouse(mouseEvent(MouseKind::Down, 10.5F));
        check(field->caret() == 0, "field: mouse down at the start");
        field->onMouse(mouseEvent(MouseKind::Move, 80.0F));
        field->onMouse(mouseEvent(MouseKind::Up, 80.0F));
        check(field->anchor() == 0 && field->caret() > 3, "field: drag selects");
        int const end = field->caret();
        field->onMouse(mouseEvent(MouseKind::Down, 120.0F, 15.0F, true));
        field->onMouse(mouseEvent(MouseKind::Up, 120.0F));
        check(field->anchor() == 0 && field->caret() > end, "field: shift+click extends");

        // The input method's window goes to the caret, and only while a field has focus.
        field->setSelection(4, 4);
        HIMC const context = ImmGetContext(handle);
        check(context != nullptr, "ime: a focused field has an input context");
        if (context != nullptr)
        {
            COMPOSITIONFORM form{};
            ImmGetCompositionWindow(context, &form);
            check(form.dwStyle == CFS_POINT, "ime: composition window is positioned by point");
            check(form.ptCurrentPos.x > static_cast<LONG>(scaled(origin.x)) && form.ptCurrentPos.x < static_cast<LONG>(scaled(origin.x + 80.0F)), "ime: composition x follows the caret");
            check(form.ptCurrentPos.y >= static_cast<LONG>(scaled(origin.y)) && form.ptCurrentPos.y <= static_cast<LONG>(scaled(origin.y + 30.0F)), "ime: composition y is on the line");
            ImmReleaseContext(handle, context);
        }

        check(!settled(), "field: a blink timer runs while focused");

        // The default window proc turns input method results into WM_CHAR, which a real IME relies on.
        field->setText(L"");
        SendMessageW(handle, WM_IME_CHAR, L'語', 0);
    });
    then(50, [root]
    {
        TextField* field = root->field;
        Window& window = Window::get();
        check(field->text() == L"語", "ime: a committed character arrives through WM_IME_CHAR");
        field->onChange = nullptr;

        window.focus(nullptr);
        HIMC const context = ImmGetContext(window.handle());
        check(context == nullptr, "ime: no input context once nothing is focused");
        if (context != nullptr)
        {
            ImmReleaseContext(window.handle(), context);
        }

        TextField* tiny = root->short_;
        window.focus(tiny);
        tiny->setText(L"a long enough name to overflow this small field 千と千尋の神隠し");
        check(tiny->scrollX() > 0.0F, "field: scrolls to keep the caret visible");
        tiny->handleKey(VK_HOME, false, false);
        check(tiny->scrollX() == 0.0F, "field: home scrolls back");

        int submitted = 0;
        int cancelled = 0;
        tiny->onSubmit = [&submitted] { ++submitted; };
        tiny->onCancel = [&cancelled] { ++cancelled; };
        key(VK_RETURN);
        key(VK_ESCAPE);
        check(submitted == 1 && cancelled == 1, "field: return and esc callbacks");
        tiny->onSubmit = nullptr;
        tiny->onCancel = nullptr;
        check(!tiny->handleKey(VK_ESCAPE, false, false), "field: an unhandled esc falls through");
        check(!tiny->handleKey(VK_DOWN, false, false), "field: down falls through");

        tiny->setFilter([](wchar_t c) { return c >= L'0' && c <= L'9'; });
        tiny->setText(L"");
        typed(L"1a2");
        check(tiny->text() == L"12", "field: a filter drops characters");
        tiny->setFilter(nullptr);
        tiny->setText(L"");

        window.focus(nullptr);
    });
    then(100, []
    {
        check(settled(), "field: no timers remain after blur");
    });
}

void testScroll(TestRoot* root)
{
    then(50, [root]
    {
        auto const at = centre(root->scroller);
        wheel(at.x, at.y, -2);
    });
    then(kSettleMs, [root]
    {
        check(root->scroller->scrollOffset() > 0.0F, "scroll: wheel scrolls");
        check(root->scroller->scrollOffset() < root->scroller->maxScroll(), "scroll: two notches stop short of the end");
        root->scroller->scrollTo(10000.0F);
        check(root->scroller->scrollOffset() == root->scroller->maxScroll(), "scroll: clamps at the end");
        check(root->scrolledButton->absoluteOrigin().y < 540.0F, "scroll: children move with the offset");
        clickView(root->scrolledButton);
        check(root->deepClicks == 1, "scroll: a scrolled child still gets clicks");

        // The bar was flashed by scrollTo, so it's on screen and draggable by now or soon.
    });
    then(200, [root]
    {
        auto const origin = root->scroller->absoluteOrigin();
        float const x = origin.x + root->scroller->width() - 6.0F;
        float const thumbBottom = origin.y + root->scroller->height() - 8.0F;
        moveTo(x, thumbBottom);
        send(WM_LBUTTONDOWN, MK_LBUTTON, x, thumbBottom);
        moveTo(x, origin.y + 8.0F);
        send(WM_LBUTTONUP, 0, x, origin.y + 8.0F);
        check(root->scroller->scrollOffset() < 200.0F, "scroll: dragging the thumb scrolls");
        moveTo(5.0F, 700.0F);
    });
    then(1400, [root]
    {
        auto const origin = root->scroller->absoluteOrigin();
        float const x = origin.x + root->scroller->width() - 6.0F;
        float const y = origin.y + 100.0F;
        send(WM_LBUTTONDOWN, MK_LBUTTON, x, y);
        send(WM_LBUTTONUP, 0, x, y);
        check(settled(), "scroll: a faded bar has stopped all its timers");
    });
}

void testList(TestRoot* root)
{
    then(50, [root]
    {
        ListView* list = root->list;
        Window& window = Window::get();
        auto const origin = list->absoluteOrigin();

        clickView(root->title); // nothing there, only to clear the pointer
        click(origin.x + 100.0F, origin.y + 50.0F);
        check(list->selectedCount() == 1 && list->isSelected(1), "list: click selects");
        check(window.focused() == list, "list: click takes focus");

        MouseEvent shiftClick = mouseEvent(MouseKind::Down, 100.0F, 170.0F, true);
        list->onMouse(shiftClick);
        check(list->selectedCount() == 4 && list->isSelected(4), "list: shift+click extends");

        MouseEvent controlClick = mouseEvent(MouseKind::Down, 100.0F, 250.0F);
        controlClick.control = true;
        list->onMouse(controlClick);
        check(list->selectedCount() == 5 && list->isSelected(6), "list: ctrl+click adds");
        list->onMouse(controlClick);
        check(list->selectedCount() == 4 && !list->isSelected(6), "list: ctrl+click removes");

        list->selectRow(0);
        list->handleKey(VK_DOWN, false, false);
        check(list->isSelected(1) && list->selectedCount() == 1, "list: down moves");
        list->handleKey(VK_DOWN, true, false);
        check(list->selectedCount() == 2, "list: shift+down extends");
        list->handleKey(VK_UP, false, false);
        check(list->cursorRow() == 1, "list: up moves");
        list->handleKey(VK_END, false, false);
        check(list->cursorRow() == 99999 && list->scrollOffset() == list->maxScroll(), "list: end reveals the last row");
        list->handleKey(VK_HOME, false, false);
        check(list->cursorRow() == 0 && list->scrollOffset() == 0.0F, "list: home reveals the first row");
        list->handleKey(VK_NEXT, false, false);
        check(list->cursorRow() == 9, "list: page down");
        list->handleKey(VK_PRIOR, false, false);
        check(list->cursorRow() == 0, "list: page up");
        list->handleKey('A', false, true);
        check(list->selectedCount() == 100000, "list: ctrl+a");
        list->clearSelection();

        key(VK_RETURN);
        key(VK_DELETE);
        check(root->enters == 1 && root->deletes == 1, "list: enter and delete callbacks");

        float const rowY = origin.y + 90.0F;
        float const rowX = origin.x + 100.0F;
        moveTo(rowX, rowY);
        send(WM_LBUTTONDOWN, MK_LBUTTON, rowX, rowY);
        send(WM_LBUTTONUP, 0, rowX, rowY);
        send(WM_LBUTTONDBLCLK, MK_LBUTTON, rowX, rowY);
        send(WM_LBUTTONUP, 0, rowX, rowY);
        check(root->doubleClicked == 2, "list: double click");

        send(WM_RBUTTONUP, 0, origin.x + 100.0F, origin.y + 130.0F);
        check(root->rightClicked == 3 && list->isSelected(3) && list->selectedCount() == 1, "list: right click selects and fires");

        list->setRowCount(2);
        check(list->selectedCount() == 0, "list: shrinking drops selection past the end");
        list->setRowCount(100000);
    });
    // Long enough for the bar to fade away, so its own repaints aren't counted.
    then(1500, [root]
    {
        root->rowsDrawn = 0;
        Window::get().invalidateAll();
    });
    then(60, [root]
    {
        check(root->rowsDrawn >= 10 && root->rowsDrawn <= 11, "list: only visible rows draw out of 100000");
        note("     rows in a full repaint: %d\n", root->rowsDrawn);
        auto const origin = root->list->absoluteOrigin();
        moveTo(origin.x + 100.0F, origin.y + 210.0F);
    });
    // The move showed the bar, so wait for that fade before measuring a hover change alone.
    then(220, [root]
    {
        root->rowsDrawn = 0;
        auto const origin = root->list->absoluteOrigin();
        moveTo(origin.x + 100.0F, origin.y + 250.0F);
    });
    then(60, [root]
    {
        note("     rows after a hover change: %d\n", root->rowsDrawn);
        check(root->rowsDrawn >= 1 && root->rowsDrawn <= 2, "list: a hover change repaints only the rows involved");
        Window::get().focus(nullptr);
        moveTo(5.0F, 700.0F);
    });
    then(1400, []
    {
        check(settled(), "list: settles to no timers");
    });
}

void testPopup(TestRoot* root)
{
    then(50, [root]
    {
        Window& window = Window::get();
        clickView(root->picker);
        check(window.popup() != nullptr, "dropdown: click opens the popup");

        key(VK_DOWN);
        key(VK_DOWN);
        key(VK_RETURN);
        check(window.popup() == nullptr, "popup: enter closes");
        check(root->lastPicked == 1 && root->picker->selected() == 1, "dropdown: keyboard picks");

        clickView(root->picker);
        check(window.popup() != nullptr, "dropdown: opens again");
        key(VK_ESCAPE);
        check(window.popup() == nullptr, "popup: esc closes");

        clickView(root->picker);
        int const clicksBefore = root->clicks;
        clickView(root->button);
        check(window.popup() == nullptr, "popup: outside click closes");
        check(root->clicks == clicksBefore, "popup: outside click doesn't reach what's below");

        int fired = 0;
        std::vector<MenuItem> items;
        MenuItem off;
        off.title = L"Disabled";
        off.enabled = false;
        items.push_back(off);
        items.push_back(MenuItem::line());
        MenuItem first;
        first.title = L"First";
        first.action = [&fired] { fired = 1; };
        items.push_back(first);
        MenuItem more;
        more.title = L"More";
        MenuItem nested;
        nested.title = L"Nested";
        nested.action = [&fired] { fired = 2; };
        more.submenu.push_back(nested);
        items.push_back(more);

        PopupMenu* menu = PopupMenu::show(items, D2D1::Point2F(200.0F, 200.0F));
        key(VK_DOWN);
        check(menu->highlighted() == 2, "popup: keys skip disabled rows and separators");
        key(VK_RETURN);
        check(fired == 1, "popup: enter runs the action");

        menu = PopupMenu::show(items, D2D1::Point2F(200.0F, 200.0F));
        key(VK_END);
        check(menu->highlighted() == 3, "popup: end");
        key(VK_RIGHT);
        check(menu->submenu() != nullptr && menu->submenu()->highlighted() == 0, "popup: right opens the submenu");
        key(VK_LEFT);
        check(menu->submenu() == nullptr, "popup: left closes the submenu");
        key(VK_RIGHT);
        key(VK_RETURN);
        check(fired == 2 && window.popup() == nullptr, "popup: enter in a submenu runs its action");

        menu = PopupMenu::show(items, D2D1::Point2F(200.0F, 200.0F));
        auto const& frame = menu->frame();
        moveTo(frame.left + 30.0F, frame.top + 6.0F + 30.0F + 9.0F + 45.0F);
        check(menu->submenu() != nullptr, "popup: hovering opens a submenu");
        PopupMenu* sub = menu->submenu();
        auto const subOrigin = sub->absoluteOrigin();
        click(subOrigin.x + 30.0F, subOrigin.y + 20.0F);
        check(fired == 2 && window.popup() == nullptr, "popup: a submenu row outside the parent's bounds is clickable");

        fired = 0;
        menu = PopupMenu::show(items, D2D1::Point2F(200.0F, 200.0F));
        click(menu->frame().left + 30.0F, menu->frame().top + 20.0F);
        check(fired == 0 && window.popup() != nullptr, "popup: clicking a disabled row does nothing");
        window.setPopup(nullptr);

        PopupMenu* corner = PopupMenu::show(items, D2D1::Point2F(window.width() - 5.0F, window.height() - 5.0F));
        check(corner->frame().right <= window.width() && corner->frame().bottom <= window.height(), "popup: flips to stay inside the window");
        window.setPopup(nullptr);
    });
}

void testPanel(TestRoot* root)
{
    auto* slot = new TestPanel*(nullptr);

    then(50, [root, slot]
    {
        Window& window = Window::get();
        window.focus(root->field);
        *slot = Panel::open<TestPanel>();
        check(window.panel() == *slot, "panel: opens");
        check(window.focused() == nullptr, "panel: takes focus away from what's below");

        float const widthNow = (*slot)->width();
        check((*slot)->frame().top == Panel::kTopMargin, "panel: 72 below the top");
        check(std::abs((*slot)->frame().left - (window.width() - widthNow) / 2.0F) <= 1.0F, "panel: centred");
        check((*slot)->opacity() < 1.0F, "panel: starts faded");
    });
    then(kSettleMs, [root, slot]
    {
        Window& window = Window::get();
        TestPanel* panel = *slot;
        check(panel->opacity() == 1.0F, "panel: finishes opening");

        int const clicksBefore = root->clicks;
        key(VK_RETURN);
        typed(L"x");
        check(root->field->text().find(L'x') == std::wstring::npos, "panel: keys don't reach the view below");

        panel->setClosesOnOutsideClick(false);
        clickView(root->button);
        check(root->clicks == clicksBefore, "panel: captures clicks for the views below");
        check(!panel->closing(), "panel: outside click can be turned off");

        clickView(panel->field);
        typed(L"inside");
        check(panel->field->text() == L"inside", "panel: its own field works");

        clickView(panel->menu);
        check(window.popup() != nullptr, "panel: a dropdown opens a popup over the panel");
        key(VK_DOWN);
        key(VK_RETURN);
        check(window.popup() == nullptr && panel->menu->selected() == 0, "panel: popup keyboard over a panel");

        window.focus(nullptr);
        key(VK_ESCAPE);
        check(panel->closing(), "panel: esc starts closing");
    });
    then(kSettleMs, [root, slot]
    {
        Window& window = Window::get();
        check(window.panel() == nullptr, "panel: gone after the close animation");
        check(window.focused() == root->field, "panel: focus returns to what had it");
        window.focus(nullptr);

        *slot = Panel::open<TestPanel>();
    });
    then(kSettleMs, [root, slot]
    {
        TestPanel* panel = *slot;
        int const clicksBefore = root->clicks;
        clickView(root->button);
        check(root->clicks == clicksBefore, "panel: an outside click doesn't reach the button");
        check(panel->closing(), "panel: an outside click closes by default");
    });
    then(kSettleMs, [slot]
    {
        check(Window::get().panel() == nullptr, "panel: closed by the outside click");

        *slot = Panel::open<TestPanel>();
        (*slot)->shouldClose = [] { return false; };
        key(VK_ESCAPE);
        check(!(*slot)->closing(), "panel: shouldClose can veto");
        (*slot)->shouldClose = nullptr;

        // Closing from inside a click is the common case, and must not free the view under the pointer.
        (*slot)->ok->onClick = [] { Window::get().setPanel(nullptr); };
    });
    then(kSettleMs, [slot]
    {
        clickView((*slot)->ok);
        check(Window::get().panel() == nullptr, "panel: can be closed straight from a button's click");
        delete slot;
    });
    then(50, []
    {
        check(settled(), "panel: nothing left running");
    });
}

void testIdle()
{
    auto* frames = new uint64_t(0);
    then(600, [frames]
    {
        Window& window = Window::get();
        moveTo(5.0F, 700.0F);
        *frames = window.framesPainted();
    });
    then(1200, [frames]
    {
        Window& window = Window::get();
        note("     frames while idle: %llu\n", static_cast<unsigned long long>(window.framesPainted() - *frames));
        check(window.framesPainted() - *frames <= 2, "idle: no frames once everything settles");
        check(settled(), "idle: nothing but this test's own timer");
        delete frames;
    });
}

} // namespace

void runWidgetTests()
{
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring logPath = path;
    logPath.resize(logPath.find_last_of(L'\\') + 1);
    logPath += L"widgets-test.log";
    _wfopen_s(&gLog, logPath.c_str(), L"w");

    Window& window = Window::get();
    auto root = std::make_unique<TestRoot>();
    TestRoot* raw = root.get();
    window.setRoot(std::move(root));

    testBasics(raw);
    testTextField(raw);
    testScroll(raw);
    testList(raw);
    testPopup(raw);
    testPanel(raw);
    testIdle();

    // A safety net: a test that hangs still has to end the run with a failure.
    window.after(60000, [] { Window::get().quit(99); });
    runNext();
}

} // namespace app
