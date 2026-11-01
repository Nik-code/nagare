#include <windows.h>

#include <psapi.h>
#include <shellapi.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include <libtransmission/transmission.h>

#include <libtransmission/error.h>
#include <libtransmission/makemeta.h>

#include "engine/engine.h"
#include "engine/qbittorrent_import.h"
#include "engine/search_source.h"
#include "engine/shared.h"

// nagare.exe --engine-test <folder> runs without a window and returns non-zero on failure, so it
// works over plain SSH. Each check writes one line to <folder>\engine-test.log.

namespace engine
{

namespace
{

constexpr double kVerifySeconds = 30.0;
constexpr double kEventSeconds = 20.0;
constexpr double kMetadataSeconds = 75.0;
constexpr double kProgressSeconds = 90.0;
constexpr double kSearchSeconds = 40.0;
constexpr double kShutdownSeconds = 8.0;
constexpr double kIdleSettleSeconds = 5.0;
constexpr double kIdleCpuSeconds = 10.0;
constexpr uint64_t kWantedBytes = 2U * 1024U * 1024U;
constexpr size_t kDotCount = 250;
constexpr size_t kFirstFileBytes = 1500 * 1024;
constexpr size_t kSecondFileBytes = 700 * 1024;
constexpr int kChurnRounds = 60;
constexpr int kManyTransfers = 100;
constexpr int kDeepLevels = 12;

wchar_t const* const kSintelMagnet =
    L"magnet:?xt=urn:btih:08ada5a7a6183aae1e09d831df6748d566095a10&dn=Sintel"
    L"&tr=udp%3A%2F%2Ftracker.opentrackr.org%3A1337%2Fannounce"
    L"&tr=udp%3A%2F%2Ftracker.torrent.eu.org%3A451%2Fannounce"
    L"&tr=udp%3A%2F%2Fexplodie.org%3A6969"
    L"&ws=https%3A%2F%2Fwebtorrent.io%2Ftorrents%2F";
wchar_t const* const kSintelTorrent = L"https://webtorrent.io/torrents/sintel.torrent";

class Log
{
public:
    explicit Log(std::wstring const& path)
    {
        if (_wfopen_s(&file_, path.c_str(), L"wb") != 0)
        {
            file_ = nullptr;
        }
    }

    ~Log()
    {
        if (file_ != nullptr)
        {
            std::fclose(file_);
        }
    }

    Log(Log const&) = delete;
    Log& operator=(Log const&) = delete;

    void line(std::string const& text)
    {
        if (file_ != nullptr)
        {
            std::fprintf(file_, "%s\r\n", text.c_str());
            std::fflush(file_);
        }
    }

    void check(bool condition, std::string const& name)
    {
        line((condition ? "ok   " : "FAIL ") + name);
        failures_ += condition ? 0 : 1;
    }

    [[nodiscard]] int failures() const
    {
        return failures_;
    }

private:
    std::FILE* file_ = nullptr;
    int failures_ = 0;
};

std::string formatMib(uint64_t bytes)
{
    auto buffer = std::array<char, 32>{};
    std::snprintf(buffer.data(), buffer.size(), "%.1f MiB", static_cast<double>(bytes) / (1024.0 * 1024.0));
    return buffer.data();
}

void removeTree(std::wstring const& path)
{
    auto from = path;
    from.push_back(L'\0');
    auto operation = SHFILEOPSTRUCTW{};
    operation.wFunc = FO_DELETE;
    operation.pFrom = from.c_str();
    operation.fFlags = FOF_NO_UI;
    SHFileOperationW(&operation);
}

bool exists(std::wstring const& path)
{
    return GetFileAttributesW(extendedPath(path).c_str()) != INVALID_FILE_ATTRIBUTES;
}

bool writePatternFile(std::wstring const& path, size_t size, uint32_t seed)
{
    auto data = std::string(size, '\0');
    auto value = seed;
    for (auto& byte : data)
    {
        value = value * 1664525U + 1013904223U;
        byte = static_cast<char>(value >> 24);
    }
    return writeFileAtomic(path, data);
}

void reportMemory(Log& log, std::string const& label)
{
    auto counters = PROCESS_MEMORY_COUNTERS_EX2{};
    counters.cb = sizeof(counters);
    GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof(counters));
    log.line("info " + label + ": private working set " + formatMib(counters.PrivateWorkingSetSize) + ", working set " + formatMib(counters.WorkingSetSize) + ", commit " +
        formatMib(counters.PrivateUsage));
}

/// Process CPU time so far, in seconds, across all threads.
double cpuSeconds()
{
    auto created = FILETIME{};
    auto exited = FILETIME{};
    auto kernel = FILETIME{};
    auto user = FILETIME{};
    GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user);
    auto const seconds = [](FILETIME const& time) { return static_cast<double>(ULARGE_INTEGER{ { time.dwLowDateTime, time.dwHighDateTime } }.QuadPart) * 1e-7; };
    return seconds(kernel) + seconds(user);
}

std::string buildTorrent(std::wstring const& path)
{
    auto builder = tr_metainfo_builder{ narrow(path) };
    if (builder.make_checksums().get())
    {
        return {};
    }
    return builder.benc();
}

/// The UI's side of the wake signal, for a test with no window.
class Driver
{
public:
    Driver()
        : wake_{ CreateEventW(nullptr, FALSE, FALSE, nullptr) }
    {
    }

    ~Driver()
    {
        CloseHandle(wake_);
    }

    Driver(Driver const&) = delete;
    Driver& operator=(Driver const&) = delete;

    [[nodiscard]] StartOptions options(std::wstring const& configDir)
    {
        auto options = StartOptions{};
        options.configDir = configDir;
        options.wake = [wake = wake_] { SetEvent(wake); };
        options.onEvent = [this](Event const& event) { events.push_back(event); };
        return options;
    }

    bool waitFor(Engine& engine, double seconds, std::function<bool()> const& condition)
    {
        auto const deadline = monotonicSeconds() + seconds;
        for (;;)
        {
            engine.pump();
            engine.refresh();
            if (condition())
            {
                return true;
            }
            if (monotonicSeconds() >= deadline)
            {
                return false;
            }
            WaitForSingleObject(wake_, 250);
        }
    }

    void wait(Engine& engine, double seconds)
    {
        waitFor(engine, seconds, [] { return false; });
    }

    [[nodiscard]] bool saw(EventType type, TransferId id) const
    {
        return std::any_of(events.begin(), events.end(), [type, id](Event const& event) { return event.type == type && event.id == id; });
    }

    std::vector<Event> events;

private:
    HANDLE wake_ = nullptr;
};

TransferInfo const* rowFor(Engine& engine, TransferId id)
{
    auto const& rows = engine.refresh();
    auto const it = std::find_if(rows.begin(), rows.end(), [id](TransferInfo const& info) { return info.id == id; });
    return it != rows.end() ? &*it : nullptr;
}

std::string quote(std::string const& text)
{
    auto out = std::string{};
    for (auto const c : text)
    {
        if (c == '"' || c == '\\')
        {
            out += '\\';
        }
        out += c;
    }
    return "\"" + out + "\"";
}

