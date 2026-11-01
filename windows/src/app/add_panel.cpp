#include "app/add_panel.h"

#include <shellapi.h>
#include <shlwapi.h>
#include <shobjidl.h>

#include <algorithm>
#include <array>
#include <cmath>

#include "app/app.h"
#include "app/details_common.h"
#include "engine/shared.h"
#include "ui/widgets_internal.h"

namespace app
{

namespace
{

using namespace ui;
using details::fileSizeText;
using details::refuse;
using details::TextBuffer;
using details::trimmed;
using engine::FileNode;

// The numbers in AddWindowController.mm.
constexpr float kPanelWidth = 560.0F;
constexpr float kMargin = 28.0F;
constexpr float kTop = 26.0F;
constexpr float kBottom = 23.0F;
constexpr float kTitleHeight = 28.0F;
constexpr float kTitleGap = 4.0F;
constexpr float kStatusHeight = 16.0F;
constexpr float kStatusGap = 18.0F;
constexpr float kPencilFrame = 26.0F;
constexpr float kPencilSize = 18.0F;
constexpr float kPencilGap = 10.0F;
constexpr float kFilesHeader = 18.0F;
constexpr float kFilesHeaderGap = 11.0F;
constexpr float kSmallButtonHeight = 20.0F;
constexpr float kAllWidth = 34.0F;
constexpr float kNoneWidth = 47.0F;
constexpr float kSmallButtonGap = 6.0F;
constexpr float kListHeight = 251.0F;
constexpr float kListGap = 10.0F;
constexpr float kSaveRow = 56.0F;
constexpr float kToggleRow = 50.0F;
constexpr float kButtonsGap = 11.0F;
constexpr float kButtonGap = 10.0F;
constexpr float kChangeWidth = 76.0F;
constexpr float kCancelWidth = 70.0F;
constexpr float kGetWidth = 84.0F;
constexpr float kPriorityWidth = 150.0F;
constexpr float kRowTextGap = 2.0F;
constexpr float kRowTitleHeight = 18.0F;
constexpr float kRowHintHeight = 16.0F;
constexpr float kControlGap = 16.0F;

// The list reaches past the margins so its bar clears the checkboxes, and insets its rows to match.
constexpr float kListOverhang = 16.0F;

// FileOutlineController.mm and FileNameCellView.mm.
constexpr float kFileRowHeight = 44.0F;
constexpr float kIndent = 14.0F;
constexpr float kChevronX = 7.0F;
constexpr float kChevronSize = 7.0F;
constexpr float kChevronReach = 10.0F;
constexpr float kIconLeft = 20.0F;
constexpr float kIconSize = 18.0F;
constexpr float kNameGap = 10.0F;
constexpr float kNameTop = 5.0F;
constexpr float kNameHeight = 18.0F;
constexpr float kFileSizeTop = 23.0F;
constexpr float kFileSizeHeight = 15.0F;
constexpr float kCheckColumn = 30.0F;
constexpr float kFilePencil = 24.0F;
constexpr float kFilePencilIcon = 16.0F;
constexpr float kFilePencilGap = 4.0F;

constexpr TextStyle kPanelTitle{ 22.0F, DWRITE_FONT_WEIGHT_MEDIUM, true };
constexpr TextStyle kRowTitle{ 14.0F, DWRITE_FONT_WEIGHT_MEDIUM, false };
constexpr TextStyle kBody{ 13.0F, DWRITE_FONT_WEIGHT_NORMAL, false };
constexpr TextStyle kHint{ 12.0F, DWRITE_FONT_WEIGHT_NORMAL, false };
constexpr TextStyle kFileName{ 13.0F, DWRITE_FONT_WEIGHT_MEDIUM, false };
constexpr TextStyle kFileSize{ 11.0F, DWRITE_FONT_WEIGHT_NORMAL, false };

// Wide enough for the Mac's sentence on one line, since text here doesn't wrap.
constexpr float kAlertWidth = 560.0F;
constexpr float kAlertHeight = 150.0F;
constexpr float kAlertBodyTop = 62.0F;
constexpr float kAlertBodyHeight = 18.0F;

float panelHeight(bool files, bool trash)
{
    float height = kTop + kTitleHeight + kTitleGap + kStatusHeight + kStatusGap;
    height += files ? kFilesHeader + kFilesHeaderGap + kListHeight + kListGap : 0.0F;
    height += kSaveRow + kToggleRow + (trash ? kToggleRow : 0.0F);
    return height + kButtonsGap + Button::kHeight + kBottom;
}

/// The Mac's torrent.folder. A single-file torrent has one node and no root in its path.
bool isFolderTorrent(engine::AddPreview const& preview)
{
    auto const& nodes = preview.files.nodes;
    return !preview.isMagnet && (nodes.size() > 1 || (nodes.size() == 1 && nodes[0].path.find(L'/') != std::wstring::npos));
}

template<typename T>
T* place(T* view, float left, float top, float width, float height)
{
    view->setFrame(D2D1::RectF(left, top, left + width, top + height));
    return view;
}

/// The Mac lists a folder's files and subfolders together by name, and the engine's tree puts
/// folders first, so the panel lays out its own copy once as it opens.
void appendSorted(std::vector<FileNode>& from, int first, int end, int parent, std::vector<FileNode>& to)
{
    std::vector<int> children;
    for (int child = first; child < end; child = from[static_cast<size_t>(child)].next)
    {
        children.push_back(child);
    }
    std::sort(
        children.begin(),
        children.end(),
        [&from](int a, int b) { return StrCmpLogicalW(from[static_cast<size_t>(a)].name.c_str(), from[static_cast<size_t>(b)].name.c_str()) < 0; });
    for (int const child : children)
    {
        FileNode& old = from[static_cast<size_t>(child)];
        int const childEnd = old.next;
        int const index = static_cast<int>(to.size());
        to.push_back(std::move(old));
        to.back().parent = parent;
        appendSorted(from, child + 1, childEnd, index, to);
        to[static_cast<size_t>(index)].next = static_cast<int>(to.size());
    }
}

std::optional<std::wstring> pickFolder(std::wstring const& name, std::wstring const& current)
{
    ComPtr<IFileOpenDialog> dialog;
    if (FAILED(CoCreateInstance(__uuidof(FileOpenDialog), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog))))
    {
        return std::nullopt;
    }
    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    std::wstring const title = L"Select the download folder for \"" + name + L"\"";
    dialog->SetTitle(title.c_str());
    dialog->SetOkButtonLabel(L"Select");

