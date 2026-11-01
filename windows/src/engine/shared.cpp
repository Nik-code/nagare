#include "engine/shared.h"

#include <shlobj.h>
#include <shlwapi.h>

#include <algorithm>
#include <cmath>
#include <map>

#include <rapidjson/document.h>
#include <rapidjson/prettywriter.h>
#include <rapidjson/stringbuffer.h>

namespace engine
{

namespace
{

constexpr double kDefaultSeedGoal = 2.0;

// DropletView's Dot values.
constexpr PieceDot kRestDot{ 0.0F, 3.0F, 0.34F, DotInk::Main };
constexpr PieceDot kScanDot{ 0.0F, 7.0F, 1.0F, DotInk::Scan };
constexpr PieceDot kScannedDot{ 1.0F, 6.0F, 0.95F, DotInk::Main };
constexpr PieceDot kUnscannedDot{ 0.0F, 3.0F, 0.42F, DotInk::Main };
constexpr PieceDot kRingDot{ 0.0F, 6.0F, 1.0F, DotInk::Main };
constexpr PieceDot kOffRingDot{ 0.0F, 2.6F, 0.34F, DotInk::Main };
constexpr PieceDot kDeepDot{ 1.0F, 7.0F, 1.0F, DotInk::Main };
constexpr PieceDot kCrestDot{ 1.0F, 5.0F, 1.0F, DotInk::Crest };
constexpr PieceDot kAboveDot{ 1.0F, 2.4F, 0.5F, DotInk::Main };
constexpr PieceDot kFinishedDot{ 1.0F, 5.0F, 0.55F, DotInk::Main };

void getBool(rapidjson::Value const& root, char const* key, bool& value)
{
    auto const it = root.FindMember(key);
    if (it != root.MemberEnd() && it->value.IsBool())
    {
        value = it->value.GetBool();
    }
}

void getInt(rapidjson::Value const& root, char const* key, int& value)
{
    auto const it = root.FindMember(key);
    if (it != root.MemberEnd() && it->value.IsInt())
    {
        value = it->value.GetInt();
    }
}

void getString(rapidjson::Value const& root, char const* key, std::wstring& value)
{
    auto const it = root.FindMember(key);
    if (it != root.MemberEnd() && it->value.IsString())
    {
        value = widen({ it->value.GetString(), it->value.GetStringLength() });
    }
}

/// One list of keys, so that load and save can't drift apart.
template<typename Prefs, typename Visitor>
void visitPrefs(Prefs& prefs, Visitor&& visit)
{
    visit("ThemeMode", prefs.themeMode);
    visit("QBittorrentImported", prefs.qbittorrentImported);
    visit("AskedToBeDefault", prefs.askedToBeDefault);
    visit("DownloadAsk", prefs.downloadAsk);
    visit("DownloadAskMulti", prefs.downloadAskMulti);
    visit("DownloadAskManual", prefs.downloadAskManual);
    visit("MagnetOpenAsk", prefs.magnetOpenAsk);
    visit("CheckQuit", prefs.checkQuit);
    visit("CheckQuitDownloading", prefs.checkQuitDownloading);
    visit("CheckRemove", prefs.checkRemove);
    visit("CheckRemoveDownloading", prefs.checkRemoveDownloading);
    visit("OpenFinishedOnDoubleClick", prefs.openFinishedOnDoubleClick);
    visit("DeleteOriginalTorrent", prefs.deleteOriginalTorrent);
    visit("WarningRemainingSpace", prefs.warningRemainingSpace);
    visit("RemoveWhenFinishSeeding", prefs.removeWhenFinishSeeding);
    visit("SleepPrevent", prefs.sleepPrevent);
    visit("DisplayNotifications", prefs.displayNotifications);
    visit("PlayDownloadSound", prefs.playDownloadSound);
    visit("PlaySeedingSound", prefs.playSeedingSound);
    visit("BlocklistAutoUpdate", prefs.blocklistAutoUpdate);
    visit("AutoImport", prefs.watchFolderOn);
    visit("AutoImportDirectory", prefs.watchFolder);
    visit("Sort", prefs.sort);
    visit("SortReverse", prefs.sortReverse);
    visit("Filter", prefs.filter);
}

/// Splits on '/', and on '\' which some Windows-made torrents use.
std::vector<std::string_view> pathComponents(std::string_view path)
{
    auto parts = std::vector<std::string_view>{};
    size_t start = 0;
    for (size_t i = 0; i <= path.size(); ++i)
    {
        if (i == path.size() || path[i] == '/' || path[i] == '\\')
        {
            if (i > start)
            {
                parts.push_back(path.substr(start, i - start));
            }
            start = i + 1;
        }
    }
    return parts;
}

/// Sorted with Explorer's natural compare, which is the nearest thing to localizedStandardCompare.
struct LogicalLess
{
    bool operator()(std::wstring const& a, std::wstring const& b) const
    {
        // It ignores case, and two names differing only in case are still two files.
        auto const order = StrCmpLogicalW(a.c_str(), b.c_str());
        return order != 0 ? order < 0 : a < b;
    }
};

struct BuildNode
{
    std::map<std::wstring, BuildNode, LogicalLess> folders;
    std::map<std::wstring, std::pair<int, uint64_t>, LogicalLess> files;
};

void flatten(BuildNode const& node, std::wstring const& path, int depth, int parent, FileTree& tree)
{
    for (auto const& [name, child] : node.folders)
    {
        auto const index = static_cast<int>(tree.nodes.size());
        // A copy, since flattening the children can reallocate the very node it came from.
        auto const childPath = path + L"/" + name;
        auto& folder = tree.nodes.emplace_back();
        folder.name = name;
        folder.path = childPath;
        folder.isFolder = true;
        folder.depth = depth;
        folder.parent = parent;
        flatten(child, childPath, depth + 1, index, tree);
        tree.nodes[static_cast<size_t>(index)].next = static_cast<int>(tree.nodes.size());
    }
    for (auto const& [name, entry] : node.files)
    {
        auto const index = static_cast<int>(tree.nodes.size());
        auto& file = tree.nodes.emplace_back();
        file.name = name;
        file.path = path + L"/" + name;
        file.depth = depth;
        file.parent = parent;
        file.size = entry.second;
        file.fileIndexes.push_back(entry.first);
        file.next = index + 1;
    }
}

} // namespace

bool isValidFileName(std::wstring const& name)
{
    constexpr auto kInvalid = std::wstring_view{ L"<>:\"/\\|?*" };
    constexpr auto kDevices = std::array<std::wstring_view, 22>{ L"CON", L"PRN", L"AUX", L"NUL", L"COM1", L"COM2", L"COM3", L"COM4", L"COM5", L"COM6", L"COM7",
                                                                 L"COM8", L"COM9", L"LPT1", L"LPT2", L"LPT3", L"LPT4", L"LPT5", L"LPT6", L"LPT7", L"LPT8", L"LPT9" };
    if (name.empty() || name == L"." || name == L".." || name.back() == L'.' || name.back() == L' ')
    {
        return false;
    }
    auto const stem = name.substr(0, name.find(L'.'));
    auto const isDevice = std::any_of(kDevices.begin(), kDevices.end(), [&stem](std::wstring_view device) { return _wcsicmp(stem.c_str(), std::wstring{ device }.c_str()) == 0; });
    return !isDevice && std::none_of(name.begin(), name.end(), [kInvalid](wchar_t c) { return c < 0x20 || kInvalid.find(c) != std::wstring_view::npos; });
}

std::wstring widen(std::string_view utf8)
{
    if (utf8.empty())
    {
        return {};
    }
    auto const size = static_cast<int>(utf8.size());
    auto const needed = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), size, nullptr, 0);
    auto out = std::wstring(static_cast<size_t>(needed), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), size, out.data(), needed);
    return out;
}

