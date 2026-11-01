#include <windows.h>

#include <shellapi.h>
#include <shlobj.h>

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <deque>
#include <functional>
#include <string>
#include <vector>

#include "app/main_drop.h"
#include "app/main_panels.h"
#include "app/main_view.h"
#include "ui/window.h"

// --main-test drives the real main view through the demo engine and the real message path, then
// exits with the failure count. --main-test --main-idle only parks the app at rest for measuring, and
// --main-test --main-busy keeps the swell moving for the same reason.
// --main-shots <folder> also saves a picture of each state along the way.

namespace app
{

namespace
{

using engine::TransferId;
using engine::TransferInfo;
using engine::TransferState;
using ui::Window;

constexpr DWORD kPaintMs = 300;
constexpr DWORD kPanelMs = 320;
constexpr DWORD kTickMs = 1000;
constexpr int kIdleTicks = 3;
constexpr int kLargeList = 5000;
constexpr double kBudgetMs = 40.0;
constexpr float kMaxFramesPerSecond = 16.0F;

int gFailures = 0;
int gChecks = 0;
FILE* gLog = nullptr;
std::wstring gShots;

struct Step
{
    DWORD delay;
    std::function<void()> run;
};

std::deque<Step> gSteps;

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
    gFailures += passed ? 0 : 1;
    note("%s %s\n", passed ? "ok  " : "FAIL", name);
}

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
    gSteps.push_back({ delay, std::move(run) });
}

MainView* view()
{
    return static_cast<MainView*>(Window::get().root());
}

float scaled(float dips)
{
    return dips * Window::get().renderer().dpi() / 96.0F;
}

void send(UINT message, WPARAM wparam, float x, float y)
{
    SendMessageW(Window::get().handle(), message, wparam, MAKELPARAM(static_cast<int>(scaled(x) + 0.5F), static_cast<int>(scaled(y) + 0.5F)));
}

void click(float x, float y)
{
    send(WM_MOUSEMOVE, 0, x, y);
    send(WM_LBUTTONDOWN, MK_LBUTTON, x, y);
    send(WM_LBUTTONUP, 0, x, y);
}

void rightClick(float x, float y)
{
    send(WM_MOUSEMOVE, 0, x, y);
    send(WM_RBUTTONUP, 0, x, y);
}

void key(UINT code)
{
    SendMessageW(Window::get().handle(), WM_KEYDOWN, code, 0);
    SendMessageW(Window::get().handle(), WM_KEYUP, code, 0);
}

D2D1_POINT_2F rowPoint(int row, float x = 150.0F)
{
    TransferList* list = view()->list();
    auto const origin = list->absoluteOrigin();
    D2D1_RECT_F const rect = list->rowRect(row);
    return D2D1::Point2F(origin.x + x, origin.y + (rect.top + rect.bottom) / 2.0F);
}

void clickPill(int index)
{
    ui::PillBar* pills = view()->pills();
    auto const origin = pills->absoluteOrigin();
    D2D1_RECT_F const rect = pills->pillRect(index);
    click(origin.x + (rect.left + rect.right) / 2.0F, origin.y + (rect.top + rect.bottom) / 2.0F);
}

void parkPointer()
{
    send(WM_MOUSEMOVE, 0, 600.0F, 300.0F);
}

std::vector<TransferId> allIds()
{
    std::vector<TransferId> ids;
    for (TransferInfo const& info : App::get().transfers())
    {
        ids.push_back(info.id);
    }
    return ids;
}

bool allIn(TransferState state)
{
    auto const& transfers = App::get().transfers();
    return !transfers.empty() && std::all_of(transfers.begin(), transfers.end(), [state](TransferInfo const& info) { return info.state == state; });
}

void holdSeaStill()
{
    view()->header()->swellSource = [](engine::SwellData& data)
    {
        data = engine::SwellData{};
        return false;
    };
}

void freeSea()
{
    view()->header()->swellSource = [](engine::SwellData& data) { return App::get().engine().swell(data); };
}

void shot(wchar_t const* name)
{
    if (!gShots.empty())
    {
        then(kPaintMs,
            [name]
            {
                Window::get().saveNextFrame(gShots + L"\\" + name + L".png");
                Window::get().invalidateAll();
            });
        then(kPaintMs, [] {});
    }
}

