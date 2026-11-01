#pragma once

#include <windows.h>

#include <array>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace engine
{

// Threading contract
// ------------------
// Every Engine method must be called on the UI thread, the one that called start(). Nothing here
// blocks on the network. Most calls take libtransmission's session lock for a few microseconds.
//
// libtransmission's callbacks arrive on its own thread. They only queue work and then signal the
// UI in one of two ways: StartOptions::wake, or a PostMessageW to notifyWindow. On that signal the
// UI calls pump(), which runs everything queued, so onEvent and all `done` callbacks run on the UI
// thread, inside pump(). The signal coalesces, so one pump() always drains the whole queue.

using TransferId = int;

inline constexpr TransferId kNoTransfer = 0;
inline constexpr int kDefaultDotRows = 5;
inline constexpr int kOverviewDotRows = 20;
inline constexpr size_t kSwellPoints = 22;
inline constexpr UINT kDefaultNotifyMessage = WM_APP + 77;
/// etaSeconds when there is no estimate. Sorts last, as on the Mac.
inline constexpr int64_t kEtaUnknown = std::numeric_limits<int64_t>::max();

/// DropletState on the Mac, same order.
enum class TransferState : uint8_t
{
    None,
    Download,
    Fetching,
    Verifying,
    Seeding,
    Paused,
    Finished,
    Problem,
};

enum class Priority : int8_t
{
    Low = -1,
    Normal = 0,
    High = 1,
};

/// tr_ratiolimit and tr_idlelimit share these values.
enum class LimitMode : uint8_t
{
    Global = 0,
    Single = 1,
    Unlimited = 2,
};

enum class EncryptionMode : uint8_t
{
    Allowed = 0,
    Preferred = 1,
    Required = 2,
};

enum class QueueMove : uint8_t
{
    Top,
    Up,
    Down,
    Bottom,
};

enum class TrackerState : uint8_t
{
    Inactive,
    Waiting,
    Queued,
    Active,
};

enum class Mixed : uint8_t
{
    Off,
    On,
    Partly,
};

struct SessionStats
{
    double downBytesPerSecond = 0.0;
    double upBytesPerSecond = 0.0;
    int all = 0;
    int downloading = 0;
    int seeding = 0;
    int paused = 0;
    /// Only worth a pill when it is above zero.
    int problems = 0;
    /// Transferring right now.
    int active = 0;
    /// Running, not stalled and not in error: the Mac's reason to keep the machine awake.
    bool anyWorking = false;
    bool altSpeedOn = false;
};

/// One row. `name`, `hash` and `error` only reallocate when they change.
struct TransferInfo
{
    TransferId id = kNoTransfer;
    std::wstring name;
    std::wstring hash;
    TransferState state = TransferState::None;
    /// Empty when all is well. A tracker warning fills this without making the state Problem.
    std::wstring error;
    bool hasWarning = false;

    float percentDone = 0.0F;
    float percentComplete = 0.0F;
    float verifyProgress = 0.0F;
    float metadataProgress = 0.0F;

    uint64_t haveBytes = 0;
    uint64_t totalBytes = 0;
    uint64_t leftBytes = 0;
    uint64_t selectedBytes = 0;

    double downBytesPerSecond = 0.0;
    double upBytesPerSecond = 0.0;

    /// The most any tracker reports, or -1 if none has said.
    int seeds = -1;
    int peers = -1;
    int peersConnected = 0;

    /// May be negative when there is nothing to divide by: -1 is n/a, -2 is infinite.
    float ratio = 0.0F;
    /// The seed ratio target, or -1 when this transfer seeds without a ratio limit.
    double ratioStop = -1.0;
    /// tideForTorrent in DropletView: how full the seeding wave is, 0 to 1.
    float tide = 0.0F;

    /// Seconds, or kEtaUnknown. Only meaningful when showEta is true.
    int64_t etaSeconds = kEtaUnknown;
    bool showEta = false;

    int64_t addedDate = 0;
    int64_t activityDate = 0;
    int64_t doneDate = 0;
    size_t queuePosition = 0;

    int group = -1;
    std::vector<std::wstring> labels;

    bool isMagnet = false;
    bool isFolder = false;
    bool isStalled = false;
    bool isWaiting = false;
    bool isFinishedSeeding = false;
    bool removeWhenDone = false;
};

enum class DotInk : uint8_t
{
    Main,
    Fresh,
    Crest,
    Scan,
    Bad,
};

/// One dot in a droplet row, addressed column first: dots[column * rows + row].
/// `size` and `alpha` are exactly what DropletView draws. Fresh is a piece finished since the last
/// update, Crest the top of the seeding wave.
struct PieceDot
{
    float have = 0.0F;
    float size = 0.0F;
    float alpha = 0.0F;
    DotInk ink = DotInk::Main;

    bool operator==(PieceDot const&) const = default;
};

/// A flat depth-first tree: children follow their parent, and `next` skips a node's subtree.
struct FileNode
{
    std::wstring name;
    /// Full path inside the torrent, '/' separated. This is what renamePath takes.
    std::wstring path;
    bool isFolder = false;
    int depth = 0;
    /// Index of the node after this subtree, which is nodes.size() at the end.
    int next = 0;
    int parent = -1;
    uint64_t size = 0;
    uint64_t have = 0;
    Mixed wanted = Mixed::On;
    /// Meaningless when hasMixedPriority is true.
    Priority priority = Priority::Normal;
    bool hasMixedPriority = false;
    /// False once a file is fully downloaded, so its checkbox can't be cleared.
    bool canChangeWanted = true;
    /// Every file in this subtree.
    std::vector<int> fileIndexes;
};

struct FileTree
{
    std::vector<FileNode> nodes;
    size_t fileCount = 0;
};

struct PeerInfo
{
    std::wstring address;
    int port = 0;
    std::wstring client;
    std::wstring flags;
    float progress = 0.0F;
    double downBytesPerSecond = 0.0;
    double upBytesPerSecond = 0.0;
    bool isUtp = false;
    bool isEncrypted = false;
    bool isSeed = false;
    bool isIncoming = false;
    bool isDownloadingFrom = false;
    bool isUploadingTo = false;
    /// tr_peer_from: 0 incoming, 1 LPD, 2 tracker, 3 DHT, 4 PEX, 5 resume, 6 LTEP.
    int from = 0;
};

struct TrackerInfo
{
    uint32_t id = 0;
    int tier = 0;
    std::wstring announce;
    std::wstring host;
    std::wstring site;
    TrackerState announceState = TrackerState::Inactive;
    TrackerState scrapeState = TrackerState::Inactive;
    bool isBackup = false;
    /// -1 when unknown.
    int seeders = -1;
    int leechers = -1;
    int downloads = -1;
    bool hasAnnounced = false;
    bool lastAnnounceSucceeded = false;
    bool lastAnnounceTimedOut = false;
    std::wstring lastAnnounceResult;
    int lastAnnouncePeers = 0;
    int64_t lastAnnounceTime = 0;
    int64_t nextAnnounceTime = 0;
    bool hasScraped = false;
    bool lastScrapeSucceeded = false;
    std::wstring lastScrapeResult;
    int64_t lastScrapeTime = 0;
    int64_t nextScrapeTime = 0;
};

struct WebseedInfo
{
    std::wstring url;
    double downBytesPerSecond = 0.0;
    bool isDownloading = false;
};

/// The things the Overview and Activity screens show that the row doesn't.
struct TransferDetails
{
    std::wstring comment;
    std::wstring creator;
    std::wstring source;
    std::wstring downloadFolder;
    std::wstring currentFolder;
    std::wstring dataPath;
    std::wstring torrentFile;
    std::wstring magnetLink;
    int64_t dateCreated = 0;
    int64_t startDate = 0;
    uint32_t pieceSize = 0;
    uint32_t pieceCount = 0;
    size_t fileCount = 0;
    bool isPrivate = false;
    uint64_t haveVerifiedBytes = 0;
    uint64_t desiredAvailableBytes = 0;
    uint64_t downloadedEver = 0;
    uint64_t uploadedEver = 0;
    uint64_t corruptEver = 0;
    int64_t secondsDownloading = 0;
    int64_t secondsSeeding = 0;
    int peersSendingToUs = 0;
    int peersGettingFromUs = 0;
    int webseedsSendingToUs = 0;
    bool canReannounce = false;
    /// Indexed by the same `from` values as PeerInfo.
    std::array<int, 7> peersFrom{};
};

/// InfoOptionsViewController.
struct TransferOptions
{
    bool limitDown = false;
    int downKBps = 0;
    bool limitUp = false;
    int upKBps = 0;
    bool honorSessionLimits = true;
    LimitMode ratioMode = LimitMode::Global;
    double ratioLimit = 0.0;
    LimitMode idleMode = LimitMode::Global;
    int idleMinutes = 0;
    Priority priority = Priority::Normal;
    int peerLimit = 0;
    bool sequential = false;
    bool removeWhenDone = false;
};

/// Step one of the Add panel. Holds the metainfo so that step two needs nothing else.
struct AddPreview
{
    bool valid = false;
    std::wstring error;
    bool isMagnet = false;
    std::wstring name;
    std::wstring hash;
    uint64_t totalBytes = 0;
    FileTree files;
    /// The transfer this would duplicate, or kNoTransfer.
    TransferId duplicateOf = kNoTransfer;
    std::wstring comment;
    std::wstring creator;
    uint32_t pieceSize = 0;
    uint32_t pieceCount = 0;
    bool isPrivate = false;

    /// Opaque to the UI: the raw metainfo, or the magnet link in UTF-8.
    std::string source;
    /// The .torrent on disk this came from, when there is one for the panel to offer to bin.
    std::wstring torrentFile;
};

struct FileRename
{
    std::wstring path;
    std::wstring newName;
};

/// Step two. Empty `wanted` and `priorities` mean every file, at normal priority.
struct AddRequest
{
    std::wstring destination;
    /// Empty keeps the torrent's name. A magnet applies this once its metadata arrives.
    std::wstring nameOverride;
    std::vector<uint8_t> wanted;
    std::vector<Priority> priorities;
    /// Folder paths come from the preview, before any rename is applied. Ignored for magnets.
    std::vector<FileRename> renames;
    bool startPaused = false;
    int group = -1;
};

struct AddResult
{
    TransferId id = kNoTransfer;
    std::wstring error;
};

/// One struct, kept in settings.json through tr_sessionSaveSettings.
struct SessionSettings
{
    bool limitDown = false;
    int downKBps = 100;
    bool limitUp = false;
    int upKBps = 50;

    bool altSpeedOn = false;
    int altDownKBps = 10;
    int altUpKBps = 10;
    bool altScheduleOn = false;
    /// Minutes since midnight.
    int altBeginMinutes = 180;
    int altEndMinutes = 780;
    /// tr_sched_day bits, Sunday first. 127 is every day.
    int altDays = 127;

    int peerPort = 51413;
    bool randomPortOnStart = false;
    bool portForwarding = true;
    int peerLimitGlobal = 200;
    int peerLimitPerTorrent = 60;
    EncryptionMode encryption = EncryptionMode::Preferred;
    bool dht = true;
    bool pex = true;
    bool lpd = false;
    bool utp = true;

    bool downloadQueueOn = false;
    int downloadQueueSize = 3;
    bool seedQueueOn = false;
    int seedQueueSize = 3;
    bool stalledOn = false;
    int stalledMinutes = 30;

    bool ratioLimitOn = true;
    double ratioLimit = 2.0;
    bool idleLimitOn = false;
    int idleMinutes = 30;

    bool blocklistOn = false;
    std::wstring blocklistUrl;

    std::wstring downloadFolder;
    std::wstring incompleteFolder;
    bool incompleteFolderOn = false;
    bool renamePartialFiles = true;
    bool verifyOnCompletion = false;
    bool startAddedTransfers = true;
};

/// What the Mac keeps in NSUserDefaults, in a small JSON file beside settings.json.
struct AppPrefs
{
    /// 0 system, 1 light, 2 dark.
    int themeMode = 0;
    bool qbittorrentImported = false;
    bool askedToBeDefault = false;

    bool downloadAsk = true;
    bool downloadAskMulti = false;
    bool downloadAskManual = false;
    bool magnetOpenAsk = true;
    bool checkQuit = true;
    bool checkQuitDownloading = false;
    bool checkRemove = true;
    bool checkRemoveDownloading = false;
    bool openFinishedOnDoubleClick = false;
    bool deleteOriginalTorrent = false;
    bool warningRemainingSpace = true;
    bool removeWhenFinishSeeding = false;
    bool sleepPrevent = true;
    bool displayNotifications = true;
    bool playDownloadSound = true;
    bool playSeedingSound = false;
    bool blocklistAutoUpdate = false;

    bool watchFolderOn = false;
    std::wstring watchFolder;

    std::wstring sort = L"Order";
    bool sortReverse = false;
    std::wstring filter = L"None";
};

/// The swell header's 22 bucketed points, oldest first, in bytes per second.
struct SwellData
{
    std::array<float, kSwellPoints> down{};
    std::array<float, kSwellPoints> up{};
    float peak = 0.0F;
};

struct SearchResult
{
    std::wstring title;
    std::wstring link;
    std::wstring sourceName;
    uint64_t size = 0;
    /// -1 when the source doesn't report it.
    int64_t seeds = -1;
};

struct SearchOutcome
{
    /// Best seeded first. Empty if cancelled.
    std::vector<SearchResult> results;
    int responded = 0;
    int asked = 0;
    bool cancelled = false;
};

enum class EventType : uint8_t
{
    /// A transfer started, stopped or was added or removed. Refresh when convenient.
    TransfersChanged,
    /// A magnet's metadata arrived: its name, size and file tree are now real.
    MetadataReceived,
    /// `flag`: was running when it finished.
    DownloadFinished,
    DownloadRestarted,
    /// Hit its ratio or idle limit.
    SeedingFinished,
    /// `flag`: on. `flag2`: the user asked, rather than the schedule.
    AltSpeedChanged,
    /// A transfer the watch folder added. `text` is the file name.
    WatchFolderAdded,
    /// A rename that add() queued didn't land, though the add itself stands. `text` says why.
    AddRenameFailed,
};

struct Event
{
    EventType type = EventType::TransfersChanged;
    TransferId id = kNoTransfer;
    bool flag = false;
    bool flag2 = false;
    std::wstring text;
};

using Done = std::function<void(bool ok, std::wstring const& error)>;

struct StartOptions
{
    /// Empty means %LOCALAPPDATA%\Nagare.
    std::wstring configDir;
    /// Used when `wake` is empty: receives notifyMessage, and should call pump().
    HWND notifyWindow = nullptr;
    UINT notifyMessage = kDefaultNotifyMessage;
    /// Called from any thread, so it may only signal. The UI then calls pump() on its own thread.
    std::function<void()> wake;
    /// Runs on the UI thread, inside pump().
    std::function<void(Event const&)> onEvent;
    /// The Mac holds Option at launch to do this: every transfer starts paused.
    bool pauseOnStart = false;
};

class Engine
{
public:
    virtual ~Engine() = default;

    Engine() = default;
    Engine(Engine const&) = delete;
    Engine& operator=(Engine const&) = delete;

    // Lifecycle

    /// False if the session couldn't start.
    virtual bool start(StartOptions const& options) = 0;
    /// Saves everything and closes the session, waiting at most `maxSeconds`. Hide the window first,
    /// since this blocks. False means it ran out of time with libtransmission's thread still alive:
    /// everything is saved, and destroying this object then ends the process on the spot.
    virtual bool shutdown(int maxSeconds = 2) = 0;
    /// Runs queued callbacks. Call on the wake signal.
    virtual void pump() = 0;
    /// True when another process owns the config folder, so nothing here will be saved.
    [[nodiscard]] virtual bool isConfigContended() const = 0;

    // Snapshots

    /// Call from the UI's timer: every second when visible, every 5 seconds when hidden. Calls closer
    /// than a fifth of a second apart return the last snapshot, unless an action has changed
    /// something. The vector stays valid until the next call, and is sorted by id, not display order.
    virtual std::vector<TransferInfo> const& refresh() = 0;
    /// From the latest refresh().
    [[nodiscard]] virtual SessionStats sessionStats() const = 0;
    /// From the latest refresh(). Fills `out` and returns false if the sea is still.
    virtual bool swell(SwellData& out) = 0;
    /// Fills `count` dots. `out` must be the caller's own buffer of `count` dots, kept between calls:
    /// it returns false when `out` already matches, so the view can skip its repaint. Never allocates
    /// once a view's size has been seen. Pass trackFresh false for a second view of the same
    /// transfer, like Overview with kOverviewDotRows.
    virtual bool pieceDots(TransferId id, size_t count, PieceDot* out, int rows = kDefaultDotRows, bool trackFresh = true) = 0;
    /// tr_torrentAvailability: each byte is -1 for a piece we have, else how many peers have it.
    virtual void availability(TransferId id, int count, int8_t* out) = 0;

    // Detail screens: each fills `out`, reusing its capacity. Call only for what is on screen.

    virtual TransferDetails details(TransferId id) = 0;
    /// Builds the tree. Only needed after a rename or MetadataReceived.
    virtual FileTree files(TransferId id) = 0;
    /// Updates have, wanted and priority in place, for a tree from files().
    virtual void updateFiles(TransferId id, FileTree& tree) = 0;
    virtual void peers(TransferId id, std::vector<PeerInfo>& out) = 0;
    virtual void trackers(TransferId id, std::vector<TrackerInfo>& out) = 0;
    virtual void webseeds(TransferId id, std::vector<WebseedInfo>& out) = 0;

    // Files

    virtual void setFilesWanted(TransferId id, std::vector<int> const& fileIndexes, bool wanted) = 0;
    virtual void setFilesPriority(TransferId id, std::vector<int> const& fileIndexes, Priority priority) = 0;
    /// `path` is FileNode::path. Renaming the root renames the transfer. Refresh the tree afterwards.
    virtual void renamePath(TransferId id, std::wstring const& path, std::wstring const& newName, Done done) = 0;
    /// `done` runs when the data has finished moving, noticed by refresh().
    virtual void moveData(TransferId id, std::wstring const& folder, Done done) = 0;

    // Trackers

    /// Goes on a new tier. A missing scheme becomes http://, as on the Mac.
    virtual bool addTracker(TransferId id, std::wstring const& announce) = 0;
    virtual bool removeTrackers(TransferId id, std::vector<std::wstring> const& announces) = 0;
    virtual bool editTracker(TransferId id, std::wstring const& oldAnnounce, std::wstring const& newAnnounce) = 0;

    // Actions

    virtual void pause(std::vector<TransferId> const& ids) = 0;
    virtual void resume(std::vector<TransferId> const& ids) = 0;
    /// Skips the queue.
    virtual void resumeNow(std::vector<TransferId> const& ids) = 0;
    virtual void verify(std::vector<TransferId> const& ids) = 0;
    virtual void reannounce(std::vector<TransferId> const& ids) = 0;
    /// With deleteData the files go to the Recycle Bin, or are deleted where there isn't one.
    virtual void remove(std::vector<TransferId> const& ids, bool deleteData) = 0;
    virtual void moveInQueue(std::vector<TransferId> const& ids, QueueMove move) = 0;
    virtual void setGroup(TransferId id, int group) = 0;
    virtual void setLabels(TransferId id, std::vector<std::wstring> const& labels) = 0;

    // Per-transfer options

    [[nodiscard]] virtual TransferOptions options(TransferId id) = 0;
    virtual void setOptions(TransferId id, TransferOptions const& options) = 0;

    // Adding

    virtual AddPreview parseTorrentFile(std::wstring const& path) = 0;
    /// For a .torrent that came over the network, such as from fetchTorrent().
    virtual AddPreview parseTorrentData(std::string data) = 0;
    /// A magnet has no files until it connects, so `files` is empty and isMagnet is true.
    virtual AddPreview parseMagnet(std::wstring const& uri) = 0;
    virtual AddResult add(AddPreview const& preview, AddRequest const& request) = 0;

    // Settings

    [[nodiscard]] virtual SessionSettings settings() = 0;
    /// Applies and saves.
    virtual void applySettings(SessionSettings const& settings) = 0;
    [[nodiscard]] virtual AppPrefs const& prefs() const = 0;
    /// Saves.
    virtual void setPrefs(AppPrefs const& prefs) = 0;
    virtual void setAltSpeed(bool on) = 0;
    /// `done` runs when the download is applied, with ok false on failure.
    virtual void updateBlocklist(Done done) = 0;
    [[nodiscard]] virtual size_t blocklistRuleCount() = 0;
    /// Whether the peer port is reachable from outside. `done` says, through `ok`.
    virtual void testPort(Done done) = 0;
    /// Free bytes on the drive holding `path`, or -1 if unknown.
    [[nodiscard]] virtual int64_t freeDiskSpace(std::wstring const& path) = 0;

    // qBittorrent

    /// Adds what qBittorrent has on file, paused, with its data verified. Sets the imported pref and
    /// is safe to run twice. Returns how many were new. An empty folder means its usual places. It
    /// only ever reads there.
    virtual size_t importFromQBittorrent(std::wstring const& backupFolder = {}) = 0;

    // Search

    [[nodiscard]] virtual std::vector<std::wstring> searchSourceNames() = 0;
    [[nodiscard]] virtual std::wstring searchSourcesFolder() = 0;
    /// `sourceIndex` is a place in searchSourceNames(), or -1 for all. A new search replaces the last.
    virtual void search(std::wstring const& query, int sourceIndex, std::function<void(SearchOutcome const&)> done) = 0;
    virtual void cancelSearch() = 0;
    /// Downloads an https .torrent, for parseTorrentData. Bytes are empty on failure.
    virtual void fetchTorrent(std::wstring const& url, std::function<void(std::string bytes, std::wstring const& error)> done) = 0;
};

/// The real thing, on libtransmission.
[[nodiscard]] std::unique_ptr<Engine> createEngine();
/// Canned transfers from docs/nagare with no session, network or disk, for building the UI.
/// Plays hero.png, or light.png when `lightScene` is true. Its theme pref follows the picture.
[[nodiscard]] std::unique_ptr<Engine> createDemoEngine(bool lightScene = false);
/// A demo engine if the command line has --demo or --demo-light, else createEngine().
[[nodiscard]] std::unique_ptr<Engine> createEngineFromCommandLine();

/// %LOCALAPPDATA%\Nagare.
[[nodiscard]] std::wstring defaultConfigDir();

/// Runs the --engine-test checks in `folder`. Returns the process exit code.
int runEngineTest(std::wstring const& folder);

} // namespace engine