void testSearch(Log& log)
{
    log.line("-- search sources");

    constexpr auto kBase = R"({"name":"Test","url":"https://example.com/s?q={query}","results":"data.items","link":"{link}","title":"{title}"})";

    log.check(!SearchSource::fromJson(R"({"name":"Test","url":"http://example.com/s?q={query}","results":"data.items","link":"{link}"})"), "rejects a source over plain http");
    log.check(!SearchSource::fromJson(R"({"name":"Test","url":"https://example.com/fixed","results":"data.items","link":"{link}"})"), "rejects a source that can't take a query");

    auto const source = SearchSource::fromJson(kBase);
    log.check(source.has_value(), "accepts a complete source");
    if (!source)
    {
        return;
    }
    log.check(source->urlForQuery(L"a b&c=d") == "https://example.com/s?q=a%20b%26c%3Dd", "encodes the query");

    auto const links = source->resultsFromData(R"json({"data":{"items":[
        {"title":"file","link":"file:///etc/passwd"},
        {"title":"script","link":"javascript:alert(1)"},
        {"title":"http","link":"http://example.com/a.torrent"},
        {"title":"magnet","link":"magnet:?xt=urn:btih:abc"},
        {"title":"https","link":"https://example.com/a.torrent"},
        {"title":"missing"},
        "not a dictionary"
    ]}})json");
    log.check(links.size() == 2 && links[0].title == L"magnet" && links[1].title == L"https", "keeps only magnet and https links");

    auto const nested = SearchSource::fromJson(R"({"name":"Nested","url":"https://example.com/s?q={query}","results":"data.movies","items":"torrents",
        "title":"{title} {quality}","size":"size","seeds":"seeds","link":"magnet:?xt=urn:btih:{hash}&dn={title}"})");
    log.check(nested.has_value(), "accepts a nested source");
    if (!nested)
    {
        return;
    }
    auto const movies = nested->resultsFromData(R"({"data":{"movies":[
        {"title":"A & B","torrents":[{"quality":"1080p","hash":"ff","size":"1.5 GB","seeds":42}]}
    ]}})");
    log.check(movies.size() == 1, "reads results nested inside results");
    log.check(!movies.empty() && movies[0].title == L"A & B 1080p", "titles take fields from item and parent");
    log.check(!movies.empty() && movies[0].link == L"magnet:?xt=urn:btih:ff&dn=A%20%26%20B", "encodes values inside a link");
    log.check(!movies.empty() && movies[0].size == 1500000000ULL && movies[0].seeds == 42, "parses size text and seeds");

    log.check(source->resultsFromData("not json").empty(), "survives invalid JSON");
    log.check(source->resultsFromData(std::string(9U * 1024U * 1024U, '\0')).empty(), "refuses oversized responses");

    // Beyond the Mac's cases: a hostile document must neither crash nor run away.
    log.check(source->resultsFromData(std::string(200000, '[')).empty(), "survives very deep nesting");
    auto const tricky = source->resultsFromData(R"({"data":{"items":[
        {"title":"backslash","link":"https://example.com\\@evil.test/a.torrent"},
        {"title":"space","link":"https://example.com/a b.torrent"},
        {"title":"nohost","link":"https:///a.torrent"},
        {"title":"nohost2","link":"https://:443/a.torrent"},
        {"title":"upper","link":"HTTPS://EXAMPLE.COM/a.torrent"},
        {"title":"","link":"magnet:?xt=urn:btih:abc"}
    ]}})");
    log.check(tricky.size() == 1 && tricky[0].title == L"upper", "drops links a browser might read differently");

    auto const rootArray = SearchSource::fromJson(R"({"name":"Root","url":"https://example.com/?q={query}","results":"","link":"magnet:?xt=urn:btih:{h}","title":"{t}","seeds":"s","size":"z"})");
    auto const list = rootArray ? rootArray->resultsFromData(R"([{"t":"x","h":"aa","s":"17 seeds","z":2048},{"t":"y","h":"bb","z":"3 KiB"}])") : std::vector<SearchResult>{};
    log.check(list.size() == 2 && list[0].seeds == 17 && list[0].size == 2048 && list[1].seeds == -1 && list[1].size == 3072, "reads a bare array, numbers and unit text");

    auto many = std::string{ R"({"data":{"items":[)" };
    for (auto i = 0; i < 500; ++i)
    {
        many += i > 0 ? "," : "";
        many += R"({"title":"t","link":"magnet:?xt=urn:btih:abc"})";
    }
    many += "]}}";
    log.check(source->resultsFromData(many).size() == 200, "caps results at 200");

    auto const builtIn = SearchSource::allSources(L"C:\\nagare-no-such-folder");
    log.check(builtIn.size() == 1 && builtIn[0].name() == L"Internet Archive", "has the built-in source");
    log.check(!builtIn.empty() && builtIn[0].urlForQuery(L"sintel").find("title:(sintel)") != std::string::npos, "builds the Internet Archive query");
}

void testShared(Log& log)
{
    log.line("-- shared helpers");

    auto history = SwellHistory{};
    auto data = SwellData{};
    log.check(!history.buckets(0.0, data), "an empty swell is still water");
    constexpr double kBase = 10000.0;
    for (auto i = 0; i < 1000; ++i)
    {
        history.record(kBase + i, i >= 700 ? 100.0 : 0.0, 5.0);
    }
    log.check(history.buckets(kBase + 999.0, data) && data.down[kSwellPoints - 1] == 100.0F, "newest samples land in the last bucket");
    log.check(data.down[0] == 0.0F, "samples older than ten minutes fall out");
    log.check(data.up[5] == 5.0F, "buckets average rather than add");

    auto const files = std::vector<FlatFile>{
        { "Root/sub/b.bin", 10 },
        { "Root/a.bin", 20 },
        { "Root/README.txt", 1 },
        { "Root/readme.TXT", 2 },
        { "Root/file10.txt", 3 },
        { "Root/file2.txt", 4 },
    };
    auto tree = buildFileTree(files, true);
    auto const states = std::vector<FileState>{ { 5, 10, true, Priority::High }, { 20, 20, true, Priority::Normal }, { 0, 1, false, Priority::Normal }, { 0, 2, true, Priority::Normal }, { 0, 3, true, Priority::Normal }, { 0, 4, true, Priority::Normal } };
    rollUpFileState(tree, states);
    log.check(tree.nodes.size() == 7 && tree.nodes[0].isFolder && tree.nodes[0].name == L"sub" && tree.nodes[0].path == L"Root/sub", "folders come first and paths include the root");
    log.check(tree.nodes.size() == 7 && tree.nodes[0].next == 2 && tree.nodes[1].parent == 0 && tree.nodes[0].size == 10 && tree.nodes[0].have == 5, "folders roll up their children");
    auto names = std::vector<std::wstring>{};
    for (auto const& node : tree.nodes)
    {
        names.push_back(node.name);
    }
    log.check(std::find(names.begin(), names.end(), L"README.txt") != names.end() && std::find(names.begin(), names.end(), L"readme.TXT") != names.end(), "names differing only in case stay two files");
    auto const two = std::find(names.begin(), names.end(), L"file2.txt");
    auto const ten = std::find(names.begin(), names.end(), L"file10.txt");
    log.check(two < ten, "numbers sort naturally");

    // Deep enough that the nodes vector has to grow while a folder's children are being flattened.
    auto wide = std::vector<FlatFile>{};
    for (auto i = 0; i < 50; ++i)
    {
        wide.push_back({ "Root/folder/inner/file" + std::to_string(i) + ".bin", 1 });
    }
    auto const grown = buildFileTree(wide, true);
    log.check(!grown.nodes.empty() && grown.nodes.back().path.starts_with(L"Root/folder/inner/file"), "paths survive the tree growing");

    auto single = buildFileTree(std::vector<FlatFile>{ { "movie.mp4", 9 } }, false);
    log.check(single.nodes.size() == 1 && single.nodes[0].name == L"movie.mp4" && single.nodes[0].path == L"movie.mp4", "a single file is one node");

    auto dots = std::vector<PieceDot>(25);
    auto input = DotInput{};
    input.columns = 5;
    input.rows = 5;
    input.state = TransferState::Finished;
    fillDots(input, dots);
    log.check(dots[0].size == 5.0F && dots[0].alpha == 0.55F, "finished dots match DropletView");
    input.state = TransferState::Verifying;
    input.verifyProgress = 0.5F;
    fillDots(input, dots);
    log.check(dots[10].ink == DotInk::Scan && dots[5].size == 6.0F && dots[15].size == 3.0F, "the verify sweep matches DropletView");
    input.state = TransferState::Seeding;
    input.tide = 0.5F;
    fillDots(input, dots);
    log.check(std::any_of(dots.begin(), dots.end(), [](PieceDot const& dot) { return dot.ink == DotInk::Crest; }), "a seeding wave has a crest");

    auto const finished = std::vector<float>{ 1.0F, 1.0F, 0.5F, 0.0F };
    auto const previous = std::vector<uint8_t>{ 1, 0, 0, 0 };
    auto done = std::vector<uint8_t>(4);
    auto pieces = std::vector<PieceDot>(4);
    auto const anyFresh = fillPieceDots(finished, previous, true, done, pieces);
    log.check(anyFresh && pieces[0].ink == DotInk::Main && pieces[1].ink == DotInk::Fresh, "only newly finished pieces are fresh");
    log.check(pieces[2].size == 4.5F && pieces[3].size == 3.0F && std::fabs(pieces[2].alpha - 0.65F) < 0.001F, "part-done pieces scale as in DropletView");

    log.check(tideFor(0.25F, 9.0F) == 0.25F, "the tide follows the ratio target when there is one");
    log.check(tideFor(1.0F, 1.0F) == 0.5F && tideFor(0.0F, -1.0F) == 0.0F && tideFor(0.0F, 7.0F) == 1.0F, "otherwise it follows the default goal, clamped");

    auto const longPath = std::wstring{ L"C:\\" } + std::wstring(300, L'a') + L"\\b";
    log.check(extendedPath(longPath).starts_with(L"\\\\?\\C:\\"), "long paths get the extended prefix");
    log.check(widen(narrow(L"caf\u00e9 \u6d41")) == L"caf\u00e9 \u6d41", "UTF-8 round trips");
}

