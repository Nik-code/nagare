#include <algorithm>
#include <cmath>
#include <mutex>
#include <string>
#include <vector>

#include "engine/engine.h"
#include "engine/shared.h"

// The same public API with nothing behind it: no session, network or disk. It replays the transfers
// in docs/nagare/hero.png and light.png, so the UI can be built and screenshotted anywhere.

namespace engine
{

namespace
{

constexpr double kMegabyte = 1e6;
constexpr double kPhaseStep = 0.45;
constexpr double kSwellSeedStep = 5.0;
constexpr double kBytesPerKilobyte = 1000.0;
constexpr double kMinRefreshSeconds = 0.2;
constexpr int kVerifyTicks = 8;
constexpr float kFreshShare = 0.06F;

struct FileSeed
{
    wchar_t const* path;
    double megabytes;
};

struct TransferSeed
{
    wchar_t const* name;
    wchar_t const* hash;
    double totalMegabytes;
    double haveMegabytes;
    double downMegabytes;
    int seeds;
    int peers;
    int64_t etaSeconds;
    bool showEta;
    bool seeding;
};

// Sizes, speeds, swarm counts and times exactly as the pictures show them.
constexpr TransferSeed kHero[] = {
    { L"Sintel", L"08ada5a7a6183aae1e09d831df6748d566095a10", 129.3, 125.9, 3.64, 255, 50, 1, false, false },
    { L"Big Buck Bunny", L"dd8255ecdc7ca55fb0bbf81323d87062db1f6d1c", 276.4, 240.1, 7.73, 347, 88, 4, true, false },
    { L"Tears of Steel", L"209c8226b299b308beaf2b9cd3fb49212dbd13ec", 571.4, 17.6, 0.9667, 55, 8, 600, true, false },
    { L"Cosmos Laundromat", L"c9e15763f722f23e98a29decdfae341b98d53056", 220.9, 103.9, 4.28, 66, 11, 31, true, false },
};

constexpr TransferSeed kLight[] = {
    { L"Sintel", L"08ada5a7a6183aae1e09d831df6748d566095a10", 129.3, 129.3, 0.0, 256, 57, kEtaUnknown, false, true },
    { L"Big Buck Bunny", L"dd8255ecdc7ca55fb0bbf81323d87062db1f6d1c", 276.4, 276.4, 0.0, 356, 93, kEtaUnknown, false, true },
    { L"Tears of Steel", L"209c8226b299b308beaf2b9cd3fb49212dbd13ec", 571.4, 559.5, 1.92, 55, 9, 3, true, false },
    { L"Cosmos Laundromat", L"c9e15763f722f23e98a29decdfae341b98d53056", 220.9, 220.9, 0.0, 64, 12, kEtaUnknown, false, true },
    { L"The WIRED CD - Rip. Sample. Mash. Share", L"a88fda5954e89178c372716a6a78b8180ed4dad3", 56.1, 0.0, 0.0, 44, 13, kEtaUnknown, false, false },
};

constexpr FileSeed kSintelFiles[] = {
    { L"Sintel/Sintel.mp4", 129.2 },
    { L"Sintel/Sintel.en.srt", 0.0014 },
    { L"Sintel/Sintel.de.srt", 0.0013 },
    { L"Sintel/Sintel.fr.srt", 0.0014 },
    { L"Sintel/poster.jpg", 0.0465 },
};

struct SearchSeed
{
    wchar_t const* title;
    double megabytes;
};

constexpr SearchSeed kSearch[] = {
    { L"Sintel", 1810.0 },
    { L"Sintel - Third Open Movie by Blender Foundation", 129.9 },
    { L"Sintel Open Movie By Blender", 690.5 },
    { L"Sintel: Original Film Score", 24.7 },
    { L"Sintel. 2010.1080p", 1270.0 },
    { L"sintel-torrent", 199.4 },
};

/// Small and stable: the same cell of the same transfer always looks the same.
float noise(uint32_t a, uint32_t b)
{
    auto value = a * 2654435761U ^ b * 2246822519U;
    value ^= value >> 15;
    value *= 3266489917U;
    value ^= value >> 13;
    return static_cast<float>(value % 10000U) / 10000.0F;
}

struct DemoTransfer
{
    double baseDown = 0.0;
    uint32_t seed = 0;
    double phase = 0.0;
    int verifyTicksLeft = 0;
    bool paused = false;
    TransferOptions options;
    std::vector<FlatFile> files;
    std::vector<FileState> fileStates;
};

class DemoEngine final : public Engine
{
public:
    explicit DemoEngine(bool lightScene)
        : lightScene_{ lightScene }
    {
    }

