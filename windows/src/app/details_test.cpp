#include "app/details_test.h"

#include <windows.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <deque>
#include <functional>
#include <string>
#include <vector>

#include "app/app.h"
#include "app/details_files.h"
#include "app/details_panel.h"
#include "engine/shared.h"
#include "ui/widgets.h"

namespace app
{

namespace
{

using namespace ui;
using namespace details;

constexpr DWORD kStep = 40;
constexpr DWORD kSettleMs = 320;
constexpr DWORD kSafetyMs = 90000;

// Big enough that anything worse than linear in the visible rows would show.
constexpr int kFolders = 400;
constexpr int kFilesPerFolder = 250;
constexpr int kTreeCycles = 50;
constexpr double kMaxCycleMs = 8.0;
constexpr float kOutsideX = 20.0F;
constexpr float kOutsideY = 400.0F;

int gFailures = 0;
int gChecks = 0;
FILE* gLog = nullptr;
std::wstring gFolder;
std::deque<std::pair<DWORD, std::function<void()>>> gSteps;

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

constexpr DWORD kQuietMs = 2500;

void step(std::function<void()> run, DWORD delay = kStep)
{
    gSteps.emplace_back(delay, std::move(run));
}

void settle(std::function<void()> run)
{
    step(std::move(run), kSettleMs);
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
    auto next = std::move(gSteps.front());
    gSteps.pop_front();
    Window::get().after(
        next.first,
        [run = std::move(next.second)]
        {
            run();
            runNext();
        });
}

LPARAM clientPoint(D2D1_POINT_2F point)
{
    float const scale = Window::get().renderer().dpi() / 96.0F;
    return MAKELPARAM(static_cast<int>(point.x * scale + 0.5F), static_cast<int>(point.y * scale + 0.5F));
}

void send(UINT message, WPARAM wparam, D2D1_POINT_2F point)
{
    SendMessageW(Window::get().handle(), message, wparam, clientPoint(point));
}

void moveTo(D2D1_POINT_2F point)
{
    send(WM_MOUSEMOVE, 0, point);
}

void click(D2D1_POINT_2F point)
{
    moveTo(point);
    send(WM_LBUTTONDOWN, MK_LBUTTON, point);
    send(WM_LBUTTONUP, 0, point);
}

void doubleClick(D2D1_POINT_2F point)
{
    moveTo(point);
    send(WM_LBUTTONDBLCLK, MK_LBUTTON, point);
    send(WM_LBUTTONUP, 0, point);
}

void rightClick(D2D1_POINT_2F point)
{
    moveTo(point);
    send(WM_RBUTTONDOWN, MK_RBUTTON, point);
    send(WM_RBUTTONUP, 0, point);
}

D2D1_POINT_2F centreOf(View const* view)
{
    auto const origin = view->absoluteOrigin();
    return D2D1::Point2F(origin.x + view->width() / 2.0F, origin.y + view->height() / 2.0F);
}

void clickView(View const* view)
{
    click(centreOf(view));
}

void press(UINT key)
{
    SendMessageW(Window::get().handle(), WM_KEYDOWN, key, 0);
    SendMessageW(Window::get().handle(), WM_KEYUP, key, 0);
}

void type(std::wstring_view text)
{
    for (wchar_t const c : text)
    {
        SendMessageW(Window::get().handle(), WM_CHAR, c, 0);
    }
}

void capture(wchar_t const* name)
{
    Window::get().saveNextFrame(gFolder + L"details-test-" + name + L".png");
    Window::get().invalidateAll();
}

/// The nth view of a type below `root`, in the order they were added.
template<typename T>
T* find(View* root, int nth = 0)
{
    int seen = 0;
    std::function<T*(View*)> walk = [&](View* view) -> T*
    {
        if (auto* match = dynamic_cast<T*>(view); match != nullptr && seen++ == nth)
        {
            return match;
        }
        for (auto const& child : view->children())
        {
            if (T* found = walk(child.get()); found != nullptr)
            {
                return found;
            }
        }
        return nullptr;
    };
    return root != nullptr ? walk(root) : nullptr;
}

DetailsPanel* panel()
{
    return dynamic_cast<DetailsPanel*>(Window::get().panel());
}

engine::TransferId firstId()
{
    return App::get().transfers().front().id;
}

engine::Engine& theEngine()
{
    return App::get().engine();
}

engine::TransferOptions options()
{
    return theEngine().options(firstId());
}

FilesTab* filesTab()
{
    return panel() != nullptr ? dynamic_cast<FilesTab*>(panel()->tabView(Tab::Files)) : nullptr;
}

/// The tree as the engine has it now, which is the truth a click must reach.
engine::FileTree liveTree()
{
    return theEngine().files(firstId());
}

void testFormats()
{
    step(
        []
        {
            TextBuffer buffer;
            check(fileSizeText(0, buffer) == L"Zero KB", "size: nothing");
            check(fileSizeText(999, buffer) == L"999 bytes", "size: bytes");
            check(fileSizeText(46'700, buffer) == L"47 KB", "size: kilobytes have no decimals");
            check(fileSizeText(129'300'000, buffer) == L"129.3 MB", "size: megabytes have one");
            check(fileSizeText(999'600, buffer) == L"1 MB", "size: rounding up crosses the unit");
            check(fileSizeText(2'000'000'000, buffer) == L"2 GB", "size: whole numbers lose the point");
            check(speedText(0.0, buffer) == L"0.0 KB/s", "speed: idle");
            check(speedText(3'640'000.0, buffer) == L"3.64 MB/s", "speed: megabytes");
            check(percentText(1.0, 2, buffer) == L"100%", "percent: done");
            check(percentText(0.97375, 2, buffer) == L"97.38%", "percent: two places");
            check(percentText(0.5, 2, buffer) == L"50%", "percent: no trailing zeros");
            check(percentText(0.99999, 2, buffer) == L"99.99%", "percent: never 100 before the end");
            check(ratioText(2.0, buffer) == L"2.00", "ratio");
            check(durationText(3903) == L"1h 5m 3s", "duration");
            check(durationText(59) == L"59s", "duration: seconds alone");
        });
}

void testTabs()
{
    step([] { openDetailsPanel(firstId()); });
    settle(
        []
        {
            check(panel() != nullptr, "the panel opens");
            check(panel() != nullptr && panel()->currentTab() == Tab::Overview, "it starts on Overview");
            check(panel() != nullptr && panel()->tabView(Tab::Peers) == nullptr, "tabs not yet seen aren't built");
        });

    for (int index = 0; index < kTabCount; ++index)
    {
        step(
            [index]
            {
                PillBar* pills = panel()->pills();
                D2D1_RECT_F const pill = pills->pillRect(index);
                auto const origin = pills->absoluteOrigin();
                click(D2D1::Point2F(origin.x + (pill.left + pill.right) / 2.0F, origin.y + (pill.top + pill.bottom) / 2.0F));
            });
        step(
            [index]
            {
                auto const tab = static_cast<Tab>(index);
                check(panel()->currentTab() == tab, "a pill switches the tab");
                bool onlyOne = true;
                for (int other = 0; other < kTabCount; ++other)
                {
                    auto* view = panel()->tabView(static_cast<Tab>(other));
                    onlyOne = onlyOne && (view == nullptr || view->hidden() == (other != index));
                }
                check(onlyOne, "only that tab is visible");

                int const before = panel()->refreshCount(tab);
                int others = 0;
                for (int other = 0; other < kTabCount; ++other)
                {
                    others += other != index ? panel()->refreshCount(static_cast<Tab>(other)) : 0;
                }
                App::get().refreshNow();
                App::get().refreshNow();
                check(panel()->refreshCount(tab) == before + 2, "the showing tab refreshes");
                int after = 0;
                for (int other = 0; other < kTabCount; ++other)
                {
                    after += other != index ? panel()->refreshCount(static_cast<Tab>(other)) : 0;
                }
                check(after == others, "hidden tabs read nothing");
            });
    }
}

void testOptions()
{
    step([] { panel()->showTab(Tab::Options); });
    step(
        []
        {
            auto* tab = panel()->tabView(Tab::Options);
            clickView(find<Switch>(tab, 0));
            check(options().limitDown, "download limit switch writes");
            check(Window::get().focused() == find<TextField>(tab, 0), "turning it on focuses the field");
            type(L"321");
            press(VK_RETURN);
            check(options().downKBps == 321, "the field writes on Return");
            check(Window::get().focused() == nullptr, "and lets go of the keyboard");

            clickView(find<Switch>(tab, 1));
            check(options().limitUp, "upload limit switch writes");
            type(L"77");
            Window::get().focus(nullptr);
            check(options().upKBps == 77, "the field writes when it loses focus");

            clickView(find<Switch>(tab, 0));
            check(!options().limitDown, "turning a limit off writes");
            check(options().downKBps == 321, "and keeps its value");

            clickView(find<Switch>(tab, 2));
            check(!options().honorSessionLimits, "global limits switch writes");
            clickView(find<Switch>(tab, 3));
            check(options().removeWhenDone, "remove when done writes");
            clickView(find<Switch>(tab, 4));
            check(options().sequential, "sequential writes");
        });
    step(
        []
        {
            auto* tab = panel()->tabView(Tab::Options);
            auto* priority = find<Dropdown>(tab, 0);
            clickView(priority);
            check(Window::get().popup() != nullptr, "the priority dropdown opens its menu");
            press(VK_DOWN);
            press(VK_RETURN);
        });
    step(
        []
        {
            check(options().priority == engine::Priority::High, "picking from the menu writes the priority");

            auto* tab = panel()->tabView(Tab::Options);
            auto* ratio = find<Dropdown>(tab, 1);
            ratio->setSelected(2);
            ratio->onChange(2);
            check(options().ratioMode == engine::LimitMode::Single, "ratio mode writes");
            auto* ratioField = find<TextField>(tab, 2);
            check(!ratioField->hidden() && Window::get().focused() == ratioField, "custom ratio shows and focuses its field");
            type(L"1.5");
            press(VK_RETURN);
            check(options().ratioLimit == 1.5, "custom ratio writes");
            check(ratioField->text() == L"1.50", "and shows two places");

            Window::get().focus(ratioField);
            ratioField->selectAll();
            type(L"1.2.3");
            Window::get().focus(nullptr);
            check(options().ratioLimit == 1.5, "a bad ratio changes nothing");
            check(ratioField->text() == L"1.50", "and puts the old one back");

            auto* idle = find<Dropdown>(tab, 2);
            idle->setSelected(2);
            idle->onChange(2);
            auto* idleField = find<TextField>(tab, 3);
            type(L"45");
            press(VK_RETURN);
            check(options().idleMode == engine::LimitMode::Single && options().idleMinutes == 45, "custom idle writes");
            idle->setSelected(1);
            idle->onChange(1);
            check(options().idleMode == engine::LimitMode::Unlimited, "don't stop writes");
            check(idleField->hidden(), "and hides the field");

            auto* peers = find<TextField>(tab, 4);
            Window::get().focus(peers);
            peers->selectAll();
            type(L"99");
            press(VK_RETURN);
            check(options().peerLimit == 99, "maximum connections writes");
        });
    step([] { capture(L"options"); });

    // Half-typed text must survive leaving the tab, which is what closing does too.
    step(
        []
        {
            auto* peers = find<TextField>(panel()->tabView(Tab::Options), 4);
            Window::get().focus(peers);
            peers->selectAll();
            type(L"123");
            panel()->showTab(Tab::Peers);
            check(options().peerLimit == 123, "leaving the tab commits what was typed");
            panel()->showTab(Tab::Options);
        });
}

void testFiles()
{
    step([] { panel()->showTab(Tab::Files); });
    step(
        []
        {
            FilesTab* tab = filesTab();
            check(tab != nullptr && tab->model().rows().size() == 5, "the demo's files list");

            click(tab->zoneCentre(0, FileZone::Check));
            check(liveTree().nodes[0].wanted == engine::Mixed::Off, "a check click unwants the file");
            check(tab->model().tree().nodes[0].wanted == engine::Mixed::Off, "the row's tree catches up");
            click(tab->zoneCentre(0, FileZone::Check));
            check(liveTree().nodes[0].wanted == engine::Mixed::On, "and wants it again");

            click(tab->zoneCentre(1, FileZone::Priority));
            check(liveTree().nodes[1].priority == engine::Priority::High, "priority cycles to high");
            click(tab->zoneCentre(1, FileZone::Priority));
            check(liveTree().nodes[1].priority == engine::Priority::Low, "then low");
            click(tab->zoneCentre(1, FileZone::Priority));
            check(liveTree().nodes[1].priority == engine::Priority::Normal, "then normal");
            check(tab->list()->selectedCount() == 0, "clicking a control doesn't select the row");

            clickView(find<Button>(tab, 1));
            auto const none = liveTree();
            check(std::all_of(none.nodes.begin(), none.nodes.end(), [](auto const& node) { return node.wanted == engine::Mixed::Off; }), "None unwants everything");
            check(find<Button>(tab, 0)->enabled(), "All is available then");
            clickView(find<Button>(tab, 0));
            auto const all = liveTree();
            check(std::all_of(all.nodes.begin(), all.nodes.end(), [](auto const& node) { return node.wanted == engine::Mixed::On; }), "All wants everything");
            check(!find<Button>(tab, 0)->enabled(), "All turns itself off when there's nothing to do");
        });
    step(
        []
        {
            FilesTab* tab = filesTab();
            moveTo(tab->zoneCentre(2, FileZone::Rename));
            capture(L"files-hover");
        });
    step(
        []
        {
            FilesTab* tab = filesTab();
            auto const row = tab->list()->rowRect(1);
            auto const origin = tab->list()->absoluteOrigin();
            rightClick(D2D1::Point2F(origin.x + 200.0F, origin.y + (row.top + row.bottom) / 2.0F));
            check(Window::get().popup() != nullptr, "right click opens the menu");
            check(tab->list()->selectedCount() == 1, "and selects the row under it");
            capture(L"files-menu");
        },
        kSettleMs);
    step(
        []
        {
            press(VK_ESCAPE);
            check(Window::get().popup() == nullptr, "Esc closes the menu");
            check(panel() != nullptr && !panel()->closing(), "and only the menu");

            FilesTab* tab = filesTab();
            tab->beginRename(4);
            auto* field = tab->renameField();
            check(!field->hidden() && Window::get().focused() == field, "rename opens an inline field");
            check(field->text() == tab->model().nodeAt(4).name, "with the name in it");
            int const stem = static_cast<int>(field->text().find_last_of(L'.'));
            check(field->caret() == stem && field->anchor() == 0, "and the stem selected");
            capture(L"files-rename");
        });
    step(
        []
        {
            FilesTab* tab = filesTab();
            press(VK_ESCAPE);
            check(tab->renameField()->hidden(), "Esc cancels a rename");
            check(panel() != nullptr && !panel()->closing(), "without closing the panel");

            tab->beginRename(4);
            press(VK_DELETE);
            type(L"renamed");
            press(VK_RETURN);
            check(tab->renameField()->hidden(), "Return commits a rename");
        });
    settle(
        []
        {
            FilesTab* tab = filesTab();
            check(tab->model().rows().size() == 5, "the tree reloads after a rename");

            Window::get().focus(tab->filterField());
            type(L"SRT");
        });
    step(
        []
        {
            FilesTab* tab = filesTab();
            check(tab->model().rows().size() == 3, "the filter ignores case");
            check(tab->model().filtering(), "and is filtering");
            capture(L"files-filter");
        });
    step(
        []
        {
            FilesTab* tab = filesTab();
            tab->filterField()->setText(L"sintél  fr");
            tab->filterField()->onChange(tab->filterField()->text());
            check(tab->model().rows().size() == 1, "every word must match, and accents don't count");
            press(VK_ESCAPE);
            check(tab->model().rows().size() == 5, "Esc clears the filter");
            check(panel() != nullptr && !panel()->closing(), "without closing the panel");
        });
}

engine::FileTree bigTree()
{
    std::vector<engine::FlatFile> files;
    files.reserve(static_cast<size_t>(kFolders) * kFilesPerFolder);
    for (int folder = 0; folder < kFolders; ++folder)
    {
        std::string const prefix = "Root/Folder " + std::to_string(1000 + folder) + "/Inner/";
        for (int file = 0; file < kFilesPerFolder; ++file)
        {
            files.push_back({ prefix + "file " + std::to_string(1000 + file) + ".bin", 1'000'000 });
        }
    }
    return engine::buildFileTree(files, true);
}

void testBigTree()
{
    step(
        []
        {
            FilesTab* tab = filesTab();
            tab->adoptTree(bigTree());
            FileModel& model = tab->model();
            check(model.tree().fileCount == static_cast<size_t>(kFolders) * kFilesPerFolder, "a hundred thousand files load");
            check(model.rows().size() == kFolders, "folders start closed");

            tab->list()->selectRow(5);
            int const selectedNode = model.rows()[5];
            click(tab->zoneCentre(0, FileZone::Disclosure));
            check(model.rows().size() == kFolders + 1, "the chevron opens a folder");
            check(tab->list()->selectedCount() == 1, "the selection survives");
            check(model.rows()[tab->list()->selectedRows().front()] == selectedNode, "on the same node");

            click(tab->zoneCentre(1, FileZone::Disclosure));
            check(model.rows().size() == static_cast<size_t>(kFolders) + 1 + kFilesPerFolder, "and the folder inside it");
            capture(L"files-tree");
        });
    settle(
        []
        {
            FilesTab* tab = filesTab();
            FileModel& model = tab->model();
            click(tab->zoneCentre(0, FileZone::Disclosure));
            check(model.rows().size() == kFolders, "closing folds everything inside");
            click(tab->zoneCentre(0, FileZone::Disclosure));
            check(model.rows().size() == static_cast<size_t>(kFolders) + 1 + kFilesPerFolder, "and its folders remember");
            click(tab->zoneCentre(0, FileZone::Disclosure));

            LARGE_INTEGER frequency{};
            LARGE_INTEGER start{};
            LARGE_INTEGER end{};
            QueryPerformanceFrequency(&frequency);
            QueryPerformanceCounter(&start);
            for (int cycle = 0; cycle < kTreeCycles; ++cycle)
            {
                model.setSubtreeExpanded(model.rows().front(), true);
                model.setSubtreeExpanded(model.rows().front(), false);
            }
            QueryPerformanceCounter(&end);
            double const ms = static_cast<double>(end.QuadPart - start.QuadPart) * 1000.0 / static_cast<double>(frequency.QuadPart) / kTreeCycles;
            note("open and close one folder of %d: %.3f ms\n", kFilesPerFolder, ms);
            check(ms < kMaxCycleMs, "folders open and close in time");

            model.setSubtreeExpanded(model.rows().front(), false);
            int const total = static_cast<int>(model.tree().nodes.size());
            std::vector<int> everything;
            for (int node = 0; node < total; ++node)
            {
                if (model.tree().nodes[static_cast<size_t>(node)].depth == 0)
                {
                    model.setSubtreeExpanded(node, true);
                }
            }
            check(static_cast<int>(model.rows().size()) == total, "the whole tree can be opened");
            tab->adoptTree(std::move(model.tree()));
            tab->list()->scrollTo(tab->list()->maxScroll());
        });
    settle(
        []
        {
            FilesTab* tab = filesTab();
            check(Window::get().framesPainted() > 0, "the far end of a huge list paints");

            uint64_t const frames = Window::get().framesPainted();
            App::get().refreshNow();
            App::get().refreshNow();
            check(Window::get().framesPainted() == frames, "a refresh with nothing new paints nothing");

            tab->filterField()->setText(L"file 1005");
            tab->filterField()->onChange(tab->filterField()->text());
            check(tab->model().rows().size() == kFolders, "the filter reaches every folder");
            tab->filterField()->setText(L"");
            tab->filterField()->onChange(tab->filterField()->text());

            tab->metadataChanged();
            panel()->showTab(Tab::Overview);
            panel()->showTab(Tab::Files);
            check(tab->model().tree().fileCount == 5, "fresh metadata reloads the real tree");
        });
}

void testPeers()
{
    step([] { panel()->showTab(Tab::Peers); });
    step(
        []
        {
            auto* list = find<ListView>(panel()->tabView(Tab::Peers));
            check(list->rowCount() > 0, "peers list");
            auto const origin = list->absoluteOrigin();
            click(D2D1::Point2F(origin.x + 100.0F, origin.y + 20.0F));
            check(list->selectedCount() == 0, "a peer can't be selected");
            capture(L"peers-hover");
        },
        kStep);
}

void testTrackers()
{
    step([] { panel()->showTab(Tab::Trackers); });
    step(
        []
        {
            auto* tab = panel()->tabView(Tab::Trackers);
            auto* field = find<TextField>(tab, 1);
            auto* removeButton = find<Button>(tab, 1);
            check(!removeButton->enabled(), "nothing to remove yet");

            clickView(field);
            type(L"tracker.example.org:6969/announce");
            press(VK_RETURN);
            check(field->text().empty(), "adding a tracker clears the field");

            auto const origin = tab->absoluteOrigin();
            D2D1_POINT_2F const firstTracker = D2D1::Point2F(origin.x + 200.0F, origin.y + 6.0F + 30.0F + 20.0F);
            click(firstTracker);
            check(removeButton->enabled(), "selecting a tracker allows removing it");
            capture(L"trackers-selected");
        });
    step(
        []
        {
            auto* tab = panel()->tabView(Tab::Trackers);
            auto* removeButton = find<Button>(tab, 1);
            clickView(removeButton);
            check(removeButton->enabled(), "the first click only asks");
            capture(L"trackers-confirm");
        });
    step(
        []
        {
            auto* tab = panel()->tabView(Tab::Trackers);
            auto* removeButton = find<Button>(tab, 1);
            clickView(removeButton);
            check(!removeButton->enabled(), "the second removes, and clears the selection");

            auto const origin = tab->absoluteOrigin();
            doubleClick(D2D1::Point2F(origin.x + 200.0F, origin.y + 6.0F + 30.0F + 20.0F));
            auto* edit = find<TextField>(tab, 0);
            check(!edit->hidden() && Window::get().focused() == edit, "double click edits a tracker");
            capture(L"trackers-edit");
        });
    step(
        []
        {
            auto* tab = panel()->tabView(Tab::Trackers);
            auto* edit = find<TextField>(tab, 0);
            press(VK_ESCAPE);
            check(edit->hidden(), "Esc cancels the edit");
            check(panel() != nullptr && !panel()->closing(), "without closing the panel");
            moveTo(D2D1::Point2F(kOutsideX, kOutsideY));

            // Everything has to stop moving first, or the list behind the panel repaints for its own reasons.
            std::vector<int> ids;
            for (auto const& transfer : App::get().transfers())
            {
                ids.push_back(transfer.id);
            }
            App::get().engine().pause(ids);
            App::get().refreshNow();
        });
    step([] {}, kQuietMs);
    settle(
        []
        {
            App::get().refreshNow();
        });
    // The swell's history moves on a fixed clock, so one refresh can land on a real change. Two can't.
    static bool quiet = false;
    settle(
        []
        {
            App::get().refreshNow();
            quiet = !Window::get().framePending();
        });
    settle(
        []
        {
            App::get().refreshNow();
            check(quiet || !Window::get().framePending(), "a refresh with nothing new paints nothing");
        });
}

void testClosing()
{
    static size_t baseline = 0;
    settle(
        []
        {
            Window::get().focus(nullptr);
            press(VK_ESCAPE);
        });
    settle(
        []
        {
            check(Window::get().panel() == nullptr, "Esc closes the panel");
            baseline = Window::get().pendingWork();

            openDetailsPanel(firstId());
        });
    settle(
        []
        {
            check(panel() != nullptr && panel()->currentTab() == Tab::Trackers, "it reopens on the last tab");
            panel()->showTab(Tab::Options);
            auto* tab = panel()->tabView(Tab::Options);
            check(find<Switch>(tab, 3)->on(), "options read back on opening");
            check(find<TextField>(tab, 4)->text() == L"123", "values too");
            click(D2D1::Point2F(kOutsideX, kOutsideY));
        });
    settle(
        []
        {
            check(Window::get().panel() == nullptr, "an outside click closes it");

            auto const last = App::get().transfers().back().id;
            openDetailsPanel(last);
        });
    settle(
        []
        {
            check(panel() != nullptr, "the panel opens for another transfer");
            theEngine().remove({ App::get().transfers().back().id }, false);
            App::get().refreshNow();
        });
    settle(
        []
        {
            check(Window::get().panel() == nullptr, "removing the transfer closes its panel");
            // Only the app's own timer may be left: an open panel's listeners and timers die with it.
            check(Window::get().pendingWork() <= baseline, "nothing runs once it's closed");
        });
}

} // namespace

void runDetailsTests()
{
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    gFolder = path;
    gFolder.resize(gFolder.find_last_of(L'\\') + 1);
    _wfopen_s(&gLog, (gFolder + L"details-test.log").c_str(), L"w");

    testFormats();
    testTabs();
    testOptions();
    testFiles();
    testBigTree();
    testPeers();
    testTrackers();
    testClosing();

    // A test that hangs still has to end the run with a failure.
    Window::get().after(kSafetyMs, [] { Window::get().quit(99); });
    runNext();
}

} // namespace app