    ComPtr<IShellItem> start;
    if (!current.empty() && SUCCEEDED(SHCreateItemFromParsingName(current.c_str(), nullptr, IID_PPV_ARGS(&start))))
    {
        dialog->SetFolder(start.Get());
    }

    ComPtr<IShellItem> chosen;
    if (FAILED(dialog->Show(Window::get().handle())) || FAILED(dialog->GetResult(&chosen)))
    {
        return std::nullopt;
    }
    PWSTR path = nullptr;
    if (FAILED(chosen->GetDisplayName(SIGDN_FILESYSPATH, &path)))
    {
        return std::nullopt;
    }
    std::wstring result = path;
    CoTaskMemFree(path);
    return result;
}

void moveToRecycleBin(std::wstring const& path)
{
    // pFrom is a list, and a second null ends it.
    std::wstring const from = path + L'\0';
    SHFILEOPSTRUCTW operation{};
    operation.wFunc = FO_DELETE;
    operation.pFrom = from.c_str();
    operation.fFlags = FOF_ALLOWUNDO | FOF_NO_UI;
    SHFileOperationW(&operation);
}

/// Deferred, because a panel dies while the window is still clearing out retired views, and the
/// next add opening from inside that would retire another one. A panel that's replaced never runs
/// onClosed, so the destructors below are the one place every path goes through.
void addPanelGone()
{
    Window::get().after(0, [] { App::get().addPanelClosed(); });
}

/// The panel's title: reads as a label, and a click turns it into an editor.
class TitleLabel final : public Label
{
public:
    explicit TitleLabel(std::wstring text)
        : Label(std::move(text), kPanelTitle, ColorRole::Bright)
    {
        interactive_ = true;
        takesFocus_ = false;
        setTrim(Trim::Middle);
    }

    std::function<void()> onClick;

    bool onMouse(MouseEvent const& event) override
    {
        if (detail::isPress(event.kind) && onClick)
        {
            onClick();
            return true;
        }
        return false;
    }

    HCURSOR cursor() const override { return LoadCursorW(nullptr, IDC_IBEAM); }
};

/// The Mac's duplicateOpenAlert, shown for this item instead of the Add panel.
class DuplicatePanel final : public Panel
{
public:
    explicit DuplicatePanel(std::wstring const& name)
        : Panel(kAlertWidth, kAlertHeight)
    {
        setClosesOnOutsideClick(false);
        title_ = add<Label>(L"A transfer of \"" + name + L"\" already exists.", kPanelTitle, ColorRole::Bright);
        title_->setTrim(Trim::Middle);
        body_ = add<Label>(L"The transfer cannot be added because it is a duplicate of an already existing transfer.", kBody, ColorRole::Foreground);
        ok_ = add<Button>(L"OK", ButtonStyle::Accent, [this] { close(); });
    }

    ~DuplicatePanel() override { addPanelGone(); }

    bool onKey(UINT key, bool down) override
    {
        if (key == VK_RETURN)
        {
            if (down)
            {
                close();
            }
            return true;
        }
        return Panel::onKey(key, down);
    }

protected:
    void layoutContent() override
    {
        float const right = width() - kMargin;
        place(title_, kMargin, kTop, right - kMargin, kTitleHeight);
        place(body_, kMargin, kAlertBodyTop, right - kMargin, kAlertBodyHeight);
        place(ok_, right - kGetWidth, height() - kBottom - Button::kHeight, kGetWidth, Button::kHeight);
    }

private:
    Label* title_ = nullptr;
    Label* body_ = nullptr;
    Button* ok_ = nullptr;
};

/// The Mac's choice between the panel and adding straight away. AddTypeAuto has no equivalent here,
/// since everything that reaches App was opened by hand.
bool shouldAsk(engine::AddPreview const& preview, engine::AppPrefs const& prefs, std::wstring const& folder)
{
    if (folder.empty())
    {
        return true;
    }
    if (preview.isMagnet)
    {
        return prefs.magnetOpenAsk;
    }
    return prefs.downloadAsk && (preview.files.fileCount > 1 || !prefs.downloadAskMulti);
}

} // namespace

