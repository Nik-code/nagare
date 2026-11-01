#include <windows.h>

#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>

#include <algorithm>
#include <sstream>

#include "app/main_panels.h"
#include "app/main_view.h"

// What the Mac's Controller does for the menu bar and context menus, gathered here because Windows
// has no menu bar: every item lives in a context menu or behind the toolbar's `...` button.

namespace app
{

namespace
{

using engine::QueueMove;
using engine::TransferId;
using engine::TransferInfo;
using engine::TransferState;
using ui::MenuItem;
using ui::Window;

constexpr int kNoGroup = -1;

constexpr wchar_t const* kGroupNames[] = { L"Red", L"Orange", L"Yellow", L"Green", L"Blue", L"Purple", L"Gray" };

bool isRunning(TransferInfo const& info)
{
    return info.state == TransferState::Download || info.state == TransferState::Fetching || info.state == TransferState::Seeding ||
        info.state == TransferState::Verifying || info.isWaiting;
}

bool isActive(TransferInfo const& info)
{
    return isRunning(info) && !info.isWaiting;
}

bool isStopped(TransferInfo const& info)
{
    return !isRunning(info) && !info.isFinishedSeeding;
}

bool isDownloading(TransferInfo const& info)
{
    return isActive(info) && info.state != TransferState::Seeding;
}

MenuItem item(std::wstring title, std::function<void()> action, bool enabled = true, bool checked = false)
{
    MenuItem entry;
    entry.title = std::move(title);
    entry.action = std::move(action);
    entry.enabled = enabled;
    entry.checked = checked;
    return entry;
}

void openShell(std::wstring const& target)
{
    ShellExecuteW(nullptr, L"open", target.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

std::wstring parentOf(std::wstring const& path)
{
    size_t const slash = path.find_last_of(L"\\/");
    return slash != std::wstring::npos ? path.substr(0, slash) : path;
}

bool pickFile(bool saving, std::wstring const& suggested, std::vector<std::wstring>& out, bool folders = false)
{
    ComPtr<IFileDialog> dialog;
    HRESULT const created = saving ? CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog))
                                   : CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog));
    if (FAILED(created))
    {
        return false;
    }

    FILEOPENDIALOGOPTIONS options = 0;
    dialog->GetOptions(&options);
    if (folders)
    {
        options |= FOS_PICKFOLDERS;
    }
    else
    {
        COMDLG_FILTERSPEC const types[] = { { L"Torrent files", L"*.torrent" } };
        dialog->SetFileTypes(1, types);
        dialog->SetDefaultExtension(L"torrent");
        if (!saving)
        {
            options |= FOS_ALLOWMULTISELECT;
        }
    }
    dialog->SetOptions(options | FOS_FORCEFILESYSTEM);
    if (!suggested.empty())
    {
        dialog->SetFileName(suggested.c_str());
    }

    if (FAILED(dialog->Show(Window::get().handle())))
    {
        return false;
    }

    auto const take = [&out](IShellItem* shellItem)
    {
        wchar_t* path = nullptr;
        if (SUCCEEDED(shellItem->GetDisplayName(SIGDN_FILESYSPATH, &path)))
        {
            out.emplace_back(path);
            CoTaskMemFree(path);
        }
    };

    ComPtr<IFileOpenDialog> opener;
    ComPtr<IShellItemArray> items;
    if (!saving && !folders && SUCCEEDED(dialog.As(&opener)) && SUCCEEDED(opener->GetResults(&items)))
    {
        DWORD count = 0;
        items->GetCount(&count);
        for (DWORD i = 0; i < count; ++i)
        {
            ComPtr<IShellItem> shellItem;
            if (SUCCEEDED(items->GetItemAt(i, &shellItem)))
            {
                take(shellItem.Get());
            }
        }
    }
    else
    {
        ComPtr<IShellItem> shellItem;
        if (SUCCEEDED(dialog->GetResult(&shellItem)))
        {
            take(shellItem.Get());
        }
    }
    return !out.empty();
}