void testFastresume(Log& log, std::wstring const& folder)
{
    log.line("-- qBittorrent parser");

    auto const parse = [](std::string const& data) { return parseFastresume(data); };
    log.check(parse("d9:save_path11:C:/Torrents6:pausedi0ee") == L"C:\\Torrents", "reads save_path and flips its slashes");
    log.check(parse("d12:qBt-savePath8:D:/Mine/9:save_path5:C:/x/e") == L"D:\\Mine\\", "prefers qBt-savePath");
    log.check(parse("d12:qBt-savePath0:9:save_path5:C:/x/e") == L"C:\\x\\", "falls back when qBt-savePath is empty");
    log.check(parse("d4:infod9:save_path5:C:/x/ee").empty(), "ignores nested save paths");
    log.check(parse("d9:save_path5:C:/x").empty(), "rejects truncated data");
    log.check(parse("d9:save_path99999:C:/x/e").empty(), "rejects a string longer than the file");
    log.check(parse("i42e").empty(), "rejects a non-dictionary");
    log.check(parse(std::string(100, 'l') + std::string(100, 'e')).empty(), "rejects absurd nesting");
    log.check(parse("").empty(), "rejects an empty file");

    auto const backup = folder + L"\\qbt-parse";
    CreateDirectoryW(backup.c_str(), nullptr);
    writeFileAtomic(backup + L"\\good.fastresume", "d9:save_path5:C:/x/e");
    writeFileAtomic(backup + L"\\good.torrent", "d4:infod6:lengthi1eee");
    writeFileAtomic(backup + L"\\magnet.fastresume", "d9:save_path5:C:/x/e");
    writeFileAtomic(backup + L"\\broken.fastresume", "not bencode");
    writeFileAtomic(backup + L"\\broken.torrent", "x");
    auto const entries = readQBittorrentEntries(backup);
    log.check(entries.size() == 1 && entries[0].torrentPath.ends_with(L"good.torrent") && entries[0].savePath == L"C:\\x\\", "skips magnets with no .torrent and broken files");
    log.check(readQBittorrentEntries(folder + L"\\qbt-none").empty(), "copes with a folder that isn't there");
}

void testDemo(Log& log)
{
    log.line("-- demo engine");

    auto engine = createDemoEngine(false);
    auto driver = Driver{};
    engine->start(driver.options({}));
    auto const& rows = engine->refresh();
    auto const stats = engine->sessionStats();
    log.check(rows.size() == 4 && rows[0].name == L"Sintel" && rows[3].name == L"Cosmos Laundromat", "the hero scene has its four transfers");
    log.check(std::fabs(stats.downBytesPerSecond - 16616700.0) < 1000.0, "the hero scene sums to its header speed");
    log.check(stats.downloading == 4 && stats.seeding == 0, "the hero scene fills the pills");

    auto swell = SwellData{};
    log.check(engine->swell(swell) && swell.peak > 0.0F, "the demo swell has history");

    auto dots = std::vector<PieceDot>(kDotCount);
    log.check(engine->pieceDots(rows[0].id, dots.size(), dots.data(), kDefaultDotRows, true), "demo dots paint");

    auto light = createDemoEngine(true);
    light->start(driver.options({}));
    auto const& lightRows = light->refresh();
    log.check(lightRows.size() == 5 && lightRows[0].state == TransferState::Seeding && lightRows[4].name.starts_with(L"The WIRED CD"), "the light scene has its five transfers");
    log.check(light->sessionStats().seeding == 3 && light->sessionStats().downloading == 2, "the light scene fills the pills");
    log.check(light->prefs().themeMode == 1, "the light scene asks for the light theme");

    auto searched = false;
    light->search(L"sintel", -1, [&searched](SearchOutcome const& outcome) { searched = outcome.results.size() == 6; });
    log.check(driver.waitFor(*light, 2.0, [&searched] { return searched; }), "demo search answers through pump");
}