AddFileList::AddFileList(engine::FileTree tree)
    : wanted_(tree.fileCount, uint8_t{ 1 })
{
    nodes_.reserve(tree.nodes.size());
    appendSorted(tree.nodes, 0, static_cast<int>(tree.nodes.size()), -1, nodes_);
    wantedInside_.resize(nodes_.size());
    expanded_.resize(nodes_.size());
    rowOf_.resize(nodes_.size());
    rollUp();
    rebuildRows();
}

void AddFileList::rollUp()
{
    std::fill(wantedInside_.begin(), wantedInside_.end(), 0);
    wantedFiles_ = 0;
    selectedBytes_ = 0;
    // Children follow their parents, so walking backwards folds each into the one above.
    for (size_t index = nodes_.size(); index-- > 0;)
    {
        FileNode const& node = nodes_[index];
        if (!node.isFolder && !node.fileIndexes.empty() && wanted_[static_cast<size_t>(node.fileIndexes.front())] != 0)
        {
            wantedInside_[index] += 1;
            wantedFiles_ += 1;
            selectedBytes_ += node.size;
        }
        if (node.parent >= 0)
        {
            wantedInside_[static_cast<size_t>(node.parent)] += wantedInside_[index];
        }
    }
}

CheckState AddFileList::checkState(int node) const
{
    int const wanted = wantedInside_[static_cast<size_t>(node)];
    if (wanted == 0)
    {
        return CheckState::Off;
    }
    return static_cast<size_t>(wanted) == nodes_[static_cast<size_t>(node)].fileIndexes.size() ? CheckState::On : CheckState::Mixed;
}

void AddFileList::setWanted(int node, bool wanted)
{
    for (int const file : nodes_[static_cast<size_t>(node)].fileIndexes)
    {
        wanted_[static_cast<size_t>(file)] = wanted ? uint8_t{ 1 } : uint8_t{ 0 };
    }
    rollUp();
}

void AddFileList::setAllWanted(bool wanted)
{
    std::fill(wanted_.begin(), wanted_.end(), wanted ? uint8_t{ 1 } : uint8_t{ 0 });
    rollUp();
}

void AddFileList::rebuildRows()
{
    rows_.clear();
    std::fill(rowOf_.begin(), rowOf_.end(), -1);
    int const count = static_cast<int>(nodes_.size());
    for (int node = 0; node < count;)
    {
        rowOf_[static_cast<size_t>(node)] = static_cast<int>(rows_.size());
        rows_.push_back(node);
        FileNode const& entry = nodes_[static_cast<size_t>(node)];
        node = entry.isFolder && !expanded(node) ? entry.next : node + 1;
    }
}

void AddFileList::setExpanded(int node, bool expanded)
{
    expanded_[static_cast<size_t>(node)] = expanded ? uint8_t{ 1 } : uint8_t{ 0 };
    rebuildRows();
}

bool AddFileList::nameTaken(int node, std::wstring const& name) const
{
    int const parent = nodes_[static_cast<size_t>(node)].parent;
    int const first = parent >= 0 ? parent + 1 : 0;
    int const end = parent >= 0 ? nodes_[static_cast<size_t>(parent)].next : static_cast<int>(nodes_.size());
    for (int sibling = first; sibling < end; sibling = nodes_[static_cast<size_t>(sibling)].next)
    {
        // Windows folds case, so these would be one file on disk.
        if (sibling != node && _wcsicmp(nodes_[static_cast<size_t>(sibling)].name.c_str(), name.c_str()) == 0)
        {
            return true;
        }
    }
    return false;
}

bool AddFileList::rename(int node, std::wstring const& name)
{
    FileNode& entry = nodes_[static_cast<size_t>(node)];
    if (name == entry.name)
    {
        return true;
    }
    if (!engine::isValidFileName(name) || nameTaken(node, name))
    {
        return false;
    }
    auto const [original, inserted] = originalNames_.try_emplace(node, entry.name);
    entry.name = name;
    if (!inserted && original->second == name)
    {
        originalNames_.erase(original);
    }
    return true;
}

std::vector<engine::FileRename> AddFileList::renames() const
{
    std::vector<engine::FileRename> renames;
    renames.reserve(originalNames_.size());
    for (auto const& [node, original] : originalNames_)
    {
        FileNode const& entry = nodes_[static_cast<size_t>(node)];
        renames.push_back({ entry.path, entry.name });
    }
    return renames;
}

AddFileRows::AddFileRows(AddFileList& files)
    : files_(files)
{
    clipsChildren_ = true;
    setRowHeight(kFileRowHeight);
    setRowInsets(kListOverhang, kListOverhang);
    setRowCount(static_cast<int>(files_.rows().size()));
    drawRow = [this](ID2D1DeviceContext* context, int row, D2D1_RECT_F rect, RowState state) { paintRow(context, row, rect, state); };
    onDoubleClick = [this](int row) { beginRename(row); };
    onRightClick = [this](int, D2D1_POINT_2F point) { showMenu(point); };

    editor_ = add<TextField>();
    editor_->setFieldStyle(FieldStyle::Plain);
    editor_->setTextStyle(kFileName);
    editor_->setHidden(true);
    editor_->onSubmit = [this] { finishRename(true); };
    editor_->onCancel = [this] { finishRename(false); };
    editor_->onBlur = [this] { finishRename(true); };
}