void setClipboard(std::wstring const& text)
{
    if (!OpenClipboard(Window::get().handle()))
    {
        return;
    }
    EmptyClipboard();
    size_t const bytes = (text.size() + 1) * sizeof(wchar_t);
    if (HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes); memory != nullptr)
    {
        memcpy(GlobalLock(memory), text.c_str(), bytes);
        GlobalUnlock(memory);
        if (SetClipboardData(CF_UNICODETEXT, memory) == nullptr)
        {
            GlobalFree(memory);
        }
    }
    CloseClipboard();
}

void showAbout()
{
    ConfirmRequest request;
    request.title = L"Nagare";
    request.message = L"A torrent client for Windows, built on libtransmission.";
    openConfirmPanel(std::move(request));
}

std::wstring clipboardText()
{
    std::wstring text;
    if (!OpenClipboard(Window::get().handle()))
    {
        return text;
    }
    if (HANDLE data = GetClipboardData(CF_UNICODETEXT); data != nullptr)
    {
        if (auto const* chars = static_cast<wchar_t const*>(GlobalLock(data)); chars != nullptr)
        {
            text = chars;
            GlobalUnlock(data);
        }
    }
    CloseClipboard();
    return text;
}

} // namespace

TransferInfo const* MainView::find(TransferId id) const
{
    auto const& transfers = App::get().transfers();
    auto const found = std::lower_bound(transfers.begin(), transfers.end(), id, [](TransferInfo const& info, TransferId wanted) { return info.id < wanted; });
    return found != transfers.end() && found->id == id ? &*found : nullptr;
}

MainView::Ids MainView::idsWhere(bool (*test)(TransferInfo const&)) const
{
    Ids ids;
    for (TransferInfo const& info : App::get().transfers())
    {
        if (test(info))
        {
            ids.push_back(info.id);
        }
    }
    return ids;
}

bool MainView::anySelected(bool (*test)(TransferInfo const&)) const
{
    for (TransferId const id : list_->selectedIds())
    {
        TransferInfo const* info = find(id);
        if (info != nullptr && test(*info))
        {
            return true;
        }
    }
    return false;
}

void MainView::pause(Ids const& ids)
{
    if (!ids.empty())
    {
        App::get().engine().pause(ids);
        App::get().refreshNow();
    }
}

void MainView::resume(Ids const& ids)
{
    if (!ids.empty())
    {
        App::get().engine().resume(ids);
        App::get().refreshNow();
    }
}

void MainView::resumeNow(Ids const& ids)
{
    if (!ids.empty())
    {
        App::get().engine().resumeNow(ids);
        App::get().refreshNow();
    }
}

void MainView::toggle(Ids const& ids)
{
    bool anyRunning = false;
    for (TransferId const id : ids)
    {
        TransferInfo const* info = find(id);
        anyRunning = anyRunning || (info != nullptr && isRunning(*info));
    }
    if (anyRunning)
    {
        pause(ids);
    }
    else
    {
        resume(ids);
    }
}

void MainView::verify(Ids const& ids)
{
    if (!ids.empty())
    {
        App::get().engine().verify(ids);
        App::get().refreshNow();
    }
}

void MainView::reannounce(Ids const& ids)
{
    if (!ids.empty())
    {
        App::get().engine().reannounce(ids);
    }
}

void MainView::removeNow(Ids const& ids, bool deleteData)
{
    App::get().engine().remove(ids, deleteData);
    App::get().refreshNow();
}