std::string narrow(std::wstring_view wide)
{
    if (wide.empty())
    {
        return {};
    }
    auto const size = static_cast<int>(wide.size());
    auto const needed = WideCharToMultiByte(CP_UTF8, 0, wide.data(), size, nullptr, 0, nullptr, nullptr);
    auto out = std::string(static_cast<size_t>(needed), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), size, out.data(), needed, nullptr, nullptr);
    return out;
}

std::wstring extendedPath(std::wstring const& path)
{
    auto const kPrefix = std::wstring_view{ L"\\\\?\\" };
    if (path.starts_with(kPrefix) || path.size() < 3)
    {
        return path;
    }

    auto full = std::wstring(32768, L'\0');
    auto const length = GetFullPathNameW(path.c_str(), static_cast<DWORD>(full.size()), full.data(), nullptr);
    if (length == 0 || length >= full.size())
    {
        return path;
    }
    full.resize(length);

    if (full.starts_with(L"\\\\"))
    {
        return L"\\\\?\\UNC\\" + full.substr(2);
    }
    return std::wstring{ kPrefix } + full;
}

bool readFile(std::wstring const& path, std::string& out, size_t maxBytes)
{
    // Share everything, because the file may be open in the program that owns it.
    auto const file = CreateFileW(
        extendedPath(path).c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
        nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        return false;
    }

    auto ok = false;
    auto size = LARGE_INTEGER{};
    if (GetFileSizeEx(file, &size) && static_cast<uint64_t>(size.QuadPart) <= maxBytes)
    {
        out.resize(static_cast<size_t>(size.QuadPart));
        auto read = DWORD{ 0 };
        ok = out.empty() || (ReadFile(file, out.data(), static_cast<DWORD>(out.size()), &read, nullptr) && read == out.size());
    }
    CloseHandle(file);
    return ok;
}

