#pragma once

#include <span>
#include <string>
#include <vector>

namespace engine
{

struct QBittorrentEntry
{
    std::wstring torrentPath;
    std::wstring savePath;
};

/// qBittorrent's save folder from one .fastresume, or nothing if the file is malformed or has none.
[[nodiscard]] std::wstring parseFastresume(std::span<char const> data);

/// Reads what qBittorrent has on file in one BT_backup folder. It only ever reads.
[[nodiscard]] std::vector<QBittorrentEntry> readQBittorrentEntries(std::wstring const& backupFolder);

/// %LOCALAPPDATA%\qBittorrent\BT_backup, and the portable profile beside a default install.
[[nodiscard]] std::vector<std::wstring> qbittorrentBackupFolders();

} // namespace engine