void MainView::remove(Ids const& ids, bool deleteData)
{
    if (ids.empty())
    {
        return;
    }

    engine::AppPrefs const& prefs = App::get().engine().prefs();
    if (!prefs.checkRemove)
    {
        removeNow(ids, deleteData);
        return;
    }

    size_t active = 0;
    size_t downloading = 0;
    for (TransferId const id : ids)
    {
        if (TransferInfo const* info = find(id); info != nullptr && isActive(*info))
        {
            ++active;
            downloading += isDownloading(*info) ? 1U : 0U;
        }
    }
    if ((prefs.checkRemoveDownloading ? downloading : active) == 0)
    {
        removeNow(ids, deleteData);
        return;
    }

    ConfirmRequest request;
    request.confirm = L"Remove";
    request.cancel = L"Cancel";
    size_t const selected = ids.size();
    if (selected == 1)
    {
        TransferInfo const* info = find(ids.front());
        std::wstring const name = info != nullptr ? info->name : std::wstring{};
        request.title = L"Are you sure you want to remove \"" + name + L"\" from the transfer list";
        request.title += deleteData ? L" and move the data file to the Recycle Bin?" : L"?";
        request.message = L"This transfer is active. Once removed, continuing the transfer will require the torrent file or magnet link.";
    }
    else
    {
        std::wstring const count = countText(static_cast<int>(selected));
        request.title = L"Are you sure you want to remove " + count + L" transfers from the transfer list";
        request.title += deleteData ? L" and move the data files to the Recycle Bin?" : L"?";
        request.message = selected == active ? L"There are " + count + L" active transfers." :
                                               L"There are " + count + L" transfers (" + countText(static_cast<int>(active)) + L" active).";
        request.message += L" Once removed, continuing the transfers will require the torrent files or magnet links.";
    }
    request.onConfirm = [this, ids, deleteData] { removeNow(ids, deleteData); };
    openConfirmPanel(std::move(request));
}

void MainView::removeCompleted()
{
    Ids const ids = idsWhere([](TransferInfo const& info) { return info.isFinishedSeeding; });
    if (ids.empty())
    {
        return;
    }

    ConfirmRequest request;
    request.confirm = L"Remove";
    request.cancel = L"Cancel";
    if (ids.size() == 1)
    {
        TransferInfo const* info = find(ids.front());
        request.title = L"Are you sure you want to remove \"" + (info != nullptr ? info->name : std::wstring{}) + L"\" from the transfer list?";
        request.message = L"Once removed, continuing the transfer will require the torrent file or magnet link.";
    }
    else
    {
        request.title = L"Are you sure you want to remove " + countText(static_cast<int>(ids.size())) + L" completed transfers from the transfer list?";
        request.message = L"Once removed, continuing the transfers will require the torrent files or magnet links.";
    }
    request.onConfirm = [this, ids] { removeNow(ids, false); };
    openConfirmPanel(std::move(request));
}

void MainView::moveInQueue(QueueMove move)
{
    Ids const ids = selection();
    if (!ids.empty())
    {
        App::get().engine().moveInQueue(ids, move);
        App::get().refreshNow();
    }
}

void MainView::setGroup(int group)
{
    for (TransferId const id : selection())
    {
        App::get().engine().setGroup(id, group);
    }
    App::get().refreshNow();
}

void MainView::openFolder(Ids const& ids)
{
    for (TransferId const id : ids)
    {
        std::wstring const path = App::get().engine().details(id).dataPath;
        if (!path.empty())
        {
            bool const isFolder = (GetFileAttributesW(path.c_str()) & FILE_ATTRIBUTE_DIRECTORY) != 0 && GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
            openShell(isFolder ? path : parentOf(path));
        }
    }
}

void MainView::reveal(Ids const& ids)
{
    for (TransferId const id : ids)
    {
        std::wstring const path = App::get().engine().details(id).dataPath;
        if (path.empty())
        {
            continue;
        }
        PIDLIST_ABSOLUTE pidl = nullptr;
        if (SUCCEEDED(SHParseDisplayName(path.c_str(), nullptr, &pidl, 0, nullptr)))
        {
            SHOpenFolderAndSelectItems(pidl, 0, nullptr, 0);
            CoTaskMemFree(pidl);
        }
        else
        {
            openShell(parentOf(path));
        }
    }
}