bool writeFileAtomic(std::wstring const& path, std::string_view data)
{
    auto const temporary = extendedPath(path + L".tmp");
    auto const file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        return false;
    }

    auto written = DWORD{ 0 };
    auto const ok = WriteFile(file, data.data(), static_cast<DWORD>(data.size()), &written, nullptr) && written == data.size();
    CloseHandle(file);
    if (!ok)
    {
        DeleteFileW(temporary.c_str());
        return false;
    }
    return MoveFileExW(temporary.c_str(), extendedPath(path).c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
}

std::wstring downloadsFolder()
{
    auto* known = PWSTR{ nullptr };
    auto folder = std::wstring{};
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Downloads, KF_FLAG_DEFAULT, nullptr, &known)))
    {
        folder = known;
    }
    CoTaskMemFree(known);
    if (folder.empty())
    {
        auto buffer = std::array<wchar_t, 32768>{};
        auto const length = GetEnvironmentVariableW(L"USERPROFILE", buffer.data(), static_cast<DWORD>(buffer.size()));
        folder = std::wstring(buffer.data(), length) + L"\\Downloads";
    }
    return folder;
}

std::wstring defaultConfigDir()
{
    auto* known = PWSTR{ nullptr };
    auto folder = std::wstring{};
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &known)))
    {
        folder = known;
    }
    CoTaskMemFree(known);
    return folder + L"\\Nagare";
}

double monotonicSeconds()
{
    static auto const frequency = []
    {
        auto value = LARGE_INTEGER{};
        QueryPerformanceFrequency(&value);
        return static_cast<double>(value.QuadPart);
    }();
    auto counter = LARGE_INTEGER{};
    QueryPerformanceCounter(&counter);
    return static_cast<double>(counter.QuadPart) / frequency;
}

AppPrefs loadPrefs(std::wstring const& path)
{
    auto prefs = AppPrefs{};
    auto text = std::string{};
    constexpr size_t kMaxPrefsBytes = 1024 * 1024;
    if (!readFile(path, text, kMaxPrefsBytes))
    {
        return prefs;
    }

    auto document = rapidjson::Document{};
    document.Parse(text.c_str(), text.size());
    if (document.HasParseError() || !document.IsObject())
    {
        return prefs;
    }

    visitPrefs(
        prefs,
        [&document](char const* key, auto& value)
        {
            if constexpr (std::is_same_v<std::decay_t<decltype(value)>, bool>)
            {
                getBool(document, key, value);
            }
            else if constexpr (std::is_same_v<std::decay_t<decltype(value)>, int>)
            {
                getInt(document, key, value);
            }
            else
            {
                getString(document, key, value);
            }
        });
    return prefs;
}