void AddFileRows::paintRow(ID2D1DeviceContext* context, int row, D2D1_RECT_F rect, RowState state)
{
    Theme& theme = Theme::get();
    Palette const& colors = theme.colors();
    int const index = files_.rows()[static_cast<size_t>(row)];
    FileNode const& node = files_.nodes()[static_cast<size_t>(index)];
    CheckState const check = files_.checkState(index);
    float const left = rect.left + kListOverhang + static_cast<float>(node.depth) * kIndent;
    float const midY = (rect.top + rect.bottom) / 2.0F;

    if (node.isFolder)
    {
        auto const direction = files_.expanded(index) ? detail::Chevron::Down : detail::Chevron::Right;
        detail::drawChevron(context, D2D1::Point2F(left + kChevronX, rect.top + kNameTop + kNameHeight / 2.0F), kChevronSize, direction, colors.foreground);
    }
    float const iconLeft = left + kIconLeft;
    theme.icon(context, node.isFolder ? L"Folder" : L"File", D2D1::RectF(iconLeft, midY - kIconSize / 2.0F, iconLeft + kIconSize, midY + kIconSize / 2.0F), colors.foreground);

    float const checkLeft = rect.right - kListOverhang - kCheckColumn;
    float const pencilRight = checkLeft - kFilePencilGap;
    float const textLeft = iconLeft + kIconSize + kNameGap;
    float const textRight = pencilRight - kFilePencil - kFilePencilGap;
    if (row != editRow_)
    {
        D2D1_RECT_F const name = D2D1::RectF(textLeft, rect.top + kNameTop, textRight, rect.top + kNameTop + kNameHeight);
        theme.text(context, node.name, kFileName, name, check != CheckState::Off ? colors.bright : colors.foreground, DWRITE_TEXT_ALIGNMENT_LEADING, Trim::Middle);
    }
    TextBuffer size{};
    theme.text(context, fileSizeText(node.size, size), kFileSize, D2D1::RectF(textLeft, rect.top + kFileSizeTop, textRight, rect.top + kFileSizeTop + kFileSizeHeight), colors.foreground);

    if (state.hover && row != editRow_)
    {
        float const iconRight = pencilRight - (kFilePencil - kFilePencilIcon) / 2.0F;
        D2D1_RECT_F const pencil = D2D1::RectF(iconRight - kFilePencilIcon, midY - kFilePencilIcon / 2.0F, iconRight, midY + kFilePencilIcon / 2.0F);
        theme.icon(context, L"Pencil", pencil, colors.foreground);
    }
    float const boxLeft = checkLeft + (kCheckColumn - Checkbox::kSize) / 2.0F;
    Checkbox::paint(context, D2D1::RectF(boxLeft, midY - Checkbox::kSize / 2.0F, boxLeft + Checkbox::kSize, midY + Checkbox::kSize / 2.0F), check, true, state.hover);
}

AddFileRows::Zone AddFileRows::zoneAt(int row, D2D1_POINT_2F point) const
{
    FileNode const& node = files_.rowNode(row);
    float const left = kListOverhang + static_cast<float>(node.depth) * kIndent;
    float const checkLeft = width() - kListOverhang - kCheckColumn;
    if (point.x >= checkLeft)
    {
        return Zone::Check;
    }
    if (point.x >= checkLeft - kFilePencilGap - kFilePencil && point.x < checkLeft - kFilePencilGap)
    {
        return Zone::Pencil;
    }
    if (node.isFolder && std::abs(point.x - (left + kChevronX)) <= kChevronReach)
    {
        return Zone::Chevron;
    }
    return Zone::None;
}

bool AddFileRows::onMouse(MouseEvent const& event)
{
    if (detail::isPress(event.kind))
    {
        pressedRow_ = rowAt(event.point);
        pressedZone_ = pressedRow_ >= 0 ? zoneAt(pressedRow_, event.point) : Zone::None;
        if (pressedZone_ != Zone::None)
        {
            return true;
        }
    }
    else if (event.kind == MouseKind::Up && pressedZone_ != Zone::None)
    {
        Zone const zone = pressedZone_;
        pressedZone_ = Zone::None;
        if (rowAt(event.point) != pressedRow_ || zoneAt(pressedRow_, event.point) != zone)
        {
            return true;
        }
        if (zone == Zone::Chevron)
        {
            setExpanded(pressedRow_, !files_.expanded(files_.rows()[static_cast<size_t>(pressedRow_)]));
        }
        else if (zone == Zone::Check)
        {
            toggleCheck(pressedRow_);
        }
        else
        {
            beginRename(pressedRow_);
        }
        return true;
    }
    return ListView::onMouse(event);
}

