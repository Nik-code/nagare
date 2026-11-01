#include "engine/search_source.h"

#include <winhttp.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstring>

#include <rapidjson/document.h>
#include <shlwapi.h>

#include "engine/shared.h"

namespace engine
{

namespace
{

constexpr size_t kMaxResponseBytes = 8 * 1024 * 1024;
constexpr size_t kMaxTorrentBytes = 32 * 1024 * 1024;
constexpr size_t kMaxResults = 200;
constexpr size_t kMaxSourceBytes = 1024 * 1024;
constexpr DWORD kTimeoutMs = 20000;
constexpr size_t kReadChunk = 64 * 1024;

char const* const kReadme =
    "Each .json file here adds a search source to Nagare. Sources are plain data: nothing here is ever run.\r\n"
    "\r\n"
    "{\r\n"
    "  \"name\": \"Example\",\r\n"
    "  \"url\": \"https://example.com/api/search?q={query}\",\r\n"
    "  \"results\": \"data.items\",\r\n"
    "  \"items\": \"\",\r\n"
    "  \"title\": \"{title}\",\r\n"
    "  \"size\": \"size_bytes\",\r\n"
    "  \"seeds\": \"seeders\",\r\n"
    "  \"link\": \"magnet:?xt=urn:btih:{hash}&dn={title}\"\r\n"
    "}\r\n"
    "\r\n"
    "url      The address to call, https only. {query} becomes the search text.\r\n"
    "results  Dotted path to the list of results in the JSON answer.\r\n"
    "items    Optional. Path to a list inside each result, when one result has several torrents.\r\n"
    "title    Text for the row. {name} inserts a field, and an item's fields win over its parent's.\r\n"
    "size     Field holding bytes, or text like 1.4 GB. Optional.\r\n"
    "seeds    Field holding the seed count. Optional.\r\n"
    "link     A magnet link or an https .torrent address, built the same way as title.\r\n";

char const* const kBuiltIn = R"({
    "name": "Internet Archive",
    "url": "https://archive.org/advancedsearch.php?q=title:({query})%20AND%20mediatype:(movies%20OR%20software%20OR%20audio)&fl%5B%5D=identifier&fl%5B%5D=title&fl%5B%5D=item_size&sort%5B%5D=downloads%20desc&rows=50&output=json",
    "results": "response.docs",
    "title": "{title}",
    "size": "item_size",
    "link": "https://archive.org/download/{identifier}/{identifier}_archive.torrent"
})";

using Json = rapidjson::Value;

/// Untrusted documents can nest as deep as they like, so the parser must not recurse.
constexpr auto kParseFlags = rapidjson::kParseIterativeFlag | rapidjson::kParseValidateEncodingFlag;

Json const* valueAtPath(Json const* object, std::string_view path)
{
    auto const* value = object;
    size_t start = 0;
    while (start <= path.size())
    {
        auto const dot = path.find('.', start);
        auto const end = dot == std::string_view::npos ? path.size() : dot;
        auto const key = path.substr(start, end - start);
        start = end + 1;
        if (key.empty())
        {
            continue;
        }
        if (value == nullptr || !value->IsObject())
        {
            return nullptr;
        }
        auto const name = Json{ rapidjson::StringRef(key.data(), static_cast<rapidjson::SizeType>(key.size())) };
        auto const it = value->FindMember(name);
        value = it != value->MemberEnd() ? &it->value : nullptr;
    }
    return value;
}

/// Numbers print the way NSNumber's stringValue does, which is what links are built from.
std::optional<std::string> stringFor(Json const* value)
{
    if (value == nullptr)
    {
        return std::nullopt;
    }
    if (value->IsString())
    {
        return std::string{ value->GetString(), value->GetStringLength() };
    }
    if (value->IsBool())
    {
        return value->GetBool() ? "1" : "0";
    }
    if (!value->IsNumber())
    {
        return std::nullopt;
    }

    auto buffer = std::array<char, 40>{};
    auto const end = value->IsInt64()  ? std::to_chars(buffer.data(), buffer.data() + buffer.size(), value->GetInt64()) :
        value->IsUint64()              ? std::to_chars(buffer.data(), buffer.data() + buffer.size(), value->GetUint64()) :
                                         std::to_chars(buffer.data(), buffer.data() + buffer.size(), value->GetDouble());
    return std::string{ buffer.data(), end.ptr };
}