    bool start(StartOptions const& options) override
    {
        options_ = options;
        prefs_.themeMode = lightScene_ ? 1 : 2;
        prefs_.qbittorrentImported = true;
        settings_.downloadFolder = L"C:\\Users\\demo\\Downloads";
        settings_.incompleteFolder = settings_.downloadFolder;
        settings_.blocklistUrl = L"https://example.com/blocklist.gz";

        auto id = kNoTransfer;
        auto const seeds = lightScene_ ? std::span<TransferSeed const>{ kLight } : std::span<TransferSeed const>{ kHero };
        for (auto const& seed : seeds)
        {
            addSeed(seed, ++id);
        }
        seedSwell();
        return true;
    }

    bool shutdown(int /*maxSeconds*/) override
    {
        return true;
    }

    void pump() override
    {
        auto work = std::vector<std::function<void()>>{};
        {
            auto const lock = std::unique_lock{ mutex_ };
            work.swap(queue_);
        }
        for (auto const& item : work)
        {
            item();
        }
    }

    bool isConfigContended() const override
    {
        return false;
    }

    std::vector<TransferInfo> const& refresh() override
    {
        auto const now = monotonicSeconds();
        if (!dirty_ && now - lastRefresh_ < kMinRefreshSeconds)
        {
            return rows_;
        }
        dirty_ = false;
        lastRefresh_ = now;

        stats_ = SessionStats{};
        stats_.altSpeedOn = settings_.altSpeedOn;
        stats_.all = static_cast<int>(rows_.size());
        for (size_t i = 0; i < rows_.size(); ++i)
        {
            advance(rows_[i], demo_[i]);
        }
        swell_.record(now, stats_.downBytesPerSecond, stats_.upBytesPerSecond);
        ++tick_;
        return rows_;
    }

    SessionStats sessionStats() const override
    {
        return stats_;
    }

    bool swell(SwellData& out) override
    {
        return swell_.buckets(monotonicSeconds(), out);
    }

    bool pieceDots(TransferId id, size_t count, PieceDot* out, int rows, bool trackFresh) override
    {
        auto const index = indexOf(id);
        if (index < 0 || out == nullptr || rows <= 0 || count < static_cast<size_t>(rows))
        {
            return false;
        }
        auto const& info = rows_[static_cast<size_t>(index)];
        auto const& demo = demo_[static_cast<size_t>(index)];
        auto const dots = std::span{ out, count };

        if (info.state == TransferState::Download || info.state == TransferState::Paused || info.state == TransferState::Problem)
        {
            finished_.resize(std::max(finished_.size(), count));
            previous_.resize(std::max(previous_.size(), count));
            done_.resize(std::max(done_.size(), count));
            auto const seed = demo.seed + static_cast<uint32_t>(count);
            for (size_t i = 0; i < count; ++i)
            {
                auto const cell = static_cast<uint32_t>(i);
                // A dot shows done when its share of the file is, and the rest sit part way.
                auto const reached = noise(seed, cell) < info.percentDone;
                finished_[i] = reached ? 1.0F : noise(seed + 1, cell) * 0.6F;
                previous_[i] = trackFresh && noise(seed + 2 + static_cast<uint32_t>(tick_), cell) < kFreshShare ? uint8_t{ 0 } : uint8_t{ 1 };
            }
            fillPieceDots(
                std::span{ finished_ }.first(count),
                std::span<uint8_t const>{ previous_ }.first(count),
                info.state == TransferState::Download,
                std::span{ done_ }.first(count),
                dots);
        }
        else
        {
            auto input = DotInput{};
            input.state = info.state;
            input.verifyProgress = info.verifyProgress;
            input.tide = info.tide;
            input.phase = demo.phase;
            input.columns = std::max(1, static_cast<int>(count) / rows);
            input.rows = rows;
            fillDots(input, dots);
        }
        return true;
    }