void savePrefs(std::wstring const& path, AppPrefs const& prefs)
{
    auto buffer = rapidjson::StringBuffer{};
    auto writer = rapidjson::PrettyWriter<rapidjson::StringBuffer>{ buffer };
    writer.StartObject();
    visitPrefs(
        prefs,
        [&writer](char const* key, auto const& value)
        {
            writer.Key(key);
            if constexpr (std::is_same_v<std::decay_t<decltype(value)>, bool>)
            {
                writer.Bool(value);
            }
            else if constexpr (std::is_same_v<std::decay_t<decltype(value)>, int>)
            {
                writer.Int(value);
            }
            else
            {
                auto const utf8 = narrow(value);
                writer.String(utf8.c_str(), static_cast<rapidjson::SizeType>(utf8.size()));
            }
        });
    writer.EndObject();
    writeFileAtomic(path, { buffer.GetString(), buffer.GetSize() });
}

FileTree buildFileTree(std::span<FlatFile const> files, bool isFolder)
{
    auto tree = FileTree{};
    tree.fileCount = files.size();

    if (!isFolder)
    {
        if (!files.empty())
        {
            auto const parts = pathComponents(files[0].path);
            auto& node = tree.nodes.emplace_back();
            node.name = widen(parts.empty() ? std::string_view{ files[0].path } : parts.back());
            node.path = widen(files[0].path);
            node.size = files[0].size;
            node.next = 1;
            node.fileIndexes.push_back(0);
        }
        return tree;
    }

    auto root = BuildNode{};
    auto rootName = std::wstring{};
    for (size_t index = 0; index < files.size(); ++index)
    {
        auto const parts = pathComponents(files[index].path);
        if (parts.empty())
        {
            continue;
        }
        if (rootName.empty())
        {
            rootName = widen(parts[0]);
        }

        auto* node = &root;
        for (size_t part = 1; part + 1 < parts.size(); ++part)
        {
            node = &node->folders[widen(parts[part])];
        }
        // A file with no folder under the root keeps its own name, which the Mac pads the same way.
        auto const leaf = parts.size() > 1 ? parts.back() : parts[0];
        node->files[widen(leaf)] = { static_cast<int>(index), files[index].size };
    }

    flatten(root, rootName, 0, -1, tree);

    // Children come after their parents, so walking backwards folds each into the one above.
    for (auto index = tree.nodes.size(); index-- > 0;)
    {
        auto const& node = tree.nodes[index];
        if (node.parent >= 0)
        {
            auto& parent = tree.nodes[static_cast<size_t>(node.parent)];
            parent.size += node.size;
            parent.fileIndexes.insert(parent.fileIndexes.end(), node.fileIndexes.begin(), node.fileIndexes.end());
        }
    }
    return tree;
}

void rollUpFileState(FileTree& tree, std::span<FileState const> files)
{
    for (auto& node : tree.nodes)
    {
        auto anyWanted = false;
        auto anyUnwanted = false;
        auto have = uint64_t{ 0 };
        auto mixed = false;
        auto first = true;
        auto changeable = false;
        auto priority = Priority::Normal;

        for (auto const index : node.fileIndexes)
        {
            if (index < 0 || static_cast<size_t>(index) >= files.size())
            {
                continue;
            }
            auto const& file = files[static_cast<size_t>(index)];
            have += file.have;
            anyWanted |= file.wanted;
            anyUnwanted |= !file.wanted;
            changeable |= file.have < file.length;
            if (first)
            {
                priority = file.priority;
                first = false;
            }
            else if (file.priority != priority)
            {
                mixed = true;
            }
        }

        node.have = have;
        node.wanted = anyWanted && anyUnwanted ? Mixed::Partly : anyWanted ? Mixed::On : Mixed::Off;
        node.priority = priority;
        node.hasMixedPriority = mixed;
        node.canChangeWanted = changeable;
    }
}

void SwellHistory::record(double now, double down, double up)
{
    // The ring drops the oldest on its own, so a burst of refreshes can only shorten the history.
    if (count_ > 0)
    {
        auto const last = (head_ + kCapacity - 1) % kCapacity;
        if (now - samples_[last].time < kMinSpacingSeconds)
        {
            return;
        }
    }
    samples_[head_] = Sample{ now, static_cast<float>(down), static_cast<float>(up) };
    head_ = (head_ + 1) % kCapacity;
    count_ = std::min(count_ + 1, kCapacity);
}