/// Everything that needs a session but no swarm, on a torrent built here from generated bytes.
void testLocalTorrent(Log& log, Engine& engine, Driver& driver, std::wstring const& folder)
{
    log.line("-- local torrent");

    // Not ASCII on purpose: every path crosses UTF-8 on its way through libtransmission.
    auto const seedFolder = folder + L"\\seed-\u6d41\u308c";
    auto const root = seedFolder + L"\\NagareTest";
    CreateDirectoryW(seedFolder.c_str(), nullptr);
    CreateDirectoryW(root.c_str(), nullptr);
    CreateDirectoryW((root + L"\\sub").c_str(), nullptr);
    writePatternFile(root + L"\\a.bin", kFirstFileBytes, 1);
    writePatternFile(root + L"\\sub\\b.bin", kSecondFileBytes, 2);

    auto builder = tr_metainfo_builder{ narrow(root) };
    auto const checksumError = builder.make_checksums().get();
    auto error = tr_error{};
    auto const benc = builder.benc(&error);
    log.check(!checksumError && !benc.empty(), "builds a test torrent");
    auto const torrentPath = folder + L"\\NagareTest.torrent";
    writeFileAtomic(torrentPath, benc);

    auto const preview = engine.parseTorrentFile(torrentPath);
    log.check(preview.valid && preview.name == L"NagareTest", "parses the torrent file");
    log.check(preview.files.fileCount == 2 && preview.totalBytes == kFirstFileBytes + kSecondFileBytes, "the preview has the size and files");
    log.check(preview.files.nodes.size() == 3 && preview.files.nodes[0].isFolder, "the preview has a tree");
    log.check(preview.duplicateOf == kNoTransfer, "it isn't a duplicate yet");
    log.check(!engine.parseTorrentData("nonsense").valid, "refuses a corrupt torrent");
    log.check(!engine.parseMagnet(L"magnet:?nothing").valid, "refuses a bad magnet");

    auto request = AddRequest{};
    request.destination = seedFolder;
    auto const added = engine.add(preview, request);
    log.check(added.id != kNoTransfer, "adds the transfer");
    auto const id = added.id;
    if (id == kNoTransfer)
    {
        return;
    }

    log.check(driver.waitFor(engine, kVerifySeconds, [&engine, id] { auto const* row = rowFor(engine, id); return row != nullptr && row->state == TransferState::Seeding; }), "verifies existing data and seeds it");
    auto const* row = rowFor(engine, id);
    log.check(row != nullptr && row->percentDone == 1.0F && row->leftBytes == 0, "progress is complete");
    log.check(row != nullptr && row->name == L"NagareTest" && row->hash == preview.hash, "the row has the name and hash");
    log.check(engine.parseTorrentFile(torrentPath).duplicateOf == id, "a second parse reports the duplicate");
    log.check(engine.add(preview, request).id == kNoTransfer, "a second add is refused");

    // The torrent is smaller than kDotCount blocks, which is the divide-by-zero case in the library.
    auto dots = std::vector<PieceDot>(kDotCount);
    engine.pause({ id });
    driver.waitFor(engine, kEventSeconds, [&engine, id] { auto const* r = rowFor(engine, id); return r != nullptr && r->state == TransferState::Finished; });
    row = rowFor(engine, id);
    log.check(row != nullptr && row->state == TransferState::Finished, "pause stops it, and a complete transfer shows finished");
    log.check(engine.pieceDots(id, dots.size(), dots.data(), kDefaultDotRows, true), "first dots paint");
    log.check(!engine.pieceDots(id, dots.size(), dots.data(), kDefaultDotRows, true), "unchanged dots report no change");
    log.check(std::all_of(dots.begin(), dots.end(), [](PieceDot const& dot) { return std::isfinite(dot.size); }), "every dot is a real number");

    engine.resume({ id });
    driver.waitFor(engine, kEventSeconds, [&engine, id] { auto const* r = rowFor(engine, id); return r != nullptr && r->state == TransferState::Seeding; });
    row = rowFor(engine, id);
    log.check(row != nullptr && row->state == TransferState::Seeding, "resume restarts it");
    log.check(engine.pieceDots(id, dots.size(), dots.data(), kDefaultDotRows, true), "a state change repaints");
    log.check(std::any_of(dots.begin(), dots.end(), [](PieceDot const& dot) { return dot.ink == DotInk::Crest; }), "a seeding transfer shows a crest");

    auto tree = engine.files(id);
    log.check(tree.nodes.size() == 3, "the file tree is there");
    auto indexes = std::vector<int>{};
    for (auto const& node : tree.nodes)
    {
        if (!node.isFolder && node.name == L"a.bin")
        {
            indexes = node.fileIndexes;
        }
    }
    engine.setFilesPriority(id, indexes, Priority::High);
    engine.updateFiles(id, tree);
    auto const& highNode = *std::find_if(tree.nodes.begin(), tree.nodes.end(), [](FileNode const& node) { return node.name == L"a.bin"; });
    log.check(highNode.priority == Priority::High, "a file priority change sticks");
    engine.setFilesPriority(id, indexes, Priority::Normal);

    auto options = engine.options(id);
    options.limitDown = true;
    options.downKBps = 1234;
    options.ratioMode = LimitMode::Single;
    options.ratioLimit = 3.5;
    options.idleMode = LimitMode::Unlimited;
    options.priority = Priority::Low;
    options.peerLimit = 33;
    options.sequential = true;
    options.honorSessionLimits = false;
    engine.setOptions(id, options);
    auto const back = engine.options(id);
    log.check(back.limitDown && back.downKBps == 1234 && back.ratioMode == LimitMode::Single && back.ratioLimit == 3.5 && back.idleMode == LimitMode::Unlimited, "limits round trip");
    log.check(back.priority == Priority::Low && back.peerLimit == 33 && !back.honorSessionLimits, "priority and peer limit round trip");
    log.check(back.sequential, "sequential download round trips");
    row = rowFor(engine, id);
    log.check(row != nullptr && row->ratioStop == 3.5, "the row reflects the ratio target");

    log.check(engine.addTracker(id, L"tracker.invalid/announce"), "adds a tracker, with a default scheme");
    log.check(engine.addTracker(id, L"http://tracker2.invalid/announce"), "adds a second tracker");
    auto trackers = std::vector<TrackerInfo>{};
    engine.trackers(id, trackers);
    log.check(trackers.size() == 2 && trackers[0].announce == L"http://tracker.invalid/announce" && trackers[0].tier != trackers[1].tier, "trackers land on new tiers");
    log.check(engine.editTracker(id, L"http://tracker.invalid/announce", L"http://tracker3.invalid/announce"), "edits a tracker");
    log.check(engine.removeTrackers(id, { L"http://tracker2.invalid/announce" }), "removes a tracker");
    engine.trackers(id, trackers);
    log.check(trackers.size() == 1 && trackers[0].announce == L"http://tracker3.invalid/announce", "the tracker list ends up right");

    engine.setLabels(id, { L"films", L"caf\u00e9 \"quoted\"" });
    engine.setGroup(id, 3);
    row = rowFor(engine, id);
    log.check(row != nullptr && row->group == 3, "the group sticks");
    log.check(row != nullptr && row->labels.size() == 2 && row->labels[1] == L"caf\u00e9 \"quoted\"", "labels round trip");

    auto const details = engine.details(id);
    log.check(details.fileCount == 2 && details.dataPath == root && !details.magnetLink.empty() && details.pieceCount > 0, "details are filled in");

    auto renamed = 0;
    engine.renamePath(id, L"NagareTest/sub/b.bin", L"c.bin", [&renamed](bool ok, std::wstring const&) { renamed = ok ? 1 : -1; });
    log.check(driver.waitFor(engine, kEventSeconds, [&renamed] { return renamed != 0; }) && renamed == 1, "renames a file");
    log.check(exists(root + L"\\sub\\c.bin") && !exists(root + L"\\sub\\b.bin"), "the rename happened on disk");

    renamed = 0;
    engine.renamePath(id, L"NagareTest", L"a/b", [&renamed](bool ok, std::wstring const&) { renamed = ok ? 1 : -1; });
    log.check(driver.waitFor(engine, kEventSeconds, [&renamed] { return renamed != 0; }) && renamed == -1, "refuses a name Windows can't hold");

    renamed = 0;
    engine.renamePath(id, L"NagareTest", L"NagareRenamed", [&renamed](bool ok, std::wstring const&) { renamed = ok ? 1 : -1; });
    log.check(driver.waitFor(engine, kEventSeconds, [&renamed] { return renamed != 0; }) && renamed == 1, "renames the root");
    log.check(driver.waitFor(engine, kEventSeconds, [&engine, id] { auto const* r = rowFor(engine, id); return r != nullptr && r->name == L"NagareRenamed"; }), "the row picks up the new name");
    auto const newRoot = seedFolder + L"\\NagareRenamed";
    log.check(exists(newRoot + L"\\a.bin"), "the root rename happened on disk");

    auto const moved = folder + L"\\moved";
    CreateDirectoryW(moved.c_str(), nullptr);
    auto moveResult = 0;
    engine.moveData(id, moved, [&moveResult](bool ok, std::wstring const&) { moveResult = ok ? 1 : -1; });
    log.check(driver.waitFor(engine, kEventSeconds, [&moveResult] { return moveResult != 0; }) && moveResult == 1, "moves the data");
    log.check(exists(moved + L"\\NagareRenamed\\a.bin") && !exists(newRoot), "the move happened on disk");
    moveResult = 0;
    engine.moveData(id, moved + L"\\NagareRenamed\\inner", [&moveResult](bool ok, std::wstring const&) { moveResult = ok ? 1 : -1; });
    log.check(moveResult == -1, "refuses to move a folder inside itself");

    engine.remove({ id }, true);
    log.check(driver.waitFor(engine, kEventSeconds, [&engine, id] { return rowFor(engine, id) == nullptr; }), "remove takes it off the list");
    log.check(driver.waitFor(engine, kEventSeconds, [&moved] { return !exists(moved + L"\\NagareRenamed"); }), "remove with data clears the files");
}