double millisecondsOf(std::function<void()> const& work)
{
    LARGE_INTEGER frequency{};
    LARGE_INTEGER start{};
    LARGE_INTEGER end{};
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&start);
    work();
    QueryPerformanceCounter(&end);
    return static_cast<double>(end.QuadPart - start.QuadPart) * 1000.0 / static_cast<double>(frequency.QuadPart);
}

void testShape()
{
    then(800,
        []
        {
            MainView* main = view();
            size_t const count = App::get().transfers().size();
            check(static_cast<size_t>(main->list()->rowCount()) == count, "every transfer has a row");
            check(main->totalLabel()->text() == countText(static_cast<int>(count)) + L" transfers", "the count reads as the Mac's");
            check(main->emptyLabel()->hidden(), "no empty state with transfers around");
            check(main->list()->counters().rowsDrawn > 0, "rows were painted");
            check(main->pills()->selected() == 0, "All starts selected");
            check(main->header()->frame().left == 12.0F && main->header()->frame().top == 12.0F, "the tile is inset by 12");
            check(main->header()->height() == SwellHeader::kHeight, "the tile is the Mac's height");
        });
}

void testFilter()
{
    then(0,
        []
        {
            clickPill(2);
            check(view()->filter() == TransferFilter::Seed, "clicking a pill filters");
            check(view()->list()->rowCount() == 0, "nothing is seeding in the hero scene");
            check(view()->totalLabel()->text() == L"0 of 4 transfers", "a filter shows how much of the list is left");
            clickPill(1);
            check(view()->filter() == TransferFilter::Download, "pills switch directly");
            check(view()->list()->rowCount() == 4, "all four are downloading");
            clickPill(0);
            check(view()->filter() == TransferFilter::All, "All clears the filter");
            check(view()->totalLabel()->text() == L"4 transfers", "the total is plain again");
        });
}

void testSort()
{
    then(0,
        []
        {
            MainView* main = view();
            TransferList* list = main->list();
            main->setSort(TransferSort::Name);
            check(list->idAt(0) == 2, "name order puts Big Buck Bunny first");
            check(list->idAt(3) == 3, "and Tears of Steel last");

            main->setSort(TransferSort::Size);
            check(list->idAt(0) == 1, "size order puts Sintel first");
            main->setSort(TransferSort::Order);
            check(list->idAt(0) == 1 && list->idAt(3) == 4, "queue order is the order added");
        });
}

void testSelection()
{
    then(0,
        []
        {
            TransferList* list = view()->list();
            auto const point = rowPoint(1);
            click(point.x, point.y);
            check(list->selectedIds() == std::vector<TransferId>{ 2 }, "a click selects its transfer");
            check(Window::get().focused() == list, "and focuses the list");

            view()->setSort(TransferSort::Name);
            check(list->idAt(0) == 2 && list->isSelected(0) && list->selectedCount() == 1, "selection follows the transfer through a sort");
            view()->setSort(TransferSort::Order);
            check(list->isSelected(1) && list->selectedCount() == 1, "and back again");

            view()->setFilter(TransferFilter::Seed);
            check(list->selectedIds().empty(), "a filtered out row leaves the selection, so Delete can't reach it");
            view()->setFilter(TransferFilter::All);

            list->handleKey('A', false, true);
            check(list->selectedCount() == 4 && list->selectedIds() == allIds(), "Ctrl+A selects everything");
            key(VK_ESCAPE);
            check(list->selectedCount() == 0, "Esc clears the selection");
        });
    shot(L"list");
}