bool AddFileRows::onKey(UINT key, bool down)
{
    // Up and Down fall through the editor for result lists, and must not move rows under it.
    if (editRow_ >= 0)
    {
        return true;
    }
    int const row = cursorRow();
    if (down && row >= 0)
    {
        int const node = files_.rows()[static_cast<size_t>(row)];
        switch (key)
        {
        case VK_LEFT:
        case VK_RIGHT:
            if (files_.nodes()[static_cast<size_t>(node)].isFolder)
            {
                setExpanded(row, key == VK_RIGHT);
            }
            return true;
        case VK_SPACE:
            setSelectedWanted(files_.checkState(node) != CheckState::On);
            return true;
        case VK_F2:
            beginRename(row);
            return true;
        default:
            break;
        }
    }
    return ListView::onKey(key, down);
}

void AddFileRows::scrolled()
{
    ListView::scrolled();
    placeEditor();
}

void AddFileRows::toggleCheck(int row)
{
    int const node = files_.rows()[static_cast<size_t>(row)];
    files_.setWanted(node, files_.checkState(node) != CheckState::On);
    wantedChanged();
}

std::vector<int> AddFileRows::selectedNodes() const
{
    std::vector<int> nodes = selectedRows();
    for (int& entry : nodes)
    {
        entry = files_.rows()[static_cast<size_t>(entry)];
    }
    return nodes;
}

void AddFileRows::setExpanded(int row, bool expanded)
{
    // The selection belongs to the files, and every row below this one is about to move.
    std::vector<int> const selected = selectedNodes();
    files_.setExpanded(files_.rows()[static_cast<size_t>(row)], expanded);
    clearSelection();
    setRowCount(static_cast<int>(files_.rows().size()));
    for (int const node : selected)
    {
        setRowSelected(files_.rowOf(node), true);
    }
    reloadData();
}

void AddFileRows::setSelectedWanted(bool wanted)
{
    for (int const node : selectedNodes())
    {
        files_.setWanted(node, wanted);
    }
    wantedChanged();
}

void AddFileRows::wantOnlySelected()
{
    files_.setAllWanted(false);
    setSelectedWanted(true);
}

void AddFileRows::wantedChanged()
{
    reloadData();
    if (onWantedChanged)
    {
        onWantedChanged();
    }
}

void AddFileRows::placeEditor()
{
    if (editRow_ < 0)
    {
        return;
    }
    D2D1_RECT_F const row = rowRect(editRow_);
    float const left = kListOverhang + static_cast<float>(files_.rowNode(editRow_).depth) * kIndent + kIconLeft + kIconSize + kNameGap;
    float const right = width() - kListOverhang - kCheckColumn - kFilePencilGap;
    editor_->setFrame(D2D1::RectF(left, row.top + kNameTop, right, row.top + kNameTop + kNameHeight));
}

void AddFileRows::beginRename(int row)
{
    if (row < 0 || row >= rowCount() || editRow_ >= 0)
    {
        return;
    }
    editRow_ = row;
    FileNode const& node = files_.rowNode(row);
    editor_->setText(node.name);
    editor_->setHidden(false);
    placeEditor();
    Window::get().focus(editor_);

    // As on the Mac, a file keeps its extension unless you reach for it.
    auto const dot = node.isFolder ? std::wstring::npos : node.name.find_last_of(L'.');
    int const stem = static_cast<int>(dot == std::wstring::npos || dot == 0 ? node.name.size() : dot);
    editor_->setSelection(0, stem);
    reloadRow(row);
}

void AddFileRows::finishRename(bool keep)
{
    if (editRow_ < 0)
    {
        return;
    }
    int const row = editRow_;
    editRow_ = -1;

    Window& window = Window::get();
    editor_->setHidden(true);
    if (window.focused() == editor_)
    {
        window.focus(this);
    }
    if (keep)
    {
        std::wstring const name = trimmed(editor_->text());
        if (!name.empty() && !files_.rename(files_.rows()[static_cast<size_t>(row)], name))
        {
            refuse();
        }
    }
    reloadRow(row);
}

void AddFileRows::showMenu(D2D1_POINT_2F point)
{
    std::vector<int> const nodes = selectedNodes();
    if (nodes.empty())
    {
        return;
    }
    bool const anyOff = std::any_of(nodes.begin(), nodes.end(), [this](int node) { return files_.checkState(node) != CheckState::On; });
    bool const anyOn = std::any_of(nodes.begin(), nodes.end(), [this](int node) { return files_.checkState(node) != CheckState::Off; });

    std::vector<MenuItem> menu;
    MenuItem check;
    check.title = L"Check Selected";
    check.enabled = anyOff;
    check.action = [this] { setSelectedWanted(true); };
    menu.push_back(std::move(check));
    MenuItem uncheck;
    uncheck.title = L"Uncheck Selected";
    uncheck.enabled = anyOn;
    uncheck.action = [this] { setSelectedWanted(false); };
    menu.push_back(std::move(uncheck));
    MenuItem only;
    only.title = L"Only Check Selected";
    only.action = [this] { wantOnlySelected(); };
    menu.push_back(std::move(only));
    menu.push_back(MenuItem::line());
    MenuItem rename;
    rename.title = L"Rename File…";
    rename.enabled = nodes.size() == 1;
    rename.action = [this, row = files_.rowOf(nodes.front())] { beginRename(row); };
    menu.push_back(std::move(rename));
    PopupMenu::show(std::move(menu), point);
}