/// The Add panel's second step: which files, what they're called, and whether it starts.
void testAddOptions(Log& log, Engine& engine, Driver& driver, std::wstring const& folder)
{
    log.line("-- add options");

    auto const source = folder + L"\\addsource";
    CreateDirectoryW(source.c_str(), nullptr);
    CreateDirectoryW((source + L"\\inner").c_str(), nullptr);
    writePatternFile(source + L"\\one.bin", kSecondFileBytes, 11);
    writePatternFile(source + L"\\inner\\two.bin", kSecondFileBytes, 12);
    writePatternFile(source + L"\\three.bin", kSecondFileBytes, 13);
    auto const preview = engine.parseTorrentData(buildTorrent(source));
    log.check(preview.valid && preview.files.fileCount == 3, "parses a three-file torrent");

    auto const destination = folder + L"\\addtarget";
    CreateDirectoryW(destination.c_str(), nullptr);

    auto indexOf = [&preview](std::wstring const& name)
    {
        auto const it = std::find_if(preview.files.nodes.begin(), preview.files.nodes.end(), [&name](FileNode const& node) { return node.name == name; });
        return it != preview.files.nodes.end() ? it->fileIndexes[0] : -1;
    };

    auto request = AddRequest{};
    request.destination = destination;
    request.nameOverride = L"Better Name";
    request.startPaused = true;
    request.group = 2;
    request.wanted.assign(3, 1);
    request.wanted[static_cast<size_t>(indexOf(L"three.bin"))] = 0;
    request.priorities.assign(3, Priority::Normal);
    request.priorities[static_cast<size_t>(indexOf(L"one.bin"))] = Priority::High;
    request.renames.push_back({ L"addsource/inner/two.bin", L"second.bin" });
    request.renames.push_back({ L"addsource/inner", L"nested" });
    auto const added = engine.add(preview, request);
    log.check(added.id != kNoTransfer, "adds with options");
    auto const id = added.id;
    if (id == kNoTransfer)
    {
        return;
    }

    log.check(driver.waitFor(engine, kEventSeconds, [&engine, id] { auto const* r = rowFor(engine, id); return r != nullptr && r->name == L"Better Name"; }), "the name override lands");
    // Every new torrent passes through a short verify of its own, so paused comes just after.
    log.check(driver.waitFor(engine, kVerifySeconds, [&engine, id] { auto const* r = rowFor(engine, id); return r != nullptr && r->state == TransferState::Paused; }), "start paused is honoured");
    auto const* row = rowFor(engine, id);
    log.check(row != nullptr && row->group == 2, "the group carries over");
    // Counted in whole pieces, so the file boundaries it shares push this a little over two files.
    log.check(row != nullptr && row->selectedBytes >= 2 * kSecondFileBytes && row->selectedBytes < preview.totalBytes, "an unwanted file leaves the selection");

    auto const tree = engine.files(id);
    log.check(!driver.saw(EventType::AddRenameFailed, id), "every queued rename lands");
    auto const named = [&tree](std::wstring const& name)
    {
        return std::find_if(tree.nodes.begin(), tree.nodes.end(), [&name](FileNode const& node) { return node.name == name; });
    };
    log.check(named(L"nested") != tree.nodes.end() && named(L"nested")->path == L"Better Name/nested", "folder renames and the root rename compose");
    log.check(named(L"second.bin") != tree.nodes.end() && named(L"second.bin")->path == L"Better Name/nested/second.bin", "a file rename lands under its renamed folder");
    log.check(named(L"three.bin") != tree.nodes.end() && named(L"three.bin")->wanted == Mixed::Off, "per-file wanted is honoured");
    log.check(named(L"one.bin") != tree.nodes.end() && named(L"one.bin")->priority == Priority::High, "per-file priority is honoured");

    auto bad = request;
    bad.nameOverride = L"no/slashes";
    log.check(!engine.add(preview, bad).error.empty(), "an invalid name is refused before anything happens");

    engine.remove({ id }, true);
    driver.waitFor(engine, kEventSeconds, [&engine, id] { return rowFor(engine, id) == nullptr; });
}

/// Paths past Windows' old 260 character limit, where the shell can't go.
void testLongPath(Log& log, Engine& engine, Driver& driver, std::wstring const& folder)
{
    log.line("-- long paths");

    auto deep = folder + L"\\deep";
    CreateDirectoryW(deep.c_str(), nullptr);
    for (auto i = 0; i < kDeepLevels; ++i)
    {
        deep += L"\\" + std::wstring(40, static_cast<wchar_t>(L'a' + i));
        CreateDirectoryW(extendedPath(deep).c_str(), nullptr);
    }
    log.check(deep.size() > 400 && exists(deep), "a folder past MAX_PATH exists");

    auto const file = deep + L"\\long.bin";
    writePatternFile(file, kSecondFileBytes, 21);
    auto const torrent = buildTorrent(file);
    log.check(!torrent.empty(), "the torrent builds from a long path");
    writeFileAtomic(deep + L"\\long.torrent", torrent);

    auto const preview = engine.parseTorrentFile(deep + L"\\long.torrent");
    log.check(preview.valid, "parses a .torrent at a long path");

    auto request = AddRequest{};
    request.destination = deep;
    auto const id = engine.add(preview, request).id;
    log.check(id != kNoTransfer, "adds to a long destination");
    log.check(driver.waitFor(engine, kVerifySeconds, [&engine, id] { auto const* r = rowFor(engine, id); return r != nullptr && r->state == TransferState::Seeding; }), "verifies data at a long path");

    engine.remove({ id }, true);
    log.check(driver.waitFor(engine, kEventSeconds, [&engine, id] { return rowFor(engine, id) == nullptr; }), "removes it");
    log.check(driver.waitFor(engine, kEventSeconds, [&file] { return !exists(file); }), "data at a long path still goes");
}

void testWatchFolder(Log& log, Engine& engine, Driver& driver, std::wstring const& folder)
{
    log.line("-- watch folder");

    auto const watched = folder + L"\\watched";
    CreateDirectoryW(watched.c_str(), nullptr);
    writePatternFile(folder + L"\\dropped.bin", kSecondFileBytes, 31);
    writeFileAtomic(watched + L"\\dropped.torrent", buildTorrent(folder + L"\\dropped.bin"));
    writeFileAtomic(watched + L"\\empty.torrent", "");

    auto prefs = engine.prefs();
    prefs.watchFolderOn = true;
    prefs.watchFolder = watched;
    engine.setPrefs(prefs);

    auto id = kNoTransfer;
    log.check(driver.waitFor(engine, kEventSeconds, [&driver, &id]
        {
            for (auto const& event : driver.events)
            {
                if (event.type == EventType::WatchFolderAdded)
                {
                    id = event.id;
                    return event.text == L"dropped.torrent";
                }
            }
            return false;
        }),
        "a dropped torrent is picked up");

    prefs.watchFolderOn = false;
    engine.setPrefs(prefs);
    log.check(rowFor(engine, id) != nullptr, "it joins the list");
    engine.remove({ id }, false);
    driver.waitFor(engine, kEventSeconds, [&engine, id] { return rowFor(engine, id) == nullptr; });
}

/// Thread safety is the main risk, so race removal on libtransmission's thread against the snapshot
/// calls here. A use-after-free shows up as a crash, and the run never reaches its last line.
void testChurn(Log& log, Engine& engine, Driver& driver, std::wstring const& folder)
{
    log.line("-- churn");

    auto const source = folder + L"\\churn.bin";
    writePatternFile(source, kSecondFileBytes, 41);
    auto const preview = engine.parseTorrentData(buildTorrent(source));
    log.check(preview.valid, "parses the churn torrent");

    auto dots = std::vector<PieceDot>(kDotCount);
    auto peers = std::vector<PeerInfo>{};
    auto trackers = std::vector<TrackerInfo>{};
    auto webseeds = std::vector<WebseedInfo>{};
    auto request = AddRequest{};
    request.destination = folder;
    request.nameOverride = L"churned.bin";
    request.startPaused = true;

    auto survived = true;
    for (auto round = 0; round < kChurnRounds; ++round)
    {
        // Each round leaves an add's rename in flight as the transfer goes, on purpose.
        auto const result = engine.add(preview, request);
        auto const id = result.id;
        if (id == kNoTransfer)
        {
            log.line("info round " + std::to_string(round) + " add refused: " + narrow(result.error));
            survived = false;
        }
        engine.refresh();
        engine.pieceDots(id, dots.size(), dots.data(), kDefaultDotRows, true);
        engine.resume({ id });
        engine.remove({ id }, false);

        // All of these now name a transfer on its way out.
        engine.pause({ id });
        engine.peers(id, peers);
        engine.trackers(id, trackers);
        engine.webseeds(id, webseeds);
        auto tree = engine.files(id);
        engine.updateFiles(id, tree);
        (void)engine.details(id);
        (void)engine.options(id);
        engine.availability(id, static_cast<int>(dots.size()), reinterpret_cast<int8_t*>(dots.data()));
        engine.setFilesWanted(id, { 0 }, true);
        engine.moveInQueue({ id }, QueueMove::Top);
        engine.pieceDots(id, dots.size(), dots.data(), kDefaultDotRows, true);
        engine.refresh();
        engine.pump();

        // A row goes as soon as it's asked to, but the hash is only free once the session has let go.
        auto const gone = driver.waitFor(engine, kEventSeconds, [&engine, &preview] { return engine.parseTorrentData(preview.source).duplicateOf == kNoTransfer; });
        if (!gone)
        {
            log.line("info round " + std::to_string(round) + " never let go");
            survived = false;
        }
    }
    log.check(survived, "add and remove race the snapshot calls");
    log.check(engine.refresh().empty(), "churn leaves nothing behind");
}