void testMenus()
{
    then(0,
        []
        {
            auto const point = rowPoint(0);
            rightClick(point.x, point.y);
            check(Window::get().popup() != nullptr, "right click opens the row menu");
            check(view()->list()->selectedIds() == std::vector<TransferId>{ 1 }, "and selects the row under it");
        });
    shot(L"row-menu");
    then(0,
        []
        {
            key(VK_ESCAPE);
            auto const origin = view()->list()->absoluteOrigin();
            rightClick(300.0F, origin.y + view()->list()->height() - 10.0F);
            check(Window::get().popup() != nullptr, "right click below the rows opens the other menu");
            check(view()->list()->selectedCount() == 0, "and clears the selection");
            key(VK_ESCAPE);
            check(Window::get().popup() == nullptr, "the menu closes");

            // The dots lead the toolbar on Windows: the header's 12 inset, the pill's 16, then half a cell.
            float const moreX = 12.0F + 16.0F + 21.0F;
            click(moreX, 28.0F);
            check(Window::get().popup() != nullptr, "the dots button opens the menu bar's leftovers");
        });
    shot(L"more-menu");
    then(0, [] { key(VK_ESCAPE); });
}

void testKeys()
{
    then(kPaintMs,
        []
        {
            auto const point = rowPoint(2);
            click(point.x, point.y);
            key(VK_SPACE);
            auto const& transfers = App::get().transfers();
            check(transfers[2].state == TransferState::Paused, "Space pauses the selection");
            check(view()->pills()->selected() == 0, "pills stay put");
            key(VK_SPACE);
            check(transfers[2].state == TransferState::Download, "and Space again resumes it");

            view()->list()->handleKey('A', false, true);
            key(VK_SPACE);
            check(allIn(TransferState::Paused), "Space pauses all of a mixed selection");
        });
    shot(L"paused");
}

void testIdle()
{
    static uint64_t frames = 0;
    static ListCounters before;

    then(0,
        []
        {
            parkPointer();
            holdSeaStill();
            App::get().refreshNow();
        });
    // Long enough for the scrollbar's hide timer and fade from earlier clicks to end.
    then(3000,
        []
        {
            frames = Window::get().framesPainted();
            before = view()->list()->counters();
            check(!view()->header()->easing(), "a still sea stops easing");
        });
    then(kTickMs * kIdleTicks + 200,
        []
        {
            ListCounters const& now = view()->list()->counters();
            check(Window::get().framesPainted() == frames, "no frames across idle ticks");
            check(!Window::get().framePending(), "and none waiting");
            check(now.rowsDrawn == before.rowsDrawn, "no row repainted");
            check(now.rowsReloaded == before.rowsReloaded, "no row was even invalidated");
            check(Window::get().pendingWork() == 2, "only the app's tick and this run's safety timer are alive");
        });
}

void testTransferring()
{
    static uint64_t frames = 0;
    static ListCounters before;
    static bool sawEasing = false;
    static ULONGLONG started = 0;

    then(0,
        []
        {
            view()->list()->clearSelection();
            freeSea();
            view()->header()->watched = [] { return true; };
            App::get().engine().resume(allIds());
            App::get().refreshNow();
        });
    then(kTickMs,
        []
        {
            frames = Window::get().framesPainted();
            before = view()->list()->counters();
            started = GetTickCount64();
        });
    then(kTickMs + 100, [] { sawEasing = view()->header()->easing(); });
    then(kTickMs * 2 - 100,
        []
        {
            auto const seconds = static_cast<float>(GetTickCount64() - started) / 1000.0F;
            uint64_t const painted = Window::get().framesPainted() - frames;
            auto const fps = static_cast<float>(painted) / seconds;
            note("     %.1f frames a second while transferring\n", static_cast<double>(fps));
            check(sawEasing, "the swell eases while transferring");
            check(fps <= kMaxFramesPerSecond, "frames stay at or under 15 a second");
            check(fps >= 10.0F, "and the swell really does animate");

            view()->header()->watched = [] { return false; };
            App::get().refreshNow();
            check(!view()->header()->easing(), "and behind another window it steps instead of easing");
            view()->header()->watched = [] { return true; };

            ListCounters const& now = view()->list()->counters();
            auto const visible = static_cast<uint64_t>(view()->list()->slotCount());
            check(now.rowsDrawn - before.rowsDrawn <= visible * 4, "rows repaint at most once a tick");
        });
}

