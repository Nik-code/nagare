#include "app/details_files.h"

#include <algorithm>
#include <unordered_set>

#include "app/app.h"
#include "ui/widgets_internal.h"

namespace app::details
{

namespace
{

using namespace ui;

constexpr float kBarTop = 14.0F;
constexpr float kBarGap = 10.0F;
constexpr float kButtonGap = 6.0F;
constexpr float kListGap = 10.0F;
constexpr float kBottomPadding = 12.0F;

// FileOutlineController.mm and its cells.
constexpr float kRowHeight = 44.0F;
constexpr float kIndent = 14.0F;
constexpr float kColumnWidth = 30.0F;
constexpr float kDisclosureWidth = 18.0F;
constexpr float kChevronSize = 8.0F;
constexpr float kIconSize = 18.0F;
constexpr float kIconGap = 10.0F;
constexpr float kNameTop = 6.0F;
constexpr float kNameHeight = 18.0F;
constexpr float kStatusTop = 24.0F;
constexpr float kStatusHeight = 15.0F;
constexpr float kButtonSize = 24.0F;
constexpr float kPencilRight = 4.0F;
constexpr float kTextRight = 34.0F;
constexpr float kPriorityIcon = 16.0F;
constexpr float kPencilIcon = 15.0F;
constexpr float kQuietPriority = 0.55F;
constexpr float kRenameInsetX = 10.0F; // the field's own padding, so the name doesn't jump
constexpr float kRenameHeight = 22.0F;
constexpr float kRenameTop = 3.0F;

constexpr size_t kMaxLocationChecks = 64;

constexpr wchar_t const* kPriorityMenu[] = { L"High", L"Normal", L"Low" };
constexpr engine::Priority kPriorityValues[] = { engine::Priority::High, engine::Priority::Normal, engine::Priority::Low };

CheckState checkState(engine::Mixed wanted)
{
    switch (wanted)
    {
    case engine::Mixed::Off:
        return CheckState::Off;
    case engine::Mixed::Partly:
        return CheckState::Mixed;
    case engine::Mixed::On:
        break;
    }
    return CheckState::On;
}

bool containsIgnoringMarks(std::wstring_view text, std::wstring_view term)
{
    int const found = FindNLSStringEx(
        LOCALE_NAME_USER_DEFAULT,
        FIND_FROMSTART | LINGUISTIC_IGNORECASE | LINGUISTIC_IGNOREDIACRITIC,
        text.data(),
        static_cast<int>(text.size()),
        term.data(),
        static_cast<int>(term.size()),
        nullptr,
        nullptr,
        nullptr,
        0);
    return found >= 0;
}

/// The last part of a path, without its extension, is what a rename preselects on the Mac.
int stemLength(engine::FileNode const& node)
{
    if (node.isFolder)
    {
        return static_cast<int>(node.name.size());
    }
    size_t const dot = node.name.find_last_of(L'.');
    return static_cast<int>(dot == std::wstring::npos || dot == 0 ? node.name.size() : dot);
}

} // namespace

void FileModel::setTree(engine::FileTree tree)
{
    if (openPaths_.empty())
    {
        for (size_t i = 0; i < expanded_.size() && i < tree_.nodes.size(); ++i)
        {
            if (expanded_[i] != 0)
            {
                openPaths_.push_back(tree_.nodes[i].path);
            }
        }
    }

    tree_ = std::move(tree);
    expanded_.assign(tree_.nodes.size(), 0);
    if (!openPaths_.empty())
    {
        std::unordered_set<std::wstring_view> const open(openPaths_.begin(), openPaths_.end());
        for (size_t i = 0; i < tree_.nodes.size(); ++i)
        {
            expanded_[i] = tree_.nodes[i].isFolder && open.contains(tree_.nodes[i].path) ? 1 : 0;
        }
        openPaths_.clear();
    }
    rebuild();
}

void FileModel::renameExpanded(std::wstring const& from, std::wstring const& to)
{
    openPaths_.clear();
    std::wstring const prefix = from + L"/";
    for (size_t i = 0; i < expanded_.size(); ++i)
    {
        if (expanded_[i] == 0)
        {
            continue;
        }
        std::wstring const& path = tree_.nodes[i].path;
        if (path == from)
        {
            openPaths_.push_back(to);
        }
        else if (path.starts_with(prefix))
        {
            openPaths_.push_back(to + path.substr(from.size()));
        }
        else
        {
            openPaths_.push_back(path);
        }
    }
}

void FileModel::setExpanded(int node, bool expanded)
{
    auto& slot = expanded_[static_cast<size_t>(node)];
    uint8_t const value = expanded ? 1 : 0;
    if (slot != value)
    {
        slot = value;
        rebuild();
    }
}

void FileModel::setSubtreeExpanded(int node, bool expanded)
{
    int const end = tree_.nodes[static_cast<size_t>(node)].next;
    for (int i = node; i < end; ++i)
    {
        expanded_[static_cast<size_t>(i)] = expanded && tree_.nodes[static_cast<size_t>(i)].isFolder ? 1 : 0;
    }
    rebuild();
}

void FileModel::setFilter(std::wstring_view text)
{
    terms_.clear();
    size_t at = 0;
    while (at < text.size())
    {
        size_t const end = std::min(text.find_first_of(L" \t　", at), text.size());
        if (end > at)
        {
            terms_.emplace_back(text.substr(at, end - at));
        }
        at = end + 1;
    }
    rebuild();
}

bool FileModel::matches(engine::FileNode const& node) const
{
    return !node.isFolder && std::all_of(terms_.begin(), terms_.end(), [&node](std::wstring const& term) { return containsIgnoringMarks(node.name, term); });
}

void FileModel::rebuild()
{
    rows_.clear();
    auto const count = static_cast<int>(tree_.nodes.size());
    if (filtering())
    {
        for (int i = 0; i < count; ++i)
        {
            if (matches(tree_.nodes[static_cast<size_t>(i)]))
            {
                rows_.push_back(i);
            }
        }
        return;
    }
    // `next` steps over a closed folder, so a closed tree costs only what shows.
    for (int i = 0; i < count;)
    {
        rows_.push_back(i);
        auto const& node = tree_.nodes[static_cast<size_t>(i)];
        i = node.isFolder && expanded_[static_cast<size_t>(i)] == 0 ? node.next : i + 1;
    }
}

int FileModel::rowOf(int node) const
{
    auto const found = std::lower_bound(rows_.begin(), rows_.end(), node);
    return found != rows_.end() && *found == node ? static_cast<int>(found - rows_.begin()) : -1;
}

std::vector<int> FileModel::fileIndexes(std::span<int const> rows) const
{
    std::vector<int> files;
    for (int const row : rows)
    {
        auto const& indexes = nodeAt(row).fileIndexes;
        files.insert(files.end(), indexes.begin(), indexes.end());
    }
    std::sort(files.begin(), files.end());
    files.erase(std::unique(files.begin(), files.end()), files.end());
    return files;
}

/// The Mac's outline view in one list: disclosure, icon, two lines, priority and check.
class FileList final : public ListView
{
public:
    explicit FileList(FilesTab* tab)
        : tab_(tab)
    {
        clipsChildren_ = true;
        setRowHeight(kRowHeight);
        setRowInsets(0.0F, 0.0F);
        drawRow = [this](ID2D1DeviceContext* context, int row, D2D1_RECT_F rect, RowState state) { paintRow(context, row, rect, state); };
        onDoubleClick = [this](int) { tab_->revealSelected(); };
        onRightClick = [this](int, D2D1_POINT_2F point) { tab_->showMenu(point); };
        onEnter = [this] { tab_->openSelected(); };

        rename_ = add<TextField>();
        rename_->setHidden(true);
        rename_->setTextStyle(kBody);
        rename_->onSubmit = [this] { endRename(true); };
        rename_->onCancel = [this] { endRename(false); };
        rename_->onBlur = [this] { endRename(true); };
    }