/// How the row snapshot scales, in the shape the UI uses it.
void testScale(Log& log, Engine& engine, Driver& driver, std::wstring const& folder)
{
    log.line("-- scale");

    auto const source = folder + L"\\scale";
    CreateDirectoryW(source.c_str(), nullptr);
    auto ids = std::vector<TransferId>{};
    auto request = AddRequest{};
    request.destination = source;
    request.startPaused = true;
    for (auto i = 0; i < kManyTransfers; ++i)
    {
        auto const path = source + L"\\file" + std::to_wstring(i) + L".bin";
        writePatternFile(path, 64 * 1024, static_cast<uint32_t>(i + 100));
        auto const preview = engine.parseTorrentData(buildTorrent(path));
        ids.push_back(engine.add(preview, request).id);
    }
    log.check(static_cast<int>(engine.refresh().size()) == kManyTransfers, "holds a hundred transfers");
    driver.wait(engine, 3.0);
    reportMemory(log, std::to_string(kManyTransfers) + " paused torrents");

    constexpr int kRounds = 200;
    auto const start = monotonicSeconds();
    for (auto i = 0; i < kRounds; ++i)
    {
        engine.pause({ ids[0] });
        engine.refresh();
    }
    auto const microseconds = (monotonicSeconds() - start) * 1e6 / kRounds;
    log.line("info refresh() with " + std::to_string(kManyTransfers) + " transfers: " + std::to_string(static_cast<int>(microseconds)) + " us");

    engine.moveInQueue({ ids[kManyTransfers - 1] }, QueueMove::Top);
    auto const* first = rowFor(engine, ids[kManyTransfers - 1]);
    log.check(driver.waitFor(engine, kEventSeconds, [&engine, &ids] { auto const* r = rowFor(engine, ids[kManyTransfers - 1]); return r != nullptr && r->queuePosition == 0; }) && first != nullptr, "move to top reorders the queue");
    engine.moveInQueue({ ids[kManyTransfers - 1] }, QueueMove::Down);
    log.check(driver.waitFor(engine, kEventSeconds, [&engine, &ids] { auto const* r = rowFor(engine, ids[kManyTransfers - 1]); return r != nullptr && r->queuePosition == 1; }), "move down steps it back");
    engine.moveInQueue({ ids[kManyTransfers - 1] }, QueueMove::Bottom);
    log.check(driver.waitFor(engine, kEventSeconds, [&engine, &ids] { auto const* r = rowFor(engine, ids[kManyTransfers - 1]); return r != nullptr && r->queuePosition == kManyTransfers - 1; }), "move to bottom sends it last");
    engine.moveInQueue({ ids[kManyTransfers - 1] }, QueueMove::Up);
    log.check(driver.waitFor(engine, kEventSeconds, [&engine, &ids] { auto const* r = rowFor(engine, ids[kManyTransfers - 1]); return r != nullptr && r->queuePosition == kManyTransfers - 2; }), "move up steps it forward");

    engine.remove(ids, false);
    log.check(driver.waitFor(engine, kEventSeconds, [&engine] { return engine.refresh().empty(); }), "a batch remove clears them");
}

/// Calls answered by the network through libtransmission's own web layer.
void testAsyncRpc(Log& log, Engine& engine, Driver& driver)
{
    log.line("-- blocklist and port test");

    auto settings = engine.settings();
    settings.blocklistUrl = L"https://127.0.0.1:9/blocklist";
    engine.applySettings(settings);

    auto blocklist = 0;
    engine.updateBlocklist([&blocklist](bool ok, std::wstring const&) { blocklist = ok ? 1 : -1; });
    log.check(driver.waitFor(engine, kEventSeconds, [&blocklist] { return blocklist != 0; }) && blocklist == -1, "a blocklist that can't be fetched reports failure");

    auto port = 0;
    engine.testPort([&port](bool ok, std::wstring const&) { port = ok ? 1 : -1; });
    log.check(driver.waitFor(engine, kSearchSeconds, [&port] { return port != 0; }), "the port test answers");
    log.line(std::string{ "info peer port is " } + (port == 1 ? "open" : "closed or unreachable"));
}

/// Leaves one running and one paused, for the next session to find.
struct HistoryIds
{
    std::wstring running;
    std::wstring paused;
};

HistoryIds prepareHistory(Log& log, Engine& engine, Driver& driver, std::wstring const& folder)
{
    log.line("-- history, before");

    auto const source = folder + L"\\history";
    CreateDirectoryW(source.c_str(), nullptr);
    writePatternFile(source + L"\\running.bin", kSecondFileBytes, 51);
    writePatternFile(source + L"\\paused.bin", kSecondFileBytes, 52);

    auto request = AddRequest{};
    request.destination = source;
    auto const running = engine.add(engine.parseTorrentData(buildTorrent(source + L"\\running.bin")), request).id;
    auto const paused = engine.add(engine.parseTorrentData(buildTorrent(source + L"\\paused.bin")), request).id;
    log.check(running != kNoTransfer && paused != kNoTransfer, "adds two transfers");
    log.check(driver.waitFor(engine, kVerifySeconds, [&engine, running, paused]
        {
            auto const* a = rowFor(engine, running);
            auto const running_ = a != nullptr && a->state == TransferState::Seeding;
            auto const* b = rowFor(engine, paused);
            return running_ && b != nullptr && b->state == TransferState::Seeding;
        }),
        "both verify");

    engine.pause({ paused });
    engine.setGroup(running, 7);
    auto options = engine.options(running);
    options.removeWhenDone = true;
    engine.setOptions(running, options);

    auto ids = HistoryIds{};
    ids.running = rowFor(engine, running)->hash;
    ids.paused = rowFor(engine, paused)->hash;
    return ids;
}

void checkHistory(Log& log, Engine& engine, Driver& driver, HistoryIds const& ids)
{
    auto const byHash = [&engine](std::wstring const& hash) -> TransferInfo const*
    {
        auto const& rows = engine.refresh();
        auto const it = std::find_if(rows.begin(), rows.end(), [&hash](TransferInfo const& info) { return info.hash == hash; });
        return it != rows.end() ? &*it : nullptr;
    };

    log.check(driver.waitFor(engine, kVerifySeconds, [&byHash, &ids] { auto const* r = byHash(ids.running); return r != nullptr && r->state == TransferState::Seeding; }), "what was running starts again");
    auto const* running = byHash(ids.running);
    log.check(running != nullptr && running->group == 7 && running->removeWhenDone, "group and remove-when-done survive");
    auto const* paused = byHash(ids.paused);
    log.check(paused != nullptr && paused->state == TransferState::Finished, "what was paused stays paused");
}