D2D1_POINT_2F AddFileRows::checkPoint(int row) const
{
    auto const origin = absoluteOrigin();
    D2D1_RECT_F const rect = rowRect(row);
    return D2D1::Point2F(origin.x + width() - kListOverhang - kCheckColumn / 2.0F, origin.y + (rect.top + rect.bottom) / 2.0F);
}

D2D1_POINT_2F AddFileRows::chevronPoint(int row) const
{
    auto const origin = absoluteOrigin();
    D2D1_RECT_F const rect = rowRect(row);
    float const left = kListOverhang + static_cast<float>(files_.rowNode(row).depth) * kIndent;
    return D2D1::Point2F(origin.x + left + kChevronX, origin.y + (rect.top + rect.bottom) / 2.0F);
}

D2D1_POINT_2F AddFileRows::pencilPoint(int row) const
{
    auto const origin = absoluteOrigin();
    D2D1_RECT_F const rect = rowRect(row);
    return D2D1::Point2F(origin.x + width() - kListOverhang - kCheckColumn - kFilePencilGap - kFilePencil / 2.0F, origin.y + (rect.top + rect.bottom) / 2.0F);
}

AddPanel::AddPanel(engine::AddPreview preview)
    : Panel(kPanelWidth, panelHeight(isFolderTorrent(preview), !preview.torrentFile.empty()))
    , showFiles_(isFolderTorrent(preview))
    , preview_(std::move(preview))
    , files_(std::move(preview_.files))
    , originalName_(preview_.name.empty() ? preview_.hash : preview_.name)
    , name_(originalName_)
{
    setClosesOnOutsideClick(false);
    build();
}

AddPanel::~AddPanel()
{
    addPanelGone();
}

void AddPanel::build()
{
    auto& engine = App::get().engine();
    engine::SessionSettings const settings = engine.settings();
    engine::AppPrefs const& prefs = engine.prefs();

    auto* title = add<TitleLabel>(name_);
    title->onClick = [this] { beginTitleRename(); };
    parts_.title = title;
    parts_.titleEditor = add<TextField>();
    parts_.titleEditor->setFieldStyle(FieldStyle::Plain);
    parts_.titleEditor->setTextStyle(kPanelTitle);
    parts_.titleEditor->setHidden(true);
    parts_.titleEditor->onSubmit = [this] { finishTitleRename(true); };
    parts_.titleEditor->onCancel = [this] { finishTitleRename(false); };
    parts_.titleEditor->onBlur = [this] { finishTitleRename(true); };

    parts_.pencil = add<IconButton>(L"Pencil", [this] { beginTitleRename(); });
    parts_.pencil->setIconSize(kPencilSize);
    parts_.status = add<Label>(L"", kBody, ColorRole::Foreground);

    if (showFiles_)
    {
        parts_.filesTitle = add<Label>(L"Files", kRowTitle, ColorRole::Bright);
        parts_.all = add<Button>(
            L"All",
            ButtonStyle::Glass,
            [this]
            {
                files_.setAllWanted(true);
                parts_.rows->reloadData();
                updateStatus();
            });
        parts_.none = add<Button>(
            L"None",
            ButtonStyle::Glass,
            [this]
            {
                files_.setAllWanted(false);
                parts_.rows->reloadData();
                updateStatus();
            });
        parts_.rows = add<AddFileRows>(files_);
        parts_.rows->onWantedChanged = [this] { updateStatus(); };
    }

    parts_.saveTitle = add<Label>(L"Save to", kRowTitle, ColorRole::Bright);
    parts_.saveHint = add<Label>(L"", kHint, ColorRole::Foreground);
    parts_.saveHint->setTrim(Trim::Middle);
    parts_.change = add<Button>(L"Change", ButtonStyle::Glass, [this] { changeFolder(); });

    parts_.startTitle = add<Label>(L"Start right away", kRowTitle, ColorRole::Bright);
    parts_.start = add<Switch>();
    parts_.start->setOn(settings.startAddedTransfers);

    if (!preview_.torrentFile.empty())
    {
        parts_.trashTitle = add<Label>(L"Move the .torrent file to the Recycle Bin", kRowTitle, ColorRole::Bright);
        parts_.trash = add<Switch>();
        parts_.trash->setOn(prefs.deleteOriginalTorrent);
    }

    parts_.priority = add<Dropdown>(std::vector<std::wstring>{ L"Normal priority", L"High priority", L"Low priority" });
    parts_.cancel = add<Button>(L"Cancel", ButtonStyle::Glass, [this] { close(); });
    parts_.cancel->setCapsule(true);
    parts_.get = add<Button>(L"Get", ButtonStyle::Accent, [this] { get(); });

    setDestination(settings.downloadFolder);
    updateStatus();
}