    TextField* renameField() const { return rename_; }
    bool showsCheck() const { return showsCheck_; }

    void setShowsCheck(bool shows)
    {
        if (shows != showsCheck_)
        {
            showsCheck_ = shows;
            invalidate();
        }
    }

    D2D1_RECT_F zoneRect(int row, FileZone zone) const
    {
        D2D1_RECT_F const rect = rowRect(row);
        float const midY = (rect.top + rect.bottom) / 2.0F;
        float const half = kButtonSize / 2.0F;
        float const checkRight = rect.right;
        float const priorityRight = showsCheck_ ? checkRight - kColumnWidth : checkRight;
        float const cellRight = priorityRight - kColumnWidth;
        auto const box = [&](float centreX) { return D2D1::RectF(centreX - half, midY - half, centreX + half, midY + half); };
        switch (zone)
        {
        case FileZone::Check:
            return box(checkRight - kColumnWidth / 2.0F);
        case FileZone::Priority:
            return box(priorityRight - kColumnWidth / 2.0F);
        case FileZone::Rename:
            return box(cellRight - kPencilRight - half);
        case FileZone::Disclosure:
        {
            float const left = rowLeft(row);
            return D2D1::RectF(left, rect.top, left + kDisclosureWidth, rect.bottom);
        }
        case FileZone::None:
            break;
        }
        return rect;
    }

