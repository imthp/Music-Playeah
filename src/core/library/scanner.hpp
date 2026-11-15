#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace mp::core::library {

// A snapshot of a running scan, for a progress bar. Counters only grow during one scan.
struct ScanProgress {
    enum class Phase { Scanning, Finishing, Done };
    Phase phase = Phase::Scanning;
    std::uint64_t filesFound = 0;  // audio files seen so far by the folder walk
    std::uint64_t filesDone = 0;   // of those: handled (unchanged, stored, or failed)
    std::uint64_t added = 0;       // new files stored
    std::uint64_t updated = 0;     // changed files stored again
    std::uint64_t unchanged = 0;   // same path + mtime + size: skipped without reading
    std::uint64_t failed = 0;      // files whose tags could not be read
    bool walkFinished = false;     // false: filesFound is still growing (progress is "indeterminate")
    std::string folder;            // the folder being walked
};

struct ScanSummary {
    std::uint64_t filesSeen = 0;
    std::uint64_t added = 0;
    std::uint64_t updated = 0;
    std::uint64_t unchanged = 0;
    std::uint64_t restored = 0;       // were missing, found again at the same path
    std::uint64_t moved = 0;          // new path re-matched to a missing track (id, history, overrides kept)
    std::uint64_t markedMissing = 0;  // known files not found this time
    std::uint64_t failed = 0;
    std::vector<std::string> unavailableFolders;  // watched folders that could not be opened (unplugged?)
    std::vector<std::string> errors;              // "path: reason", at most 20
    std::string fatalError;                       // set if the scan had to stop (database error)
    std::int64_t elapsedMs = 0;
    bool cancelled = false;
};

struct ScanOptions {
    int workerThreads = 0;  // tag readers; 0 = number of cores, between 2 and 8
    std::size_t batchSize = 500;                                  // rows per database transaction
    std::chrono::milliseconds progressInterval{100};              // minimum time between progress callbacks
};

// Scans every watched folder on a background thread:
//
//   walker (1 thread) --> tag readers (N threads, TagLib) --> writer (1 thread, own connection, batched)
//
// Incremental: a file whose path, mtime and size match the database is not read again. Known files
// that were not found are marked missing (never deleted), unless the scan was cancelled or the folder
// walk ended with an error. New files are matched against missing tracks first, so a moved/renamed
// folder keeps its track ids (SPEC §7). Finally artists and albums are derived from the tags.
//
// Callbacks run on scanner threads, not on the caller's: hand the data over to your own thread
// (the Qt app uses queued signals). Don't call start() from inside a callback.
class Scanner {
public:
    using ProgressFn = std::function<void(const ScanProgress&)>;
    using DoneFn = std::function<void(const ScanSummary&)>;

    // `dbFile` must already be migrated (construct a Library first).
    explicit Scanner(std::filesystem::path dbFile, ScanOptions options = {});
    ~Scanner();  // cancels a running scan and waits for it

    Scanner(const Scanner&) = delete;
    Scanner& operator=(const Scanner&) = delete;

    // Starts a scan of all watched folders. Returns false if one is already running.
    bool start(ProgressFn onProgress, DoneFn onDone);
    // Asks the running scan to stop soon. Everything stored so far stays; the scan is not "undone".
    void cancel();
    bool running() const;
    // Blocks until the running scan (if any) has finished and its done callback has returned.
    void wait();

private:
    struct Impl;
    std::unique_ptr<Impl> d_;
};

}  // namespace mp::core::library