void AddPanel::layoutContent()
{
    float const right = width() - kMargin;
    float const inner = right - kMargin;
    float y = kTop;

    float const titleWidth = inner - kPencilSize - kPencilGap;
    place(parts_.title, kMargin, y, titleWidth, kTitleHeight);
    place(parts_.titleEditor, kMargin, y, titleWidth, kTitleHeight);
    // The icon's edge sits on the margin, and the frame around it is only there for the pointer.
    float const pencilLeft = right - kPencilSize - (kPencilFrame - kPencilSize) / 2.0F;
    place(parts_.pencil, pencilLeft, y + (kTitleHeight - kPencilFrame) / 2.0F, kPencilFrame, kPencilFrame);
    y += kTitleHeight + kTitleGap;

    place(parts_.status, kMargin, y, inner, kStatusHeight);
    y += kStatusHeight + kStatusGap;

    rules_.clear();
    if (parts_.rows != nullptr)
    {
        // A short window takes its height out of the list, and the rows below stay reachable.
        float const below = panelHeight(false, parts_.trash != nullptr) - y;
        float const listHeight = std::max(0.0F, height() - y - kFilesHeader - kFilesHeaderGap - kListGap - below);
        place(parts_.filesTitle, kMargin, y, inner / 2.0F, kFilesHeader);
        float const buttonTop = y + (kFilesHeader - kSmallButtonHeight) / 2.0F;
        place(parts_.none, right - kNoneWidth, buttonTop, kNoneWidth, kSmallButtonHeight);
        place(parts_.all, right - kNoneWidth - kSmallButtonGap - kAllWidth, buttonTop, kAllWidth, kSmallButtonHeight);
        y += kFilesHeader + kFilesHeaderGap;
        place(parts_.rows, kMargin - kListOverhang, y, inner + kListOverhang * 2.0F, listHeight);
        y += listHeight + kListGap;
    }

    float const textHeight = kRowTitleHeight + kRowTextGap + kRowHintHeight;
    float const controlWidth = kChangeWidth + kControlGap;
    rules_.push_back(y);
    float const saveTop = y + (kSaveRow - textHeight) / 2.0F;
    place(parts_.saveTitle, kMargin, saveTop, inner - controlWidth, kRowTitleHeight);
    place(parts_.saveHint, kMargin, saveTop + kRowTitleHeight + kRowTextGap, inner - controlWidth, kRowHintHeight);
    place(parts_.change, right - kChangeWidth, y + (kSaveRow - Button::kHeight) / 2.0F, kChangeWidth, Button::kHeight);
    y += kSaveRow;

    auto const toggleRow = [&](Label* label, Switch* toggle)
    {
        rules_.push_back(y);
        place(label, kMargin, y + (kToggleRow - kRowTitleHeight) / 2.0F, inner - Switch::kWidth - kControlGap, kRowTitleHeight);
        place(toggle, right - Switch::kWidth, y + (kToggleRow - Switch::kHeight) / 2.0F, Switch::kWidth, Switch::kHeight);
        y += kToggleRow;
    };
    toggleRow(parts_.startTitle, parts_.start);
    if (parts_.trash != nullptr)
    {
        toggleRow(parts_.trashTitle, parts_.trash);
    }

    y += kButtonsGap;
    place(parts_.get, right - kGetWidth, y, kGetWidth, Button::kHeight);
    place(parts_.cancel, right - kGetWidth - kButtonGap - kCancelWidth, y, kCancelWidth, Button::kHeight);
    place(parts_.priority, kMargin, y, kPriorityWidth, Dropdown::kHeight);
}

void AddPanel::draw(ID2D1DeviceContext* context)
{
    auto* brush = Theme::get().brush(Theme::get().colors().border);
    float const pixel = detail::hairline();
    for (float const y : rules_)
    {
        context->FillRectangle(D2D1::RectF(kMargin, y, width() - kMargin, y + pixel), brush);
    }
}

bool AddPanel::onMouse(MouseEvent const& event)
{
    // Clicking the bare glass ends an edit. Nothing there takes focus by itself.
    if (detail::isPress(event.kind))
    {
        Window::get().focus(nullptr);
    }
    return Panel::onMouse(event);
}

bool AddPanel::onKey(UINT key, bool down)
{
    if (key == VK_RETURN)
    {
        if (down)
        {
            get();
        }
        return true;
    }
    return Panel::onKey(key, down);
}

void AddPanel::beginTitleRename()
{
    if (renamingTitle_)
    {
        return;
    }
    renamingTitle_ = true;
    parts_.title->setHidden(true);
    parts_.titleEditor->setHidden(false);
    parts_.titleEditor->setText(name_);
    Window::get().focus(parts_.titleEditor);
    parts_.titleEditor->selectAll();
}

void AddPanel::finishTitleRename(bool keep)
{
    if (!renamingTitle_)
    {
        return;
    }
    renamingTitle_ = false;

    Window& window = Window::get();
    parts_.titleEditor->setHidden(true);
    parts_.title->setHidden(false);
    if (window.focused() == parts_.titleEditor)
    {
        window.focus(nullptr);
    }
    if (keep)
    {
        std::wstring const name = trimmed(parts_.titleEditor->text());
        name_ = name.empty() ? originalName_ : name;
        parts_.title->setText(name_);
        updateStatus();
    }
}