    void beginRename(int row)
    {
        if (renaming_ >= 0)
        {
            return;
        }
        FileModel& model = tab_->model();
        renaming_ = model.rows()[static_cast<size_t>(row)];
        auto const& node = model.nodeAt(row);
        rename_->setText(node.name);
        rename_->setSelection(0, stemLength(node));
        placeRename();
        rename_->setHidden(false);
        Window::get().focus(rename_);
        reloadRow(row);
    }

    bool onMouse(MouseEvent const& event) override
    {
        switch (event.kind)
        {
        case MouseKind::Enter:
        case MouseKind::Move:
            setHoverZone(event.point);
            return ListView::onMouse(event);
        case MouseKind::Leave:
            clearHoverZone();
            return ListView::onMouse(event);
        case MouseKind::Down:
        case MouseKind::DoubleClick:
        {
            int const row = rowAt(event.point);
            FileZone const zone = row >= 0 ? zoneAt(row, event.point) : FileZone::None;
            if (zone == FileZone::None)
            {
                return ListView::onMouse(event);
            }
            pressRow_ = row;
            pressZone_ = zone;
            pressControl_ = event.control;
            reloadRow(row);
            return true;
        }
        case MouseKind::Up:
        {
            if (pressZone_ == FileZone::None)
            {
                return ListView::onMouse(event);
            }
            int const row = pressRow_;
            FileZone const zone = pressZone_;
            pressRow_ = -1;
            pressZone_ = FileZone::None;
            reloadRow(row);
            if (rowAt(event.point) == row && zoneAt(row, event.point) == zone)
            {
                activateZone(row, zone);
            }
            return true;
        }
        default:
            return ListView::onMouse(event);
        }
    }

    bool onKey(UINT key, bool down) override
    {
        if (down && cursorRow() >= 0 && (key == VK_LEFT || key == VK_RIGHT))
        {
            int const row = cursorRow();
            auto const& node = tab_->model().nodeAt(row);
            if (node.isFolder && !tab_->model().filtering())
            {
                bool const open = tab_->model().isExpanded(tab_->model().rows()[static_cast<size_t>(row)]);
                if (open != (key == VK_RIGHT))
                {
                    tab_->toggleExpanded(row, false);
                }
                return true;
            }
        }
        return ListView::onKey(key, down);
    }

protected:
    void scrolled() override
    {
        ListView::scrolled();
        placeRename();
    }

private:
    float rowLeft(int row) const
    {
        FileModel const& model = tab_->model();
        return model.filtering() ? 0.0F : static_cast<float>(model.nodeAt(row).depth) * kIndent;
    }

    FileZone zoneAt(int row, D2D1_POINT_2F point) const
    {
        auto const& node = tab_->model().nodeAt(row);
        auto const inside = [&](FileZone zone) { return detail::contains(zoneRect(row, zone), point); };
        if (showsCheck_ && inside(FileZone::Check))
        {
            return FileZone::Check;
        }
        if (inside(FileZone::Priority))
        {
            return FileZone::Priority;
        }
        if (inside(FileZone::Rename))
        {
            return FileZone::Rename;
        }
        if (node.isFolder && !tab_->model().filtering() && inside(FileZone::Disclosure))
        {
            return FileZone::Disclosure;
        }
        return FileZone::None;
    }

    void activateZone(int row, FileZone zone)
    {
        switch (zone)
        {
        case FileZone::Disclosure:
            tab_->toggleExpanded(row, pressControl_);
            break;
        case FileZone::Rename:
            beginRename(row);
            break;
        case FileZone::Priority:
            tab_->cyclePriority(row);
            break;
        case FileZone::Check:
            tab_->toggleWanted(row, pressControl_);
            break;
        case FileZone::None:
            break;
        }
    }

