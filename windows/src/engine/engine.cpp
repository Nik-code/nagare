#include "engine/engine.h"

#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <list>
#include <mutex>
#include <optional>
#include <set>
#include <thread>
#include <tuple>
#include <unordered_map>

#include <libtransmission/transmission.h>

#include <libtransmission/error.h>
#include <libtransmission/file.h>
#include <libtransmission/log.h>
#include <libtransmission/rpcimpl.h>
#include <libtransmission/torrent-metainfo.h>
#include <libtransmission/utils.h>
#include <libtransmission/variant.h>

#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include "engine/qbittorrent_import.h"
#include "engine/search_source.h"
#include "engine/shared.h"

namespace engine
{

namespace
{

constexpr double kMinRefreshSeconds = 0.2;
constexpr double kBytesPerKilobyte = 1000.0;
constexpr int kETAIdleDisplaySeconds = 2 * 60;
constexpr double kPreviousPiecesLifetime = 2.0;
constexpr double kDotTrackerLifetime = 30.0;
constexpr double kPhaseStep = 0.45;
constexpr double kWatchFolderSeconds = 10.0;
constexpr double kHistorySaveSeconds = 30.0;
constexpr double kLogDrainSeconds = 60.0;
/// Tracker scrapes come minutes apart, so reading their numbers every second would be wasted work.
constexpr int kSwarmRefreshTicks = 10;
constexpr size_t kCacheMib = 4;
constexpr size_t kMaxTorrentBytes = 64 * 1024 * 1024;
constexpr size_t kMaxHistoryBytes = 64 * 1024 * 1024;
constexpr int kFirstEphemeralPort = 49152;
constexpr int kEphemeralPortCount = 65536 - kFirstEphemeralPort;

wchar_t const* const kSettingsFile = L"\\settings.json";
wchar_t const* const kPrefsFile = L"\\prefs.json";
wchar_t const* const kHistoryFile = L"\\transfers.json";

/// What the Mac keeps per transfer in Transfers.plist, because libtransmission has nowhere for it.
struct TransferMeta
{
    int group = -1;
    bool removeWhenDone = false;
};

/// Private bookkeeping for one row. Sits beside it, at the same index.
struct Slot
{
    std::string name;
    std::string error;
    int64_t editDate = -1;
    double phase = 0.0;
    int swarmTick = 0;
    bool stale = true;
};

/// One view's memory of one transfer, which is what makes pieceDots cheap.
struct DotTracker
{
    TransferId id = kNoTransfer;
    size_t count = 0;
    int rows = 0;
    bool trackFresh = false;

    std::vector<PieceDot> last;
    std::vector<uint8_t> previous;
    std::vector<uint8_t> done;
    bool hasPrevious = false;
    double previousTime = 0.0;
    double lastUsed = 0.0;

    bool primed = false;
    uint64_t have = 0;
    TransferState state = TransferState::None;
    double phase = 0.0;
    float tide = 0.0F;
    float verifyProgress = 0.0F;
    bool anyFresh = false;
};

struct MoveJob
{
    TransferId id = kNoTransfer;
    Done done;
    /// libtransmission writes this from its thread, and refresh() watches it.
    int volatile state = TR_LOC_MOVING;
};

/// Renames an Add left to do, which have to finish before the transfer starts.
struct PendingAdd
{
    std::vector<std::pair<std::string, std::string>> renames;
    bool startWhenDone = false;
};

auto dotTrackerKey(TransferId id, size_t count, int rows, bool trackFresh)
{
    return std::make_tuple(id, count, rows, trackFresh);
}

std::wstring trimmed(std::wstring const& text)
{
    constexpr auto kSpace = std::wstring_view{ L" \t\r\n" };
    auto const first = text.find_first_not_of(kSpace);
    if (first == std::wstring::npos)
    {
        return {};
    }
    return text.substr(first, text.find_last_not_of(kSpace) - first + 1);
}

std::wstring withBackslashes(std::string_view utf8)
{
    auto path = widen(utf8);
    std::replace(path.begin(), path.end(), L'/', L'\\');
    return path;
}

bool pathExists(std::wstring const& path)
{
    return GetFileAttributesW(extendedPath(path).c_str()) != INVALID_FILE_ATTRIBUTES;
}

void makeFolder(std::wstring const& path)
{
    SHCreateDirectoryExW(nullptr, path.c_str(), nullptr);
}

/// The Mac's trash path, on libtransmission's thread: Recycle Bin first, so that a mistaken delete
/// can be undone, and a plain delete where there is no bin.
bool recycle(std::wstring const& path)
{
    auto const initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    auto ok = false;

    auto* operation = static_cast<IFileOperation*>(nullptr);
    if (SUCCEEDED(CoCreateInstance(CLSID_FileOperation, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&operation))))
    {
        auto* item = static_cast<IShellItem*>(nullptr);
        if (SUCCEEDED(operation->SetOperationFlags(FOF_NO_UI | FOFX_RECYCLEONDELETE)) &&
            SUCCEEDED(SHCreateItemFromParsingName(path.c_str(), nullptr, IID_PPV_ARGS(&item))))
        {
            auto aborted = BOOL{ FALSE };
            ok = SUCCEEDED(operation->DeleteItem(item, nullptr)) && SUCCEEDED(operation->PerformOperations()) &&
                SUCCEEDED(operation->GetAnyOperationsAborted(&aborted)) && !aborted;
            item->Release();
        }
        operation->Release();
    }

    if (SUCCEEDED(initialized))
    {
        CoUninitialize();
    }
    return ok && !pathExists(path);
}

bool trashDataFile(char const* filename, void* /*user_data*/, tr_error* error)
{
    if (filename == nullptr)
    {
        return false;
    }
    if (recycle(withBackslashes(filename)))
    {
        return true;
    }

    // The shell can't reach paths past its old limit, and a folder that fails here comes back
    // file by file, deepest first, so plain deletion always gets there.
    auto localError = tr_error{};
    if (tr_sys_path_remove(filename, &localError))
    {
        return true;
    }
    if (error != nullptr)
    {
        error->set(localError.code(), std::string{ localError.message() });
    }
    return false;
}

std::string escapedJson(std::string_view text)
{
    auto buffer = rapidjson::StringBuffer{};
    auto writer = rapidjson::Writer<rapidjson::StringBuffer>{ buffer };
    writer.String(text.data(), static_cast<rapidjson::SizeType>(text.size()));
    return { buffer.GetString(), buffer.GetSize() };
}

class TransmissionEngine final : public Engine
{
public:
    TransmissionEngine()
        : search_{ [this](std::function<void()> work) { post(std::move(work)); } }
    {
    }

    ~TransmissionEngine() override
    {
        shutdown();
        if (closeTimedOut_)
        {
            // libtransmission's thread still holds pointers into this object and is still running,
            // so nothing here can be freed, and static destructors mustn't run either.
            TerminateProcess(GetCurrentProcess(), 0);
        }
    }

    bool start(StartOptions const& options) override;
    bool shutdown(int maxSeconds = 2) override;
    void pump() override;
    bool isConfigContended() const override;

    std::vector<TransferInfo> const& refresh() override;
    SessionStats sessionStats() const override;
    bool swell(SwellData& out) override;
    bool pieceDots(TransferId id, size_t count, PieceDot* out, int rows, bool trackFresh) override;
    void availability(TransferId id, int count, int8_t* out) override;

    TransferDetails details(TransferId id) override;
    FileTree files(TransferId id) override;
    void updateFiles(TransferId id, FileTree& tree) override;
    void peers(TransferId id, std::vector<PeerInfo>& out) override;
    void trackers(TransferId id, std::vector<TrackerInfo>& out) override;
    void webseeds(TransferId id, std::vector<WebseedInfo>& out) override;

    void setFilesWanted(TransferId id, std::vector<int> const& fileIndexes, bool wanted) override;
    void setFilesPriority(TransferId id, std::vector<int> const& fileIndexes, Priority priority) override;
    void renamePath(TransferId id, std::wstring const& path, std::wstring const& newName, Done done) override;
    void moveData(TransferId id, std::wstring const& folder, Done done) override;

    bool addTracker(TransferId id, std::wstring const& announce) override;
    bool removeTrackers(TransferId id, std::vector<std::wstring> const& announces) override;
    bool editTracker(TransferId id, std::wstring const& oldAnnounce, std::wstring const& newAnnounce) override;

    void pause(std::vector<TransferId> const& ids) override;
    void resume(std::vector<TransferId> const& ids) override;
    void resumeNow(std::vector<TransferId> const& ids) override;
    void verify(std::vector<TransferId> const& ids) override;
    void reannounce(std::vector<TransferId> const& ids) override;
    void remove(std::vector<TransferId> const& ids, bool deleteData) override;
    void moveInQueue(std::vector<TransferId> const& ids, QueueMove move) override;
    void setGroup(TransferId id, int group) override;
    void setLabels(TransferId id, std::vector<std::wstring> const& labels) override;

    TransferOptions options(TransferId id) override;
    void setOptions(TransferId id, TransferOptions const& options) override;

    AddPreview parseTorrentFile(std::wstring const& path) override;
    AddPreview parseTorrentData(std::string data) override;
    AddPreview parseMagnet(std::wstring const& uri) override;
    AddResult add(AddPreview const& preview, AddRequest const& request) override;

    SessionSettings settings() override;
    void applySettings(SessionSettings const& settings) override;
    AppPrefs const& prefs() const override;
    void setPrefs(AppPrefs const& prefs) override;
    void setAltSpeed(bool on) override;
    void updateBlocklist(Done done) override;
    size_t blocklistRuleCount() override;
    void testPort(Done done) override;
    int64_t freeDiskSpace(std::wstring const& path) override;

    size_t importFromQBittorrent(std::wstring const& backupFolder) override;

    std::vector<std::wstring> searchSourceNames() override;
    std::wstring searchSourcesFolder() override;
    void search(std::wstring const& query, int sourceIndex, std::function<void(SearchOutcome const&)> done) override;
    void cancelSearch() override;
    void fetchTorrent(std::wstring const& url, std::function<void(std::string, std::wstring const&)> done) override;

private:
    // Any thread.
    void post(std::function<void()> work) noexcept;
    void signal() noexcept;
    void postEvent(Event event) noexcept;
    void postIdEvent(EventType type, tr_torrent* tor, bool flag = false) noexcept;

    // libtransmission's thread: these may only queue work.
    static void onCompleteness(tr_torrent* tor, tr_completeness status, bool wasRunning, void* self);
    static void onMetadata(tr_session* session, tr_torrent* tor, void* self);
    static void onRatioLimit(tr_session* session, tr_torrent* tor, void* self);
    static void onIdleLimit(tr_session* session, tr_torrent* tor, void* self);
    static void onQueueStart(tr_session* session, tr_torrent* tor, void* self);
    static void onAltSpeed(tr_session* session, bool active, bool byUser, void* self);

    // UI thread. Everything below that takes a tr_torrent needs the session lock held.
    [[nodiscard]] tr_torrent* find(TransferId id) const;
    [[nodiscard]] TransferInfo* row(TransferId id);
    template<typename Action>
    void forEachTransfer(std::vector<TransferId> const& ids, Action&& action);
    void updateRow(tr_torrent* tor, tr_stat const& stat, TransferInfo& info, Slot& slot);
    void reconcileRows(std::span<tr_torrent* const> handles);
    void refreshLabels();
    void loadTransfers();
    void saveHistory();
    void flushHistoryIfDue(double now);
    void pollMoves();
    void checkWatchFolder(double now);
    void markStale(TransferId id);
    void handleEvent(Event const& event);
    void runRename(TransferId id);
    void finishAdd(TransferId id, bool start);
    void applyPendingName(TransferId id);
    void rpc(std::string const& request, tr_rpc_response_func&& callback = {});
    void rpcAsync(std::string const& request, tr_quark resultKey, Done done);
    void applyMeta(TransferInfo& info);
    [[nodiscard]] tr_variant appDefaults() const;
    void saveSessionSettings();
    [[nodiscard]] std::vector<SearchSource> const& sources();
    [[nodiscard]] DotTracker& dotTracker(TransferId id, size_t count, int rows, bool trackFresh);