    void availability(TransferId id, int count, int8_t* out) override
    {
        auto const index = indexOf(id);
        if (index < 0 || out == nullptr)
        {
            return;
        }
        auto const& demo = demo_[static_cast<size_t>(index)];
        for (auto i = 0; i < count; ++i)
        {
            auto const have = noise(demo.seed, static_cast<uint32_t>(i)) < rows_[static_cast<size_t>(index)].percentDone;
            out[i] = have ? int8_t{ -1 } : static_cast<int8_t>(noise(demo.seed + 3, static_cast<uint32_t>(i)) * 12.0F);
        }
    }

    TransferDetails details(TransferId id) override
    {
        auto details = TransferDetails{};
        auto const index = indexOf(id);
        if (index < 0)
        {
            return details;
        }
        auto const& info = rows_[static_cast<size_t>(index)];
        details.comment = L"Canned data from the demo engine";
        details.creator = L"WebTorrent";
        details.downloadFolder = settings_.downloadFolder;
        details.currentFolder = settings_.downloadFolder;
        details.dataPath = settings_.downloadFolder + L"\\" + info.name;
        details.torrentFile = L"C:\\Demo\\Torrents\\" + info.hash + L".torrent";
        details.magnetLink = L"magnet:?xt=urn:btih:" + info.hash;
        details.dateCreated = 1'400'000'000;
        details.startDate = info.addedDate;
        details.pieceSize = 256U * 1024U;
        details.pieceCount = static_cast<uint32_t>(info.totalBytes / details.pieceSize) + 1U;
        details.fileCount = demo_[static_cast<size_t>(index)].files.size();
        details.haveVerifiedBytes = info.haveBytes;
        details.desiredAvailableBytes = info.leftBytes;
        details.downloadedEver = info.haveBytes;
        details.secondsDownloading = 240;
        details.peersSendingToUs = info.state == TransferState::Download ? info.peersConnected / 2 : 0;
        details.peersFrom[2] = info.peersConnected / 2;
        details.peersFrom[3] = info.peersConnected / 3;
        details.peersFrom[4] = info.peersConnected - details.peersFrom[2] - details.peersFrom[3];
        details.canReannounce = true;
        return details;
    }

    FileTree files(TransferId id) override
    {
        auto const index = indexOf(id);
        if (index < 0)
        {
            return {};
        }
        auto const& demo = demo_[static_cast<size_t>(index)];
        auto tree = buildFileTree(demo.files, demo.files.size() > 1);
        rollUpFileState(tree, demo.fileStates);
        return tree;
    }

    void updateFiles(TransferId id, FileTree& tree) override
    {
        if (auto const index = indexOf(id); index >= 0)
        {
            rollUpFileState(tree, demo_[static_cast<size_t>(index)].fileStates);
        }
    }

    void peers(TransferId id, std::vector<PeerInfo>& out) override
    {
        out.clear();
        auto const index = indexOf(id);
        if (index < 0)
        {
            return;
        }
        auto const& info = rows_[static_cast<size_t>(index)];
        auto const seed = demo_[static_cast<size_t>(index)].seed;
        auto const clients = std::array<wchar_t const*, 4>{ L"Transmission 4.1.0", L"qBittorrent 5.0.2", L"WebTorrent 2.5.1", L"libtorrent 2.0.10" };
        auto const count = std::min(info.peersConnected, 24);
        out.resize(static_cast<size_t>(count));
        for (auto i = 0; i < count; ++i)
        {
            auto& peer = out[static_cast<size_t>(i)];
            auto const cell = static_cast<uint32_t>(i);
            peer.address = L"203.0.113." + std::to_wstring(10 + i);
            peer.port = 51413 + i;
            peer.client = clients[static_cast<size_t>(i) % clients.size()];
            peer.flags = i % 3 == 0 ? L"TDE" : L"DE";
            peer.progress = noise(seed, cell);
            peer.isUtp = i % 3 == 0;
            peer.isEncrypted = true;
            peer.isSeed = i % 2 == 0;
            peer.isDownloadingFrom = info.downBytesPerSecond > 0.0;
            peer.downBytesPerSecond = info.downBytesPerSecond / count;
            peer.from = 2 + i % 3;
        }
    }