    void setHoverZone(D2D1_POINT_2F point)
    {
        int const row = rowAt(point);
        FileZone const zone = row >= 0 ? zoneAt(row, point) : FileZone::None;
        if (row != hoverRow_ || zone != hoverZone_)
        {
            int const previous = hoverRow_;
            hoverRow_ = row;
            hoverZone_ = zone;
            reloadRow(previous);
            reloadRow(row);
        }
    }

    void clearHoverZone()
    {
        int const previous = hoverRow_;
        hoverRow_ = -1;
        hoverZone_ = FileZone::None;
        reloadRow(previous);
    }

    void placeRename()
    {
        if (renaming_ < 0)
        {
            return;
        }
        int const row = tab_->model().rowOf(renaming_);
        if (row < 0)
        {
            return;
        }
        D2D1_RECT_F const rect = rowRect(row);
        float const left = rowLeft(row) + kDisclosureWidth + kIconSize + kIconGap - kRenameInsetX;
        float const right = zoneRect(row, FileZone::Rename).right;
        rename_->setFrame(D2D1::RectF(left, rect.top + kRenameTop, right, rect.top + kRenameTop + kRenameHeight));
    }

    void endRename(bool commit)
    {
        if (renaming_ < 0)
        {
            return;
        }
        int const node = renaming_;
        renaming_ = -1;
        rename_->setHidden(true);
        if (Window::get().focused() == rename_)
        {
            Window::get().focus(this);
        }
        invalidate();

        auto const& nodes = tab_->model().tree().nodes;
        if (static_cast<size_t>(node) >= nodes.size())
        {
            return;
        }
        std::wstring const name = trimmed(rename_->text());
        auto const& current = nodes[static_cast<size_t>(node)];
        if (commit && !name.empty() && name != current.name)
        {
            tab_->finishRename(current.path, name);
        }
    }

    bool isHot(int row, FileZone zone) const
    {
        return row == hoverRow_ && hoverZone_ == zone;
    }

    void paintRow(ID2D1DeviceContext* context, int row, D2D1_RECT_F rect, RowState state)
    {
        Theme& theme = Theme::get();
        Palette const& colors = theme.colors();
        FileModel const& model = tab_->model();
        auto const& node = model.nodeAt(row);
        int const nodeIndex = model.rows()[static_cast<size_t>(row)];
        float const midY = (rect.top + rect.bottom) / 2.0F;
        float const left = rect.left + rowLeft(row);

        if (node.isFolder && !model.filtering())
        {
            detail::Chevron const direction = model.isExpanded(nodeIndex) ? detail::Chevron::Down : detail::Chevron::Right;
            detail::drawChevron(context, D2D1::Point2F(left + kDisclosureWidth / 2.0F, midY), kChevronSize, direction, isHot(row, FileZone::Disclosure) ? colors.bright : colors.foreground);
        }

        float const iconLeft = left + kDisclosureWidth;
        theme.icon(context, node.isFolder ? L"Folder" : L"File", D2D1::RectF(iconLeft, midY - kIconSize / 2.0F, iconLeft + kIconSize, midY + kIconSize / 2.0F), colors.foreground);

        D2D1_RECT_F const pencil = zoneRect(row, FileZone::Rename);
        float const textLeft = iconLeft + kIconSize + kIconGap;
        float const textRight = pencil.right + kPencilRight - kTextRight;

        if (renaming_ != nodeIndex)
        {
            D2D1_COLOR_F const nameColor = node.wanted != engine::Mixed::Off ? colors.bright : colors.foreground;
            theme.text(context, node.name, kBody, D2D1::RectF(textLeft, rect.top + kNameTop, textRight, rect.top + kNameTop + kNameHeight), nameColor, DWRITE_TEXT_ALIGNMENT_LEADING, Trim::Middle);
        }

        double const progress = node.size > 0 ? static_cast<double>(node.have) / static_cast<double>(node.size) : 1.0;
        TextBuffer sizeBuffer;
        std::wstring_view status = fileSizeText(node.size, sizeBuffer);
        TextBuffer line;
        if (progress > 0.0)
        {
            TextBuffer percentBuffer;
            auto const percent = percentText(progress, 2, percentBuffer);
            int const length = swprintf_s(line.data(), line.size(), L"%.*s of %.*s", static_cast<int>(percent.size()), percent.data(), static_cast<int>(status.size()), status.data());
            status = std::wstring_view(line.data(), length > 0 ? static_cast<size_t>(length) : size_t{ 0 });
        }
        theme.text(context, status, kTiny, D2D1::RectF(textLeft, rect.top + kStatusTop, textRight, rect.top + kStatusTop + kStatusHeight), colors.foreground);

        if (state.hover && renaming_ < 0)
        {
            D2D1_COLOR_F const tint = isHot(row, FileZone::Rename) ? colors.bright : colors.foreground;
            paintButton(context, pencil, L"Pencil", kPencilIcon, tint, row, FileZone::Rename);
        }
        paintPriority(context, row, node);
        if (showsCheck_)
        {
            D2D1_RECT_F const zone = zoneRect(row, FileZone::Check);
            float const boxLeft = (zone.left + zone.right - Checkbox::kSize) / 2.0F;
            float const boxTop = (zone.top + zone.bottom - Checkbox::kSize) / 2.0F;
            bool const pressed = row == pressRow_ && pressZone_ == FileZone::Check;
            Checkbox::paint(context, D2D1::RectF(boxLeft, boxTop, boxLeft + Checkbox::kSize, boxTop + Checkbox::kSize), checkState(node.wanted), node.canChangeWanted, isHot(row, FileZone::Check), pressed);
        }
    }