void testRemoval()
{
    static size_t count = 0;

    then(0,
        []
        {
            count = App::get().transfers().size();
            auto const point = rowPoint(3);
            click(point.x, point.y);
            key(VK_DELETE);
            check(Window::get().panel() != nullptr, "Delete on an active transfer asks first");
            check(App::get().transfers().size() == count, "and removes nothing yet");
        });
    shot(L"confirm");
    then(kPanelMs, [] { key(VK_ESCAPE); });
    then(kPanelMs,
        []
        {
            check(Window::get().panel() == nullptr, "Esc cancels");
            check(App::get().transfers().size() == count, "and nothing was removed");
            view()->list()->selectIds({ 4 });
            key(VK_DELETE);
        });
    then(kPanelMs, [] { key(VK_RETURN); });
    then(kPanelMs,
        []
        {
            check(App::get().transfers().size() == count - 1, "Return confirms");
            check(view()->list()->rowCount() == static_cast<int>(count) - 1, "and the row goes with it");
            check(view()->list()->selectedCount() == 0, "the gone row leaves the selection");
        });
}

void testRename()
{
    then(0,
        []
        {
            auto const id = App::get().transfers().front().id;
            openRenamePanel(L"Sintel", [id](std::wstring const& name) { App::get().engine().renamePath(id, L"Sintel", name, [](bool, std::wstring const&) { App::get().refreshNow(); }); });
            check(Window::get().panel() != nullptr, "the rename sheet opens");
            check(Window::get().focused() != nullptr, "with its field focused, ready to type");
        });
    shot(L"rename");
    then(kPanelMs, [] { key(VK_RETURN); });
    then(kPanelMs,
        []
        {
            check(Window::get().panel() == nullptr, "Return commits the rename");
            App::get().refreshNow();
            check(App::get().transfers().front().name == L"Sintel", "and the name stayed, as it wasn't edited");
        });
}

void testRowButtons()
{
    then(0,
        []
        {
            TransferList* list = view()->list();
            auto const origin = list->absoluteOrigin();
            D2D1_RECT_F const button = RowGeometry::button(list->rowRect(0), RowButton::Control);
            float const x = origin.x + (button.left + button.right) / 2.0F;
            float const y = origin.y + (button.top + button.bottom) / 2.0F;
            click(x, y);
            check(App::get().transfers()[0].state == TransferState::Paused, "the row's own button pauses it");
            check(list->selectedCount() == 0, "without selecting the row");
        });
    // Windows reports the pointer leaving at once, as it isn't really there, so hover right before the shot.
    if (!gShots.empty())
    {
        then(kPaintMs,
            []
            {
                auto const point = rowPoint(1);
                send(WM_MOUSEMOVE, 0, point.x, point.y);
                Window::get().saveNextFrame(gShots + L"\\hover.png");
                Window::get().invalidateAll();
            });
    }
    then(kPaintMs,
        []
        {
            parkPointer();
            App::get().engine().resume(allIds());
            App::get().refreshNow();
        });
}