void testImport(Log& log, Engine& engine, Driver& driver, std::wstring const& folder)
{
    log.line("-- qBittorrent import");

    auto const dataFolder = folder + L"\\qbt-data";
    auto const backup = folder + L"\\qbt-backup";
    CreateDirectoryW(dataFolder.c_str(), nullptr);
    CreateDirectoryW(backup.c_str(), nullptr);
    writePatternFile(dataFolder + L"\\imported.bin", kFirstFileBytes, 3);

    auto builder = tr_metainfo_builder{ narrow(dataFolder + L"\\imported.bin") };
    auto const checksumError = builder.make_checksums().get();
    log.check(!checksumError, "builds a fixture torrent");

    auto slashes = narrow(dataFolder);
    std::replace(slashes.begin(), slashes.end(), '\\', '/');
    writeFileAtomic(backup + L"\\0123.fastresume", "d6:pausedi0e12:qBt-savePath" + std::to_string(slashes.size()) + ":" + slashes + "e");
    writeFileAtomic(backup + L"\\0123.torrent", builder.benc());

    log.check(!engine.prefs().qbittorrentImported, "the import flag starts clear");
    auto const first = engine.importFromQBittorrent(backup);
    log.check(first == 1, "imports one transfer");
    log.check(engine.prefs().qbittorrentImported, "the import flag is set");
    log.check(engine.importFromQBittorrent(backup) == 0, "a second import adds nothing");
    log.check(exists(backup + L"\\0123.torrent") && exists(backup + L"\\0123.fastresume"), "qBittorrent's folder is left alone");

    auto const& rows = engine.refresh();
    auto const it = std::find_if(rows.begin(), rows.end(), [](TransferInfo const& info) { return info.name == L"imported.bin"; });
    log.check(it != rows.end(), "the transfer is listed");
    if (it == rows.end())
    {
        return;
    }
    auto const id = it->id;
    log.check(driver.waitFor(engine, kVerifySeconds, [&engine, id] { auto const* r = rowFor(engine, id); return r != nullptr && r->state == TransferState::Finished && r->percentDone == 1.0F; }), "stays paused and verifies existing data");
    engine.remove({ id }, false);
    log.check(driver.waitFor(engine, kEventSeconds, [&engine, id] { return rowFor(engine, id) == nullptr; }), "removal without data keeps working");
    log.check(exists(dataFolder + L"\\imported.bin"), "removal without data leaves the files");
}

void testLiveSearch(Log& log, Engine& engine, Driver& driver)
{
    log.line("-- live search");

    auto const names = engine.searchSourceNames();
    log.check(!names.empty() && names[0] == L"Internet Archive", "lists the built-in source");

    auto outcome = SearchOutcome{};
    auto done = false;
    engine.search(L"sintel", -1,
        [&outcome, &done](SearchOutcome const& result)
        {
            outcome = result;
            done = true;
        });
    log.check(driver.waitFor(engine, kSearchSeconds, [&done] { return done; }), "the search comes back");
    log.line("info search returned " + std::to_string(outcome.results.size()) + " results from " + std::to_string(outcome.responded) + " of " + std::to_string(outcome.asked));
    log.check(outcome.responded == 1 && !outcome.results.empty() && outcome.results[0].link.starts_with(L"https://archive.org/"), "the Internet Archive answers with torrents");

    auto cancelled = false;
    auto answered = false;
    engine.search(L"big buck bunny", -1,
        [&cancelled, &answered](SearchOutcome const& result)
        {
            cancelled = result.cancelled;
            answered = true;
        });
    engine.cancelSearch();
    log.check(driver.waitFor(engine, kEventSeconds, [&answered] { return answered; }) && cancelled, "a search can be cancelled");

    auto replaced = 0;
    engine.search(L"first", -1, [&replaced](SearchOutcome const& result) { replaced += result.cancelled ? 1 : 0; });
    auto second = false;
    engine.search(L"sintel", -1, [&second](SearchOutcome const&) { second = true; });
    log.check(driver.waitFor(engine, kSearchSeconds, [&second] { return second; }), "a new search replaces the last");
}

/// The bounded real-swarm run on Sintel, from the public WebTorrent test set.
void testSwarm(Log& log, Engine& engine, Driver& driver, std::wstring const& folder)
{
    log.line("-- Sintel over the network");

    auto const downloads = folder + L"\\downloads";
    CreateDirectoryW(downloads.c_str(), nullptr);

    auto const preview = engine.parseMagnet(kSintelMagnet);
    log.check(preview.valid && preview.isMagnet && preview.name == L"Sintel", "parses the magnet");
    log.check(preview.hash == L"08ada5a7a6183aae1e09d831df6748d566095a10", "reads its hash");

    auto request = AddRequest{};
    request.destination = downloads;
    request.nameOverride = L"Sintel Renamed";
    auto const added = engine.add(preview, request);
    log.check(added.id != kNoTransfer, "adds the magnet");
    auto id = added.id;
    if (id == kNoTransfer)
    {
        return;
    }

    auto const* row = rowFor(engine, id);
    log.check(row != nullptr && row->isMagnet && row->state == TransferState::Fetching, "a metadata-less magnet shows as fetching");
    log.check(engine.files(id).nodes.empty(), "it has no files yet");

    auto const start = monotonicSeconds();
    auto gotMetadata = driver.waitFor(engine, kMetadataSeconds, [&driver, id] { return driver.saw(EventType::MetadataReceived, id); });
    log.line("info metadata " + std::string{ gotMetadata ? "arrived" : "did not arrive" } + " after " + std::to_string(static_cast<int>(monotonicSeconds() - start)) + "s");

    if (!gotMetadata)
    {
        // Metadata needs a live peer, which the public swarm doesn't promise inside a bounded test.
        // The .torrent has the same content and a web seed, so the rest of the run still means
        // something, and this also proves the https fetch path.
        log.line("info falling back to the .torrent from webtorrent.io");
        engine.remove({ id }, true);
        driver.waitFor(engine, kEventSeconds, [&engine, id] { return rowFor(engine, id) == nullptr; });

        auto bytes = std::string{};
        auto fetched = false;
        engine.fetchTorrent(kSintelTorrent,
            [&bytes, &fetched](std::string data, std::wstring const&)
            {
                bytes = std::move(data);
                fetched = true;
            });
        log.check(driver.waitFor(engine, kSearchSeconds, [&fetched] { return fetched; }) && !bytes.empty(), "fetches the .torrent over https");
        auto const fromFile = engine.parseTorrentData(bytes);
        log.check(fromFile.valid && fromFile.name == L"Sintel", "parses the fetched torrent");
        auto const again = engine.add(fromFile, request);
        id = again.id;
        log.check(id != kNoTransfer, "adds the fetched torrent");
        if (id == kNoTransfer)
        {
            return;
        }
    }

    auto const started = monotonicSeconds();
    auto const moved = driver.waitFor(engine, kProgressSeconds, [&engine, id] { auto const* r = rowFor(engine, id); return r != nullptr && r->haveBytes >= kWantedBytes; });
    row = rowFor(engine, id);
    log.check(moved, "progress comes in from the swarm");
    if (row != nullptr)
    {
        log.line("info had " + formatMib(row->haveBytes) + " after " + std::to_string(static_cast<int>(monotonicSeconds() - started)) + "s, peers connected " + std::to_string(row->peersConnected) + ", swarm " +
            std::to_string(row->seeds) + " seeds " + std::to_string(row->peers) + " peers, name " + narrow(row->name));
    }
    reportMemory(log, "while downloading");

    if (gotMetadata)
    {
        log.check(driver.waitFor(engine, kEventSeconds, [&engine, id] { auto const* r = rowFor(engine, id); return r != nullptr && r->name == L"Sintel Renamed"; }), "the name waiting on the magnet is applied");
    }

    auto peers = std::vector<PeerInfo>{};
    engine.peers(id, peers);
    auto seeds = std::vector<WebseedInfo>{};
    engine.webseeds(id, seeds);
    auto trackers = std::vector<TrackerInfo>{};
    engine.trackers(id, trackers);
    log.line("info " + std::to_string(peers.size()) + " peers, " + std::to_string(seeds.size()) + " web seeds, " + std::to_string(trackers.size()) + " trackers");
    log.check(!peers.empty() || !seeds.empty(), "peers or web seeds are visible");

    auto dots = std::vector<PieceDot>(kDotCount);
    engine.pieceDots(id, dots.size(), dots.data(), kDefaultDotRows, true);
    auto const before = row != nullptr ? row->haveBytes : 0;
    log.check(driver.waitFor(engine, kEventSeconds, [&engine, id, before] { auto const* r = rowFor(engine, id); return r != nullptr && r->haveBytes > before; }), "keeps moving");
    log.check(engine.pieceDots(id, dots.size(), dots.data(), kDefaultDotRows, true), "new pieces repaint the row");

    engine.pause({ id });
    log.check(driver.waitFor(engine, kEventSeconds, [&engine, id] { auto const* r = rowFor(engine, id); return r != nullptr && r->state == TransferState::Paused; }), "pause works mid-download");
    engine.resumeNow({ id });
    log.check(driver.waitFor(engine, kEventSeconds, [&engine, id] { auto const* r = rowFor(engine, id); return r != nullptr && r->state == TransferState::Download; }), "resume works mid-download");

    auto tree = engine.files(id);
    log.check(!tree.nodes.empty(), "the file tree arrives");
    auto all = std::vector<int>{};
    auto smallest = std::vector<int>{};
    auto smallestSize = UINT64_MAX;
    for (auto const& node : tree.nodes)
    {
        if (!node.isFolder)
        {
            all.push_back(node.fileIndexes[0]);
            if (node.size < smallestSize)
            {
                smallestSize = node.size;
                smallest = node.fileIndexes;
            }
        }
    }
    engine.setFilesPriority(id, smallest, Priority::High);
    engine.setFilesWanted(id, smallest, false);
    engine.updateFiles(id, tree);
    auto const& smallestNode = *std::find_if(tree.nodes.begin(), tree.nodes.end(), [&smallest](FileNode const& node) { return !node.isFolder && node.fileIndexes == smallest; });
    log.check(smallestNode.priority == Priority::High && smallestNode.wanted == Mixed::Off, "priority and wanted change mid-download");
    log.check(tree.nodes.size() > 1 ? engine.options(id).priority == Priority::Normal : true, "the transfer's own priority is untouched");

    auto const target = tree.nodes.back();
    auto renamed = 0;
    engine.renamePath(id, target.path, L"renamed-by-test" + target.name.substr(target.name.rfind(L'.')), [&renamed](bool ok, std::wstring const&) { renamed = ok ? 1 : -1; });
    log.check(driver.waitFor(engine, kEventSeconds, [&renamed] { return renamed != 0; }) && renamed == 1, "renames a file mid-download");

    engine.remove({ id }, true);
    log.check(driver.waitFor(engine, kEventSeconds, [&engine, id] { return rowFor(engine, id) == nullptr; }), "remove with data takes it off the list");
    log.check(driver.waitFor(engine, kEventSeconds, [&downloads]
        {
            auto found = WIN32_FIND_DATAW{};
            auto const handle = FindFirstFileW((downloads + L"\\*").c_str(), &found);
            auto entries = 0;
            if (handle != INVALID_HANDLE_VALUE)
            {
                do
                {
                    entries += std::wstring_view{ found.cFileName } != L"." && std::wstring_view{ found.cFileName } != L".." ? 1 : 0;
                } while (FindNextFileW(handle, &found));
                FindClose(handle);
            }
            return entries == 0;
        }),
        "remove with data leaves the downloads folder empty");
}

} // namespace