    void paintPriority(ID2D1DeviceContext* context, int row, engine::FileNode const& node)
    {
        Palette const& colors = Theme::get().colors();
        bool const hot = isHot(row, FileZone::Priority);
        wchar_t const* icon = L"Dots";
        D2D1_COLOR_F color = hot ? colors.bright : detail::mix(colors.foreground, colors.surface, kQuietPriority);
        if (!node.hasMixedPriority && node.priority == engine::Priority::High)
        {
            icon = L"Up";
            color = colors.accent;
        }
        else if (!node.hasMixedPriority && node.priority == engine::Priority::Low)
        {
            icon = L"Down";
            color = hot ? colors.bright : colors.foreground;
        }
        paintButton(context, zoneRect(row, FileZone::Priority), icon, kPriorityIcon, color, row, FileZone::Priority);
    }

    void paintButton(ID2D1DeviceContext* context, D2D1_RECT_F zone, wchar_t const* icon, float iconSize, D2D1_COLOR_F color, int row, FileZone which)
    {
        Theme& theme = Theme::get();
        bool const pressed = row == pressRow_ && pressZone_ == which;
        if (pressed || isHot(row, which))
        {
            detail::fillRounded(context, zone, kButtonSize / 2.0F, withAlpha(theme.colors().bright, pressed ? 0.06F : 0.10F));
        }
        float const left = (zone.left + zone.right - iconSize) / 2.0F;
        float const top = (zone.top + zone.bottom - iconSize) / 2.0F;
        theme.icon(context, icon, D2D1::RectF(left, top, left + iconSize, top + iconSize), color);
    }