void testLargeList()
{
    static ListCounters before;
    static uint64_t drawnBefore = 0;

    then(0,
        []
        {
            auto& engine = App::get().engine();
            for (int i = 0; i < kLargeList; ++i)
            {
                auto preview = engine.parseMagnet(L"magnet:?xt=urn:btih:08ada5a7a6183aae1e09d831df6748d566095a10");
                preview.name = L"Synthetic transfer " + std::to_wstring(i);
                preview.totalBytes = 1000000;
                engine.add(preview, {});
            }

            before = view()->list()->counters();
            double const ms = millisecondsOf([] { App::get().refreshNow(); });
            note("     %.2f ms to refresh %d transfers\n", ms, kLargeList);
            auto const visible = static_cast<uint64_t>(view()->list()->slotCount());
            ListCounters const& now = view()->list()->counters();
            check(view()->list()->rowCount() >= kLargeList, "every synthetic transfer has a row");
            check(now.textBuilds - before.textBuilds <= visible, "text is built for visible rows only");
            check(now.dotCalls - before.dotCalls <= visible, "dots are asked for visible rows only");
            check(ms < kBudgetMs, "a refresh stays well inside a frame budget of work");
            before = now;
            drawnBefore = now.rowsDrawn;
        });
    then(kPaintMs,
        []
        {
            auto const visible = static_cast<uint64_t>(view()->list()->slotCount());
            uint64_t const drawn = view()->list()->counters().rowsDrawn - drawnBefore;
            note("     %llu rows drawn after the list grew\n", static_cast<unsigned long long>(drawn));
            check(drawn <= visible * 3, "a huge list paints only what fits");

            drawnBefore = view()->list()->counters().rowsDrawn;
            view()->list()->scrollTo(TransferList::kRowHeight * 2500.0F);
        });
    shot(L"scrolled");
    then(kPaintMs,
        []
        {
            auto const visible = static_cast<uint64_t>(view()->list()->slotCount());
            uint64_t const drawn = view()->list()->counters().rowsDrawn - drawnBefore;
            note("     %llu rows drawn after the jump\n", static_cast<unsigned long long>(drawn));
            // The scrollbar fades over the rows, so a few are repainted along with it.
            check(drawn <= visible * 20, "jumping to the middle paints a screenful, not the list");
            check(view()->list()->slotCount() < 12, "row caches follow the window, not the list");

            double const ms = millisecondsOf([] { view()->setSort(TransferSort::Name); });
            note("     %.2f ms to sort %d transfers by name\n", ms, kLargeList);
            check(view()->list()->rowCount() >= kLargeList, "sorting keeps every row");

            view()->list()->handleKey('A', false, true);
            check(view()->list()->selectedCount() == view()->list()->rowCount(), "Ctrl+A reaches rows that were never drawn");
            view()->setSort(TransferSort::Order);
            check(view()->list()->selectedCount() == view()->list()->rowCount(), "and survives a sort");
            view()->list()->clearSelection();
            view()->list()->scrollTo(0.0F);
        });
}

void testHidden()
{
    static uint64_t frames = 0;

    then(0,
        []
        {
            ShowWindow(Window::get().handle(), SW_MINIMIZE);
            frames = Window::get().framesPainted();
        });
    then(kTickMs * 2,
        []
        {
            check(!Window::get().visible(), "the window really is minimised");
            check(Window::get().framesPainted() == frames, "nothing draws while minimised");
            check(!view()->header()->easing(), "and the swell has stopped");
            ShowWindow(Window::get().handle(), SW_RESTORE);
        });
    then(kPaintMs * 2, [] { check(Window::get().framesPainted() > frames, "drawing picks up again on restore"); });
}

