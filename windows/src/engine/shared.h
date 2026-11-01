#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "engine/engine.h"

// Plain helpers both engines share. Nothing here touches libtransmission.

namespace engine
{

[[nodiscard]] std::wstring widen(std::string_view utf8);
[[nodiscard]] std::string narrow(std::wstring_view wide);

/// Windows refuses these, and libtransmission would quietly substitute them.
[[nodiscard]] bool isValidFileName(std::wstring const& name);

/// Windows' own limit lifts with this prefix. Forward slashes aren't understood behind it.
[[nodiscard]] std::wstring extendedPath(std::wstring const& path);

[[nodiscard]] bool readFile(std::wstring const& path, std::string& out, size_t maxBytes);
/// Writes beside the target and swaps it in, so a crash can't leave half a file.
bool writeFileAtomic(std::wstring const& path, std::string_view data);
[[nodiscard]] std::wstring downloadsFolder();

/// Seconds on a steady clock.
[[nodiscard]] double monotonicSeconds();

[[nodiscard]] AppPrefs loadPrefs(std::wstring const& path);
void savePrefs(std::wstring const& path, AppPrefs const& prefs);

/// What a file tree is built from.
struct FlatFile
{
    std::string path;
    uint64_t size = 0;
};

/// The Mac's createFileList: a single-file torrent is one node, and a folder torrent shows what is
/// inside its root.
[[nodiscard]] FileTree buildFileTree(std::span<FlatFile const> files, bool isFolder);

/// Rolls file state up the tree, given each file's have, wanted and priority by file index.
struct FileState
{
    uint64_t have = 0;
    uint64_t length = 0;
    bool wanted = true;
    Priority priority = Priority::Normal;
};
void rollUpFileState(FileTree& tree, std::span<FileState const> files);

/// download and upload samples for the swell header, in a fixed ring.
class SwellHistory
{
public:
    /// Enough for one sample a second, the UI's fastest, over all of kHistorySeconds.
    static constexpr double kHistorySeconds = 600.0;
    static constexpr double kMinSpacingSeconds = 0.9;

    void record(double now, double down, double up);
    /// False when everything is at rest.
    bool buckets(double now, SwellData& out) const;

private:
    struct Sample
    {
        double time = 0.0;
        float down = 0.0F;
        float up = 0.0F;
    };

    static constexpr size_t kCapacity = 680;

    std::array<Sample, kCapacity> samples_{};
    size_t head_ = 0;
    size_t count_ = 0;
};

/// What droplet row drawing depends on, apart from piece data.
struct DotInput
{
    TransferState state = TransferState::None;
    float verifyProgress = 0.0F;
    float tide = 0.0F;
    double phase = 0.0;
    int columns = 0;
    int rows = 0;
};

/// Everything in DropletView's updateWithTorrent except fillPieces, which needs piece data.
void fillDots(DotInput const& input, std::span<PieceDot> dots);

/// DropletView's fillPieces. `finished` has one fraction per dot. `previous` may be empty, and
/// `done` receives which dots are complete. Returns whether any dot came out fresh.
bool fillPieceDots(std::span<float const> finished, std::span<uint8_t const> previous, bool fresh, std::span<uint8_t> done, std::span<PieceDot> dots);

/// tideForTorrent.
[[nodiscard]] float tideFor(float stopRatioDone, float ratio);

} // namespace engine