int runEngineTest(std::wstring const& folder)
{
    removeTree(folder);
    CreateDirectoryW(folder.c_str(), nullptr);

    auto log = Log{ folder + L"\\engine-test.log" };
    log.line("nagare engine test, folder " + narrow(folder));

    testSearch(log);
    testShared(log);
    testFastresume(log, folder);
    testDemo(log);

    auto history = HistoryIds{};
    {
        auto const configDir = folder + L"\\config";
        auto driver = Driver{};
        auto engine = createEngine();
        log.line("-- session");
        log.check(engine->start(driver.options(configDir)), "the session starts");
        log.check(!engine->isConfigContended(), "the config folder is ours");
        log.check(exists(configDir + L"\\settings.json"), "settings are written on first run");
        auto const settings = engine->settings();
        log.check(settings.peerLimitPerTorrent == 60 && settings.ratioLimitOn && settings.ratioLimit == 2.0 && !settings.lpd, "defaults follow the Mac");
        log.check(settings.peerPort >= 49152, "the first run picks an ephemeral port");

        driver.wait(*engine, kIdleSettleSeconds);
        reportMemory(log, "0 torrents, session idle");

        auto const cpuStart = cpuSeconds();
        auto const wallStart = monotonicSeconds();
        driver.wait(*engine, kIdleCpuSeconds);
        auto const cpuPercent = (cpuSeconds() - cpuStart) / (monotonicSeconds() - wallStart) * 100.0;
        log.line("info idle CPU with 0 torrents, refreshing every 250 ms: " + std::to_string(cpuPercent) + "% of one core");

        auto changed = settings;
        changed.upKBps = 777;
        changed.limitUp = true;
        changed.seedQueueSize = 7;
        changed.dht = settings.dht;
        engine->applySettings(changed);
        log.check(engine->settings().upKBps == 777 && engine->settings().limitUp && engine->settings().seedQueueSize == 7, "settings apply");
        auto text = std::string{};
        log.check(readFile(configDir + L"\\settings.json", text, 1024 * 1024) && text.find("777") != std::string::npos, "settings save to settings.json");

        auto prefs = engine->prefs();
        prefs.themeMode = 2;
        prefs.sort = L"Order\u6d41";
        engine->setPrefs(prefs);

        testLocalTorrent(log, *engine, driver, folder);
        testAddOptions(log, *engine, driver, folder);
        testLongPath(log, *engine, driver, folder);
        testImport(log, *engine, driver, folder);
        testWatchFolder(log, *engine, driver, folder);
        testChurn(log, *engine, driver, folder);
        testScale(log, *engine, driver, folder);
        testAsyncRpc(log, *engine, driver);
        testLiveSearch(log, *engine, driver);
        testSwarm(log, *engine, driver, folder);
        history = prepareHistory(log, *engine, driver, folder);

        auto const closing = monotonicSeconds();
        log.check(engine->shutdown(static_cast<int>(kShutdownSeconds)), "shuts down inside its bound");
        log.line("info shutdown took " + std::to_string(monotonicSeconds() - closing) + "s");

        auto const reloaded = loadPrefs(configDir + L"\\prefs.json");
        log.check(reloaded.themeMode == 2 && reloaded.sort == L"Order\u6d41" && reloaded.qbittorrentImported, "prefs reload from disk");
    }

    {
        // A second session on the same folder: settings and history must have survived.
        auto driver = Driver{};
        auto engine = createEngine();
        log.line("-- restart");
        log.check(engine->start(driver.options(folder + L"\\config")), "the session restarts");
        log.check(engine->settings().upKBps == 777, "settings survive a restart");
        log.check(engine->refresh().size() == 2, "only the two kept transfers come back");
        checkHistory(log, *engine, driver, history);
        auto const closing = monotonicSeconds();
        log.check(engine->shutdown(static_cast<int>(kShutdownSeconds)), "shuts down again");
        log.line("info shutdown with no live trackers took " + std::to_string(monotonicSeconds() - closing) + "s");
    }

    {
        auto driver = Driver{};
        auto engine = createEngine();
        log.line("-- start paused");
        auto options = driver.options(folder + L"\\config");
        options.pauseOnStart = true;
        log.check(engine->start(options), "the session starts paused");
        driver.wait(*engine, 2.0);
        auto const& rows = engine->refresh();
        log.check(rows.size() == 2 && std::all_of(rows.begin(), rows.end(), [](TransferInfo const& info) { return info.state == TransferState::Finished; }), "nothing runs");
        auto ids = std::vector<TransferId>{};
        for (auto const& row : rows)
        {
            ids.push_back(row.id);
        }
        engine->remove(ids, true);
        log.check(driver.waitFor(*engine, kEventSeconds, [&engine] { return engine->refresh().empty(); }), "cleans up");
        log.check(engine->shutdown(static_cast<int>(kShutdownSeconds)), "shuts down a third time");
    }

    log.line(std::to_string(log.failures()) + " failures");
    return log.failures() == 0 ? 0 : 1;
}

} // namespace engine