void MainView::copyMagnets(Ids const& ids)
{
    std::wstring links;
    for (TransferId const id : ids)
    {
        if (!links.empty())
        {
            links += L'\n';
        }
        links += App::get().engine().details(id).magnetLink;
    }
    if (!links.empty())
    {
        setClipboard(links);
    }
}

void MainView::moveData(Ids const& ids)
{
    if (ids.empty())
    {
        return;
    }
    std::vector<std::wstring> picked;
    if (!pickFile(false, {}, picked, true))
    {
        return;
    }
    for (TransferId const id : ids)
    {
        App::get().engine().moveData(id, picked.front(), [](bool, std::wstring const&) { App::get().refreshNow(); });
    }
}

void MainView::saveTorrentFiles(Ids const& ids)
{
    for (TransferId const id : ids)
    {
        TransferInfo const* info = find(id);
        if (info == nullptr || info->isMagnet)
        {
            continue;
        }
        std::wstring const source = App::get().engine().details(id).torrentFile;
        if (GetFileAttributesW(source.c_str()) == INVALID_FILE_ATTRIBUTES)
        {
            ConfirmRequest request;
            request.title = L"Copy of \"" + info->name + L"\" Cannot Be Created";
            request.message = L"The torrent file (" + source + L") cannot be found.";
            openConfirmPanel(std::move(request));
            return;
        }
        std::vector<std::wstring> picked;
        std::wstring const name = info->name;
        if (pickFile(true, name, picked))
        {
            CopyFileW(source.c_str(), picked.front().c_str(), FALSE);
        }
    }
}

void MainView::renameSelected()
{
    Ids const ids = selection();
    TransferInfo const* info = ids.size() == 1 ? find(ids.front()) : nullptr;
    if (info == nullptr)
    {
        return;
    }
    openRenamePanel(
        info->name,
        [id = info->id, path = info->name](std::wstring const& name)
        {
            App::get().engine().renamePath(id, path, name, [](bool, std::wstring const&) { App::get().refreshNow(); });
        });
}

void MainView::paste()
{
    // openPasteboard: anything separated by white space that looks like a link is opened.
    std::wistringstream words(clipboardText());
    std::wstring word;
    while (words >> word)
    {
        if (word.starts_with(L"magnet:"))
        {
            App::get().openMagnet(word);
        }
        else if (word.starts_with(L"http://") || word.starts_with(L"https://"))
        {
            App::get().engine().fetchTorrent(
                word,
                [](std::string bytes, std::wstring const&)
                {
                    if (!bytes.empty())
                    {
                        auto preview = App::get().engine().parseTorrentData(std::move(bytes));
                        if (preview.valid)
                        {
                            App::get().openPreview(std::move(preview));
                        }
                    }
                });
        }
    }
}

void MainView::openTorrentFiles()
{
    std::vector<std::wstring> picked;
    if (pickFile(false, {}, picked))
    {
        App::get().openPaths(picked);
    }
}

void MainView::toggleSpeedLimit()
{
    App::get().engine().setAltSpeed(!App::get().engine().sessionStats().altSpeedOn);
    App::get().refreshNow();
}

void MainView::showInfo()
{
    // The Mac's inspector can sit empty saying nothing is selected. A sheet here has to show something,
    // so with no selection it opens the top row and selects it, which keeps the two in step.
    Ids const ids = selection();
    if (!ids.empty())
    {
        int const cursor = list_->cursorRow();
        TransferId const id = list_->isSelected(cursor) ? list_->idAt(cursor) : ids.front();
        openDetailsPanel(id);
        return;
    }
    if (list_->rowCount() > 0)
    {
        list_->selectRow(0);
        openDetailsPanel(list_->idAt(0));
    }
}

void MainView::doubleClicked(int row)
{
    TransferId const id = list_->idAt(row);
    if (id == engine::kNoTransfer)
    {
        return;
    }

    App& app = App::get();
    if (app.engine().prefs().openFinishedOnDoubleClick)
    {
        auto const& transfers = app.transfers();
        auto const found = std::find_if(transfers.begin(), transfers.end(), [id](auto const& info) { return info.id == id; });
        if (found != transfers.end() && found->leftBytes == 0 && found->percentDone >= 1.0F)
        {
            std::wstring const path = app.engine().details(id).dataPath;
            if (!path.empty())
            {
                openShell(path);
                return;
            }
        }
    }
    openDetailsPanel(id);
}