bool SwellHistory::buckets(double now, SwellData& out) const
{
    auto counts = std::array<int, kSwellPoints>{};
    out.down.fill(0.0F);
    out.up.fill(0.0F);

    for (size_t i = 0; i < count_; ++i)
    {
        auto const& sample = samples_[(head_ + kCapacity - 1 - i) % kCapacity];
        if (now - sample.time > kHistorySeconds)
        {
            break;
        }
        auto const age = std::clamp((now - sample.time) / kHistorySeconds, 0.0, 1.0);
        auto const index = std::min(kSwellPoints - 1, static_cast<size_t>((1.0 - age) * (kSwellPoints - 1) + 0.5));
        out.down[index] += sample.down;
        out.up[index] += sample.up;
        counts[index] += 1;
    }

    out.peak = 0.0F;
    for (size_t i = 0; i < kSwellPoints; ++i)
    {
        if (counts[i] > 0)
        {
            out.down[i] /= static_cast<float>(counts[i]);
            out.up[i] /= static_cast<float>(counts[i]);
        }
        out.peak = std::max({ out.peak, out.down[i], out.up[i] });
    }
    return out.peak > 0.0F;
}

float tideFor(float stopRatioDone, float ratio)
{
    if (stopRatioDone > 0.0F && stopRatioDone < 1.0F)
    {
        return stopRatioDone;
    }
    return static_cast<float>(std::min(std::max(static_cast<double>(ratio), 0.0) / kDefaultSeedGoal, 1.0));
}

void fillDots(DotInput const& input, std::span<PieceDot> dots)
{
    auto const columns = input.columns;
    auto const rows = input.rows;
    if (columns <= 0 || rows <= 0 || dots.size() < static_cast<size_t>(columns) * static_cast<size_t>(rows))
    {
        return;
    }
    auto const at = [&dots, rows](int x, int y) -> PieceDot& { return dots[static_cast<size_t>(x) * rows + y]; };

    std::fill(dots.begin(), dots.end(), kRestDot);

    switch (input.state)
    {
    case TransferState::Verifying:
        {
            auto const sweep = static_cast<int>(static_cast<double>(input.verifyProgress) * columns);
            for (int x = 0; x < columns; ++x)
            {
                for (int y = 0; y < rows; ++y)
                {
                    at(x, y) = x == sweep ? kScanDot : x < sweep ? kScannedDot : kUnscannedDot;
                }
            }
        }
        break;
    case TransferState::Fetching:
        for (int x = 0; x < columns; ++x)
        {
            for (int y = 0; y < rows; ++y)
            {
                auto const distance = std::hypot((x - columns / 2.0 + 0.5) / 2.2, y - (rows - 1) / 2.0);
                auto const ring = std::fabs(distance - 1.6) < 0.5;
                at(x, y) = ring ? kRingDot : kOffRingDot;
            }
        }
        break;
    case TransferState::Seeding:
        {
            auto const level = static_cast<double>(input.tide);
            for (int x = 0; x < columns; ++x)
            {
                auto const surface = (rows - 1) * (1.0 - level * 1.06) + 1.05 * std::sin(x / 3.4 + input.phase) +
                    0.5 * std::sin(x / 1.5 + 1.3 + input.phase * 0.6);
                for (int y = 0; y < rows; ++y)
                {
                    auto const depth = y - surface;
                    at(x, y) = depth > 0.7 ? kDeepDot : depth > -0.5 ? kCrestDot : kAboveDot;
                }
            }
        }
        break;
    case TransferState::Finished:
        std::fill(dots.begin(), dots.end(), kFinishedDot);
        break;
    default:
        break;
    }
}

bool fillPieceDots(std::span<float const> finished, std::span<uint8_t const> previous, bool fresh, std::span<uint8_t> done, std::span<PieceDot> dots)
{
    auto anyFresh = false;
    for (size_t i = 0; i < dots.size(); ++i)
    {
        auto const amount = finished[i];
        // Shares of the file run down each column, so the bar still fills left to right.
        if (amount >= 1.0F)
        {
            auto const isNew = fresh && !previous.empty() && previous[i] == 0;
            anyFresh |= isNew;
            dots[i] = PieceDot{ 1.0F, 7.0F, 1.0F, isNew ? DotInk::Fresh : DotInk::Main };
            done[i] = 1;
        }
        else
        {
            dots[i] = PieceDot{ amount, 3.0F + amount * 3.0F, 0.4F + amount * 0.5F, DotInk::Main };
            done[i] = 0;
        }
    }
    return anyFresh;
}

} // namespace engine