    FilesTab* tab_;
    TextField* rename_ = nullptr;
    int renaming_ = -1;
    bool showsCheck_ = true;
    int hoverRow_ = -1;
    FileZone hoverZone_ = FileZone::None;
    int pressRow_ = -1;
    FileZone pressZone_ = FileZone::None;
    bool pressControl_ = false;
};

FilesTab::FilesTab(engine::TransferId id)
    : id_(id)
    , self_(std::make_shared<FilesTab*>(this))
{
    filter_ = add<TextField>();
    filter_->setFieldStyle(FieldStyle::Capsule);
    filter_->setIcon(L"Search");
    filter_->setPlaceholder(L"Filter");
    filter_->onChange = [this](std::wstring const& text) { applyFilter(text); };
    filter_->onCancel = [this]
    {
        if (filter_->text().empty())
        {
            Window::get().focus(nullptr);
            return;
        }
        filter_->setText(L"");
        applyFilter(L"");
    };

    all_ = add<Button>(L"All");
    all_->onClick = [this] { setAllWanted(true); };
    none_ = add<Button>(L"None");
    none_->onClick = [this] { setAllWanted(false); };

    list_ = add<FileList>(this);
}

ListView* FilesTab::list() const
{
    return list_;
}

TextField* FilesTab::renameField() const
{
    return list_->renameField();
}

D2D1_POINT_2F FilesTab::zoneCentre(int row, FileZone zone) const
{
    D2D1_RECT_F const rect = list_->zoneRect(row, zone);
    auto const origin = list_->absoluteOrigin();
    return D2D1::Point2F(origin.x + (rect.left + rect.right) / 2.0F, origin.y + (rect.top + rect.bottom) / 2.0F);
}

void FilesTab::beginRename(int row)
{
    list_->beginRename(row);
}

void FilesTab::layout()
{
    float const right = width() - kMargin;
    float x = right - none_->preferredWidth();
    float const fieldBottom = kBarTop + TextField::kHeight;
    float const buttonTop = kBarTop + (TextField::kHeight - Button::kHeight) / 2.0F;
    none_->setFrame(D2D1::RectF(x, buttonTop, right, buttonTop + Button::kHeight));
    float const allRight = x - kButtonGap;
    x = allRight - all_->preferredWidth();
    all_->setFrame(D2D1::RectF(x, buttonTop, allRight, buttonTop + Button::kHeight));
    filter_->setFrame(D2D1::RectF(kMargin, kBarTop, x - kBarGap, fieldBottom));
    list_->setFrame(D2D1::RectF(kMargin, fieldBottom + kListGap, right, height() - kBottomPadding));
}

void FilesTab::load()
{
    model_.setTree(App::get().engine().files(id_));
    list_->clearSelection();
    list_->setRowCount(static_cast<int>(model_.rows().size()));
    list_->reloadData();
    loaded_ = true;
    stale_ = false;
}

void FilesTab::adoptTree(engine::FileTree tree)
{
    model_.setTree(std::move(tree));
    list_->clearSelection();
    list_->setRowCount(static_cast<int>(model_.rows().size()));
    list_->reloadData();
    loaded_ = true;
    stale_ = false;
}

void FilesTab::metadataChanged()
{
    loaded_ = false;
}

void FilesTab::shown(engine::TransferInfo const& info)
{
    if (!loaded_)
    {
        load();
    }
    stale_ = true;
    refresh(info);
}

void FilesTab::refresh(engine::TransferInfo const& info)
{
    // A single file has nothing to tick or to filter: it's the whole transfer.
    if (info.isFolder != isFolder_)
    {
        isFolder_ = info.isFolder;
        list_->setShowsCheck(isFolder_);
        filter_->setEnabled(isFolder_);
    }

    // Nothing arrived and nothing was clicked, so the whole tree is still right.
    if (stale_ || info.haveBytes != lastHave_)
    {
        stale_ = false;
        lastHave_ = info.haveBytes;
        updateTree();
    }
    updateButtons(info);
}

void FilesTab::updateTree()
{
    auto const count = static_cast<int>(model_.rows().size());
    if (count == 0)
    {
        App::get().engine().updateFiles(id_, model_.tree());
        return;
    }
    float const rowHeight = list_->rowHeight();
    int const first = std::clamp(static_cast<int>(list_->scrollOffset() / rowHeight), 0, count - 1);
    int const last = std::min(count, first + std::max(0, static_cast<int>(list_->height() / rowHeight)) + 2);

    seen_.resize(static_cast<size_t>(last - first));
    for (int row = first; row < last; ++row)
    {
        auto const& node = model_.nodeAt(row);
        seen_[static_cast<size_t>(row - first)] = { node.have, node.wanted, node.priority, node.hasMixedPriority, node.canChangeWanted };
    }

    App::get().engine().updateFiles(id_, model_.tree());

    for (int row = first; row < last; ++row)
    {
        auto const& node = model_.nodeAt(row);
        auto const& before = seen_[static_cast<size_t>(row - first)];
        if (node.have != before.have || node.wanted != before.wanted || node.priority != before.priority || node.hasMixedPriority != before.mixedPriority || node.canChangeWanted != before.canChange)
        {
            list_->reloadRow(row);
        }
    }
}

void FilesTab::updateButtons(engine::TransferInfo const& info)
{
    if (!isFolder_)
    {
        all_->setEnabled(false);
        none_->setEnabled(false);
        return;
    }
    bool anyUnwanted = false;
    auto const& nodes = model_.tree().nodes;
    for (size_t i = 0; i < nodes.size() && !anyUnwanted; i = static_cast<size_t>(nodes[i].next))
    {
        anyUnwanted = nodes[i].wanted != engine::Mixed::On;
    }
    all_->setEnabled(anyUnwanted);
    none_->setEnabled(info.leftBytes > 0);
}

void FilesTab::reshape(std::function<void()> const& change)
{
    std::vector<int> selected;
    for (int const row : list_->selectedRows())
    {
        selected.push_back(model_.rows()[static_cast<size_t>(row)]);
    }

    list_->clearSelection();
    change();
    list_->setRowCount(static_cast<int>(model_.rows().size()));

    for (int const node : selected)
    {
        list_->setRowSelected(model_.rowOf(node), true);
    }
    list_->reloadData();
}

void FilesTab::applyFilter(std::wstring const& text)
{
    reshape([this, &text] { model_.setFilter(text); });
    list_->scrollTo(0.0F);
}

void FilesTab::toggleExpanded(int row, bool wholeSubtree)
{
    int const node = model_.rows()[static_cast<size_t>(row)];
    bool const open = !model_.isExpanded(node);
    reshape(
        [this, node, open, wholeSubtree]
        {
            if (wholeSubtree)
            {
                model_.setSubtreeExpanded(node, open);
            }
            else
            {
                model_.setExpanded(node, open);
            }
        });
}

void FilesTab::changed()
{
    stale_ = true;
    App::get().refreshNow();
}

void FilesTab::toggleWanted(int row, bool everyFile)
{
    auto const& node = model_.nodeAt(row);
    if (!node.canChangeWanted)
    {
        return;
    }
    // A folder that is partly wanted turns fully on, then off.
    bool const wanted = node.wanted != engine::Mixed::On;
    if (everyFile)
    {
        setAllWanted(wanted);
        return;
    }
    App::get().engine().setFilesWanted(id_, node.fileIndexes, wanted);
    changed();
}

void FilesTab::cyclePriority(int row)
{
    auto const& node = model_.nodeAt(row);
    if (!node.canChangeWanted)
    {
        return;
    }
    // Normal, then high, then low, and round again.
    engine::Priority next = engine::Priority::High;
    if (!node.hasMixedPriority)
    {
        next = node.priority == engine::Priority::High ? engine::Priority::Low : node.priority == engine::Priority::Low ? engine::Priority::Normal : engine::Priority::High;
    }
    App::get().engine().setFilesPriority(id_, node.fileIndexes, next);
    changed();
}

void FilesTab::setAllWanted(bool wanted)
{
    std::vector<int> files(model_.tree().fileCount);
    for (size_t i = 0; i < files.size(); ++i)
    {
        files[i] = static_cast<int>(i);
    }
    App::get().engine().setFilesWanted(id_, files, wanted);
    changed();
}

std::vector<int> FilesTab::selectedFiles() const
{
    auto const rows = list_->selectedRows();
    return model_.fileIndexes(rows);
}

void FilesTab::setSelectedWanted(bool wanted)
{
    App::get().engine().setFilesWanted(id_, selectedFiles(), wanted);
    changed();
}

void FilesTab::setOnlySelectedWanted()
{
    auto const chosen = selectedFiles();
    std::vector<int> rest;
    auto next = chosen.begin();
    for (int i = 0; i < static_cast<int>(model_.tree().fileCount); ++i)
    {
        if (next != chosen.end() && *next == i)
        {
            ++next;
        }
        else
        {
            rest.push_back(i);
        }
    }
    auto& engine = App::get().engine();
    engine.setFilesWanted(id_, chosen, true);
    engine.setFilesWanted(id_, rest, false);
    changed();
}

void FilesTab::setSelectedPriority(engine::Priority priority)
{
    App::get().engine().setFilesPriority(id_, selectedFiles(), priority);
    changed();
}

std::wstring FilesTab::locationOf(engine::FileNode const& node, std::wstring const& folder) const
{
    std::wstring relative = node.path;
    std::replace(relative.begin(), relative.end(), L'/', L'\\');
    std::wstring path = folder + L"\\" + relative;
    if (pathExists(path))
    {
        return path;
    }
    if (!node.isFolder)
    {
        path += L".part";
        if (pathExists(path))
        {
            return path;
        }
    }
    return {};
}

std::vector<std::wstring> FilesTab::selectedLocations() const
{
    std::vector<std::wstring> paths;
    std::wstring const folder = App::get().engine().details(id_).currentFolder;
    for (int const row : list_->selectedRows())
    {
        if (paths.size() == kMaxLocationChecks)
        {
            break;
        }
        if (auto path = locationOf(model_.nodeAt(row), folder); !path.empty())
        {
            paths.push_back(std::move(path));
        }
    }
    return paths;
}

void FilesTab::revealSelected()
{
    if (auto const paths = selectedLocations(); !paths.empty())
    {
        revealPaths(paths);
    }
}

void FilesTab::openSelected()
{
    if (auto const paths = selectedLocations(); !paths.empty())
    {
        openPath(paths.front());
    }
}

void FilesTab::showMenu(D2D1_POINT_2F point)
{
    auto const rows = list_->selectedRows();
    if (rows.empty())
    {
        return;
    }

    bool anyWanted = false;
    bool anyUnwanted = false;
    bool canChange = false;
    bool allPriorities[3] = {};
    for (int const row : rows)
    {
        auto const& node = model_.nodeAt(row);
        anyWanted = anyWanted || node.wanted != engine::Mixed::Off;
        anyUnwanted = anyUnwanted || node.wanted != engine::Mixed::On;
        if (node.canChangeWanted)
        {
            canChange = true;
            for (size_t i = 0; i < std::size(kPriorityValues); ++i)
            {
                allPriorities[i] = allPriorities[i] || (!node.hasMixedPriority && node.priority == kPriorityValues[i]);
            }
        }
    }
    bool const exists = !selectedLocations().empty();

    auto const item = [](wchar_t const* title, wchar_t const* icon, bool enabled, std::function<void()> action)
    {
        MenuItem entry;
        entry.title = title;
        entry.icon = icon;
        entry.enabled = enabled;
        entry.action = std::move(action);
        return entry;
    };

    std::vector<MenuItem> menu;
    menu.push_back(item(L"Check Selected", L"Check", anyUnwanted && canChange, [this] { setSelectedWanted(true); }));
    menu.push_back(item(L"Uncheck Selected", L"", anyWanted && canChange, [this] { setSelectedWanted(false); }));
    menu.push_back(item(L"Only Check Selected", L"", canChange, [this] { setOnlySelectedWanted(); }));
    menu.push_back(MenuItem::line());

    MenuItem priority;
    priority.title = L"Priority";
    priority.enabled = canChange;
    for (size_t i = 0; i < std::size(kPriorityValues); ++i)
    {
        MenuItem entry;
        entry.title = kPriorityMenu[i];
        entry.checked = allPriorities[i];
        entry.action = [this, value = kPriorityValues[i]] { setSelectedPriority(value); };
        priority.submenu.push_back(std::move(entry));
    }
    menu.push_back(std::move(priority));
    menu.push_back(MenuItem::line());

    menu.push_back(item(L"Open", L"", exists, [this] { openSelected(); }));
    menu.push_back(item(L"Show in Explorer", L"Folder", exists, [this] { revealSelected(); }));
    menu.push_back(MenuItem::line());

    int const only = rows.size() == 1 ? rows.front() : -1;
    menu.push_back(item(L"Rename File…", L"Pencil", only >= 0, [this, only] { list_->beginRename(only); }));

    PopupMenu::show(std::move(menu), point);
}

void FilesTab::finishRename(std::wstring const& path, std::wstring const& name)
{
    std::wstring const parent = path.find_last_of(L'/') == std::wstring::npos ? std::wstring{} : path.substr(0, path.find_last_of(L'/') + 1);
    std::weak_ptr<FilesTab*> const alive = self_;
    App::get().engine().renamePath(
        id_,
        path,
        name,
        [alive, path, renamed = parent + name](bool ok, std::wstring const&)
        {
            auto const self = alive.lock();
            if (!ok)
            {
                refuse();
            }
            if (self == nullptr)
            {
                return;
            }
            FilesTab* tab = *self;
            if (ok)
            {
                tab->model_.renameExpanded(path, renamed);
            }
            tab->load();
            tab->changed();
        });
}

} // namespace app::details