ui::MenuItem MainView::sortMenu()
{
    struct Entry
    {
        wchar_t const* title;
        TransferSort sort;
    };
    constexpr Entry kEntries[] = {
        { L"Queue Order", TransferSort::Order }, { L"Activity", TransferSort::Activity }, { L"Date Added", TransferSort::Date },
        { L"Name", TransferSort::Name },         { L"Progress", TransferSort::Progress }, { L"ETA", TransferSort::Eta },
        { L"Size", TransferSort::Size },         { L"State", TransferSort::State },       { L"Tracker", TransferSort::Tracker },
    };

    MenuItem menu = item(L"Sort Transfers By", {});
    for (Entry const& entry : kEntries)
    {
        menu.submenu.push_back(item(entry.title, [this, sort = entry.sort] { setSort(sort); }, true, sort_ == entry.sort));
    }
    menu.submenu.push_back(MenuItem::line());
    bool const canReverse = sort_ != TransferSort::Order;
    auto const setReverse = [this](bool reverse)
    {
        reverse_ = reverse;
        persistView();
        refresh();
    };
    menu.submenu.push_back(item(L"Ascending", [setReverse] { setReverse(false); }, canReverse, !reverse_));
    menu.submenu.push_back(item(L"Descending", [setReverse] { setReverse(true); }, canReverse, reverse_));
    return menu;
}

std::vector<ui::MenuItem> MainView::rowMenu()
{
    Ids const ids = selection();
    bool const any = !ids.empty();
    bool const one = ids.size() == 1;

    MenuItem queue = item(L"Queue", {});
    queue.submenu.push_back(item(L"Move to Top", [this] { moveInQueue(QueueMove::Top); }));
    queue.submenu.push_back(item(L"Move Up", [this] { moveInQueue(QueueMove::Up); }));
    queue.submenu.push_back(item(L"Move Down", [this] { moveInQueue(QueueMove::Down); }));
    queue.submenu.push_back(item(L"Move to Bottom", [this] { moveInQueue(QueueMove::Bottom); }));

    int const currentGroup = [this, one, &ids]
    {
        TransferInfo const* info = one ? find(ids.front()) : nullptr;
        return info != nullptr ? info->group : kNoGroup - 1;
    }();
    MenuItem groups = item(L"Group", {});
    groups.submenu.push_back(item(L"None", [this] { setGroup(kNoGroup); }, true, currentGroup == kNoGroup));
    groups.submenu.push_back(MenuItem::line());
    for (int group = 0; group < static_cast<int>(std::size(kGroupNames)); ++group)
    {
        groups.submenu.push_back(item(kGroupNames[group], [this, group] { setGroup(group); }, true, currentGroup == group));
    }

    bool const allMagnets = std::all_of(ids.begin(), ids.end(), [this](TransferId id) { auto const* info = find(id); return info == nullptr || info->isMagnet; });

    std::vector<MenuItem> items;
    items.push_back(item(L"Pause Selected", [this, ids] { pause(ids); }, anySelected(isRunning)));
    items.push_back(item(L"Resume Selected", [this, ids] { resume(ids); }, anySelected(isStopped)));
    items.push_back(item(L"Resume Selected Right Away", [this, ids] { resumeNow(ids); }, anySelected([](TransferInfo const& info) { return !isActive(info); })));
    items.push_back(MenuItem::line());
    items.push_back(item(L"Remove From List", [this, ids] { remove(ids, false); }, any));
    items.push_back(item(L"Remove and Recycle Data File", [this, ids] { remove(ids, true); }, any));
    items.push_back(MenuItem::line());
    items.push_back(item(L"Open Folder", [this, ids] { openFolder(ids); }, any));
    items.push_back(item(L"Show in Explorer", [this, ids] { reveal(ids); }, any));
    items.push_back(std::move(groups));
    items.push_back(std::move(queue));
    items.push_back(item(L"Move Data File To…", [this, ids] { moveData(ids); }, any));
    items.push_back(item(L"Rename File…", [this] { renameSelected(); }, one));
    items.push_back(MenuItem::line());
    items.push_back(item(L"Save Torrent File As…", [this, ids] { saveTorrentFiles(ids); }, any && !allMagnets));
    items.push_back(item(L"Copy Magnet Link to Clipboard", [this, ids] { copyMagnets(ids); }, any));
    items.push_back(MenuItem::line());
    items.push_back(item(L"Update Tracker", [this, ids] { reannounce(ids); }, anySelected(isActive)));
    items.push_back(item(L"Verify Local Data", [this, ids] { verify(ids); }, any));
    items.push_back(MenuItem::line());
    items.push_back(item(L"Show Inspector", [this] { showInfo(); }, any));
    return items;
}

