#pragma once

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "engine/engine.h"

namespace engine
{

/// One place to search, described by data rather than code: a JSON file that says which URL to call
/// and how to read the answer. Nothing from a source is ever executed.
class SearchSource
{
public:
    /// Nothing when the definition is incomplete or its URL isn't https.
    [[nodiscard]] static std::optional<SearchSource> fromJson(std::string_view json);

    /// Built-in sources, then every valid JSON file in `folder`.
    [[nodiscard]] static std::vector<SearchSource> allSources(std::wstring const& folder);

    /// `configDir`\Search Sources, created with its README on first use.
    [[nodiscard]] static std::wstring sourcesFolder(std::wstring const& configDir);

    [[nodiscard]] std::wstring const& name() const
    {
        return name_;
    }

    [[nodiscard]] std::string urlForQuery(std::wstring_view query) const;

    /// Results with an unsafe or missing link are dropped. Invalid or oversized data gives none.
    [[nodiscard]] std::vector<SearchResult> resultsFromData(std::string_view data) const;

private:
    std::wstring name_;
    std::string url_;
    std::string results_;
    std::string items_;
    std::string title_ = "{title}";
    std::string size_;
    std::string seeds_;
    std::string link_;
};

/// Runs network work on one thread that exits as soon as it has nothing to do, so an idle app has
/// no search thread at all. Results are handed to `post`, which puts them on the UI thread.
class SearchWorker
{
public:
    using Post = std::function<void(std::function<void()>)>;

    explicit SearchWorker(Post post);
    ~SearchWorker();

    SearchWorker(SearchWorker const&) = delete;
    SearchWorker& operator=(SearchWorker const&) = delete;

    /// Replaces any search in progress.
    void search(std::vector<SearchSource> sources, std::wstring query, std::function<void(SearchOutcome const&)> done);
    void cancelSearch();
    void fetch(std::wstring url, std::function<void(std::string bytes, std::wstring const& error)> done);
    /// Cancels everything and waits for the thread.
    void stop();

private:
    struct Job
    {
        std::vector<SearchSource> sources;
        std::wstring query;
        uint64_t generation = 0;
        std::function<void(SearchOutcome const&)> searchDone;
        std::wstring url;
        std::function<void(std::string, std::wstring const&)> fetchDone;
    };

    void run();
    void runSearch(Job& job);
    void runFetch(Job& job);
    [[nodiscard]] bool get(std::string const& url, size_t maxBytes, std::string& out, std::wstring& error, uint64_t generation);
    void closeActive();

    Post post_;
    std::mutex mutex_;
    std::deque<Job> jobs_;
    std::thread thread_;
    bool running_ = false;
    bool stopping_ = false;
    uint64_t searchGeneration_ = 0;
    /// The request in flight, so that another thread can cut it off. Guarded by mutex_.
    void* activeRequest_ = nullptr;
};

} // namespace engine