bool isUnreserved(unsigned char c)
{
    constexpr auto kAllowed = std::string_view{ "!$'()*-._~" };
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || kAllowed.find(static_cast<char>(c)) != std::string_view::npos;
}

std::string encoded(std::string_view value)
{
    constexpr auto kHex = std::string_view{ "0123456789ABCDEF" };
    auto output = std::string{};
    output.reserve(value.size());
    for (auto const raw : value)
    {
        auto const c = static_cast<unsigned char>(raw);
        if (isUnreserved(c))
        {
            output += raw;
        }
        else
        {
            output += '%';
            output += kHex[c >> 4];
            output += kHex[c & 0x0F];
        }
    }
    return output;
}

bool isKeyChar(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '.';
}

struct Field
{
    size_t begin = 0;
    size_t end = 0;
};

/// Each {field}, left to right, with the same rules as the Mac's \{([A-Za-z0-9_.]+)\}.
std::vector<Field> findFields(std::string_view format)
{
    auto fields = std::vector<Field>{};
    for (size_t i = 0; i < format.size(); ++i)
    {
        if (format[i] != '{')
        {
            continue;
        }
        auto end = i + 1;
        while (end < format.size() && isKeyChar(format[end]))
        {
            ++end;
        }
        if (end > i + 1 && end < format.size() && format[end] == '}')
        {
            fields.push_back({ i, end + 1 });
            i = end;
        }
    }
    return fields;
}

/// Replaces each {field} from the item, then its parent. A missing field gives nothing at all.
std::optional<std::string> expand(std::string_view format, Json const& item, Json const& parent, bool encode)
{
    auto const fields = findFields(format);

    // A link that is one whole field is already a finished address.
    auto const whole = fields.size() == 1 && fields[0].end - fields[0].begin == format.size();

    auto output = std::string{};
    size_t position = 0;
    for (auto const& field : fields)
    {
        output.append(format.substr(position, field.begin - position));
        auto const key = format.substr(field.begin + 1, field.end - field.begin - 2);
        auto value = stringFor(valueAtPath(&item, key));
        if (!value)
        {
            value = stringFor(valueAtPath(&parent, key));
        }
        if (!value)
        {
            return std::nullopt;
        }
        output += encode && !whole ? encoded(*value) : *value;
        position = field.end;
    }
    output.append(format.substr(position));
    return output;
}

std::string upperTrimmed(std::string_view text)
{
    constexpr auto kSpace = std::string_view{ " \t\r\n" };
    auto const first = text.find_first_not_of(kSpace);
    if (first == std::string_view::npos)
    {
        return {};
    }
    auto const last = text.find_last_not_of(kSpace);
    auto out = std::string{ text.substr(first, last - first + 1) };
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(c >= 'a' && c <= 'z' ? c - 32 : c); });
    return out;
}