void AddPanel::updateStatus()
{
    acknowledged_.clear();
    parts_.status->setRole(ColorRole::Foreground);
    if (preview_.isMagnet)
    {
        parts_.status->setText(L"Magnet link. The file list arrives once it connects.");
        return;
    }
    TextBuffer buffer{};
    std::wstring const size(fileSizeText(preview_.totalBytes, buffer));
    if (parts_.rows == nullptr)
    {
        parts_.status->setText(size);
        return;
    }

    parts_.all->setEnabled(!files_.allWanted());
    parts_.none->setEnabled(files_.anyWanted());
    size_t const count = files_.fileCount();
    std::wstring const fileWord = count == 1 ? L"1 file" : std::to_wstring(count) + L" files";
    std::wstring const selected(fileSizeText(files_.selectedBytes(), buffer));
    parts_.status->setText(fileWord + L"  ·  " + size + L"  ·  " + selected + L" selected");
}

void AddPanel::showProblem(std::wstring const& text)
{
    parts_.status->setRole(ColorRole::Danger);
    parts_.status->setText(text);
    refuse();
}

void AddPanel::setDestination(std::wstring folder)
{
    destination_ = std::move(folder);
    parts_.saveHint->setText(destination_);
}

void AddPanel::changeFolder()
{
    auto const chosen = chooseFolder ? chooseFolder() : pickFolder(name_, destination_);
    if (chosen.has_value())
    {
        setDestination(*chosen);
        updateStatus();
    }
    else if (destination_.empty())
    {
        // Nowhere to put it and no answer, which the Mac treats as cancelling the add.
        close();
    }
}

engine::AddRequest AddPanel::request() const
{
    engine::AddRequest request;
    request.destination = destination_;
    request.nameOverride = name_ != originalName_ ? name_ : std::wstring{};
    request.wanted = files_.wanted();
    request.renames = files_.renames();
    request.startPaused = !parts_.start->on();
    return request;
}

std::vector<std::wstring> AddPanel::warnings() const
{
    std::vector<std::wstring> warnings;
    auto const slash = destination_.find_last_of(L"\\/");
    std::wstring const leaf = slash == std::wstring::npos ? destination_ : destination_.substr(slash + 1);
    if (_wcsicmp(leaf.c_str(), name_.c_str()) == 0)
    {
        warnings.emplace_back(L"The folder has the same name as this transfer. Get again to add it anyway.");
    }
    auto& engine = App::get().engine();
    if (parts_.start->on() && !preview_.isMagnet && engine.prefs().warningRemainingSpace)
    {
        int64_t const free = engine.freeDiskSpace(destination_);
        if (free >= 0 && files_.selectedBytes() > static_cast<uint64_t>(free))
        {
            warnings.emplace_back(L"Not enough disk space for what's selected. Get again to add it anyway.");
        }
    }
    return warnings;
}

void AddPanel::get()
{
    if (closing())
    {
        return;
    }
    // Ends any edit, so a name still being typed counts.
    Window::get().focus(nullptr);

    // The Mac asks in an alert. This panel asks once in its status line, and a second Get means yes.
    for (std::wstring const& warning : warnings())
    {
        if (std::find(acknowledged_.begin(), acknowledged_.end(), warning) == acknowledged_.end())
        {
            showProblem(warning);
            acknowledged_.push_back(warning);
            return;
        }
    }

    auto& engine = App::get().engine();
    engine::AddResult const result = engine.add(preview_, request());
    if (result.id == engine::kNoTransfer)
    {
        // A bad name keeps the panel open, with the name ready to fix.
        showProblem(result.error);
        if (name_ != originalName_)
        {
            beginTitleRename();
        }
        return;
    }

    if (int const choice = parts_.priority->selected(); choice != 0)
    {
        engine::TransferOptions options = engine.options(result.id);
        options.priority = choice == 1 ? engine::Priority::High : engine::Priority::Low;
        engine.setOptions(result.id, options);
    }
    if (parts_.trash != nullptr && parts_.trash->on())
    {
        moveToRecycleBin(preview_.torrentFile);
    }
    App::get().refreshNow();
    close();
}

bool addPanelShowing()
{
    View* const panel = Window::get().panel();
    return dynamic_cast<AddPanel*>(panel) != nullptr || dynamic_cast<DuplicatePanel*>(panel) != nullptr;
}

void openAddPanel(engine::AddPreview preview)
{
    App& app = App::get();
    auto& engine = app.engine();
    if (preview.duplicateOf != engine::kNoTransfer)
    {
        Panel::open<DuplicatePanel>(preview.name.empty() ? preview.hash : preview.name);
        return;
    }

    engine::SessionSettings const settings = engine.settings();
    engine::AppPrefs const prefs = engine.prefs();
    if (!shouldAsk(preview, prefs, settings.downloadFolder))
    {
        engine::AddRequest request;
        request.destination = settings.downloadFolder;
        request.startPaused = !settings.startAddedTransfers;
        if (engine.add(preview, request).id != engine::kNoTransfer && prefs.deleteOriginalTorrent && !preview.torrentFile.empty())
        {
            moveToRecycleBin(preview.torrentFile);
        }
        app.refreshNow();
        app.addPanelClosed();
        return;
    }

    bool const noFolder = settings.downloadFolder.empty();
    auto* panel = Panel::open<AddPanel>(std::move(preview));
    if (noFolder)
    {
        // Once the panel is up, as on the Mac, where there's nothing to save to until this answers.
        Window::get().after(
            Panel::kAnimationMs,
            [panel]
            {
                if (Window::get().panel() == panel)
                {
                    panel->changeFolder();
                }
            });
    }
}

} // namespace app