    void trackers(TransferId id, std::vector<TrackerInfo>& out) override
    {
        out.clear();
        auto const index = indexOf(id);
        if (index < 0)
        {
            return;
        }
        auto const& info = rows_[static_cast<size_t>(index)];
        auto const hosts = std::array<wchar_t const*, 2>{ L"tracker.opentrackr.org:1337", L"tracker.torrent.eu.org:451" };
        for (size_t i = 0; i < hosts.size(); ++i)
        {
            auto& tracker = out.emplace_back();
            tracker.id = static_cast<uint32_t>(i + 1);
            tracker.tier = static_cast<int>(i);
            tracker.host = hosts[i];
            tracker.announce = std::wstring{ L"udp://" } + hosts[i] + L"/announce";
            tracker.site = i == 0 ? L"opentrackr" : L"eu";
            tracker.announceState = TrackerState::Waiting;
            tracker.seeders = info.seeds;
            tracker.leechers = info.peers;
            tracker.downloads = info.seeds * 40;
            tracker.hasAnnounced = true;
            tracker.lastAnnounceSucceeded = true;
            tracker.lastAnnounceResult = L"Success";
            tracker.lastAnnouncePeers = info.peers;
            tracker.hasScraped = true;
            tracker.lastScrapeSucceeded = true;
        }
    }

    void webseeds(TransferId id, std::vector<WebseedInfo>& out) override
    {
        out.clear();
        if (indexOf(id) >= 0)
        {
            auto& seed = out.emplace_back();
            seed.url = L"https://webtorrent.io/torrents/";
        }
    }

    void setFilesWanted(TransferId id, std::vector<int> const& fileIndexes, bool wanted) override
    {
        if (auto const index = indexOf(id); index >= 0)
        {
            auto& states = demo_[static_cast<size_t>(index)].fileStates;
            for (auto const file : fileIndexes)
            {
                if (file >= 0 && static_cast<size_t>(file) < states.size())
                {
                    states[static_cast<size_t>(file)].wanted = wanted;
                }
            }
        }
    }

    void setFilesPriority(TransferId id, std::vector<int> const& fileIndexes, Priority priority) override
    {
        if (auto const index = indexOf(id); index >= 0)
        {
            auto& states = demo_[static_cast<size_t>(index)].fileStates;
            for (auto const file : fileIndexes)
            {
                if (file >= 0 && static_cast<size_t>(file) < states.size())
                {
                    states[static_cast<size_t>(file)].priority = priority;
                }
            }
        }
    }

    void renamePath(TransferId id, std::wstring const& path, std::wstring const& newName, Done done) override
    {
        if (auto const index = indexOf(id); index >= 0)
        {
            auto& info = rows_[static_cast<size_t>(index)];
            if (path == info.name)
            {
                info.name = newName;
            }
        }
        later([done = std::move(done)] { done(true, {}); });
    }

    void moveData(TransferId /*id*/, std::wstring const& /*folder*/, Done done) override
    {
        later([done = std::move(done)] { done(true, {}); });
    }

    bool addTracker(TransferId /*id*/, std::wstring const& /*announce*/) override
    {
        return true;
    }

    bool removeTrackers(TransferId /*id*/, std::vector<std::wstring> const& /*announces*/) override
    {
        return true;
    }

    bool editTracker(TransferId /*id*/, std::wstring const& /*oldAnnounce*/, std::wstring const& /*newAnnounce*/) override
    {
        return true;
    }

    void pause(std::vector<TransferId> const& ids) override
    {
        forEach(ids, [](TransferInfo&, DemoTransfer& demo) { demo.paused = true; });
    }

    void resume(std::vector<TransferId> const& ids) override
    {
        forEach(ids, [](TransferInfo&, DemoTransfer& demo) { demo.paused = false; });
    }

    void resumeNow(std::vector<TransferId> const& ids) override
    {
        resume(ids);
    }

    void verify(std::vector<TransferId> const& ids) override
    {
        forEach(ids, [](TransferInfo&, DemoTransfer& demo) { demo.verifyTicksLeft = kVerifyTicks; });
    }

    void reannounce(std::vector<TransferId> const& /*ids*/) override
    {
    }

    void remove(std::vector<TransferId> const& ids, bool /*deleteData*/) override
    {
        for (auto const id : ids)
        {
            if (auto const index = indexOf(id); index >= 0)
            {
                rows_.erase(rows_.begin() + index);
                demo_.erase(demo_.begin() + index);
            }
        }
        dirty_ = true;
    }