    StartOptions options_;
    std::wstring configDir_;
    std::string configDirUtf8_;
    tr_session* session_ = nullptr;
    bool closeTimedOut_ = false;

    std::mutex queueMutex_;
    std::vector<std::function<void()>> queue_;
    std::atomic<bool> wakePending_ = false;

    std::vector<TransferInfo> transfers_;
    std::vector<Slot> slots_;
    std::vector<tr_torrent*> handles_;
    SessionStats stats_;
    double lastRefresh_ = -1.0;
    bool dirty_ = true;

    SwellHistory swell_;
    std::vector<DotTracker> dotTrackers_;
    std::vector<float> finishedScratch_;
    std::vector<FileState> fileStateScratch_;

    std::unordered_map<std::string, TransferMeta> meta_;
    std::vector<TransferId> removing_;
    std::list<MoveJob> moves_;
    std::unordered_map<TransferId, PendingAdd> pendingAdds_;
    std::unordered_map<TransferId, std::string> pendingNames_;
    bool historyDirty_ = false;
    double lastHistorySave_ = 0.0;
    double lastLogDrain_ = 0.0;

    AppPrefs prefs_;
    std::set<std::wstring> watchSeen_;
    double nextWatchCheck_ = 0.0;

    SearchWorker search_;
    std::vector<SearchSource> sources_;
};

void TransmissionEngine::post(std::function<void()> work) noexcept
{
    try
    {
        auto const lock = std::unique_lock{ queueMutex_ };
        queue_.push_back(std::move(work));
    }
    catch (...)
    {
        return;
    }
    signal();
}

void TransmissionEngine::signal() noexcept
{
    if (wakePending_.exchange(true))
    {
        return;
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

void TransmissionEngine::postEvent(Event event) noexcept
{
    post([this, event = std::move(event)] { handleEvent(event); });
}

void TransmissionEngine::postIdEvent(EventType type, tr_torrent* tor, bool flag) noexcept
{
    auto event = Event{};
    event.type = type;
    event.id = tr_torrentId(tor);
    event.flag = flag;
    postEvent(std::move(event));
}

void TransmissionEngine::onCompleteness(tr_torrent* tor, tr_completeness status, bool wasRunning, void* self)
{
    auto const type = status == TR_LEECH ? EventType::DownloadRestarted : EventType::DownloadFinished;
    static_cast<TransmissionEngine*>(self)->postIdEvent(type, tor, wasRunning);
}

void TransmissionEngine::onMetadata(tr_session* /*session*/, tr_torrent* tor, void* self)
{
    static_cast<TransmissionEngine*>(self)->postIdEvent(EventType::MetadataReceived, tor);
}

void TransmissionEngine::onRatioLimit(tr_session* /*session*/, tr_torrent* tor, void* self)
{
    static_cast<TransmissionEngine*>(self)->postIdEvent(EventType::SeedingFinished, tor);
}

void TransmissionEngine::onIdleLimit(tr_session* /*session*/, tr_torrent* tor, void* self)
{
    static_cast<TransmissionEngine*>(self)->postIdEvent(EventType::SeedingFinished, tor);
}

void TransmissionEngine::onQueueStart(tr_session* /*session*/, tr_torrent* tor, void* self)
{
    static_cast<TransmissionEngine*>(self)->postIdEvent(EventType::TransfersChanged, tor);
}

void TransmissionEngine::onAltSpeed(tr_session* /*session*/, bool active, bool byUser, void* self)
{
    auto event = Event{};
    event.type = EventType::AltSpeedChanged;
    event.flag = active;
    event.flag2 = byUser;
    static_cast<TransmissionEngine*>(self)->postEvent(std::move(event));
}

tr_variant TransmissionEngine::appDefaults() const
{
    // macosx/Defaults.plist, with the block cache and logging held down for memory.
    auto const defaults = SessionSettings{};
    auto map = tr_variant::make_map();
    tr_variantDictAddInt(&map, TR_KEY_alt_speed_up, defaults.altUpKBps);
    tr_variantDictAddInt(&map, TR_KEY_alt_speed_down, defaults.altDownKBps);
    tr_variantDictAddInt(&map, TR_KEY_alt_speed_time_begin, defaults.altBeginMinutes);
    tr_variantDictAddInt(&map, TR_KEY_alt_speed_time_end, defaults.altEndMinutes);
    tr_variantDictAddInt(&map, TR_KEY_alt_speed_time_day, defaults.altDays);
    tr_variantDictAddBool(&map, TR_KEY_blocklist_enabled, false);
    tr_variantDictAddInt(&map, TR_KEY_cache_size_mib, kCacheMib);
    tr_variantDictAddBool(&map, TR_KEY_dht_enabled, defaults.dht);
    tr_variantDictAddStr(&map, TR_KEY_download_dir, narrow(downloadsFolder()));
    tr_variantDictAddBool(&map, TR_KEY_download_queue_enabled, defaults.downloadQueueOn);
    tr_variantDictAddInt(&map, TR_KEY_download_queue_size, defaults.downloadQueueSize);
    tr_variantDictAddInt(&map, TR_KEY_idle_seeding_limit, defaults.idleMinutes);
    tr_variantDictAddBool(&map, TR_KEY_idle_seeding_limit_enabled, defaults.idleLimitOn);
    tr_variantDictAddStr(&map, TR_KEY_incomplete_dir, narrow(downloadsFolder()));
    tr_variantDictAddBool(&map, TR_KEY_incomplete_dir_enabled, defaults.incompleteFolderOn);
    tr_variantDictAddBool(&map, TR_KEY_lpd_enabled, defaults.lpd);
    tr_variantDictAddInt(&map, TR_KEY_message_level, TR_LOG_WARN);
    tr_variantDictAddInt(&map, TR_KEY_peer_limit_global, defaults.peerLimitGlobal);
    tr_variantDictAddInt(&map, TR_KEY_peer_limit_per_torrent, defaults.peerLimitPerTorrent);
    tr_variantDictAddBool(&map, TR_KEY_pex_enabled, defaults.pex);
    tr_variantDictAddBool(&map, TR_KEY_port_forwarding_enabled, defaults.portForwarding);
    tr_variantDictAddBool(&map, TR_KEY_queue_stalled_enabled, defaults.stalledOn);
    tr_variantDictAddInt(&map, TR_KEY_queue_stalled_minutes, defaults.stalledMinutes);
    tr_variantDictAddReal(&map, TR_KEY_ratio_limit, defaults.ratioLimit);
    tr_variantDictAddBool(&map, TR_KEY_ratio_limit_enabled, defaults.ratioLimitOn);
    tr_variantDictAddBool(&map, TR_KEY_rename_partial_files, defaults.renamePartialFiles);
    tr_variantDictAddBool(&map, TR_KEY_rpc_enabled, false);
    tr_variantDictAddBool(&map, TR_KEY_seed_queue_enabled, defaults.seedQueueOn);
    tr_variantDictAddInt(&map, TR_KEY_seed_queue_size, defaults.seedQueueSize);
    tr_variantDictAddInt(&map, TR_KEY_speed_limit_down, defaults.downKBps);
    tr_variantDictAddBool(&map, TR_KEY_speed_limit_down_enabled, defaults.limitDown);
    tr_variantDictAddInt(&map, TR_KEY_speed_limit_up, defaults.upKBps);
    tr_variantDictAddBool(&map, TR_KEY_speed_limit_up_enabled, defaults.limitUp);
    tr_variantDictAddBool(&map, TR_KEY_start_added_torrents, defaults.startAddedTransfers);
    tr_variantDictAddBool(&map, TR_KEY_utp_enabled, defaults.utp);

    // First launch, as on the Mac: a port from the ephemeral range is less likely to be blocked.
    // It sticks, because the first save below writes it into settings.json.
    auto ticks = FILETIME{};
    GetSystemTimePreciseAsFileTime(&ticks);
    tr_variantDictAddInt(&map, TR_KEY_peer_port, kFirstEphemeralPort + static_cast<int>(ticks.dwLowDateTime % kEphemeralPortCount));
    return map;
}

bool TransmissionEngine::start(StartOptions const& options)
{
    if (session_ != nullptr)
    {
        return true;
    }

    options_ = options;
    configDir_ = options.configDir.empty() ? defaultConfigDir() : options.configDir;
    configDirUtf8_ = narrow(configDir_);
    makeFolder(configDir_);
    prefs_ = loadPrefs(configDir_ + kPrefsFile);

    tr_lib_init();

    auto const defaults = appDefaults();
    auto const isFirstRun = !pathExists(configDir_ + kSettingsFile);
    auto const loaded = tr_sessionLoadSettings(configDirUtf8_, &defaults);
    // With the queue off it prints to stderr, which a windowed app doesn't have, and fmt aborts
    // on a failed write in this no-exceptions build. So: queue on, and refresh() drains it.
    session_ = tr_sessionInit(configDirUtf8_, true, loaded);
    if (session_ == nullptr)
    {
        return false;
    }
    if (isFirstRun)
    {
        saveSessionSettings();
    }

    tr_sessionSetCompletenessCallback(session_, onCompleteness, this);
    tr_sessionSetMetadataCallback(session_, onMetadata, this);
    tr_sessionSetRatioLimitHitCallback(session_, onRatioLimit, this);
    tr_sessionSetIdleLimitHitCallback(session_, onIdleLimit, this);
    tr_sessionSetQueueStartCallback(session_, onQueueStart, this);
    tr_sessionSetAltSpeedFunc(session_, onAltSpeed, this);

    loadTransfers();
    return true;
}

void TransmissionEngine::loadTransfers()
{
    // Everything loads paused, then the history decides what runs, as in awakeFromNib.
    auto* const ctor = tr_ctorNew(session_);
    tr_ctorSetPaused(ctor, TR_FORCE, true);
    tr_sessionLoadTorrents(session_, ctor);
    tr_ctorFree(ctor);

    auto text = std::string{};
    if (!readFile(configDir_ + kHistoryFile, text, kMaxHistoryBytes))
    {
        return;
    }

    auto document = rapidjson::Document{};
    document.Parse(text.c_str(), text.size());
    if (document.HasParseError() || !document.IsArray())
    {
        return;
    }

    auto const lock = tr_sessionLock(session_);
    auto count = tr_sessionGetAllTorrents(session_, nullptr, 0);
    handles_.resize(count);
    count = tr_sessionGetAllTorrents(session_, handles_.data(), handles_.size());

    auto byHash = std::unordered_map<std::string, tr_torrent*>{};
    for (size_t i = 0; i < count; ++i)
    {
        byHash.emplace(tr_torrentView(handles_[i]).hash_string, handles_[i]);
    }

    auto waiting = std::vector<tr_torrent*>{};
    for (auto const& item : document.GetArray())
    {
        if (!item.IsObject() || !item.HasMember("TorrentHash") || !item["TorrentHash"].IsString())
        {
            continue;
        }
        auto const hash = std::string{ item["TorrentHash"].GetString(), item["TorrentHash"].GetStringLength() };
        auto meta = TransferMeta{};
        if (item.HasMember("GroupValue") && item["GroupValue"].IsInt())
        {
            meta.group = item["GroupValue"].GetInt();
        }
        if (item.HasMember("RemoveWhenFinishSeeding") && item["RemoveWhenFinishSeeding"].IsBool())
        {
            meta.removeWhenDone = item["RemoveWhenFinishSeeding"].GetBool();
        }
        meta_[hash] = meta;

        auto const found = byHash.find(hash);
        if (found == byHash.end() || options_.pauseOnStart)
        {
            continue;
        }
        if (item.HasMember("Active") && item["Active"].IsBool() && item["Active"].GetBool())
        {
            tr_torrentStartNow(found->second);
        }
        if (item.HasMember("WaitToStart") && item["WaitToStart"].IsBool() && item["WaitToStart"].GetBool())
        {
            waiting.push_back(found->second);
        }
    }

    // Now that all are loaded, the ones in the queue can go back to waiting.
    for (auto* tor : waiting)
    {
        tr_torrentStart(tor);
    }
}

void TransmissionEngine::saveHistory()
{
    if (session_ == nullptr)
    {
        return;
    }

    auto buffer = rapidjson::StringBuffer{};
    auto writer = rapidjson::Writer<rapidjson::StringBuffer>{ buffer };
    writer.StartArray();
    {
        auto const lock = tr_sessionLock(session_);
        auto count = tr_sessionGetAllTorrents(session_, nullptr, 0);
        handles_.resize(count);
        count = tr_sessionGetAllTorrents(session_, handles_.data(), handles_.size());
        for (size_t i = 0; i < count; ++i)
        {
            auto* const tor = handles_[i];
            if (std::find(removing_.begin(), removing_.end(), tr_torrentId(tor)) != removing_.end())
            {
                continue;
            }
            auto const* const stat = tr_torrentStat(tor);
            auto const hash = std::string{ tr_torrentView(tor).hash_string };
            auto const it = meta_.find(hash);
            auto const meta = it != meta_.end() ? it->second : TransferMeta{};

            auto const waiting = stat->activity == TR_STATUS_DOWNLOAD_WAIT || stat->activity == TR_STATUS_SEED_WAIT;
            writer.StartObject();
            writer.Key("TorrentHash");
            writer.String(hash.c_str(), static_cast<rapidjson::SizeType>(hash.size()));
            writer.Key("Active");
            writer.Bool(stat->activity != TR_STATUS_STOPPED && !waiting);
            writer.Key("WaitToStart");
            writer.Bool(waiting);
            writer.Key("GroupValue");
            writer.Int(meta.group);
            writer.Key("RemoveWhenFinishSeeding");
            writer.Bool(meta.removeWhenDone);
            writer.EndObject();
        }
    }
    writer.EndArray();

    if (!isConfigContended())
    {
        writeFileAtomic(configDir_ + kHistoryFile, { buffer.GetString(), buffer.GetSize() });
    }
    historyDirty_ = false;
}

void TransmissionEngine::flushHistoryIfDue(double now)
{
    if (historyDirty_ && now - lastHistorySave_ >= kHistorySaveSeconds)
    {
        lastHistorySave_ = now;
        saveHistory();
    }
}

bool TransmissionEngine::shutdown(int maxSeconds)
{
    if (session_ == nullptr)
    {
        return true;
    }

    search_.stop();
    saveHistory();
    saveSessionSettings();

    // libtransmission waits on &event=stopped announces, and its deadline only covers part of the
    // close. A helper thread is the only way to promise the caller a bound.
    auto* const session = std::exchange(session_, nullptr);
    auto const trackerSeconds = static_cast<size_t>(std::max(1, maxSeconds - 1));
    auto const finished = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    auto closer = std::thread{ [session, trackerSeconds, finished]
                               {
                                   tr_sessionClose(session, trackerSeconds);
                                   SetEvent(finished);
                               } };

    auto const timeout = static_cast<DWORD>(std::max(1, maxSeconds)) * 1000U;
    auto const closed = WaitForSingleObject(finished, timeout) == WAIT_OBJECT_0;
    if (closed)
    {
        closer.join();
        CloseHandle(finished);
    }
    else
    {
        // It still needs the event, and the process is on its way out, so that handle stays.
        closer.detach();
        closeTimedOut_ = true;
    }
    return closed;
}

void TransmissionEngine::pump()
{
    wakePending_ = false;

    // A local list, because a callback may open a modal loop that comes back in here.
    auto work = std::vector<std::function<void()>>{};
    {
        auto const lock = std::unique_lock{ queueMutex_ };
        work.swap(queue_);
    }
    for (auto const& item : work)
    {
        item();
    }
}

bool TransmissionEngine::isConfigContended() const
{
    return session_ != nullptr && tr_sessionConfigDirIsContended(session_);
}

tr_torrent* TransmissionEngine::find(TransferId id) const
{
    // libtransmission frees a removed torrent on its own thread some time after tr_torrentRemove.
    if (session_ == nullptr || std::find(removing_.begin(), removing_.end(), id) != removing_.end())
    {
        return nullptr;
    }
    return tr_torrentFindFromId(session_, id);
}

TransferInfo* TransmissionEngine::row(TransferId id)
{
    auto const it = std::lower_bound(transfers_.begin(), transfers_.end(), id, [](TransferInfo const& info, TransferId value) { return info.id < value; });
    return it != transfers_.end() && it->id == id ? &*it : nullptr;
}

template<typename Action>
void TransmissionEngine::forEachTransfer(std::vector<TransferId> const& ids, Action&& action)
{
    if (session_ == nullptr)
    {
        return;
    }
    auto const lock = tr_sessionLock(session_);
    for (auto const id : ids)
    {
        if (auto* const tor = find(id); tor != nullptr)
        {
            action(tor);
        }
    }
    dirty_ = true;
    historyDirty_ = true;
}

void TransmissionEngine::applyMeta(TransferInfo& info)
{
    auto const it = meta_.find(narrow(info.hash));
    if (it != meta_.end())
    {
        info.group = it->second.group;
        info.removeWhenDone = it->second.removeWhenDone;
    }
    else
    {
        info.group = -1;
        info.removeWhenDone = prefs_.removeWhenFinishSeeding;
    }
}

void TransmissionEngine::updateRow(tr_torrent* tor, tr_stat const& stat, TransferInfo& info, Slot& slot)
{
    auto const hasMetadata = tr_torrentHasMetadata(tor);
    auto const edited = static_cast<int64_t>(stat.editDate);
    if (slot.stale || slot.editDate != edited || info.isMagnet == hasMetadata)
    {
        auto const view = tr_torrentView(tor);
        if (slot.name != view.name)
        {
            slot.name = view.name;
            info.name = widen(view.name);
        }
        if (info.hash.empty())
        {
            info.hash = widen(view.hash_string);
            applyMeta(info);
        }
        info.totalBytes = view.total_size;
        info.isFolder = view.is_folder;
        info.isMagnet = !hasMetadata;
        slot.editDate = edited;
        slot.swarmTick = 0;
    }

    auto const activity = stat.activity;
    auto const isChecking = activity == TR_STATUS_CHECK || activity == TR_STATUS_CHECK_WAIT;
    auto const isWaiting = activity == TR_STATUS_DOWNLOAD_WAIT || activity == TR_STATUS_SEED_WAIT;
    auto const isActive = activity != TR_STATUS_STOPPED && !isWaiting;
    auto const isSeeding = activity == TR_STATUS_SEED;
    auto const allDownloaded = stat.leftUntilDone == 0 && hasMetadata;

    // stateForTorrent in DropletView.
    if (stat.error == TR_STAT_LOCAL_ERROR)
    {
        info.state = TransferState::Problem;
    }
    else if (isChecking)
    {
        info.state = TransferState::Verifying;
    }
    else if (!hasMetadata)
    {
        info.state = isActive ? TransferState::Fetching : TransferState::Paused;
    }
    else if (isSeeding)
    {
        info.state = TransferState::Seeding;
    }
    else if (isActive)
    {
        info.state = TransferState::Download;
    }
    else
    {
        info.state = allDownloaded ? TransferState::Finished : TransferState::Paused;
    }

    info.hasWarning = stat.error != TR_STAT_OK;
    auto const* const message = info.hasWarning && stat.errorString != nullptr ? stat.errorString : "";
    if (slot.error != message)
    {
        slot.error = message;
        info.error = widen(message);
        // libtransmission's name for what the UI calls moving the data.
        constexpr auto kInternalName = std::wstring_view{ L"Set Location" };
        if (auto const at = info.error.find(kInternalName); at != std::wstring::npos)
        {
            info.error.replace(at, kInternalName.size(), L"Move Data To…");
        }
    }

    info.percentDone = stat.percentDone;
    info.percentComplete = stat.percentComplete;
    info.verifyProgress = stat.recheckProgress;
    info.metadataProgress = stat.metadataPercentComplete;
    info.haveBytes = stat.haveValid + stat.haveUnchecked;
    info.leftBytes = stat.leftUntilDone;
    info.selectedBytes = stat.sizeWhenDone;
    info.downBytesPerSecond = static_cast<double>(stat.pieceDownloadSpeed_KBps) * kBytesPerKilobyte;
    info.upBytesPerSecond = static_cast<double>(stat.pieceUploadSpeed_KBps) * kBytesPerKilobyte;
    info.peersConnected = stat.peersConnected;
    info.ratio = stat.ratio;
    info.tide = tideFor(stat.seedRatioPercentDone, stat.ratio);

    auto stopRatio = 0.0;
    info.ratioStop = tr_torrentGetSeedRatio(tor, &stopRatio) ? stopRatio : -1.0;

    // eta and shouldShowEta in Torrent.mm.
    if (stat.eta >= 0)
    {
        info.etaSeconds = stat.eta;
    }
    else if (stat.etaIdle >= 0 && stat.etaIdle < kETAIdleDisplaySeconds)
    {
        info.etaSeconds = stat.etaIdle;
    }
    else
    {
        info.etaSeconds = stat.leftUntilDone == 0 ? 0 : kEtaUnknown;
    }
    info.showEta = activity == TR_STATUS_DOWNLOAD || (isSeeding && (info.ratioStop >= 0.0 || (stat.etaIdle != TR_ETA_NOT_AVAIL && stat.etaIdle < kETAIdleDisplaySeconds)));

    info.addedDate = stat.addedDate;
    info.activityDate = stat.activityDate;
    info.doneDate = stat.doneDate;
    info.queuePosition = stat.queuePosition;
    info.isStalled = stat.isStalled;
    info.isWaiting = isWaiting;
    info.isFinishedSeeding = stat.finished;

    if (slot.swarmTick-- <= 0)
    {
        slot.swarmTick = kSwarmRefreshTicks;
        auto seeds = -1;
        auto leechers = -1;
        for (size_t i = 0, n = tr_torrentTrackerCount(tor); i < n; ++i)
        {
            auto const tracker = tr_torrentTracker(tor, i);
            seeds = std::max(seeds, tracker.seederCount);
            leechers = std::max(leechers, tracker.leecherCount);
        }
        info.seeds = seeds;
        info.peers = leechers;
    }

    if (info.state == TransferState::Seeding && info.upBytesPerSecond > 0.0)
    {
        slot.phase += kPhaseStep;
    }

    auto const isTransmitting = stat.peersGettingFromUs > 0 || stat.peersSendingToUs > 0 || stat.webseedsSendingToUs > 0 || activity == TR_STATUS_CHECK;

    // applyFilter in Controller.mm.
    if (isActive && activity != TR_STATUS_CHECK_WAIT)
    {
        stats_.active += isTransmitting ? 1 : 0;
        (isSeeding ? stats_.seeding : stats_.downloading) += 1;
    }
    else if (stat.error == TR_STAT_LOCAL_ERROR)
    {
        stats_.problems += 1;
    }
    else
    {
        stats_.paused += 1;
    }

    stats_.downBytesPerSecond += info.downBytesPerSecond;
    stats_.upBytesPerSecond += info.upBytesPerSecond;
    stats_.anyWorking |= isActive && !stat.isStalled && stat.error != TR_STAT_LOCAL_ERROR;
}

void TransmissionEngine::reconcileRows(std::span<tr_torrent* const> handles)
{
    // Rows stay sorted by id and ids only ever grow, so one walk matches old rows to live torrents.
    // Slots are reused rather than rebuilt so the strings in them can be too.
    size_t write = 0;
    size_t read = 0;
    for (auto* const tor : handles)
    {
        auto const id = tr_torrentId(tor);
        while (read < transfers_.size() && transfers_[read].id < id)
        {
            ++read;
        }

        if (read < transfers_.size() && transfers_[read].id == id)
        {
            if (write != read)
            {
                std::swap(transfers_[write], transfers_[read]);
                std::swap(slots_[write], slots_[read]);
            }
            ++read;
        }
        else
        {
            transfers_.insert(transfers_.begin() + static_cast<std::ptrdiff_t>(write), TransferInfo{});
            slots_.insert(slots_.begin() + static_cast<std::ptrdiff_t>(write), Slot{});
            transfers_[write].id = id;
            ++read;
        }

        updateRow(tor, *tr_torrentStat(tor), transfers_[write], slots_[write]);
        ++write;
    }
    transfers_.resize(write);
    slots_.resize(write);
}

void TransmissionEngine::refreshLabels()
{
    auto request = std::string{ R"({"jsonrpc":"2.0","id":1,"method":"torrent_get","params":{"fields":["id","labels"],"ids":[)" };
    auto any = false;
    for (size_t i = 0; i < transfers_.size(); ++i)
    {
        if (slots_[i].stale)
        {
            request += any ? "," : "";
            request += std::to_string(transfers_[i].id);
            any = true;
            slots_[i].stale = false;
        }
    }
    if (!any)
    {
        return;
    }
    request += "]}}";

    rpc(
        request,
        [this](tr_session* /*session*/, tr_variant&& response)
        {
            auto const* const top = response.get_if<tr_variant::Map>();
            auto const* const result = top != nullptr ? top->find_if<tr_variant::Map>(TR_KEY_result) : nullptr;
            auto const* const torrents = result != nullptr ? result->find_if<tr_variant::Vector>(TR_KEY_torrents) : nullptr;
            if (torrents == nullptr)
            {
                return;
            }
            for (auto const& entry : *torrents)
            {
                auto const* const map = entry.get_if<tr_variant::Map>();
                auto const id = map != nullptr ? map->value_if<int64_t>(TR_KEY_id) : std::nullopt;
                auto const* const labels = map != nullptr ? map->find_if<tr_variant::Vector>(TR_KEY_labels) : nullptr;
                auto* const info = id ? row(static_cast<TransferId>(*id)) : nullptr;
                if (info == nullptr || labels == nullptr)
                {
                    continue;
                }
                info->labels.clear();
                for (auto const& label : *labels)
                {
                    if (auto const text = label.value_if<std::string_view>())
                    {
                        info->labels.push_back(widen(*text));
                    }
                }
            }
        });
}

void TransmissionEngine::rpc(std::string const& request, tr_rpc_response_func&& callback)
{
    // Handlers normally run on the session thread. They touch nothing unlocked, so holding the
    // session lock here is equivalent, and the synchronous ones answer before this returns.
    auto const lock = tr_sessionLock(session_);
    tr_rpc_request_exec(session_, std::string_view{ request }, std::move(callback));
}

void TransmissionEngine::rpcAsync(std::string const& request, tr_quark resultKey, Done done)
{
    rpc(
        request,
        [this, resultKey, done = std::move(done)](tr_session* /*session*/, tr_variant&& response)
        {
            auto const* const top = response.get_if<tr_variant::Map>();
            auto const* const result = top != nullptr ? top->find_if<tr_variant::Map>(TR_KEY_result) : nullptr;
            auto const* const failure = top != nullptr ? top->find_if<tr_variant::Map>(TR_KEY_error) : nullptr;

            auto ok = result != nullptr && failure == nullptr;
            if (auto const flag = result != nullptr ? result->value_if<bool>(resultKey) : std::nullopt; flag)
            {
                ok = ok && *flag;
            }
            auto message = std::wstring{};
            if (auto const text = failure != nullptr ? failure->value_if<std::string_view>(TR_KEY_message) : std::nullopt; text)
            {
                message = widen(*text);
            }
            post([done, ok, message = std::move(message)] { done(ok, message); });
        });
}

std::vector<TransferInfo> const& TransmissionEngine::refresh()
{
    if (session_ == nullptr)
    {
        return transfers_;
    }

    auto const now = monotonicSeconds();
    if (!dirty_ && now - lastRefresh_ < kMinRefreshSeconds)
    {
        return transfers_;
    }
    lastRefresh_ = now;
    dirty_ = false;

    stats_ = SessionStats{};

    {
        // Held across the walk: a transfer removed mid-way would otherwise be freed under us.
        auto const lock = tr_sessionLock(session_);
        auto count = tr_sessionGetAllTorrents(session_, nullptr, 0);
        handles_.resize(count);
        count = tr_sessionGetAllTorrents(session_, handles_.data(), handles_.size());
        handles_.resize(count);

        if (!removing_.empty())
        {
            std::erase_if(removing_, [this](TransferId id) { return tr_torrentFindFromId(session_, id) == nullptr; });
            std::erase_if(handles_, [this](tr_torrent* tor) { return std::find(removing_.begin(), removing_.end(), tr_torrentId(tor)) != removing_.end(); });
        }
        if (!std::is_sorted(handles_.begin(), handles_.end(), [](auto const* a, auto const* b) { return tr_torrentId(a) < tr_torrentId(b); }))
        {
            std::sort(handles_.begin(), handles_.end(), [](auto const* a, auto const* b) { return tr_torrentId(a) < tr_torrentId(b); });
        }

        reconcileRows(handles_);
        stats_.altSpeedOn = tr_sessionUsesAltSpeed(session_);
    }
    stats_.all = static_cast<int>(transfers_.size());

    refreshLabels();
    swell_.record(now, stats_.downBytesPerSecond, stats_.upBytesPerSecond);

    if (!dotTrackers_.empty())
    {
        std::erase_if(dotTrackers_, [this, now](DotTracker const& tracker) { return now - tracker.lastUsed > kDotTrackerLifetime || row(tracker.id) == nullptr; });
    }

    pollMoves();
    checkWatchFolder(now);
    flushHistoryIfDue(now);
    if (now - lastLogDrain_ >= kLogDrainSeconds)
    {
        lastLogDrain_ = now;
        tr_logFreeQueue(tr_logGetQueue());
    }
    return transfers_;
}

SessionStats TransmissionEngine::sessionStats() const
{
    return stats_;
}

bool TransmissionEngine::swell(SwellData& out)
{
    return swell_.buckets(monotonicSeconds(), out);
}

DotTracker& TransmissionEngine::dotTracker(TransferId id, size_t count, int rows, bool trackFresh)
{
    auto const key = dotTrackerKey(id, count, rows, trackFresh);
    auto const it = std::lower_bound(
        dotTrackers_.begin(),
        dotTrackers_.end(),
        key,
        [](DotTracker const& tracker, auto const& value) { return dotTrackerKey(tracker.id, tracker.count, tracker.rows, tracker.trackFresh) < value; });
    if (it != dotTrackers_.end() && dotTrackerKey(it->id, it->count, it->rows, it->trackFresh) == key)
    {
        return *it;
    }

    auto tracker = DotTracker{};
    tracker.id = id;
    tracker.count = count;
    tracker.rows = rows;
    tracker.trackFresh = trackFresh;
    tracker.last.resize(count);
    tracker.previous.resize(count);
    tracker.done.resize(count);
    return *dotTrackers_.insert(it, std::move(tracker));
}

bool TransmissionEngine::pieceDots(TransferId id, size_t count, PieceDot* out, int rows, bool trackFresh)
{
    auto const* const info = row(id);
    if (info == nullptr || out == nullptr || rows <= 0 || count < static_cast<size_t>(rows))
    {
        return false;
    }
    auto const index = static_cast<size_t>(info - transfers_.data());
    auto const phase = slots_[index].phase;
    auto const dots = std::span{ out, count };
    auto const now = monotonicSeconds();

    auto& tracker = dotTracker(id, count, rows, trackFresh);
    tracker.lastUsed = now;

    auto const unchanged = tracker.primed && tracker.have == info->haveBytes && tracker.state == info->state && tracker.phase == phase && tracker.tide == info->tide &&
        tracker.verifyProgress == info->verifyProgress && !tracker.anyFresh;
    if (unchanged)
    {
        // The Mac refreshes its previous pieces on every update, so an idle spell can't expire them.
        tracker.previousTime = now;
        std::copy(tracker.last.begin(), tracker.last.end(), dots.begin());
        return false;
    }

    auto const usesPieces = info->state == TransferState::Download || info->state == TransferState::Paused || info->state == TransferState::Problem;
    auto anyFresh = false;
    if (usesPieces)
    {
        finishedScratch_.resize(std::max(finishedScratch_.size(), count));
        auto const finished = std::span{ finishedScratch_ }.first(count);
        {
            auto const lock = tr_sessionLock(session_);
            auto* const tor = find(id);
            if (tor == nullptr)
            {
                return false;
            }
            tr_torrentAmountFinished(tor, finished.data(), static_cast<int>(count));
        }

        // With fewer blocks than dots it divides by zero, so such a torrent is drawn evenly.
        for (auto& amount : finished)
        {
            if (std::isnan(amount))
            {
                amount = info->percentComplete;
            }
        }

        auto const hasPrevious = trackFresh && tracker.hasPrevious && now - tracker.previousTime <= kPreviousPiecesLifetime;
        auto const previous = hasPrevious ? std::span<uint8_t const>{ tracker.previous } : std::span<uint8_t const>{};
        anyFresh = fillPieceDots(finished, previous, info->state == TransferState::Download, tracker.done, dots);

        if (trackFresh)
        {
            tracker.previous.swap(tracker.done);
            tracker.hasPrevious = std::any_of(tracker.previous.begin(), tracker.previous.end(), [](uint8_t value) { return value != 0; });
            tracker.previousTime = now;
        }
    }
    else
    {
        auto input = DotInput{};
        input.state = info->state;
        input.verifyProgress = info->verifyProgress;
        input.tide = info->tide;
        input.phase = phase;
        input.columns = std::max(1, static_cast<int>(count) / rows);
        input.rows = rows;
        fillDots(input, dots);
        tracker.hasPrevious = false;
    }

    tracker.primed = true;
    tracker.have = info->haveBytes;
    tracker.state = info->state;
    tracker.phase = phase;
    tracker.tide = info->tide;
    tracker.verifyProgress = info->verifyProgress;
    tracker.anyFresh = anyFresh;

    if (std::equal(dots.begin(), dots.end(), tracker.last.begin()))
    {
        return false;
    }
    std::copy(dots.begin(), dots.end(), tracker.last.begin());
    return true;
}

void TransmissionEngine::availability(TransferId id, int count, int8_t* out)
{
    if (session_ == nullptr || out == nullptr || count <= 0)
    {
        return;
    }
    auto const lock = tr_sessionLock(session_);
    if (auto const* const tor = find(id); tor != nullptr)
    {
        tr_torrentAvailability(tor, out, count);
    }
}

TransferDetails TransmissionEngine::details(TransferId id)
{
    auto details = TransferDetails{};
    if (session_ == nullptr)
    {
        return details;
    }

    auto const lock = tr_sessionLock(session_);
    auto* const tor = find(id);
    if (tor == nullptr)
    {
        return details;
    }

    auto const view = tr_torrentView(tor);
    auto const* const stat = tr_torrentStat(tor);
    details.comment = view.comment != nullptr ? widen(view.comment) : std::wstring{};
    details.creator = view.creator != nullptr ? widen(view.creator) : std::wstring{};
    details.source = view.source != nullptr ? widen(view.source) : std::wstring{};
    auto const* const downloadDir = tr_torrentGetDownloadDir(tor);
    auto const* const currentDir = tr_torrentGetCurrentDir(tor);
    details.downloadFolder = downloadDir != nullptr ? withBackslashes(downloadDir) : std::wstring{};
    details.currentFolder = currentDir != nullptr ? withBackslashes(currentDir) : std::wstring{};
    details.torrentFile = withBackslashes(tr_torrentFilename(tor));
    details.magnetLink = widen(tr_torrentGetMagnetLink(tor));

    if (tr_torrentHasMetadata(tor))
    {
        if (view.is_folder)
        {
            auto const path = details.currentFolder + L"\\" + widen(view.name);
            details.dataPath = pathExists(path) ? path : std::wstring{};
        }
        else
        {
            details.dataPath = withBackslashes(tr_torrentFindFile(tor, 0));
        }
    }

    details.dateCreated = view.date_created;
    details.startDate = stat->startDate;
    details.pieceSize = view.piece_size;
    details.pieceCount = view.n_pieces;
    details.fileCount = tr_torrentFileCount(tor);
    details.isPrivate = view.is_private;
    details.haveVerifiedBytes = stat->haveValid;
    details.desiredAvailableBytes = stat->desiredAvailable;
    details.downloadedEver = stat->downloadedEver;
    details.uploadedEver = stat->uploadedEver;
    details.corruptEver = stat->corruptEver;
    details.secondsDownloading = stat->secondsDownloading;
    details.secondsSeeding = stat->secondsSeeding;
    details.peersSendingToUs = stat->peersSendingToUs;
    details.peersGettingFromUs = stat->peersGettingFromUs;
    details.webseedsSendingToUs = stat->webseedsSendingToUs;
    for (size_t i = 0; i < details.peersFrom.size() && i < TR_PEER_FROM_N_TYPES; ++i)
    {
        details.peersFrom[i] = stat->peersFrom[i];
    }
    details.canReannounce = tr_torrentCanManualUpdate(tor);
    return details;
}

FileTree TransmissionEngine::files(TransferId id)
{
    auto tree = FileTree{};
    if (session_ == nullptr)
    {
        return tree;
    }

    auto const lock = tr_sessionLock(session_);
    auto* const tor = find(id);
    if (tor == nullptr || !tr_torrentHasMetadata(tor))
    {
        return tree;
    }

    auto const count = tr_torrentFileCount(tor);
    auto flat = std::vector<FlatFile>{};
    flat.reserve(count);
    for (size_t i = 0; i < count; ++i)
    {
        auto const file = tr_torrentFile(tor, i);
        flat.push_back({ file.name, file.length });
    }
    tree = buildFileTree(flat, tr_torrentView(tor).is_folder);
    updateFiles(id, tree);
    return tree;
}

void TransmissionEngine::updateFiles(TransferId id, FileTree& tree)
{
    if (session_ == nullptr)
    {
        return;
    }

    {
        auto const lock = tr_sessionLock(session_);
        auto* const tor = find(id);
        if (tor == nullptr)
        {
            return;
        }
        auto const count = tr_torrentFileCount(tor);
        fileStateScratch_.resize(count);
        for (size_t i = 0; i < count; ++i)
        {
            auto const file = tr_torrentFile(tor, i);
            fileStateScratch_[i] = { file.have, file.length, file.wanted, static_cast<Priority>(file.priority) };
        }
    }
    rollUpFileState(tree, fileStateScratch_);
}

void TransmissionEngine::peers(TransferId id, std::vector<PeerInfo>& out)
{
    out.clear();
    if (session_ == nullptr)
    {
        return;
    }

    auto const lock = tr_sessionLock(session_);
    auto const* const tor = find(id);
    if (tor == nullptr)
    {
        return;
    }

    auto count = size_t{ 0 };
    auto* const stats = tr_torrentPeers(tor, &count);
    out.resize(count);
    for (size_t i = 0; i < count; ++i)
    {
        auto const& peer = stats[i];
        auto& info = out[i];
        info.address = widen(peer.addr);
        info.port = peer.port;
        info.client = peer.client != nullptr ? widen(peer.client) : std::wstring{};
        info.flags = widen(peer.flagStr);
        info.progress = peer.progress;
        info.downBytesPerSecond = peer.rateToClient_KBps * kBytesPerKilobyte;
        info.upBytesPerSecond = peer.rateToPeer_KBps * kBytesPerKilobyte;
        info.isUtp = peer.isUTP;
        info.isEncrypted = peer.isEncrypted;
        info.isSeed = peer.isSeed;
        info.isIncoming = peer.isIncoming;
        info.isDownloadingFrom = peer.isDownloadingFrom;
        info.isUploadingTo = peer.isUploadingTo;
        info.from = peer.from;
    }
    tr_torrentPeersFree(stats, count);
}

void TransmissionEngine::trackers(TransferId id, std::vector<TrackerInfo>& out)
{
    out.clear();
    if (session_ == nullptr)
    {
        return;
    }

    auto const lock = tr_sessionLock(session_);
    auto const* const tor = find(id);
    if (tor == nullptr)
    {
        return;
    }

    auto const count = tr_torrentTrackerCount(tor);
    out.resize(count);
    for (size_t i = 0; i < count; ++i)
    {
        auto const tracker = tr_torrentTracker(tor, i);
        auto& info = out[i];
        info.id = tracker.id;
        info.tier = static_cast<int>(tracker.tier);
        info.announce = widen(tracker.announce);
        info.host = widen(tracker.host_and_port);
        info.site = widen(tracker.sitename);
        info.announceState = static_cast<TrackerState>(tracker.announceState);
        info.scrapeState = static_cast<TrackerState>(tracker.scrapeState);
        info.isBackup = tracker.isBackup;
        info.seeders = tracker.seederCount;
        info.leechers = tracker.leecherCount;
        info.downloads = tracker.downloadCount;
        info.hasAnnounced = tracker.hasAnnounced;
        info.lastAnnounceSucceeded = tracker.lastAnnounceSucceeded;
        info.lastAnnounceTimedOut = tracker.lastAnnounceTimedOut;
        info.lastAnnounceResult = widen(tracker.lastAnnounceResult);
        info.lastAnnouncePeers = tracker.lastAnnouncePeerCount;
        info.lastAnnounceTime = tracker.lastAnnounceTime;
        info.nextAnnounceTime = tracker.nextAnnounceTime;
        info.hasScraped = tracker.hasScraped;
        info.lastScrapeSucceeded = tracker.lastScrapeSucceeded;
        info.lastScrapeResult = widen(tracker.lastScrapeResult);
        info.lastScrapeTime = tracker.lastScrapeTime;
        info.nextScrapeTime = tracker.nextScrapeTime;
    }
}

void TransmissionEngine::webseeds(TransferId id, std::vector<WebseedInfo>& out)
{
    out.clear();
    if (session_ == nullptr)
    {
        return;
    }

    auto const lock = tr_sessionLock(session_);
    auto const* const tor = find(id);
    if (tor == nullptr)
    {
        return;
    }

    auto const count = tr_torrentWebseedCount(tor);
    out.resize(count);
    for (size_t i = 0; i < count; ++i)
    {
        auto const webseed = tr_torrentWebseed(tor, i);
        out[i].url = widen(webseed.url);
        out[i].downBytesPerSecond = static_cast<double>(webseed.download_bytes_per_second);
        out[i].isDownloading = webseed.is_downloading;
    }
}

void TransmissionEngine::setFilesWanted(TransferId id, std::vector<int> const& fileIndexes, bool wanted)
{
    auto const indexes = std::vector<tr_file_index_t>(fileIndexes.begin(), fileIndexes.end());
    forEachTransfer({ id }, [&indexes, wanted](tr_torrent* tor) { tr_torrentSetFileDLs(tor, indexes.data(), indexes.size(), wanted); });
}

void TransmissionEngine::setFilesPriority(TransferId id, std::vector<int> const& fileIndexes, Priority priority)
{
    auto const indexes = std::vector<tr_file_index_t>(fileIndexes.begin(), fileIndexes.end());
    forEachTransfer({ id }, [&indexes, priority](tr_torrent* tor) { tr_torrentSetFilePriorities(tor, indexes.data(), indexes.size(), static_cast<tr_priority_t>(priority)); });
}

void TransmissionEngine::markStale(TransferId id)
{
    if (auto* const info = row(id); info != nullptr)
    {
        slots_[static_cast<size_t>(info - transfers_.data())].stale = true;
    }
    dirty_ = true;
}

void TransmissionEngine::renamePath(TransferId id, std::wstring const& path, std::wstring const& newName, Done done)
{
    if (session_ == nullptr || !isValidFileName(newName))
    {
        if (done)
        {
            done(false, L"That name can't be used.");
        }
        return;
    }

    auto const lock = tr_sessionLock(session_);
    auto* const tor = find(id);
    if (tor == nullptr)
    {
        if (done)
        {
            done(false, L"That transfer is gone.");
        }
        return;
    }

    auto const oldPath = narrow(path);
    auto const name = narrow(newName);
    tr_torrentRenamePath(
        tor,
        oldPath.c_str(),
        name.c_str(),
        [this, id, done = std::move(done)](tr_torrent* /*tor*/, char const* /*oldPath*/, char const* /*newName*/, int error, void* /*user_data*/)
        {
            auto message = error != 0 ? widen(tr_strerror(error)) : std::wstring{};
            post(
                [this, id, done, error, message = std::move(message)]
                {
                    markStale(id);
                    if (done)
                    {
                        done(error == 0, message);
                    }
                });
        },
        nullptr);
}

void TransmissionEngine::moveData(TransferId id, std::wstring const& folder, Done done)
{
    if (session_ == nullptr)
    {
        return;
    }

    auto const lock = tr_sessionLock(session_);
    auto* const tor = find(id);
    if (tor == nullptr)
    {
        if (done)
        {
            done(false, L"That transfer is gone.");
        }
        return;
    }

    auto const location = narrow(folder);
    auto const* const currentDir = tr_torrentGetCurrentDir(tor);
    auto const current = currentDir != nullptr ? withBackslashes(currentDir) : std::wstring{};
    auto const target = withBackslashes(location);
    if (_wcsicmp(current.c_str(), target.c_str()) == 0)
    {
        if (done)
        {
            done(true, {});
        }
        return;
    }

    // moveTorrentDataFileTo: a folder can't move to inside itself.
    auto const data = current + L"\\" + widen(tr_torrentName(tor));
    if (target.size() > data.size() && _wcsnicmp(target.c_str(), data.c_str(), data.size()) == 0 && target[data.size()] == L'\\')
    {
        if (done)
        {
            done(false, L"A folder cannot be moved to inside itself.");
        }
        return;
    }

    auto& job = moves_.emplace_back();
    job.id = id;
    job.done = std::move(done);
    tr_torrentSetLocation(tor, location.c_str(), true, &job.state);
}

void TransmissionEngine::pollMoves()
{
    for (auto it = moves_.begin(); it != moves_.end();)
    {
        if (it->state == TR_LOC_MOVING)
        {
            ++it;
            continue;
        }
        if (it->done)
        {
            auto const ok = it->state == TR_LOC_DONE;
            it->done(ok, ok ? std::wstring{} : L"There was an error moving the data.");
        }
        it = moves_.erase(it);
    }
}

bool TransmissionEngine::addTracker(TransferId id, std::wstring const& announce)
{
    if (session_ == nullptr)
    {
        return false;
    }

    auto url = trimmed(announce);
    if (url.find(L"://") == std::wstring::npos)
    {
        url = L"http://" + url;
    }

    auto const lock = tr_sessionLock(session_);
    auto* const tor = find(id);
    if (tor == nullptr)
    {
        return false;
    }
    auto const list = tr_torrentGetTrackerList(tor) + "\n\n" + narrow(url);
    dirty_ = true;
    return tr_torrentSetTrackerList(tor, list.c_str());
}

bool TransmissionEngine::removeTrackers(TransferId id, std::vector<std::wstring> const& announces)
{
    if (session_ == nullptr)
    {
        return false;
    }

    auto unwanted = std::set<std::string>{};
    for (auto const& announce : announces)
    {
        unwanted.insert(narrow(announce));
    }

    auto const lock = tr_sessionLock(session_);
    auto* const tor = find(id);
    if (tor == nullptr)
    {
        return false;
    }

    auto list = std::string{};
    auto tier = std::optional<size_t>{};
    for (size_t i = 0, n = tr_torrentTrackerCount(tor); i < n; ++i)
    {
        auto const tracker = tr_torrentTracker(tor, i);
        if (unwanted.count(tracker.announce) != 0)
        {
            continue;
        }
        if (tier && *tier != tracker.tier)
        {
            list += '\n';
        }
        list += tracker.announce;
        list += '\n';
        tier = tracker.tier;
    }
    dirty_ = true;
    return tr_torrentSetTrackerList(tor, list.c_str());
}

bool TransmissionEngine::editTracker(TransferId id, std::wstring const& oldAnnounce, std::wstring const& newAnnounce)
{
    if (session_ == nullptr)
    {
        return false;
    }

    auto const oldUrl = narrow(oldAnnounce);
    auto const newUrl = narrow(trimmed(newAnnounce));

    auto const lock = tr_sessionLock(session_);
    auto* const tor = find(id);
    if (tor == nullptr)
    {
        return false;
    }

    auto list = std::string{};
    auto tier = std::optional<size_t>{};
    auto found = false;
    for (size_t i = 0, n = tr_torrentTrackerCount(tor); i < n; ++i)
    {
        auto const tracker = tr_torrentTracker(tor, i);
        if (tier && *tier != tracker.tier)
        {
            list += '\n';
        }
        auto const replace = oldUrl == tracker.announce;
        found |= replace;
        list += replace ? newUrl : std::string{ tracker.announce };
        list += '\n';
        tier = tracker.tier;
    }
    dirty_ = true;
    return found && tr_torrentSetTrackerList(tor, list.c_str());
}

void TransmissionEngine::pause(std::vector<TransferId> const& ids)
{
    forEachTransfer(ids, [](tr_torrent* tor) { tr_torrentStop(tor); });
}

void TransmissionEngine::resume(std::vector<TransferId> const& ids)
{
    forEachTransfer(ids, [](tr_torrent* tor) { tr_torrentStart(tor); });
}

void TransmissionEngine::resumeNow(std::vector<TransferId> const& ids)
{
    forEachTransfer(ids, [](tr_torrent* tor) { tr_torrentStartNow(tor); });
}

void TransmissionEngine::verify(std::vector<TransferId> const& ids)
{
    forEachTransfer(ids, [](tr_torrent* tor) { tr_torrentVerify(tor); });
}

void TransmissionEngine::reannounce(std::vector<TransferId> const& ids)
{
    forEachTransfer(ids, [](tr_torrent* tor) { tr_torrentManualUpdate(tor); });
}

void TransmissionEngine::remove(std::vector<TransferId> const& ids, bool deleteData)
{
    forEachTransfer(
        ids,
        [this, deleteData](tr_torrent* tor)
        {
            // Don't want any of these starting then stopping.
            auto const activity = tr_torrentStat(tor)->activity;
            if (activity == TR_STATUS_DOWNLOAD_WAIT || activity == TR_STATUS_SEED_WAIT)
            {
                tr_torrentStop(tor);
            }

            auto const id = tr_torrentId(tor);
            meta_.erase(tr_torrentView(tor).hash_string);
            pendingAdds_.erase(id);
            pendingNames_.erase(id);
            removing_.push_back(id);
            tr_torrentRemove(tor, deleteData, trashDataFile, nullptr);
        });
}

void TransmissionEngine::moveInQueue(std::vector<TransferId> const& ids, QueueMove move)
{
    if (session_ == nullptr)
    {
        return;
    }

    // One lock across both steps, so nothing here can be freed before the batch call.
    auto const lock = tr_sessionLock(session_);
    auto handles = std::vector<tr_torrent*>{};
    forEachTransfer(ids, [&handles](tr_torrent* tor) { handles.push_back(tor); });
    switch (move)
    {
    case QueueMove::Top:
        tr_torrentsQueueMoveTop(handles.data(), handles.size());
        break;
    case QueueMove::Up:
        tr_torrentsQueueMoveUp(handles.data(), handles.size());
        break;
    case QueueMove::Down:
        tr_torrentsQueueMoveDown(handles.data(), handles.size());
        break;
    case QueueMove::Bottom:
        tr_torrentsQueueMoveBottom(handles.data(), handles.size());
        break;
    }
}

void TransmissionEngine::setGroup(TransferId id, int group)
{
    if (auto* const info = row(id); info != nullptr)
    {
        info->group = group;
        auto& meta = meta_[narrow(info->hash)];
        meta.group = group;
        meta.removeWhenDone = info->removeWhenDone;
        historyDirty_ = true;
    }
}

void TransmissionEngine::setLabels(TransferId id, std::vector<std::wstring> const& labels)
{
    if (session_ == nullptr)
    {
        return;
    }

    auto request = std::string{ R"({"jsonrpc":"2.0","id":1,"method":"torrent_set","params":{"ids":[)" } + std::to_string(id) + R"(],"labels":[)";
    for (size_t i = 0; i < labels.size(); ++i)
    {
        request += i > 0 ? "," : "";
        request += escapedJson(narrow(labels[i]));
    }
    request += "]}}";
    rpc(request);
    markStale(id);
}

TransferOptions TransmissionEngine::options(TransferId id)
{
    auto options = TransferOptions{};
    if (session_ == nullptr)
    {
        return options;
    }

    auto const lock = tr_sessionLock(session_);
    auto const* const tor = find(id);
    if (tor == nullptr)
    {
        return options;
    }

    options.limitDown = tr_torrentUsesSpeedLimit(tor, TR_DOWN);
    options.downKBps = static_cast<int>(tr_torrentGetSpeedLimit_KBps(tor, TR_DOWN));
    options.limitUp = tr_torrentUsesSpeedLimit(tor, TR_UP);
    options.upKBps = static_cast<int>(tr_torrentGetSpeedLimit_KBps(tor, TR_UP));
    options.honorSessionLimits = tr_torrentUsesSessionLimits(tor);
    options.ratioMode = static_cast<LimitMode>(tr_torrentGetRatioMode(tor));
    options.ratioLimit = tr_torrentGetRatioLimit(tor);
    options.idleMode = static_cast<LimitMode>(tr_torrentGetIdleMode(tor));
    options.idleMinutes = tr_torrentGetIdleLimit(tor);
    options.priority = static_cast<Priority>(tr_torrentGetPriority(tor));
    options.peerLimit = tr_torrentGetPeerLimit(tor);
    if (auto const* const info = row(id); info != nullptr)
    {
        options.removeWhenDone = info->removeWhenDone;
    }

    // Only reachable through RPC from outside the library, and a synchronous answer at that.
    auto const request = std::string{ R"({"jsonrpc":"2.0","id":1,"method":"torrent_get","params":{"fields":["sequential_download"],"ids":[)" } + std::to_string(id) + "]}}";
    rpc(request,
        [&options](tr_session* /*session*/, tr_variant&& response)
        {
            auto const* const top = response.get_if<tr_variant::Map>();
            auto const* const result = top != nullptr ? top->find_if<tr_variant::Map>(TR_KEY_result) : nullptr;
            auto const* const torrents = result != nullptr ? result->find_if<tr_variant::Vector>(TR_KEY_torrents) : nullptr;
            if (torrents != nullptr && !torrents->empty())
            {
                if (auto const* const map = (*torrents)[0].get_if<tr_variant::Map>(); map != nullptr)
                {
                    options.sequential = map->value_if<bool>(TR_KEY_sequential_download).value_or(false);
                }
            }
        });
    return options;
}

void TransmissionEngine::setOptions(TransferId id, TransferOptions const& options)
{
    auto const before = this->options(id);
    forEachTransfer(
        { id },
        [&options](tr_torrent* tor)
        {
            tr_torrentUseSpeedLimit(tor, TR_DOWN, options.limitDown);
            tr_torrentSetSpeedLimit_KBps(tor, TR_DOWN, static_cast<size_t>(std::max(options.downKBps, 0)));
            tr_torrentUseSpeedLimit(tor, TR_UP, options.limitUp);
            tr_torrentSetSpeedLimit_KBps(tor, TR_UP, static_cast<size_t>(std::max(options.upKBps, 0)));
            tr_torrentUseSessionLimits(tor, options.honorSessionLimits);
            tr_torrentSetRatioMode(tor, static_cast<tr_ratiolimit>(options.ratioMode));
            tr_torrentSetRatioLimit(tor, std::max(options.ratioLimit, 0.0));
            tr_torrentSetIdleMode(tor, static_cast<tr_idlelimit>(options.idleMode));
            if (options.idleMinutes > 0)
            {
                tr_torrentSetIdleLimit(tor, static_cast<uint16_t>(options.idleMinutes));
            }
            tr_torrentSetPriority(tor, static_cast<tr_priority_t>(options.priority));
            if (options.peerLimit > 0)
            {
                tr_torrentSetPeerLimit(tor, static_cast<uint16_t>(options.peerLimit));
            }
        });

    if (options.sequential != before.sequential)
    {
        auto const request = std::string{ R"({"jsonrpc":"2.0","id":1,"method":"torrent_set","params":{"ids":[)" } + std::to_string(id) + R"(],"sequential_download":)" +
            (options.sequential ? "true" : "false") + "}}";
        rpc(request);
    }

    if (auto* const info = row(id); info != nullptr && info->removeWhenDone != options.removeWhenDone)
    {
        info->removeWhenDone = options.removeWhenDone;
        auto& meta = meta_[narrow(info->hash)];
        meta.group = info->group;
        meta.removeWhenDone = options.removeWhenDone;
    }
}

AddPreview TransmissionEngine::parseTorrentFile(std::wstring const& path)
{
    auto data = std::string{};
    if (!readFile(path, data, kMaxTorrentBytes))
    {
        auto preview = AddPreview{};
        preview.error = L"That file couldn't be read.";
        return preview;
    }
    return parseTorrentData(std::move(data));
}

AddPreview TransmissionEngine::parseTorrentData(std::string data)
{
    auto preview = AddPreview{};

    auto metainfo = tr_torrent_metainfo{};
    auto error = tr_error{};
    if (!metainfo.parse_benc(data, &error))
    {
        preview.error = error ? widen(error.message()) : L"That isn't a valid torrent file.";
        return preview;
    }

    preview.valid = true;
    preview.name = widen(metainfo.name());
    preview.hash = widen(metainfo.info_hash_string().sv());
    preview.totalBytes = metainfo.total_size();
    preview.comment = widen(metainfo.comment());
    preview.creator = widen(metainfo.creator());
    preview.pieceSize = metainfo.piece_size();
    preview.pieceCount = metainfo.piece_count();
    preview.isPrivate = metainfo.is_private();

    auto const count = metainfo.file_count();
    auto flat = std::vector<FlatFile>{};
    auto states = std::vector<FileState>{};
    flat.reserve(count);
    states.reserve(count);
    for (size_t i = 0; i < count; ++i)
    {
        flat.push_back({ metainfo.file_subpath(i), metainfo.file_size(i) });
        states.push_back({ 0, metainfo.file_size(i), true, Priority::Normal });
    }
    auto const isFolder = count > 1 || (count == 1 && metainfo.file_subpath(0).find('/') != std::string::npos);
    preview.files = buildFileTree(flat, isFolder);
    rollUpFileState(preview.files, states);

    if (session_ != nullptr)
    {
        auto const lock = tr_sessionLock(session_);
        if (auto const* const duplicate = tr_torrentFindFromMetainfo(session_, &metainfo); duplicate != nullptr)
        {
            preview.duplicateOf = tr_torrentId(duplicate);
        }
    }

    preview.source = std::move(data);
    return preview;
}

AddPreview TransmissionEngine::parseMagnet(std::wstring const& uri)
{
    auto preview = AddPreview{};
    auto const link = narrow(trimmed(uri));

    auto metainfo = tr_magnet_metainfo{};
    auto error = tr_error{};
    if (!metainfo.parseMagnet(link, &error))
    {
        preview.error = error ? widen(error.message()) : L"That isn't a valid magnet link.";
        return preview;
    }

    preview.valid = true;
    preview.isMagnet = true;
    // Magnet names come straight from the link, where spaces are often plus signs.
    preview.name = widen(metainfo.name());
    std::replace(preview.name.begin(), preview.name.end(), L'+', L' ');
    preview.hash = widen(metainfo.info_hash_string().sv());

    if (session_ != nullptr)
    {
        auto const lock = tr_sessionLock(session_);
        if (auto const* const duplicate = tr_torrentFindFromMagnetLink(session_, link.c_str()); duplicate != nullptr)
        {
            preview.duplicateOf = tr_torrentId(duplicate);
        }
    }

    preview.source = link;
    return preview;
}

AddResult TransmissionEngine::add(AddPreview const& preview, AddRequest const& request)
{
    auto result = AddResult{};
    if (session_ == nullptr || !preview.valid)
    {
        result.error = L"That can't be added.";
        return result;
    }

    auto const name = trimmed(request.nameOverride);
    auto const renamesRoot = !name.empty() && name != preview.name;
    if (renamesRoot && !isValidFileName(name))
    {
        result.error = L"That name can't be used.";
        return result;
    }

    auto* const ctor = tr_ctorNew(session_);
    // Stops first, so that renames can land before anything touches the disk. Never deletes the
    // source: this add works from a copy in memory, whatever the session's setting says.
    tr_ctorSetPaused(ctor, TR_FORCE, true);
    tr_ctorSetDeleteSource(ctor, false);
    if (!request.destination.empty())
    {
        tr_ctorSetDownloadDir(ctor, TR_FORCE, narrow(request.destination).c_str());
    }

    auto error = tr_error{};
    auto const loaded = preview.isMagnet ? tr_ctorSetMetainfoFromMagnetLink(ctor, preview.source.c_str(), &error) :
                                           tr_ctorSetMetainfo(ctor, preview.source.data(), preview.source.size(), &error);
    if (!loaded)
    {
        tr_ctorFree(ctor);
        result.error = error ? widen(error.message()) : L"That can't be added.";
        return result;
    }

    if (!preview.isMagnet)
    {
        auto unwanted = std::vector<tr_file_index_t>{};
        for (size_t i = 0; i < request.wanted.size(); ++i)
        {
            if (request.wanted[i] == 0)
            {
                unwanted.push_back(i);
            }
        }
        if (!unwanted.empty())
        {
            tr_ctorSetFilesWanted(ctor, unwanted.data(), unwanted.size(), false);
        }

        for (auto const priority : { Priority::Low, Priority::High })
        {
            auto files = std::vector<tr_file_index_t>{};
            for (size_t i = 0; i < request.priorities.size(); ++i)
            {
                if (request.priorities[i] == priority)
                {
                    files.push_back(i);
                }
            }
            if (!files.empty())
            {
                tr_ctorSetFilePriorities(ctor, files.data(), files.size(), static_cast<tr_priority_t>(priority));
            }
        }
    }

    auto const lock = tr_sessionLock(session_);
    auto* duplicate = static_cast<tr_torrent*>(nullptr);
    auto* const tor = tr_torrentNew(ctor, &duplicate);
    tr_ctorFree(ctor);
    if (tor == nullptr)
    {
        result.error = duplicate != nullptr ? L"That transfer is already here." : L"That can't be added.";
        return result;
    }

    result.id = tr_torrentId(tor);
    dirty_ = true;
    historyDirty_ = true;

    if (request.group != -1)
    {
        meta_[narrow(preview.hash)] = TransferMeta{ request.group, prefs_.removeWhenFinishSeeding };
    }

    if (preview.isMagnet)
    {
        if (renamesRoot)
        {
            pendingNames_[result.id] = narrow(name);
        }
        if (!request.startPaused)
        {
            tr_torrentStart(tor);
        }
        return result;
    }

    // The new folder may already hold some of this. Left to itself the library would trust any files
    // of the right size, which is why the Mac's Add panel forces this check too.
    auto const root = (request.destination.empty() ? withBackslashes(tr_torrentGetDownloadDir(tor)) : request.destination) + L"\\" + preview.name;
    if (pathExists(root))
    {
        tr_torrentVerify(tor);
    }

    auto pending = PendingAdd{};
    pending.startWhenDone = !request.startPaused;

    // Deepest first, so that renaming a folder can't move a file out from under its own rename.
    auto renames = request.renames;
    std::stable_sort(
        renames.begin(),
        renames.end(),
        [](FileRename const& a, FileRename const& b) { return std::count(a.path.begin(), a.path.end(), L'/') > std::count(b.path.begin(), b.path.end(), L'/'); });
    for (auto const& rename : renames)
    {
        if (isValidFileName(rename.newName))
        {
            pending.renames.emplace_back(narrow(rename.path), narrow(rename.newName));
        }
    }
    if (renamesRoot)
    {
        pending.renames.emplace_back(tr_torrentName(tor), narrow(name));
    }

    if (pending.renames.empty())
    {
        finishAdd(result.id, pending.startWhenDone);
        return result;
    }

    pendingAdds_[result.id] = std::move(pending);
    runRename(result.id);
    return result;
}

void TransmissionEngine::runRename(TransferId id)
{
    auto const it = pendingAdds_.find(id);
    if (it == pendingAdds_.end())
    {
        return;
    }

    if (it->second.renames.empty())
    {
        auto const start = it->second.startWhenDone;
        pendingAdds_.erase(it);
        finishAdd(id, start);
        return;
    }

    auto const [oldPath, newName] = it->second.renames.front();
    it->second.renames.erase(it->second.renames.begin());

    auto const lock = tr_sessionLock(session_);
    auto* const tor = find(id);
    if (tor == nullptr)
    {
        pendingAdds_.erase(id);
        return;
    }

    // A failed rename doesn't stop the add. Names were checked up front, so this is exotic.
    tr_torrentRenamePath(
        tor,
        oldPath.c_str(),
        newName.c_str(),
        [this, id](tr_torrent* /*tor*/, char const* /*oldPath*/, char const* /*newName*/, int error, void* /*user_data*/)
        {
            auto message = error != 0 ? widen(tr_strerror(error)) : std::wstring{};
            post(
                [this, id, error, message = std::move(message)]
                {
                    markStale(id);
                    if (error != 0)
                    {
                        auto event = Event{};
                        event.type = EventType::AddRenameFailed;
                        event.id = id;
                        event.text = message;
                        handleEvent(event);
                    }
                    runRename(id);
                });
        },
        nullptr);
}

void TransmissionEngine::finishAdd(TransferId id, bool start)
{
    if (start)
    {
        resume({ id });
    }
    auto event = Event{};
    event.id = id;
    postEvent(std::move(event));
}

void TransmissionEngine::applyPendingName(TransferId id)
{
    auto const it = pendingNames_.find(id);
    if (it == pendingNames_.end())
    {
        return;
    }
    auto const name = std::move(it->second);
    pendingNames_.erase(it);

    auto const lock = tr_sessionLock(session_);
    if (auto* const tor = find(id); tor != nullptr)
    {
        auto const oldPath = std::string{ tr_torrentName(tor) };
        tr_torrentRenamePath(
            tor,
            oldPath.c_str(),
            name.c_str(),
            [this, id](tr_torrent* /*tor*/, char const* /*oldPath*/, char const* /*newName*/, int /*error*/, void* /*user_data*/)
            {
                post([this, id] { markStale(id); });
            },
            nullptr);
    }
}

void TransmissionEngine::handleEvent(Event const& event)
{
    dirty_ = true;

    switch (event.type)
    {
    case EventType::MetadataReceived:
        markStale(event.id);
        applyPendingName(event.id);
        historyDirty_ = true;
        break;
    case EventType::SeedingFinished:
        if (auto const* const info = row(event.id); info != nullptr && info->removeWhenDone)
        {
            remove({ event.id }, false);
        }
        break;
    default:
        break;
    }

    if (options_.onEvent)
    {
        options_.onEvent(event);
    }
}

void TransmissionEngine::checkWatchFolder(double now)
{
    if (!prefs_.watchFolderOn || prefs_.watchFolder.empty() || now < nextWatchCheck_)
    {
        return;
    }
    nextWatchCheck_ = now + kWatchFolderSeconds;

    auto names = std::set<std::wstring>{};
    auto found = WIN32_FIND_DATAW{};
    auto const search = FindFirstFileExW(
        (extendedPath(prefs_.watchFolder) + L"\\*.torrent").c_str(),
        FindExInfoBasic,
        &found,
        FindExSearchNameMatch,
        nullptr,
        FIND_FIRST_EX_LARGE_FETCH);
    if (search == INVALID_HANDLE_VALUE)
    {
        return;
    }

    do
    {
        // Browsers can write in two steps, the first one empty, so that one is seen again next time.
        if (found.nFileSizeLow == 0 && found.nFileSizeHigh == 0)
        {
            continue;
        }
        names.insert(found.cFileName);
    } while (FindNextFileW(search, &found));
    FindClose(search);

    for (auto const& name : names)
    {
        if (watchSeen_.count(name) != 0)
        {
            continue;
        }

        auto const path = prefs_.watchFolder + L"\\" + name;
        auto const preview = parseTorrentFile(path);
        if (!preview.valid || preview.duplicateOf != kNoTransfer)
        {
            continue;
        }

        auto request = AddRequest{};
        request.startPaused = !settings().startAddedTransfers;
        auto const added = add(preview, request);
        if (added.id == kNoTransfer)
        {
            continue;
        }

        if (prefs_.deleteOriginalTorrent)
        {
            recycle(path);
        }

        auto event = Event{};
        event.type = EventType::WatchFolderAdded;
        event.id = added.id;
        event.text = name;
        handleEvent(event);
    }
    watchSeen_ = std::move(names);
}

void TransmissionEngine::saveSessionSettings()
{
    if (session_ != nullptr)
    {
        tr_sessionSaveSettings(session_, configDirUtf8_.c_str(), tr_variant::make_map());
    }
}

SessionSettings TransmissionEngine::settings()
{
    auto out = SessionSettings{};
    if (session_ == nullptr)
    {
        return out;
    }

    out.limitDown = tr_sessionIsSpeedLimited(session_, TR_DOWN);
    out.downKBps = static_cast<int>(tr_sessionGetSpeedLimit_KBps(session_, TR_DOWN));
    out.limitUp = tr_sessionIsSpeedLimited(session_, TR_UP);
    out.upKBps = static_cast<int>(tr_sessionGetSpeedLimit_KBps(session_, TR_UP));
    out.altSpeedOn = tr_sessionUsesAltSpeed(session_);
    out.altDownKBps = static_cast<int>(tr_sessionGetAltSpeed_KBps(session_, TR_DOWN));
    out.altUpKBps = static_cast<int>(tr_sessionGetAltSpeed_KBps(session_, TR_UP));
    out.altScheduleOn = tr_sessionUsesAltSpeedTime(session_);
    out.altBeginMinutes = static_cast<int>(tr_sessionGetAltSpeedBegin(session_));
    out.altEndMinutes = static_cast<int>(tr_sessionGetAltSpeedEnd(session_));
    out.altDays = tr_sessionGetAltSpeedDay(session_);
    out.peerPort = tr_sessionGetPeerPort(session_);
    out.randomPortOnStart = tr_sessionGetPeerPortRandomOnStart(session_);
    out.portForwarding = tr_sessionIsPortForwardingEnabled(session_);
    out.peerLimitGlobal = tr_sessionGetPeerLimit(session_);
    out.peerLimitPerTorrent = tr_sessionGetPeerLimitPerTorrent(session_);
    out.encryption = static_cast<EncryptionMode>(tr_sessionGetEncryption(session_));
    out.dht = tr_sessionIsDHTEnabled(session_);
    out.pex = tr_sessionIsPexEnabled(session_);
    out.lpd = tr_sessionIsLPDEnabled(session_);
    out.utp = tr_sessionIsUTPEnabled(session_);
    out.downloadQueueOn = tr_sessionGetQueueEnabled(session_, TR_DOWN);
    out.downloadQueueSize = static_cast<int>(tr_sessionGetQueueSize(session_, TR_DOWN));
    out.seedQueueOn = tr_sessionGetQueueEnabled(session_, TR_UP);
    out.seedQueueSize = static_cast<int>(tr_sessionGetQueueSize(session_, TR_UP));
    out.stalledOn = tr_sessionGetQueueStalledEnabled(session_);
    out.stalledMinutes = static_cast<int>(tr_sessionGetQueueStalledMinutes(session_));
    out.ratioLimitOn = tr_sessionIsRatioLimited(session_);
    out.ratioLimit = tr_sessionGetRatioLimit(session_);
    out.idleLimitOn = tr_sessionIsIdleLimited(session_);
    out.idleMinutes = tr_sessionGetIdleLimit(session_);
    out.blocklistOn = tr_blocklistIsEnabled(session_);
    out.blocklistUrl = widen(tr_blocklistGetURL(session_));
    out.downloadFolder = withBackslashes(tr_sessionGetDownloadDir(session_));
    out.incompleteFolder = withBackslashes(tr_sessionGetIncompleteDir(session_));
    out.incompleteFolderOn = tr_sessionIsIncompleteDirEnabled(session_);
    out.renamePartialFiles = tr_sessionIsIncompleteFileNamingEnabled(session_);

    // Two settings with a setter but no getter in the public API.
    auto current = tr_sessionGetSettings(session_);
    auto flag = false;
    if (tr_variantDictFindBool(&current, TR_KEY_torrent_complete_verify_enabled, &flag))
    {
        out.verifyOnCompletion = flag;
    }
    if (tr_variantDictFindBool(&current, TR_KEY_start_added_torrents, &flag))
    {
        out.startAddedTransfers = flag;
    }
    return out;
}

void TransmissionEngine::applySettings(SessionSettings const& next)
{
    if (session_ == nullptr)
    {
        return;
    }

    auto const current = settings();

    tr_sessionLimitSpeed(session_, TR_DOWN, next.limitDown);
    tr_sessionSetSpeedLimit_KBps(session_, TR_DOWN, static_cast<size_t>(next.downKBps));
    tr_sessionLimitSpeed(session_, TR_UP, next.limitUp);
    tr_sessionSetSpeedLimit_KBps(session_, TR_UP, static_cast<size_t>(next.upKBps));

    tr_sessionSetAltSpeed_KBps(session_, TR_DOWN, static_cast<size_t>(next.altDownKBps));
    tr_sessionSetAltSpeed_KBps(session_, TR_UP, static_cast<size_t>(next.altUpKBps));
    tr_sessionUseAltSpeedTime(session_, next.altScheduleOn);
    tr_sessionSetAltSpeedBegin(session_, static_cast<size_t>(next.altBeginMinutes));
    tr_sessionSetAltSpeedEnd(session_, static_cast<size_t>(next.altEndMinutes));
    tr_sessionSetAltSpeedDay(session_, static_cast<tr_sched_day>(next.altDays));
    if (!next.altScheduleOn)
    {
        tr_sessionUseAltSpeed(session_, next.altSpeedOn);
    }

    // Each of these rebinds a socket or restarts a subsystem, so only a real change gets through.
    if (next.peerPort != current.peerPort)
    {
        tr_sessionSetPeerPort(session_, static_cast<uint16_t>(next.peerPort));
    }
    tr_sessionSetPeerPortRandomOnStart(session_, next.randomPortOnStart);
    if (next.portForwarding != current.portForwarding)
    {
        tr_sessionSetPortForwardingEnabled(session_, next.portForwarding);
    }
    tr_sessionSetPeerLimit(session_, static_cast<uint16_t>(next.peerLimitGlobal));
    tr_sessionSetPeerLimitPerTorrent(session_, static_cast<uint16_t>(next.peerLimitPerTorrent));
    tr_sessionSetEncryption(session_, static_cast<tr_encryption_mode>(next.encryption));
    if (next.dht != current.dht)
    {
        tr_sessionSetDHTEnabled(session_, next.dht);
    }
    if (next.pex != current.pex)
    {
        tr_sessionSetPexEnabled(session_, next.pex);
    }
    if (next.lpd != current.lpd)
    {
        tr_sessionSetLPDEnabled(session_, next.lpd);
    }
    if (next.utp != current.utp)
    {
        tr_sessionSetUTPEnabled(session_, next.utp);
    }

    tr_sessionSetQueueEnabled(session_, TR_DOWN, next.downloadQueueOn);
    tr_sessionSetQueueSize(session_, TR_DOWN, static_cast<size_t>(next.downloadQueueSize));
    tr_sessionSetQueueEnabled(session_, TR_UP, next.seedQueueOn);
    tr_sessionSetQueueSize(session_, TR_UP, static_cast<size_t>(next.seedQueueSize));
    tr_sessionSetQueueStalledEnabled(session_, next.stalledOn);
    tr_sessionSetQueueStalledMinutes(session_, next.stalledMinutes);

    tr_sessionSetRatioLimited(session_, next.ratioLimitOn);
    tr_sessionSetRatioLimit(session_, next.ratioLimit);
    tr_sessionSetIdleLimited(session_, next.idleLimitOn);
    tr_sessionSetIdleLimit(session_, static_cast<uint16_t>(next.idleMinutes));

    tr_blocklistSetEnabled(session_, next.blocklistOn);
    tr_blocklistSetURL(session_, narrow(next.blocklistUrl).c_str());

    tr_sessionSetDownloadDir(session_, narrow(next.downloadFolder).c_str());
    tr_sessionSetIncompleteDir(session_, narrow(next.incompleteFolder).c_str());
    tr_sessionSetIncompleteDirEnabled(session_, next.incompleteFolderOn);
    tr_sessionSetIncompleteFileNamingEnabled(session_, next.renamePartialFiles);
    tr_sessionSetCompleteVerifyEnabled(session_, next.verifyOnCompletion);
    tr_sessionSetPaused(session_, !next.startAddedTransfers);

    saveSessionSettings();
}

AppPrefs const& TransmissionEngine::prefs() const
{
    return prefs_;
}

void TransmissionEngine::setPrefs(AppPrefs const& prefs)
{
    prefs_ = prefs;
    savePrefs(configDir_ + kPrefsFile, prefs_);
}

void TransmissionEngine::setAltSpeed(bool on)
{
    if (session_ != nullptr)
    {
        tr_sessionUseAltSpeed(session_, on);
        dirty_ = true;
    }
}

void TransmissionEngine::updateBlocklist(Done done)
{
    if (session_ == nullptr)
    {
        return;
    }
    rpcAsync(R"({"jsonrpc":"2.0","id":1,"method":"blocklist_update"})", TR_KEY_blocklist_update, std::move(done));
}

size_t TransmissionEngine::blocklistRuleCount()
{
    return session_ != nullptr ? tr_blocklistGetRuleCount(session_) : 0;
}

void TransmissionEngine::testPort(Done done)
{
    if (session_ == nullptr)
    {
        return;
    }
    rpcAsync(R"({"jsonrpc":"2.0","id":1,"method":"port_test"})", TR_KEY_port_is_open, std::move(done));
}

int64_t TransmissionEngine::freeDiskSpace(std::wstring const& path)
{
    auto available = ULARGE_INTEGER{};
    if (!GetDiskFreeSpaceExW(extendedPath(path).c_str(), &available, nullptr, nullptr))
    {
        return -1;
    }
    return static_cast<int64_t>(available.QuadPart);
}

size_t TransmissionEngine::importFromQBittorrent(std::wstring const& backupFolder)
{
    if (session_ == nullptr)
    {
        return 0;
    }

    auto const folders = backupFolder.empty() ? qbittorrentBackupFolders() : std::vector<std::wstring>{ backupFolder };

    size_t added = 0;
    for (auto const& folder : folders)
    {
        for (auto const& entry : readQBittorrentEntries(folder))
        {
            auto const preview = parseTorrentFile(entry.torrentPath);
            if (!preview.valid || preview.duplicateOf != kNoTransfer)
            {
                continue;
            }

            auto request = AddRequest{};
            request.destination = entry.savePath;
            request.startPaused = true;
            // Stays paused and add() checks the files that are there, so nothing downloads again.
            if (add(preview, request).id != kNoTransfer)
            {
                ++added;
            }
        }
    }

    prefs_.qbittorrentImported = true;
    savePrefs(configDir_ + kPrefsFile, prefs_);
    return added;
}

std::vector<SearchSource> const& TransmissionEngine::sources()
{
    sources_ = SearchSource::allSources(searchSourcesFolder());
    return sources_;
}

std::vector<std::wstring> TransmissionEngine::searchSourceNames()
{
    auto names = std::vector<std::wstring>{};
    for (auto const& source : sources())
    {
        names.push_back(source.name());
    }
    return names;
}

std::wstring TransmissionEngine::searchSourcesFolder()
{
    return SearchSource::sourcesFolder(configDir_);
}

void TransmissionEngine::search(std::wstring const& query, int sourceIndex, std::function<void(SearchOutcome const&)> done)
{
    auto chosen = sources();
    if (sourceIndex >= 0 && static_cast<size_t>(sourceIndex) < chosen.size())
    {
        chosen = { chosen[static_cast<size_t>(sourceIndex)] };
    }
    search_.search(std::move(chosen), query, std::move(done));
}

void TransmissionEngine::cancelSearch()
{
    search_.cancelSearch();
}

void TransmissionEngine::fetchTorrent(std::wstring const& url, std::function<void(std::string, std::wstring const&)> done)
{
    search_.fetch(url, std::move(done));
}

} // namespace

std::unique_ptr<Engine> createEngine()
{
    return std::make_unique<TransmissionEngine>();
}

std::unique_ptr<Engine> createEngineFromCommandLine()
{
    auto argc = 0;
    auto** const argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    auto demo = false;
    auto light = false;
    for (auto i = 1; i < argc; ++i)
    {
        auto const argument = std::wstring_view{ argv[i] };
        demo |= argument == L"--demo";
        light |= argument == L"--demo-light";
    }
    LocalFree(argv);
    return demo || light ? createDemoEngine(light) : createEngine();
}

} // namespace engine