std::vector<ui::MenuItem> MainView::emptyMenu()
{
    std::vector<MenuItem> items;
    items.push_back(item(L"Open Torrent File…", [this] { openTorrentFiles(); }));
    items.push_back(item(L"Open Torrent Address…", [] { openLinkPanel(); }));
    items.push_back(MenuItem::line());
    items.push_back(item(L"Show Inspector", [this] { showInfo(); }, list_->rowCount() > 0));
    return items;
}

std::vector<ui::MenuItem> MainView::moreMenu()
{
    auto const& transfers = App::get().transfers();
    auto const anyWhere = [&transfers](bool (*test)(TransferInfo const&)) { return std::any_of(transfers.begin(), transfers.end(), test); };

    MenuItem help = item(L"Help", {});
    help.submenu.push_back(item(L"Transmission Project", [] { openShell(L"https://transmissionbt.com/"); }));
    help.submenu.push_back(item(L"Support & Development", [] { openShell(L"https://github.com/Nik-code/nagare"); }));
    help.submenu.push_back(MenuItem::line());
    help.submenu.push_back(item(L"About Nagare", [] { showAbout(); }));

    std::vector<MenuItem> items;
    items.push_back(item(L"Settings…", [] { openSettingsPanel(); }));
    items.push_back(MenuItem::line());
    items.push_back(item(L"Open Torrent File…", [this] { openTorrentFiles(); }));
    items.push_back(item(L"Open Torrent Address…", [] { openLinkPanel(); }));
    items.push_back(item(L"Search…", [] { openSearchPanel(); }));
    items.push_back(MenuItem::line());
    items.push_back(item(L"Speed Limit", [this] { toggleSpeedLimit(); }, true, App::get().engine().sessionStats().altSpeedOn));
    items.push_back(item(L"Pause All", [this] { pause(idsWhere([](TransferInfo const&) { return true; })); }, anyWhere(isRunning)));
    items.push_back(item(L"Resume All", [this] { resume(idsWhere([](TransferInfo const& info) { return !info.isFinishedSeeding; })); }, anyWhere(isStopped)));
    items.push_back(item(L"Resume All Waiting", [this] { resumeNow(idsWhere([](TransferInfo const& info) { return info.isWaiting; })); }, anyWhere([](TransferInfo const& info) { return info.isWaiting; })));
    items.push_back(item(L"Remove All Completed From List", [this] { removeCompleted(); }, anyWhere([](TransferInfo const& info) { return info.isFinishedSeeding; })));
    items.push_back(MenuItem::line());
    items.push_back(sortMenu());
    items.push_back(item(L"Use Groups", [this] { useGroups_ = !useGroups_; refresh(); }, true, useGroups_));
    items.push_back(MenuItem::line());
    items.push_back(std::move(help));
    items.push_back(item(L"Quit Nagare", [] { PostMessageW(Window::get().handle(), WM_CLOSE, 0, 0); }));
    return items;
}

} // namespace app