    void moveInQueue(std::vector<TransferId> const& ids, QueueMove move) override
    {
        forEach(
            ids,
            [move, last = rows_.size()](TransferInfo& info, DemoTransfer&)
            {
                switch (move)
                {
                case QueueMove::Top:
                    info.queuePosition = 0;
                    break;
                case QueueMove::Up:
                    info.queuePosition -= info.queuePosition > 0 ? 1 : 0;
                    break;
                case QueueMove::Down:
                    info.queuePosition += 1;
                    break;
                case QueueMove::Bottom:
                    info.queuePosition = last;
                    break;
                }
            });
    }

    void setGroup(TransferId id, int group) override
    {
        forEach({ id }, [group](TransferInfo& info, DemoTransfer&) { info.group = group; });
    }

    void setLabels(TransferId id, std::vector<std::wstring> const& labels) override
    {
        forEach({ id }, [&labels](TransferInfo& info, DemoTransfer&) { info.labels = labels; });
    }

    TransferOptions options(TransferId id) override
    {
        auto const index = indexOf(id);
        return index >= 0 ? demo_[static_cast<size_t>(index)].options : TransferOptions{};
    }

    void setOptions(TransferId id, TransferOptions const& options) override
    {
        forEach({ id }, [&options](TransferInfo&, DemoTransfer& demo) { demo.options = options; });
    }

    AddPreview parseTorrentFile(std::wstring const& /*path*/) override
    {
        return cannedPreview();
    }

    AddPreview parseTorrentData(std::string /*data*/) override
    {
        return cannedPreview();
    }

    AddPreview parseMagnet(std::wstring const& uri) override
    {
        auto preview = AddPreview{};
        preview.valid = uri.starts_with(L"magnet:?");
        preview.isMagnet = true;
        preview.name = L"Sintel";
        preview.hash = L"08ada5a7a6183aae1e09d831df6748d566095a10";
        return preview;
    }

    AddResult add(AddPreview const& preview, AddRequest const& request) override
    {
        auto result = AddResult{};
        if (!preview.valid)
        {
            result.error = L"That can't be added.";
            return result;
        }

        auto seed = TransferSeed{};
        auto const name = request.nameOverride.empty() ? preview.name : request.nameOverride;
        seed.name = name.c_str();
        seed.hash = preview.hash.c_str();
        seed.totalMegabytes = static_cast<double>(preview.totalBytes) / kMegabyte;
        seed.downMegabytes = request.startPaused ? 0.0 : 1.2;
        seed.seeds = 40;
        seed.peers = 9;
        seed.etaSeconds = kEtaUnknown;

        result.id = rows_.empty() ? 1 : rows_.back().id + 1;
        addSeed(seed, result.id);
        demo_.back().paused = request.startPaused;
        dirty_ = true;
        return result;
    }

    SessionSettings settings() override
    {
        return settings_;
    }

    void applySettings(SessionSettings const& settings) override
    {
        settings_ = settings;
    }

    AppPrefs const& prefs() const override
    {
        return prefs_;
    }

    void setPrefs(AppPrefs const& prefs) override
    {
        prefs_ = prefs;
    }

    void setAltSpeed(bool on) override
    {
        settings_.altSpeedOn = on;
        dirty_ = true;
    }

    void updateBlocklist(Done done) override
    {
        later([done = std::move(done)] { done(true, {}); });
    }

    size_t blocklistRuleCount() override
    {
        return 0;
    }

    void testPort(Done done) override
    {
        later([done = std::move(done)] { done(true, {}); });
    }

    int64_t freeDiskSpace(std::wstring const& /*path*/) override
    {
        constexpr int64_t kDemoFreeBytes = 120'000'000'000;
        return kDemoFreeBytes;
    }

    size_t importFromQBittorrent(std::wstring const& /*backupFolder*/) override
    {
        return 0;
    }

    std::vector<std::wstring> searchSourceNames() override
    {
        return { L"Internet Archive" };
    }

    std::wstring searchSourcesFolder() override
    {
        return L"C:\\Demo\\Search Sources";
    }