uint64_t parseSize(Json const* value)
{
    if (value == nullptr)
    {
        return 0;
    }
    if (value->IsNumber())
    {
        if (value->IsUint64())
        {
            return value->GetUint64();
        }
        auto const number = value->GetDouble();
        return number > 0.0 ? static_cast<uint64_t>(number) : 0;
    }
    if (!value->IsString())
    {
        return 0;
    }

    auto text = std::string_view{ value->GetString(), value->GetStringLength() };
    text.remove_prefix(std::min(text.find_first_not_of(" \t\r\n"), text.size()));
    auto number = 0.0;
    auto const parsed = std::from_chars(text.data(), text.data() + text.size(), number);
    if (parsed.ec != std::errc{} || number < 0.0)
    {
        return 0;
    }

    struct Unit
    {
        std::string_view name;
        double scale;
    };
    constexpr auto kUnits = std::array<Unit, 8>{ {
        { "KB", 1e3 },
        { "MB", 1e6 },
        { "GB", 1e9 },
        { "TB", 1e12 },
        { "KIB", 1024.0 },
        { "MIB", 1048576.0 },
        { "GIB", 1073741824.0 },
        { "TIB", 1099511627776.0 },
    } };

    auto const unit = upperTrimmed({ parsed.ptr, static_cast<size_t>(text.data() + text.size() - parsed.ptr) });
    auto scale = 1.0;
    for (auto const& candidate : kUnits)
    {
        if (candidate.name == unit)
        {
            scale = candidate.scale;
        }
    }
    return static_cast<uint64_t>(number * scale);
}

int64_t parseSeeds(Json const* value)
{
    if (value == nullptr)
    {
        return -1;
    }
    if (value->IsBool())
    {
        return value->GetBool() ? 1 : 0;
    }
    if (value->IsNumber())
    {
        return value->IsInt64() ? value->GetInt64() : static_cast<int64_t>(value->GetDouble());
    }
    if (!value->IsString())
    {
        return -1;
    }

    // NSString's integerValue: leading digits, and zero if there are none.
    auto text = std::string_view{ value->GetString(), value->GetStringLength() };
    text.remove_prefix(std::min(text.find_first_not_of(" \t\r\n"), text.size()));
    if (text.starts_with('+'))
    {
        text.remove_prefix(1);
    }
    auto number = int64_t{ 0 };
    std::from_chars(text.data(), text.data() + text.size(), number);
    return number;
}

/// Anything a browser or a shell could read differently from us doesn't get through.
bool isSafeLink(std::string_view link)
{
    auto lower = std::string{ link };
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c); });

    if (lower.starts_with("magnet:?"))
    {
        return true;
    }
    if (!lower.starts_with("https://"))
    {
        return false;
    }
    for (auto const c : lower)
    {
        auto const code = static_cast<unsigned char>(c);
        if (code <= 0x20 || code == 0x7F || c == '\\')
        {
            return false;
        }
    }

    auto const authority = std::string_view{ lower }.substr(8);
    auto const end = authority.find_first_of("/?#");
    auto host = authority.substr(0, end);
    if (auto const at = host.rfind('@'); at != std::string_view::npos)
    {
        host.remove_prefix(at + 1);
    }
    return !host.substr(0, host.find(':')).empty();
}

std::string stringMember(rapidjson::Document const& document, char const* key)
{
    auto const it = document.FindMember(key);
    return it != document.MemberEnd() && it->value.IsString() ? std::string{ it->value.GetString(), it->value.GetStringLength() } : std::string{};
}

} // namespace

std::optional<SearchSource> SearchSource::fromJson(std::string_view json)
{
    auto document = rapidjson::Document{};
    document.Parse<kParseFlags>(json.data(), json.size());
    if (document.HasParseError() || !document.IsObject())
    {
        return std::nullopt;
    }

    auto const results = document.FindMember("results");
    if (results == document.MemberEnd() || !results->value.IsString())
    {
        return std::nullopt;
    }

    auto const name = stringMember(document, "name");
    auto source = SearchSource{};
    source.url_ = stringMember(document, "url");
    source.link_ = stringMember(document, "link");
    if (name.empty() || source.link_.empty() || source.url_.find("{query}") == std::string::npos)
    {
        return std::nullopt;
    }

    auto lower = source.url_;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c); });
    if (!lower.starts_with("https://"))
    {
        return std::nullopt;
    }

    source.name_ = widen(name);
    source.results_ = stringMember(document, "results");
    source.items_ = stringMember(document, "items");
    source.size_ = stringMember(document, "size");
    source.seeds_ = stringMember(document, "seeds");
    if (auto const it = document.FindMember("title"); it != document.MemberEnd() && it->value.IsString())
    {
        source.title_ = { it->value.GetString(), it->value.GetStringLength() };
    }
    return source;
}