/// A data object carrying what Explorer or a browser would, for playing a drag against the window.
ComPtr<IDataObject> dragData(UINT format, std::wstring const& text)
{
    ComPtr<IDataObject> data;
    if (FAILED(SHCreateDataObject(nullptr, 0, nullptr, nullptr, IID_PPV_ARGS(&data))))
    {
        return nullptr;
    }

    size_t const textBytes = (text.size() + 2) * sizeof(wchar_t);
    size_t const header = format == CF_HDROP ? sizeof(DROPFILES) : 0;
    HGLOBAL memory = GlobalAlloc(GHND, header + textBytes);
    auto* bytes = static_cast<char*>(GlobalLock(memory));
    if (format == CF_HDROP)
    {
        auto* files = reinterpret_cast<DROPFILES*>(bytes);
        files->pFiles = sizeof(DROPFILES);
        files->fWide = TRUE;
    }
    memcpy(bytes + header, text.c_str(), text.size() * sizeof(wchar_t));
    GlobalUnlock(memory);

    FORMATETC describe{ static_cast<CLIPFORMAT>(format), nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
    STGMEDIUM medium{};
    medium.tymed = TYMED_HGLOBAL;
    medium.hGlobal = memory;
    data->SetData(&describe, &medium, TRUE);
    return data;
}

void testDrag()
{
    then(0,
        []
        {
            check(view()->dropTarget() != nullptr, "the window took the drop registration");
            IDropTarget* target = dropInterface(view()->dropTarget());
            DWORD effect = DROPEFFECT_COPY;

            auto const torrent = dragData(CF_HDROP, L"C:\\Downloads\\sintel.torrent");
            target->DragEnter(torrent.Get(), 0, POINTL{ 300, 300 }, &effect);
            check(effect == DROPEFFECT_COPY, "a torrent file is accepted");
            check(!view()->overlay()->hidden(), "and the overlay comes up while it is still being dragged");
            target->DragOver(0, POINTL{ 310, 310 }, &effect);
            check(effect == DROPEFFECT_COPY, "and stays accepted while moving");
            target->DragLeave();

            auto const other = dragData(CF_HDROP, L"C:\\Downloads\\notes.txt");
            effect = DROPEFFECT_COPY;
            target->DragEnter(other.Get(), 0, POINTL{ 300, 300 }, &effect);
            check(effect == DROPEFFECT_NONE, "a plain file is refused");
            target->DragLeave();

            auto const magnet = dragData(CF_UNICODETEXT, L"  magnet:?xt=urn:btih:08ada5a7a6183aae1e09d831df6748d566095a10 ");
            effect = DROPEFFECT_COPY;
            target->DragEnter(magnet.Get(), 0, POINTL{ 300, 300 }, &effect);
            check(effect == DROPEFFECT_COPY, "a magnet link is accepted");
            target->DragLeave();

            auto const words = dragData(CF_UNICODETEXT, L"just some text");
            effect = DROPEFFECT_COPY;
            target->DragEnter(words.Get(), 0, POINTL{ 300, 300 }, &effect);
            check(effect == DROPEFFECT_NONE, "other text is refused");
            target->DragLeave();
        });
    then(700, [] { check(view()->overlay()->hidden(), "the overlay leaves with the drag"); });
}

void testEmpty()
{
    then(0,
        []
        {
            App::get().engine().remove([]
                {
                    auto ids = allIds();
                    std::reverse(ids.begin(), ids.end());
                    return ids;
                }(),
                false);
            App::get().refreshNow();
            check(view()->list()->rowCount() == 0, "the list empties");
            check(!view()->emptyLabel()->hidden(), "and the empty state shows");
            check(view()->totalLabel()->text() == L"0 transfers", "with a zero count");
        });
    shot(L"empty");
    then(0, [] { view()->overlay()->showBadge(L"Files", L"3 Torrent Files", L"12 files, 1.2 GB total"); });
    shot(L"overlay");
    then(0, [] { view()->overlay()->hideBadge(); });
    then(600, [] { check(view()->overlay()->hidden(), "the drag overlay fades away"); });
}

std::wstring argumentAfter(std::wstring_view flag)
{
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::wstring value;
    for (int i = 1; i + 1 < argc; ++i)
    {
        if (flag == argv[i])
        {
            value = argv[i + 1];
        }
    }
    LocalFree(argv);
    return value;
}

} // namespace

void runMainViewTests()
{
    if (wcsstr(GetCommandLineW(), L"--main-idle") != nullptr)
    {
        Window::get().after(
            kPaintMs,
            []
            {
                App::get().engine().pause(allIds());
                holdSeaStill();
                App::get().refreshNow();
            });
        return;
    }

    if (wcsstr(GetCommandLineW(), L"--main-busy") != nullptr)
    {
        // The sea moving on every tick, so the swell never rests: the worst case to measure.
        view()->header()->swellSource = [phase = 0.0F](engine::SwellData& data) mutable
        {
            phase += 1.0F;
            for (size_t i = 0; i < engine::kSwellPoints; ++i)
            {
                data.down[i] = 2.0F + std::sin(static_cast<float>(i) * 0.5F + phase);
                data.up[i] = 1.0F + std::sin(static_cast<float>(i) * 0.3F - phase);
            }
            data.peak = 3.0F;
            return true;
        };
        return;
    }

    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring logPath = path;
    logPath.resize(logPath.find_last_of(L'\\') + 1);
    logPath += L"main-test.log";
    _wfopen_s(&gLog, logPath.c_str(), L"w");

    gShots = argumentAfter(L"--main-shots");

    testShape();
    testFilter();
    testSort();
    testSelection();
    testMenus();
    testKeys();
    testIdle();
    testTransferring();
    testRemoval();
    testRename();
    testRowButtons();
    testLargeList();
    testHidden();
    testDrag();
    testEmpty();

    // A safety net: a test that hangs still has to end the run with a failure.
    Window::get().after(120000, [] { Window::get().quit(99); });
    runNext();
}

} // namespace app