    void search(std::wstring const& /*query*/, int /*sourceIndex*/, std::function<void(SearchOutcome const&)> done) override
    {
        auto outcome = SearchOutcome{};
        outcome.asked = 1;
        outcome.responded = 1;
        for (auto const& seed : kSearch)
        {
            auto& result = outcome.results.emplace_back();
            result.title = seed.title;
            result.link = L"https://archive.org/download/sintel/sintel_archive.torrent";
            result.sourceName = L"Internet Archive";
            result.size = static_cast<uint64_t>(seed.megabytes * kMegabyte);
        }
        later([done = std::move(done), outcome = std::move(outcome)] { done(outcome); });
    }

    void cancelSearch() override
    {
    }

    void fetchTorrent(std::wstring const& /*url*/, std::function<void(std::string, std::wstring const&)> done) override
    {
        later([done = std::move(done)] { done("demo", {}); });
    }

private:
    [[nodiscard]] int indexOf(TransferId id) const
    {
        auto const it = std::find_if(rows_.begin(), rows_.end(), [id](TransferInfo const& info) { return info.id == id; });
        return it != rows_.end() ? static_cast<int>(it - rows_.begin()) : -1;
    }

    template<typename Action>
    void forEach(std::vector<TransferId> const& ids, Action&& action)
    {
        for (auto const id : ids)
        {
            if (auto const index = indexOf(id); index >= 0)
            {
                action(rows_[static_cast<size_t>(index)], demo_[static_cast<size_t>(index)]);
            }
        }
        dirty_ = true;
    }

    /// Callbacks arrive through pump(), as they do from the real engine.
    void later(std::function<void()> work)
    {
        {
            auto const lock = std::unique_lock{ mutex_ };
            queue_.push_back(std::move(work));
        }
        if (options_.wake)
        {
            options_.wake();
        }
        else if (options_.notifyWindow != nullptr)
        {
            PostMessageW(options_.notifyWindow, options_.notifyMessage, 0, 0);
        }
    }

    void addSeed(TransferSeed const& seed, TransferId id)
    {
        auto info = TransferInfo{};
        auto demo = DemoTransfer{};
        info.id = id;
        info.name = seed.name;
        info.hash = seed.hash;
        info.totalBytes = static_cast<uint64_t>(seed.totalMegabytes * kMegabyte);
        info.haveBytes = static_cast<uint64_t>(seed.haveMegabytes * kMegabyte);
        info.selectedBytes = info.totalBytes;
        info.leftBytes = info.totalBytes - info.haveBytes;
        auto const done = info.totalBytes > 0 ? static_cast<float>(seed.haveMegabytes / seed.totalMegabytes) : 0.0F;
        info.percentDone = done;
        info.percentComplete = done;
        info.metadataProgress = 1.0F;
        info.seeds = seed.seeds;
        info.peers = seed.peers;
        info.peersConnected = seed.peers;
        info.ratioStop = settings_.ratioLimit;
        info.etaSeconds = seed.etaSeconds;
        info.showEta = seed.showEta;
        info.addedDate = 1'790'000'000 - static_cast<int64_t>(id) * 3600;
        info.activityDate = info.addedDate;
        info.queuePosition = static_cast<size_t>(id) - 1;
        info.isFolder = true;

        demo.baseDown = seed.downMegabytes * kMegabyte;
        demo.seed = 0x9E3779B9U * static_cast<uint32_t>(id);
        demo.paused = false;
        info.state = seed.seeding ? TransferState::Seeding : TransferState::Download;

        // A file list per transfer, in proportion to Sintel's, is enough to fill the Files screen.
        auto const rootName = narrow(seed.name);
        auto const scale = seed.totalMegabytes / 129.2513;
        for (auto const& file : kSintelFiles)
        {
            auto const tail = narrow(std::wstring_view{ file.path }.substr(std::wstring_view{ L"Sintel/" }.size()));
            auto const bytes = static_cast<uint64_t>(file.megabytes * scale * kMegabyte);
            demo.files.push_back({ rootName + "/" + tail, bytes });
            demo.fileStates.push_back({ static_cast<uint64_t>(static_cast<double>(bytes) * done), bytes, true, Priority::Normal });
        }

        rows_.push_back(std::move(info));
        demo_.push_back(std::move(demo));
    }