std::wstring SearchSource::sourcesFolder(std::wstring const& configDir)
{
    auto const folder = configDir + L"\\Search Sources";
    if (GetFileAttributesW(folder.c_str()) == INVALID_FILE_ATTRIBUTES)
    {
        CreateDirectoryW(configDir.c_str(), nullptr);
        CreateDirectoryW(folder.c_str(), nullptr);
        writeFileAtomic(folder + L"\\README.txt", kReadme);
    }
    return folder;
}

std::vector<SearchSource> SearchSource::allSources(std::wstring const& folder)
{
    auto sources = std::vector<SearchSource>{};
    if (auto builtIn = fromJson(kBuiltIn))
    {
        sources.push_back(std::move(*builtIn));
    }

    auto names = std::vector<std::wstring>{};
    auto found = WIN32_FIND_DATAW{};
    auto const find = FindFirstFileExW((folder + L"\\*.json").c_str(), FindExInfoBasic, &found, FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
    if (find != INVALID_HANDLE_VALUE)
    {
        do
        {
            if ((found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
            {
                names.emplace_back(found.cFileName);
            }
        } while (FindNextFileW(find, &found));
        FindClose(find);
    }
    std::sort(names.begin(), names.end(), [](auto const& a, auto const& b) { return StrCmpLogicalW(a.c_str(), b.c_str()) < 0; });

    for (auto const& name : names)
    {
        auto text = std::string{};
        if (readFile(folder + L"\\" + name, text, kMaxSourceBytes))
        {
            if (auto source = fromJson(text))
            {
                sources.push_back(std::move(*source));
            }
        }
    }
    return sources;
}

std::string SearchSource::urlForQuery(std::wstring_view query) const
{
    auto const value = encoded(narrow(query));
    auto url = url_;
    constexpr auto kToken = std::string_view{ "{query}" };
    for (auto at = url.find(kToken); at != std::string::npos; at = url.find(kToken, at + value.size()))
    {
        url.replace(at, kToken.size(), value);
    }
    return url;
}

std::vector<SearchResult> SearchSource::resultsFromData(std::string_view data) const
{
    auto results = std::vector<SearchResult>{};
    if (data.size() > kMaxResponseBytes)
    {
        return results;
    }

    auto document = rapidjson::Document{};
    document.Parse<kParseFlags>(data.data(), data.size());
    if (document.HasParseError())
    {
        return results;
    }

    auto const* list = document.IsArray() && results_.empty() ? static_cast<Json const*>(&document) : valueAtPath(&document, results_);
    if (list == nullptr || !list->IsArray())
    {
        return results;
    }

    for (auto const& entry : list->GetArray())
    {
        if (!entry.IsObject())
        {
            continue;
        }

        auto const* children = items_.empty() ? nullptr : valueAtPath(&entry, items_);
        if (!items_.empty() && (children == nullptr || !children->IsArray()))
        {
            continue;
        }

        auto const count = items_.empty() ? rapidjson::SizeType{ 1 } : children->Size();
        for (rapidjson::SizeType index = 0; index < count && results.size() < kMaxResults; ++index)
        {
            auto const& child = items_.empty() ? entry : (*children)[index];
            if (!child.IsObject())
            {
                continue;
            }

            auto const title = expand(title_, child, entry, false);
            auto const link = expand(link_, child, entry, true);
            if (!title || title->empty() || !link || !isSafeLink(*link))
            {
                continue;
            }

            auto const* size = size_.empty() ? nullptr : valueAtPath(&child, size_);
            if (size == nullptr && !size_.empty())
            {
                size = valueAtPath(&entry, size_);
            }
            auto const* seeds = seeds_.empty() ? nullptr : valueAtPath(&child, seeds_);
            if (seeds == nullptr && !seeds_.empty())
            {
                seeds = valueAtPath(&entry, seeds_);
            }

            auto& result = results.emplace_back();
            result.title = widen(*title);
            result.link = widen(*link);
            result.sourceName = name_;
            result.size = parseSize(size);
            result.seeds = parseSeeds(seeds);
        }
    }
    return results;
}

SearchWorker::SearchWorker(Post post)
    : post_{ std::move(post) }
{
}

SearchWorker::~SearchWorker()
{
    stop();
}

void SearchWorker::search(std::vector<SearchSource> sources, std::wstring query, std::function<void(SearchOutcome const&)> done)
{
    auto job = Job{};
    job.sources = std::move(sources);
    job.query = std::move(query);
    job.searchDone = std::move(done);

    auto const lock = std::unique_lock{ mutex_ };
    if (stopping_)
    {
        return;
    }
    job.generation = ++searchGeneration_;
    closeActive();
    std::erase_if(jobs_, [](Job const& queued) { return static_cast<bool>(queued.searchDone); });
    jobs_.push_back(std::move(job));

    if (!running_)
    {
        if (thread_.joinable())
        {
            thread_.join();
        }
        running_ = true;
        thread_ = std::thread{ [this] { run(); } };
    }
}

void SearchWorker::cancelSearch()
{
    auto const lock = std::unique_lock{ mutex_ };
    ++searchGeneration_;
    closeActive();
}

void SearchWorker::fetch(std::wstring url, std::function<void(std::string, std::wstring const&)> done)
{
    auto job = Job{};
    job.url = std::move(url);
    job.fetchDone = std::move(done);

    auto const lock = std::unique_lock{ mutex_ };
    if (stopping_)
    {
        return;
    }
    jobs_.push_back(std::move(job));

    if (!running_)
    {
        if (thread_.joinable())
        {
            thread_.join();
        }
        running_ = true;
        thread_ = std::thread{ [this] { run(); } };
    }
}

void SearchWorker::stop()
{
    {
        auto const lock = std::unique_lock{ mutex_ };
        stopping_ = true;
        jobs_.clear();
        ++searchGeneration_;
        closeActive();
    }
    if (thread_.joinable())
    {
        thread_.join();
    }
}

void SearchWorker::closeActive()
{
    // Closing the handle is WinHTTP's way to abort a blocked call on another thread.
    if (activeRequest_ != nullptr)
    {
        WinHttpCloseHandle(static_cast<HINTERNET>(activeRequest_));
        activeRequest_ = nullptr;
    }
}

void SearchWorker::run()
{
    for (;;)
    {
        auto job = Job{};
        {
            auto const lock = std::unique_lock{ mutex_ };
            if (jobs_.empty() || stopping_)
            {
                running_ = false;
                return;
            }
            job = std::move(jobs_.front());
            jobs_.pop_front();
        }

        if (job.searchDone)
        {
            runSearch(job);
        }
        else
        {
            runFetch(job);
        }
    }
}

void SearchWorker::runSearch(Job& job)
{
    auto outcome = SearchOutcome{};
    outcome.asked = static_cast<int>(job.sources.size());

    auto const cancelled = [this, &job]
    {
        auto const lock = std::unique_lock{ mutex_ };
        return job.generation != searchGeneration_;
    };

    for (auto const& source : job.sources)
    {
        if (cancelled())
        {
            break;
        }

        auto data = std::string{};
        auto error = std::wstring{};
        if (get(source.urlForQuery(job.query), kMaxResponseBytes + 1, data, error, job.generation))
        {
            auto results = source.resultsFromData(data);
            outcome.results.insert(outcome.results.end(), std::make_move_iterator(results.begin()), std::make_move_iterator(results.end()));
            outcome.responded += 1;
        }
    }

    outcome.cancelled = cancelled();
    if (outcome.cancelled)
    {
        outcome.results.clear();
        outcome.results.shrink_to_fit();
    }
    std::stable_sort(outcome.results.begin(), outcome.results.end(), [](auto const& a, auto const& b) { return a.seeds > b.seeds; });
    post_([done = std::move(job.searchDone), outcome = std::move(outcome)] { done(outcome); });
}

void SearchWorker::runFetch(Job& job)
{
    auto const url = narrow(job.url);
    auto bytes = std::string{};
    auto error = std::wstring{};
    if (!isSafeLink(url) || url.rfind("magnet:", 0) == 0)
    {
        error = L"Only https links can be fetched.";
    }
    else if (!get(url, kMaxTorrentBytes, bytes, error, 0))
    {
        bytes.clear();
    }
    else if (bytes.size() > kMaxTorrentBytes)
    {
        bytes.clear();
        error = L"That file is too big to be a torrent.";
    }
    post_([done = std::move(job.fetchDone), bytes = std::move(bytes), error = std::move(error)] { done(bytes, error); });
}

bool SearchWorker::get(std::string const& url, size_t maxBytes, std::string& out, std::wstring& error, uint64_t generation)
{
    auto const wideUrl = widen(url);

    auto parts = URL_COMPONENTSW{};
    parts.dwStructSize = sizeof(parts);
    parts.dwHostNameLength = static_cast<DWORD>(-1);
    parts.dwUrlPathLength = static_cast<DWORD>(-1);
    parts.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(wideUrl.c_str(), 0, 0, &parts) || parts.nScheme != INTERNET_SCHEME_HTTPS)
    {
        error = L"That isn't an https address.";
        return false;
    }
    auto const host = std::wstring{ parts.lpszHostName, parts.dwHostNameLength };
    auto const path = std::wstring{ parts.lpszUrlPath, parts.dwUrlPathLength + parts.dwExtraInfoLength };

    auto* const session = WinHttpOpen(L"Nagare/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    auto* const connection = session != nullptr ? WinHttpConnect(session, host.c_str(), parts.nPort, 0) : nullptr;
    auto* const request = connection != nullptr ? WinHttpOpenRequest(connection, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE) : nullptr;

    auto ok = false;
    if (request != nullptr)
    {
        WinHttpSetTimeouts(request, static_cast<int>(kTimeoutMs), static_cast<int>(kTimeoutMs), static_cast<int>(kTimeoutMs), static_cast<int>(kTimeoutMs));
        auto policy = DWORD{ WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP };
        WinHttpSetOption(request, WINHTTP_OPTION_REDIRECT_POLICY, &policy, sizeof(policy));
        auto decompression = DWORD{ WINHTTP_DECOMPRESSION_FLAG_ALL };
        WinHttpSetOption(request, WINHTTP_OPTION_DECOMPRESSION, &decompression, sizeof(decompression));

        auto published = false;
        {
            auto const lock = std::unique_lock{ mutex_ };
            if (generation == 0 ? !stopping_ : generation == searchGeneration_)
            {
                activeRequest_ = request;
                published = true;
            }
        }

        if (published)
        {
            ok = WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) && WinHttpReceiveResponse(request, nullptr);
            while (ok)
            {
                auto const offset = out.size();
                if (offset > maxBytes)
                {
                    // Over the limit. The caller sees an oversized body and refuses it.
                    break;
                }
                out.resize(offset + kReadChunk);
                auto read = DWORD{ 0 };
                ok = WinHttpReadData(request, out.data() + offset, static_cast<DWORD>(kReadChunk), &read) != FALSE;
                out.resize(offset + read);
                if (ok && read == 0)
                {
                    break;
                }
            }

            // A close from another thread has already consumed the handle. Otherwise it's ours.
            auto const lock = std::unique_lock{ mutex_ };
            if (activeRequest_ == request)
            {
                activeRequest_ = nullptr;
                WinHttpCloseHandle(request);
            }
        }
        else
        {
            WinHttpCloseHandle(request);
        }
    }

    if (connection != nullptr)
    {
        WinHttpCloseHandle(connection);
    }
    if (session != nullptr)
    {
        WinHttpCloseHandle(session);
    }
    if (!ok)
    {
        error = L"Couldn't reach the source.";
    }
    return ok;
}

} // namespace engine
