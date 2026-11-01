#include "engine/qbittorrent_import.h"

#include <shlobj.h>

#include <algorithm>
#include <cstdint>
#include <string_view>

#include "engine/shared.h"

namespace engine
{

namespace
{

constexpr int kMaxDepth = 32;
constexpr size_t kMaxFastresumeBytes = 64 * 1024 * 1024;

struct SavePaths
{
    std::string_view qbt;
    std::string_view libtorrent;
};

/// Walks one bencoded value, returning the offset just past it, or 0 if it's malformed.
/// `paths` collects the top level's two save path strings. The parser is hand-rolled, as on the Mac,
/// because a fastresume file can be far larger than the two strings wanted from it.
size_t skipValue(std::span<char const> bytes, size_t offset, int depth, SavePaths* paths)
{
    auto const size = bytes.size();
    if (offset >= size || depth > kMaxDepth)
    {
        return 0;
    }

    auto const type = bytes[offset];
    if (type == 'i')
    {
        while (++offset < size)
        {
            if (bytes[offset] == 'e')
            {
                return offset + 1;
            }
        }
        return 0;
    }

    if (type == 'l' || type == 'd')
    {
        auto const isDictionary = type == 'd';
        ++offset;
        while (offset < size && bytes[offset] != 'e')
        {
            auto key = std::string_view{};
            if (isDictionary)
            {
                auto const keyStart = offset;
                offset = skipValue(bytes, offset, depth + 1, nullptr);
                if (offset == 0)
                {
                    return 0;
                }
                auto const raw = std::string_view{ bytes.data() + keyStart, offset - keyStart };
                if (auto const colon = raw.find(':'); colon != std::string_view::npos)
                {
                    key = raw.substr(colon + 1);
                }
            }

            auto const valueStart = offset;
            offset = skipValue(bytes, offset, depth + 1, nullptr);
            if (offset == 0)
            {
                return 0;
            }

            if (isDictionary && paths != nullptr && depth == 0 && bytes[valueStart] >= '0' && bytes[valueStart] <= '9')
            {
                auto const raw = std::string_view{ bytes.data() + valueStart, offset - valueStart };
                auto const text = raw.substr(raw.find(':') + 1);
                if (key == "qBt-savePath")
                {
                    paths->qbt = text;
                }
                else if (key == "save_path")
                {
                    paths->libtorrent = text;
                }
            }
        }
        return offset < size ? offset + 1 : 0;
    }

    size_t length = 0;
    while (offset < size && bytes[offset] >= '0' && bytes[offset] <= '9')
    {
        if (length > size)
        {
            return 0;
        }
        length = length * 10 + static_cast<size_t>(bytes[offset++] - '0');
    }
    if (offset >= size || bytes[offset] != ':' || length > size - offset - 1)
    {
        return 0;
    }
    return offset + 1 + length;
}

} // namespace

std::wstring parseFastresume(std::span<char const> data)
{
    auto paths = SavePaths{};
    if (data.empty() || data[0] != 'd' || skipValue(data, 0, 0, &paths) == 0)
    {
        return {};
    }

    auto path = widen(!paths.qbt.empty() ? paths.qbt : paths.libtorrent);
    // qBittorrent writes forward slashes on Windows too.
    std::replace(path.begin(), path.end(), L'/', L'\\');
    return path;
}

std::vector<QBittorrentEntry> readQBittorrentEntries(std::wstring const& backupFolder)
{
    auto entries = std::vector<QBittorrentEntry>{};

    auto found = WIN32_FIND_DATAW{};
    auto const find = FindFirstFileExW(
        (extendedPath(backupFolder) + L"\\*.fastresume").c_str(),
        FindExInfoBasic,
        &found,
        FindExSearchNameMatch,
        nullptr,
        FIND_FIRST_EX_LARGE_FETCH);
    if (find == INVALID_HANDLE_VALUE)
    {
        return entries;
    }

    auto data = std::string{};
    do
    {
        auto const file = std::wstring{ found.cFileName };
        constexpr auto kExtension = std::wstring_view{ L".fastresume" };
        if (!file.ends_with(kExtension))
        {
            continue;
        }

        // A magnet that never got its metadata has no .torrent, and there's nothing to carry over.
        auto const stem = file.substr(0, file.size() - kExtension.size());
        auto const torrentPath = backupFolder + L"\\" + stem + L".torrent";
        if (GetFileAttributesW(extendedPath(torrentPath).c_str()) == INVALID_FILE_ATTRIBUTES)
        {
            continue;
        }

        if (!readFile(backupFolder + L"\\" + file, data, kMaxFastresumeBytes))
        {
            continue;
        }

        auto savePath = parseFastresume(data);
        if (!savePath.empty())
        {
            entries.push_back({ torrentPath, std::move(savePath) });
        }
    } while (FindNextFileW(find, &found));
    FindClose(find);

    return entries;
}

std::vector<std::wstring> qbittorrentBackupFolders()
{
    auto folders = std::vector<std::wstring>{};

    auto const add = [&folders](KNOWNFOLDERID const& id, wchar_t const* tail)
    {
        auto* known = PWSTR{ nullptr };
        if (SUCCEEDED(SHGetKnownFolderPath(id, KF_FLAG_DEFAULT, nullptr, &known)))
        {
            auto folder = std::wstring{ known } + tail;
            if (GetFileAttributesW(extendedPath(folder).c_str()) != INVALID_FILE_ATTRIBUTES)
            {
                folders.push_back(std::move(folder));
            }
        }
        CoTaskMemFree(known);
    };

    add(FOLDERID_LocalAppData, L"\\qBittorrent\\BT_backup");
    // Portable mode keeps everything in a profile folder beside the executable, which is only
    // findable without asking when qBittorrent sits in its default install location.
    add(FOLDERID_ProgramFiles, L"\\qBittorrent\\profile\\qBittorrent\\data\\BT_backup");
    return folders;
}

} // namespace engine