    void advance(TransferInfo& info, DemoTransfer& demo)
    {
        // The first frame is the picture to the digit, and after that the numbers breathe.
        auto const wobble = tick_ == 0 ? 1.0 : 1.0 + 0.06 * std::sin(tick_ * 0.7 + demo.seed) + 0.03 * std::sin(tick_ * 1.9 + demo.seed * 2.0);
        auto const isComplete = info.leftBytes == 0;

        if (demo.verifyTicksLeft > 0)
        {
            demo.verifyTicksLeft -= 1;
            info.state = TransferState::Verifying;
            info.verifyProgress = 1.0F - static_cast<float>(demo.verifyTicksLeft) / kVerifyTicks;
        }
        else if (demo.paused)
        {
            info.state = isComplete ? TransferState::Finished : TransferState::Paused;
            info.verifyProgress = 0.0F;
        }
        else
        {
            info.state = isComplete ? TransferState::Seeding : TransferState::Download;
            info.verifyProgress = 0.0F;
        }

        auto const running = info.state == TransferState::Download || info.state == TransferState::Seeding;
        info.downBytesPerSecond = running ? demo.baseDown * wobble : 0.0;
        info.upBytesPerSecond = 0.0;
        info.tide = tideFor(0.0F, info.ratio);

        if (running)
        {
            stats_.active += demo.baseDown > 0.0 ? 1 : 0;
            (info.state == TransferState::Seeding ? stats_.seeding : stats_.downloading) += 1;
            stats_.anyWorking = true;
        }
        else if (info.state == TransferState::Verifying)
        {
            stats_.downloading += 1;
        }
        else
        {
            stats_.paused += 1;
        }
        stats_.downBytesPerSecond += info.downBytesPerSecond;
    }

    void seedSwell()
    {
        // Ten minutes of history already on screen, in the shapes the two pictures have.
        auto total = 0.0;
        for (auto const& demo : demo_)
        {
            total += demo.baseDown;
        }

        auto const now = monotonicSeconds();
        for (auto age = SwellHistory::kHistorySeconds; age > 0.0; age -= kSwellSeedStep)
        {
            auto const share = age / SwellHistory::kHistorySeconds;
            auto const swell = 0.05 + 0.03 * std::sin(share * 9.0) + 0.02 * std::sin(share * 23.0 + 1.0);
            auto const crest = lightScene_ ? 0.7 * std::exp(-std::pow((share - 0.09) / 0.07, 2.0)) : 0.55 * std::exp(-share * 40.0);
            swell_.record(now - age, total * (swell + crest), 0.0);
        }
    }

    AddPreview cannedPreview()
    {
        auto preview = AddPreview{};
        preview.valid = true;
        preview.name = L"Sintel";
        preview.hash = L"08ada5a7a6183aae1e09d831df6748d566095a10";
        preview.totalBytes = static_cast<uint64_t>(129.3 * kMegabyte);
        preview.pieceSize = 128U * 1024U;
        preview.pieceCount = 987U;
        preview.comment = L"Canned data from the demo engine";
        preview.creator = L"WebTorrent";

        auto flat = std::vector<FlatFile>{};
        auto states = std::vector<FileState>{};
        for (auto const& file : kSintelFiles)
        {
            auto const bytes = static_cast<uint64_t>(file.megabytes * kMegabyte);
            flat.push_back({ narrow(file.path), bytes });
            states.push_back({ 0, bytes, true, Priority::Normal });
        }
        preview.files = buildFileTree(flat, true);
        rollUpFileState(preview.files, states);

        for (auto const& row : rows_)
        {
            if (row.hash == preview.hash)
            {
                preview.duplicateOf = row.id;
            }
        }
        return preview;
    }

    bool lightScene_ = false;
    StartOptions options_;
    std::mutex mutex_;
    std::vector<std::function<void()>> queue_;

    std::vector<TransferInfo> rows_;
    std::vector<DemoTransfer> demo_;
    SessionStats stats_;
    SwellHistory swell_;
    SessionSettings settings_;
    AppPrefs prefs_;

    std::vector<float> finished_;
    std::vector<uint8_t> previous_;
    std::vector<uint8_t> done_;

    double lastRefresh_ = -1.0;
    bool dirty_ = true;
    int tick_ = 0;
};

} // namespace

std::unique_ptr<Engine> createDemoEngine(bool lightScene)
{
    return std::make_unique<DemoEngine>(lightScene);
}

} // namespace engine
