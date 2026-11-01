#include <windows.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "app/add_panel.h"
#include "app/app.h"
#include "app/link_panel.h"
#include "app/search_panel.h"
#include "engine/shared.h"

namespace app
{

namespace
{

using namespace ui;

constexpr DWORD kSettleMs = 320;
constexpr DWORD kStepMs = 60;
constexpr DWORD kTimeoutMs = 90000;
constexpr int kWorkSamples = 8;

int gFailures = 0;
int gChecks = 0;
FILE* gLog = nullptr;
size_t gEmptyPanelWork = SIZE_MAX;
size_t gAddPanelWork = SIZE_MAX;
size_t gSearchPanelWork = SIZE_MAX;
uint64_t gFrames = 0;

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

float scaled(float dips)
{
    return dips * Window::get().renderer().dpi() / 96.0F;
}

LPARAM clientPoint(D2D1_POINT_2F point)
{
    return MAKELPARAM(static_cast<int>(scaled(point.x) + 0.5F), static_cast<int>(scaled(point.y) + 0.5F));
}

void send(UINT message, WPARAM wparam, D2D1_POINT_2F point)
{
    SendMessageW(Window::get().handle(), message, wparam, clientPoint(point));
}

void click(D2D1_POINT_2F point)
{
    send(WM_MOUSEMOVE, 0, point);
    send(WM_LBUTTONDOWN, MK_LBUTTON, point);
    send(WM_LBUTTONUP, 0, point);
}

void doubleClick(D2D1_POINT_2F point)
{
    click(point);
    send(WM_LBUTTONDBLCLK, MK_LBUTTON, point);
    send(WM_LBUTTONUP, 0, point);
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

/// What Ctrl+A does, sent directly since a synthetic key can't hold Control down.
void replaceText(TextField* field, std::wstring_view text)
{
    field->selectAll();
    typed(text);
}

D2D1_POINT_2F centre(View const* view)
{
    auto const origin = view->absoluteOrigin();
    return D2D1::Point2F(origin.x + view->width() / 2.0F, origin.y + view->height() / 2.0F);
}

void clickView(View const* view)
{
    click(centre(view));
}

template<typename T>
T* openPanel()
{
    return dynamic_cast<T*>(Window::get().panel());
}

void clearClipboard()
{
    if (OpenClipboard(Window::get().handle()) != FALSE)
    {
        EmptyClipboard();
        CloseClipboard();
    }
}

void setClipboard(std::wstring const& text)
{
    if (OpenClipboard(Window::get().handle()) == FALSE)
    {
        return;
    }
    EmptyClipboard();
    size_t const bytes = (text.size() + 1) * sizeof(wchar_t);
    if (HGLOBAL const memory = GlobalAlloc(GMEM_MOVEABLE, bytes); memory != nullptr)
    {
        memcpy(GlobalLock(memory), text.c_str(), bytes);
        GlobalUnlock(memory);
        SetClipboardData(CF_UNICODETEXT, memory);
    }
    CloseClipboard();
}

/// The torrent in add.png, which the demo engine's Sintel can't be: that one is always a duplicate.
engine::AddPreview blenderPreview(wchar_t const* hash)
{
    std::vector<engine::FlatFile> const flat{
        { "Blender Open Movies/Charge/Charge.mp4", 4'100'000 },
        { "Blender Open Movies/Charge/Charge.srt", 100'000 },
        { "Blender Open Movies/Notes.txt", 2'100'000 },
        { "Blender Open Movies/Sprite Fright/Sprite Fright.mp4", 4'100'000 },
        { "Blender Open Movies/Sprite Fright/Sprite Fright.srt", 100'000 },
    };
    engine::AddPreview preview;
    preview.valid = true;
    preview.name = L"Blender Open Movies";
    preview.hash = hash;
    preview.files = engine::buildFileTree(flat, true);
    preview.totalBytes = 10'500'000;
    preview.torrentFile = L"C:\\Demo\\Blender Open Movies.torrent";
    return preview;
}

engine::AddPreview magnetPreview(wchar_t const* hash)
{
    engine::AddPreview preview;
    preview.valid = true;
    preview.isMagnet = true;
    preview.name = L"Cosmic Dawn";
    preview.hash = hash;
    return preview;
}

int gHash = 0;

/// The demo engine knows nothing of hashes, but each add here still gets its own.
std::wstring const& nextHash()
{
    static std::wstring hash;
    hash = L"ffff00000000000000000000000000000000" + std::to_wstring(1000 + ++gHash);
    return hash;
}

bool hasTransfer(std::wstring const& name)
{
    auto const& transfers = App::get().transfers();
    return std::any_of(transfers.begin(), transfers.end(), [&name](engine::TransferInfo const& info) { return info.name == name; });
}

void testFileList()
{
    then(0, []
    {
        AddFileList files(blenderPreview(L"x").files);
        auto const& nodes = files.nodes();
        check(nodes.size() == 7, "tree: every node is there");
        check(files.rows().size() == 3, "tree: folders start collapsed");
        check(files.rowNode(0).name == L"Charge" && files.rowNode(1).name == L"Notes.txt" && files.rowNode(2).name == L"Sprite Fright", "tree: files and folders sort together, as in add.png");
        check(files.rowNode(2).next == 7 && files.rowNode(2).parent == -1, "tree: links survive the sort");
        check(nodes[1].parent == 0 && nodes[2].parent == 0 && nodes[0].next == 3, "tree: children stay under their folder");

        uint64_t const total = files.selectedBytes();
        check(total == 10'500'000, "wanted: starts with everything");
        check(files.allWanted() && files.anyWanted(), "wanted: all wanted");

        // Charge.mp4 is node 1 and Charge.srt node 2.
        int const charge = 0;
        files.setWanted(1, false);
        check(files.checkState(charge) == CheckState::Mixed, "wanted: one file off leaves its folder mixed");
        check(files.checkState(1) == CheckState::Off && files.checkState(2) == CheckState::On, "wanted: leaf states");
        check(files.selectedBytes() == total - 4'100'000, "wanted: bytes follow the file");
        check(!files.allWanted(), "wanted: no longer all");

        files.setWanted(2, false);
        check(files.checkState(charge) == CheckState::Off, "wanted: both off turns the folder off");
        files.setWanted(charge, true);
        check(files.checkState(charge) == CheckState::On && files.checkState(1) == CheckState::On, "wanted: a folder sets its files");
        check(files.selectedBytes() == total, "wanted: bytes come back");

        files.setAllWanted(false);
        check(!files.anyWanted() && files.selectedBytes() == 0, "wanted: none");
        check(files.wanted().size() == files.fileCount() && files.wanted()[0] == 0, "wanted: one flag per file index");
        files.setAllWanted(true);

        files.setExpanded(charge, true);
        check(files.rows().size() == 5 && files.rowNode(1).name == L"Charge.mp4", "rows: expanding shows children");
        check(files.rowOf(1) == 1, "rows: finds a child's row");
        files.setExpanded(charge, false);
        check(files.rowOf(1) == -1, "rows: a hidden child has no row");

        // Notes.txt sorts between the folders, at node 3.
        check(!files.rename(3, L"a/b"), "rename: slash refused");
        check(!files.rename(3, L"CON.txt"), "rename: device names refused");
        check(!files.rename(3, L"trailing."), "rename: a trailing dot refused");
        check(!files.rename(3, L"charge"), "rename: a sibling's name refused, whatever its case");
        check(files.renames().empty(), "rename: refusals change nothing");
        check(files.rename(3, L"Read me.txt"), "rename: a good name lands");
        check(nodes[3].name == L"Read me.txt" && nodes[3].path == L"Blender Open Movies/Notes.txt", "rename: the path stays the preview's");
        auto const renames = files.renames();
        check(renames.size() == 1 && renames[0].path == L"Blender Open Movies/Notes.txt" && renames[0].newName == L"Read me.txt", "rename: reported for the engine");
        check(files.rename(3, L"Notes.txt") && files.renames().empty(), "rename: going back cancels it");
    });
}

void openBlender(wchar_t const* name = L"Blender Open Movies")
{
    auto preview = blenderPreview(nextHash().c_str());
    preview.name = name;
    App::get().openPreview(std::move(preview));
}

/// A freed panel's address can come straight back for the next one, so only its title tells them apart.
bool showingAdd(wchar_t const* name)
{
    auto* panel = openPanel<AddPanel>();
    return panel != nullptr && !panel->closing() && panel->parts().title->text() == name;
}

void sampleWork(size_t& lowest)
{
    for (int i = 0; i < kWorkSamples; ++i)
    {
        then(kStepMs, [&lowest] { lowest = std::min(lowest, Window::get().pendingWork()); });
    }
}

void testAddPanel()
{
    then(0, []
    {
        openBlender();
        auto* panel = openPanel<AddPanel>();
        check(panel != nullptr, "add: opens");
        check(panel != nullptr && Window::get().focused() == nullptr, "add: nothing has focus, so Return means Get");
    });
    then(kSettleMs, []
    {
        auto* panel = openPanel<AddPanel>();
        auto const& parts = panel->parts();
        check(parts.status->text() == L"5 files  ·  10.5 MB  ·  10.5 MB selected", "add: the summary line");
        check(parts.saveHint->text() == App::get().engine().settings().downloadFolder, "add: starts in the download folder");
        check(parts.start->on() == App::get().engine().settings().startAddedTransfers, "add: start follows the setting");
        check(parts.trash != nullptr && !parts.trash->on(), "add: offers the Recycle Bin, off by default");
        check(parts.trashTitle->text() == L"Move the .torrent file to the Recycle Bin", "add: says Recycle Bin");
        check(!parts.all->enabled() && parts.none->enabled(), "add: All and None start right");
        check(parts.rows->rowCount() == 3, "add: three rows");

        AddFileRows* rows = parts.rows;
        click(rows->checkPoint(1));
        check(panel->files().checkState(3) == CheckState::Off, "add: a click clears a checkbox");
        check(parts.status->text() == L"5 files  ·  10.5 MB  ·  8.4 MB selected", "add: the summary follows");
        check(parts.all->enabled(), "add: All wakes up");
        check(rows->selectedCount() == 0, "add: a checkbox click doesn't select the row");
        click(rows->checkPoint(1));
        check(panel->files().checkState(3) == CheckState::On, "add: a second click sets it again");

        click(rows->chevronPoint(0));
        check(rows->rowCount() == 5, "add: the chevron expands");
        click(rows->checkPoint(1));
        check(panel->files().checkState(0) == CheckState::Mixed, "add: a child makes its folder mixed");
        click(rows->checkPoint(0));
        check(panel->files().checkState(0) == CheckState::On, "add: a mixed click turns it on");
        click(rows->checkPoint(0));
        check(panel->files().checkState(0) == CheckState::Off, "add: then off");
        click(rows->chevronPoint(0));
        check(rows->rowCount() == 3, "add: the chevron collapses");

        clickView(parts.none);
        check(!panel->files().anyWanted() && !parts.none->enabled() && parts.all->enabled(), "add: None");
        clickView(parts.all);
        check(panel->files().allWanted() && !parts.all->enabled() && parts.none->enabled(), "add: All");

        click(D2D1::Point2F(rows->absoluteOrigin().x + 120.0F, rows->checkPoint(1).y));
        check(rows->isSelected(1), "add: a click on the name selects");
        key(VK_SPACE);
        check(panel->files().checkState(3) == CheckState::Off, "add: Space clears what's selected");
        key(VK_SPACE);
        key(VK_DOWN);
        check(rows->isSelected(2), "add: arrows move");
        key(VK_RIGHT);
        check(rows->rowCount() == 5 && rows->isSelected(2), "add: Right expands and keeps the selection");
        key(VK_LEFT);
        check(rows->rowCount() == 3, "add: Left collapses");
    });
    then(kStepMs, []
    {
        auto* panel = openPanel<AddPanel>();
        auto const& parts = panel->parts();
        AddFileRows* rows = parts.rows;

        // Rename a file: the stem is selected, and typing replaces only that.
        doubleClick(D2D1::Point2F(rows->absoluteOrigin().x + 120.0F, rows->checkPoint(1).y));
        check(rows->renaming(), "rename: a double click starts editing");
        check(Window::get().focused() == rows->editor(), "rename: the editor has focus");
        check(rows->editor()->text() == L"Notes.txt", "rename: starts from the name");
        check(rows->editor()->anchor() == 0 && rows->editor()->caret() == 5, "rename: the stem is selected");
        typed(L"Read me");
        check(rows->editor()->text() == L"Read me.txt", "rename: keeps the extension");
        key(VK_RETURN);
        check(!rows->renaming(), "rename: Return commits");
        check(rows->editor()->hidden(), "rename: the editor goes away");
        check(panel->files().nodes()[3].name == L"Read me.txt", "rename: Return kept the name");
        check(panel->request().renames.size() == 1, "rename: the request carries it");

        rows->beginRename(1);
        typed(L"Other");
        key(VK_ESCAPE);
        check(!rows->renaming() && panel->files().nodes()[3].name == L"Read me.txt", "rename: Esc cancels");
        check(!panel->closing(), "rename: Esc doesn't reach the panel");

        rows->beginRename(1);
        typed(L"Bad|name");
        key(VK_RETURN);
        check(panel->files().nodes()[3].name == L"Read me.txt", "rename: an invalid name is refused");

        rows->beginRename(1);
        typed(L"Blurred");
        clickView(parts.saveTitle);
        check(!rows->renaming() && panel->files().nodes()[3].name == L"Blurred.txt", "rename: clicking away commits");

        // A folder, through its pencil.
        click(rows->pencilPoint(0));
        check(rows->renaming() && rows->editor()->text() == L"Charge", "rename: the pencil starts editing");
        check(rows->editor()->caret() == 6, "rename: a folder selects the whole name");
        typed(L"Charged");
        key(VK_RETURN);
        auto const request = panel->request();
        check(request.renames.size() == 2, "rename: both renames are there");
        check(request.renames[0].path == L"Blender Open Movies/Charge" && request.renames[0].newName == L"Charged", "rename: a folder goes by its preview path");

        // The title, through both of its ways in.
        clickView(parts.pencil);
        check(parts.title->hidden() && !parts.titleEditor->hidden(), "title: the pencil swaps in the editor");
        check(Window::get().focused() == parts.titleEditor, "title: the editor has focus");
        replaceText(parts.titleEditor, L"Open Movies");
        key(VK_RETURN);
        check(!parts.title->hidden() && parts.title->text() == L"Open Movies", "title: Return commits");
        check(panel->request().nameOverride == L"Open Movies", "title: the request carries it");

        clickView(parts.title);
        check(parts.title->hidden(), "title: clicking the name edits it");
        typed(L"Abandoned");
        key(VK_ESCAPE);
        check(parts.title->text() == L"Open Movies", "title: Esc cancels");
        check(!panel->closing(), "title: Esc doesn't reach the panel");

        clickView(parts.title);
        parts.titleEditor->selectAll();
        typed(L"  ");
        key(VK_RETURN);
        check(parts.title->text() == L"Blender Open Movies" && panel->request().nameOverride.empty(), "title: an empty name goes back to the original");

        clickView(parts.pencil);
        replaceText(parts.titleEditor, L"Typed then Get");
        clickView(parts.saveTitle);
        check(parts.title->text() == L"Typed then Get", "title: clicking the glass commits");

        panel->chooseFolder = [] { return std::optional<std::wstring>(L"D:\\Films"); };
        clickView(parts.change);
        check(parts.saveHint->text() == L"D:\\Films" && panel->request().destination == L"D:\\Films", "folder: Change picks one");
        panel->chooseFolder = [] { return std::nullopt; };
        clickView(parts.change);
        check(parts.saveHint->text() == L"D:\\Films" && !panel->closing(), "folder: a cancelled picker keeps it");

        check(!panel->request().startPaused, "start: on means not paused");
        clickView(parts.start);
        check(panel->request().startPaused, "start: off means paused");
        clickView(parts.start);

        click(D2D1::Point2F(5.0F, 300.0F));
        check(!panel->closing(), "add: an outside click doesn't close it");

        key(VK_ESCAPE);
        check(panel->closing(), "add: Esc closes");
    });
    then(kSettleMs, []
    {
        check(Window::get().panel() == nullptr, "add: Esc finished closing");
        check(!hasTransfer(L"Typed then Get"), "add: Esc adds nothing");

        // Each way out must let the next queued add in, and exactly once.
        openBlender(L"First");
        openBlender(L"Second");
        openBlender(L"Third");
    });
    then(kSettleMs, []
    {
        check(showingAdd(L"First"), "queue: the first of three is up");
        auto* first = openPanel<AddPanel>();
        clickView(first->parts().cancel);
        check(first->closing(), "queue: Cancel closes");
    });
    then(kSettleMs, []
    {
        check(showingAdd(L"Second"), "queue: Cancel lets the second in, and only the second");
        auto* second = openPanel<AddPanel>();
        auto const before = App::get().transfers().size();
        key(VK_RETURN);
        check(App::get().transfers().size() == before + 1, "queue: Return is Get");
        check(second->closing(), "queue: Get closes");
    });
    then(kSettleMs, []
    {
        check(showingAdd(L"Third"), "queue: Get lets the third in");
        // A panel replaced outright never plays its close, and that's still a close.
        openLinkPanel();
        check(showingAdd(L"Third"), "queue: Link won't replace an Add panel");
        openSearchPanel();
        check(showingAdd(L"Third"), "queue: nor will Search");
        Panel::open<Panel>(300.0F, 200.0F);
    });
    then(kSettleMs, []
    {
        auto* plain = Window::get().panel();
        check(plain != nullptr && dynamic_cast<AddPanel*>(plain) == nullptr, "queue: something else took over");
        openBlender(L"Fourth");
        check(showingAdd(L"Fourth"), "queue: being replaced still counted as closing");
    });
    then(kSettleMs, []
    {
        auto* panel = openPanel<AddPanel>();
        panel->close();
    });
    then(kSettleMs, []
    {
        Window::get().setPanel(nullptr);

        // Get, in full: wanted files, priority, a paused start and the Recycle Bin.
        wchar_t folder[MAX_PATH]{};
        GetTempPathW(MAX_PATH, folder);
        std::wstring const path = std::wstring(folder) + L"nagare-panels-test.torrent";
        if (HANDLE const file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr); file != INVALID_HANDLE_VALUE)
        {
            CloseHandle(file);
        }

        auto preview = blenderPreview(nextHash().c_str());
        preview.torrentFile = path;
        App::get().openPreview(std::move(preview));
        auto* panel = openPanel<AddPanel>();
        auto const& parts = panel->parts();
        clickView(parts.trash);
        parts.priority->setSelected(1);
        panel->chooseFolder = [] { return std::optional<std::wstring>(L"D:\\Finished"); };
        clickView(parts.change);
        clickView(parts.start);
        clickView(parts.pencil);
        replaceText(parts.titleEditor, L"Fully Added");
        clickView(parts.get);
        check(hasTransfer(L"Fully Added"), "get: the name lands, with the edit still open");
        check(panel->closing(), "get: closes");
        check(GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES, "get: the .torrent left for the Recycle Bin");

        auto const& transfers = App::get().transfers();
        auto const found = std::find_if(transfers.begin(), transfers.end(), [](engine::TransferInfo const& info) { return info.name == L"Fully Added"; });
        check(found != transfers.end() && App::get().engine().options(found->id).priority == engine::Priority::High, "get: priority lands");
        App::get().refreshNow();
        check(found != transfers.end() && found->state == engine::TransferState::Paused, "get: start off stays paused");

        panel->get();
    });
    then(kSettleMs, []
    {
        auto const& transfers = App::get().transfers();
        check(std::count_if(transfers.begin(), transfers.end(), [](engine::TransferInfo const& info) { return info.name == L"Fully Added"; }) == 1, "get: a second Get during the close adds nothing");

        // A magnet: same panel, no file list, and the name waits for its metadata.
        App::get().openPreview(magnetPreview(nextHash().c_str()));
        auto* panel = openPanel<AddPanel>();
        auto const& parts = panel->parts();
        check(parts.rows == nullptr && parts.all == nullptr, "magnet: no file list");
        check(parts.trash == nullptr, "magnet: no .torrent to bin");
        check(parts.status->text() == L"Magnet link. The file list arrives once it connects.", "magnet: says so");
        check(panel->height() < 300.0F, "magnet: a shorter panel");
        clickView(parts.pencil);
        replaceText(parts.titleEditor, L"Renamed Magnet");
        key(VK_RETURN);
        check(panel->request().nameOverride == L"Renamed Magnet", "magnet: the name goes in the request");
        clickView(parts.get);
        check(hasTransfer(L"Renamed Magnet"), "magnet: Get adds it");
    });
    then(kSettleMs, []
    {
        check(Window::get().panel() == nullptr, "magnet: closed");

        // One file: nothing to tick, as on the Mac.
        std::vector<engine::FlatFile> const flat{ { "Lone.iso", 3'000'000 } };
        engine::AddPreview preview;
        preview.valid = true;
        preview.name = L"Lone.iso";
        preview.hash = nextHash();
        preview.totalBytes = 3'000'000;
        preview.files = engine::buildFileTree(flat, false);
        App::get().openPreview(std::move(preview));
        auto* panel = openPanel<AddPanel>();
        check(panel->parts().rows == nullptr, "single: no file list");
        check(panel->parts().status->text() == L"3 MB", "single: the summary is its size");
        check(panel->parts().trash == nullptr, "single: only a real file gets the Recycle Bin row");
        check(panel->request().wanted.size() == 1 && panel->request().wanted[0] == 1, "single: its one file is wanted");
        panel->close();
    });
    then(kSettleMs, []
    {
        // A warning asks once in the status line, and a second Get means yes.
        auto preview = blenderPreview(nextHash().c_str());
        App::get().openPreview(std::move(preview));
        auto* panel = openPanel<AddPanel>();
        auto const& parts = panel->parts();
        panel->chooseFolder = [] { return std::optional<std::wstring>(L"D:\\Blender Open Movies"); };
        clickView(parts.change);
        auto const before = App::get().transfers().size();
        clickView(parts.get);
        check(App::get().transfers().size() == before && !panel->closing(), "warn: the same name stops the first Get");
        check(parts.status->text().find(L"same name") != std::wstring::npos, "warn: says why");
        clickView(parts.get);
        check(App::get().transfers().size() == before + 1 && panel->closing(), "warn: a second Get adds it");
    });
    then(kSettleMs, []
    {
        // A refused add keeps everything on screen, with a changed name ready to fix.
        auto preview = blenderPreview(nextHash().c_str());
        preview.valid = false;
        App::get().openPreview(std::move(preview));
        auto* panel = openPanel<AddPanel>();
        auto const& parts = panel->parts();
        clickView(parts.pencil);
        replaceText(parts.titleEditor, L"Refused");
        key(VK_RETURN);
        clickView(parts.get);
        check(!panel->closing(), "refused: the panel stays");
        check(!parts.status->text().empty() && parts.status->text().find(L"selected") == std::wstring::npos, "refused: says why");
        check(Window::get().focused() == parts.titleEditor, "refused: the name is ready to fix");
        key(VK_ESCAPE);
        check(!panel->closing(), "refused: Esc leaves the editor first");
        key(VK_ESCAPE);
        check(panel->closing(), "refused: then closes");
    });
    then(kSettleMs, []
    {
        // Already here: the notice stands in for the panel, and closing it counts as closing.
        auto duplicate = blenderPreview(nextHash().c_str());
        duplicate.duplicateOf = App::get().transfers().front().id;
        App::get().openPreview(std::move(duplicate));
        openBlender();
        check(Window::get().panel() != nullptr && openPanel<AddPanel>() == nullptr, "duplicate: gets a notice, not the panel");
        check(addPanelShowing(), "duplicate: counts as showing");
        key(VK_RETURN);
        check(dynamic_cast<Panel*>(Window::get().panel())->closing(), "duplicate: Return dismisses it");
    });
    then(kSettleMs, []
    {
        auto* panel = openPanel<AddPanel>();
        check(panel != nullptr, "duplicate: the next add follows");
        if (panel != nullptr)
        {
            panel->close();
        }
    });
    then(kSettleMs, []
    {
        // With the ask turned off a magnet goes straight in, and the queue must not stall.
        auto& engine = App::get().engine();
        engine::AppPrefs prefs = engine.prefs();
        prefs.magnetOpenAsk = false;
        engine.setPrefs(prefs);
        auto const before = App::get().transfers().size();
        App::get().openPreview(magnetPreview(nextHash().c_str()));
        check(App::get().transfers().size() == before + 1 && Window::get().panel() == nullptr, "direct: no panel when not asked to");
        openBlender();
        check(openPanel<AddPanel>() != nullptr, "direct: the queue carries on");
        prefs.magnetOpenAsk = true;
        engine.setPrefs(prefs);
    });
    then(kSettleMs, []
    {
        openPanel<AddPanel>()->close();
    });

    // The swell reschedules itself behind any panel, so one reading can catch it or miss it. A
    // panel's own timer would be there every time, which makes the lowest reading the fair one.
    then(kSettleMs, []
    {
        Window::get().setPanel(nullptr);
        Panel::open<Panel>(560.0F, 600.0F);
    });
    then(kSettleMs, [] {});
    sampleWork(gEmptyPanelWork);
    then(0, []
    {
        Window::get().setPanel(nullptr);
        openBlender();
    });
    then(kSettleMs, [] { gFrames = Window::get().framesPainted(); });
    sampleWork(gAddPanelWork);
    then(0, []
    {
        Window& window = Window::get();
        note("     work: %zu under an empty panel, %zu under Add, %llu frames\n", gEmptyPanelWork, gAddPanelWork, static_cast<unsigned long long>(window.framesPainted() - gFrames));
        check(gAddPanelWork <= gEmptyPanelWork, "idle: an open Add panel adds no timers");
        openPanel<AddPanel>()->close();
    });
}

void testLinkPanel()
{
    constexpr wchar_t const* kMagnet = L"magnet:?xt=urn:btih:ffff000000000000000000000000000000002000&dn=Clipboard";

    then(kSettleMs, []
    {
        check(looksLikeLink(L"magnet:?xt=abc"), "link: a magnet");
        check(looksLikeLink(L"MAGNET:?xt=abc"), "link: case doesn't matter");
        check(looksLikeLink(L"https://example.com/a.torrent"), "link: an https torrent");
        check(!looksLikeLink(L"https://example.com/a.html"), "link: not any https page");
        check(!looksLikeLink(L"http://example.com/a.torrent"), "link: not http");
        check(!looksLikeLink(L"hello"), "link: not text");

        setClipboard(std::wstring(L"  ") + kMagnet + L"\r\n");
        openLinkPanel();
        auto* panel = openPanel<LinkPanel>();
        check(panel != nullptr, "link: opens");
        check(panel->field()->text() == kMagnet, "link: prefilled and trimmed from the clipboard");
        check(panel->getButton()->enabled(), "link: Get is ready");
        check(Window::get().focused() == panel->field(), "link: the field has focus");
        panel->close();
    });
    then(kSettleMs, []
    {
        setClipboard(L"just some text");
        openLinkPanel();
        auto* panel = openPanel<LinkPanel>();
        check(panel->field()->text().empty(), "link: other text stays out");
        check(!panel->getButton()->enabled(), "link: Get needs something");
        typed(L"magnet:?xt=urn:btih:ffff000000000000000000000000000000002001");
        check(panel->getButton()->enabled(), "link: typing enables Get");
        key(VK_RETURN);
    });
    then(kSettleMs, []
    {
        auto* add = openPanel<AddPanel>();
        check(add != nullptr, "link: a good magnet goes to the Add panel");
        if (add != nullptr)
        {
            add->close();
        }
    });
    then(kSettleMs, []
    {
        check(Window::get().panel() == nullptr, "link: nothing left over");

        clearClipboard();
        openLinkPanel();
        auto* panel = openPanel<LinkPanel>();
        typed(L"magnet:broken");
        clickView(panel->getButton());
        check(openPanel<LinkPanel>() == panel && !panel->closing(), "link: a bad magnet keeps the panel");
        check(!panel->status()->text().empty(), "link: and says why");
        check(panel->getButton()->enabled(), "link: Get can be tried again");
        panel->close();
    });
    then(kSettleMs, []
    {
        openLinkPanel();
        auto* panel = openPanel<LinkPanel>();
        typed(L"https://example.com/a.torrent");
        key(VK_RETURN);
        check(panel->status()->text() == L"Fetching…", "link: a fetch shows");
        check(!panel->getButton()->enabled(), "link: and holds Get");
    });
    then(kSettleMs, []
    {
        // The demo fetch parses into Sintel, which is always a duplicate.
        check(Window::get().panel() != nullptr && addPanelShowing(), "link: a fetched torrent carries on to the add");
        key(VK_RETURN);
    });
    then(kSettleMs, []
    {
        openLinkPanel();
        clickView(openPanel<LinkPanel>()->cancelButton());
    });
    then(kSettleMs, []
    {
        check(Window::get().panel() == nullptr, "link: Cancel closes");
        openLinkPanel();
        key(VK_ESCAPE);
    });
    then(kSettleMs, []
    {
        check(Window::get().panel() == nullptr, "link: Esc closes");
        openLinkPanel();
        click(D2D1::Point2F(5.0F, 300.0F));
    });
    then(kSettleMs, []
    {
        check(Window::get().panel() == nullptr, "link: an outside click closes");
    });
}

void testSearchPanel()
{
    then(0, []
    {
        check(plainResultText(L"a\r\nb\tc") == L"a  b c", "plain: line breaks become spaces");
        check(plainResultText(L"\x202Egnp.exe") == L"gnp.exe", "plain: no bidi overrides");
        check(plainResultText(std::wstring(5000, L'x')).size() == 400, "plain: a long title is cut");

        openSearchPanel();
        auto* panel = openPanel<SearchPanel>();
        check(panel != nullptr, "search: opens");
        check(panel->status()->text() == L"Type and press Return to search", "search: starts with the hint");
        check(Window::get().focused() == panel->query(), "search: the field has focus");
        check(panel->sources()->title() == L"All sources", "search: all sources first");
        check(panel->results()->getButton()->hidden(), "search: no pill without a row");
        key(VK_RETURN);
        check(panel->status()->text() == L"Type and press Return to search", "search: nothing to search for yet");
        typed(L"sintel");
        key(VK_RETURN);
        check(panel->status()->text() == L"Searching…", "search: says it's searching");
        check(SearchPanel::searching(), "search: is in flight");
    });
    then(kSettleMs, []
    {
        auto* panel = openPanel<SearchPanel>();
        SearchResultList* list = panel->results();
        check(!SearchPanel::searching(), "search: finished");
        check(list->rowCount() == 6, "search: rows arrive");
        check(panel->status()->text() == L"6 results from 1 of 1 sources", "search: the count");
        check(list->selected() == 0, "search: the first row is selected");
        check(!list->getButton()->hidden(), "search: the pill shows");
        check(list->rows()[0].detail == L"Internet Archive  ·  1.81 GB", "search: source and size");
        check(list->rows()[0].health == -1, "search: no seeds, no dots");

        key(VK_UP);
        check(list->selected() == 0, "search: Up stops at the top");
        key(VK_DOWN);
        key(VK_DOWN);
        check(list->selected() == 2, "search: Down moves");
        check(Window::get().focused() == panel->query(), "search: the field keeps focus");
        float const pillTop = list->getButton()->frame().top;
        check(pillTop > list->rowRect(2).top && pillTop < list->rowRect(2).bottom, "search: the pill follows");
        for (int i = 0; i < 8; ++i)
        {
            key(VK_DOWN);
        }
        check(list->selected() == 5, "search: Down stops at the bottom");
        check(list->rowRect(5).bottom <= list->height() + 0.5F, "search: the last row scrolled into view");
        key(VK_UP);
        clickView(list->getButton());
        check(panel->status()->text() == L"Fetching…", "search: the pill gets that row");
    });
    then(kSettleMs, []
    {
        // The demo fetch is always a duplicate of Sintel.
        check(addPanelShowing(), "search: a fetched result reaches the add");
        key(VK_RETURN);
    });
    then(kSettleMs, []
    {
        openSearchPanel();
        auto* panel = openPanel<SearchPanel>();
        SearchResultList* list = panel->results();
        check(panel->query()->text() == L"sintel" && list->rowCount() == 6, "search: reopening shows the last search");
        check(list->selected() == 4, "search: with the same row selected");

        typed(L"new");
        check(panel->query()->text() == L"new", "search: reopening selects the old query");
        key(VK_RETURN);
        check(panel->status()->text() == L"Searching…", "search: a changed query searches instead of getting");
        check(list->rowCount() == 0 && list->getButton()->hidden(), "search: and clears the rows");
    });
    then(kSettleMs, []
    {
        auto* panel = openPanel<SearchPanel>();
        panel->sources()->setSelected(1);
        panel->sources()->onChange(1);
        check(panel->status()->text() == L"Searching…", "search: a new source searches again");
        check(SearchPanel::searching(), "search: in flight again");
        key(VK_ESCAPE);
        check(panel->closing(), "search: Esc closes");
        check(!SearchPanel::searching(), "search: closing cancels straight away");
    });
    then(kSettleMs, []
    {
        check(Window::get().panel() == nullptr, "search: gone");

        openSearchPanel();
        auto* panel = openPanel<SearchPanel>();
        check(panel->status()->text() == L"Type and press Return to search", "search: a cancelled search leaves the hint");
        std::wstring opened;
        panel->openFolder = [&opened](std::wstring const& folder) { opened = folder; };
        int const last = 2;
        panel->sources()->setSelected(last);
        panel->sources()->onChange(last);
        check(opened == App::get().engine().searchSourcesFolder(), "search: Add sources opens the folder");
        check(panel->sources()->selected() == 0, "search: and goes back to all sources");
        panel->openFolder = [](std::wstring const&) {};
        replaceText(panel->query(), L"sintel");
        key(VK_RETURN);
        // Replaced outright, the way Get's Add panel replaces it.
        Panel::open<Panel>(300.0F, 200.0F);
    });
    then(kSettleMs, []
    {
        check(!SearchPanel::searching(), "search: being replaced cancels too");
        Window::get().setPanel(nullptr);
        openSearchPanel();
        openSearchPanel();
        auto* panel = openPanel<SearchPanel>();
        check(panel != nullptr && !panel->closing(), "search: opening twice keeps the one panel");
    });
    sampleWork(gSearchPanelWork);
    then(0, []
    {
        check(gSearchPanelWork <= gEmptyPanelWork + 1, "idle: only the caret ticks under Search");
        click(D2D1::Point2F(5.0F, 300.0F));
    });
    then(kSettleMs, []
    {
        check(Window::get().panel() == nullptr, "search: an outside click closes");
    });
}

} // namespace

void openPanelDemo(std::wstring_view name)
{
    if (name == L"add-files")
    {
        App::get().openPreview(blenderPreview(L"ffff000000000000000000000000000000000001"));
        return;
    }
    openSearchPanel();
    auto* panel = openPanel<SearchPanel>();
    panel->query()->setText(L"sintel");
    panel->search();
    panel->query()->selectAll();
}

void runPanelsTest()
{
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring logPath = path;
    logPath.resize(logPath.find_last_of(L'\\') + 1);
    logPath += L"panels-test.log";
    _wfopen_s(&gLog, logPath.c_str(), L"w");

    testFileList();
    testAddPanel();
    testLinkPanel();
    testSearchPanel();

    // A safety net: a test that hangs still has to end the run with a failure.
    Window::get().after(kTimeoutMs, [] { Window::get().quit(99); });
    runNext();
}

} // namespace app
