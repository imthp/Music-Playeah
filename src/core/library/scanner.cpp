#include "core/library/scanner.hpp"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <mutex>
#include <optional>
#include <thread>
#include <unordered_map>
#include <utility>

#include "core/library/art.hpp"
#include "core/library/database.hpp"
#include "core/library/entities.hpp"
#include "core/library/works.hpp"
#include "core/library/library.hpp"
#include "core/library/tag_reader.hpp"
#include "core/text.hpp"
#include "core/util/bounded_queue.hpp"

namespace mp::core::library {

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

namespace {

constexpr std::size_t kMaxErrors = 20;
constexpr std::size_t kIdChunk = 500;
constexpr std::int64_t kMoveToleranceMs = 1500;  // a moved file's duration may differ a little between readers

std::int64_t unixSeconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}

std::int64_t mtimeTicks(const fs::file_time_type& t) {
    // Ticks since the file clock's epoch. The epoch differs per OS but is constant on one machine,
    // and the value is only ever compared with itself.
    return std::chrono::duration_cast<std::chrono::microseconds>(t.time_since_epoch()).count();
}

// album + disc + track, used to find a track again after its file moved (SPEC §7).
std::string matchKey(const FileTags& t) {
    if (t.album.empty() || t.trackNo <= 0) {
        return {};
    }
    return normalizeText(t.album) + '\x1F' + std::to_string(t.discNo) + '\x1F' + std::to_string(t.trackNo);
}

// The tag columns of `tracks`, in the order bindTags() binds them. One list builds the SQL, so the
// two cannot drift apart (the scan tests read every field back).
constexpr const char* kTagColumns[] = {"title",      "artist",         "album_artist",  "composer",    "album",
                                       "work_tag",   "movement_no",    "movement_total", "movement_name", "disc_no",
                                       "track_no",   "year",           "genre",         "duration_ms", "codec",
                                       "sample_rate", "bit_depth",     "channels",      "bitrate",     "rg_track_gain",
                                       "rg_album_gain", "credits_raw"};

std::string upsertSql() {
    // Parameters: ?1 path, ?2 mtime, ?3 size, then the tag columns, then match_key and added_at.
    std::string columns = "path, mtime, size";
    std::string values = "?1, ?2, ?3";
    std::string update = "mtime = excluded.mtime, size = excluded.size";
    int index = 3;
    for (const char* c : kTagColumns) {
        columns += std::string(", ") + c;
        values += ", ?" + std::to_string(++index);
        update += std::string(", ") + c + " = excluded." + c;
    }
    columns += ", match_key, added_at, resolved, works_done";
    values += ", ?" + std::to_string(++index);  // match_key
    values += ", ?" + std::to_string(++index);  // added_at
    values += ", 0, 0";                         // resolved, works_done
    update += ", match_key = excluded.match_key, resolved = 0, works_done = 0, missing_since = NULL";
    return "INSERT INTO tracks(" + columns + ") VALUES (" + values + ") ON CONFLICT(path) DO UPDATE SET " + update;
}

struct WorkItem {
    fs::path path;
    std::string utf8;
    std::int64_t mtime = 0;
    std::int64_t size = 0;
    bool existing = false;  // already has a row (a changed file), else a new one
};

struct WriterMsg {
    enum class Kind { Tags, Restore, MarkMissing };
    Kind kind = Kind::Tags;
    WorkItem item;              // Tags
    FileTags tags;              // Tags
    std::vector<std::int64_t> ids;  // Restore, MarkMissing
};

struct Known {
    std::int64_t id = 0;
    std::int64_t mtime = 0;
    std::int64_t size = 0;
    bool missing = false;
    bool seen = false;
};

}  // namespace

struct Scanner::Impl {
    fs::path dbFile;
    ScanOptions options;

    std::thread thread;
    std::atomic<bool> running{false};
    std::atomic<bool> cancel{false};

    ProgressFn onProgress;
    DoneFn onDone;

    std::atomic<std::uint64_t> found{0}, done{0}, added{0}, updated{0}, unchanged{0}, failed{0};
    std::atomic<std::uint64_t> restored{0}, markedMissing{0};
    std::atomic<bool> walkFinished{false};
    std::atomic<ScanProgress::Phase> phase{ScanProgress::Phase::Scanning};
    std::atomic<std::int64_t> lastReportTick{0};

    std::mutex mutex;  // guards the members below
    std::string folder;
    std::vector<std::string> errors;
    std::vector<std::string> unavailable;
    std::string fatalError;

    // ---- helpers ----

    void resetCounters() {
        found = done = added = updated = unchanged = failed = restored = markedMissing = 0;
        walkFinished = false;
        phase = ScanProgress::Phase::Scanning;
        lastReportTick = 0;
        std::lock_guard lock(mutex);
        folder.clear();
        errors.clear();
        unavailable.clear();
        fatalError.clear();
    }

    void addError(const std::string& text) {
        std::lock_guard lock(mutex);
        if (errors.size() < kMaxErrors) {
            errors.push_back(text);
        }
    }

    ScanProgress snapshot() {
        ScanProgress p;
        p.phase = phase;
        p.filesFound = found;
        p.filesDone = done;
        p.added = added;
        p.updated = updated;
        p.unchanged = unchanged;
        p.failed = failed;
        p.walkFinished = walkFinished;
        std::lock_guard lock(mutex);
        p.folder = folder;
        return p;
    }

    // Called from the walker and the writer; at most one callback per progressInterval overall.
    void report(bool force = false) {
        if (!onProgress) {
            return;
        }
        const std::int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();
        std::int64_t last = lastReportTick.load();
        if (!force) {
            if (now - last < options.progressInterval.count() || !lastReportTick.compare_exchange_strong(last, now)) {
                return;
            }
        } else {
            lastReportTick = now;
        }
        onProgress(snapshot());
    }

    void setFolder(const std::string& path) {
        std::lock_guard lock(mutex);
        folder = path;
    }

    // ---- the pipeline ----

    void run();
    void walk(Database& reader, const std::vector<FolderInfo>& folders, BoundedQueue<WorkItem>& work,
              BoundedQueue<WriterMsg>& results);
    void readerLoop(BoundedQueue<WorkItem>& work, BoundedQueue<WriterMsg>& results);
    void writerLoop(Database& db, BoundedQueue<WriterMsg>& results);
    std::uint64_t rematchMoved(Database& db, std::int64_t maxIdBefore);
};

// ---------------------------------------------------------------- walker

void Scanner::Impl::walk(Database& reader, const std::vector<FolderInfo>& folders, BoundedQueue<WorkItem>& work,
                         BoundedQueue<WriterMsg>& results) {
    Statement load = reader.prepare(
        "SELECT id, path, mtime, size, missing_since IS NOT NULL FROM tracks WHERE path >= ?1 AND path < ?2");

    auto sendIds = [&](WriterMsg::Kind kind, std::vector<std::int64_t>& ids, bool all) {
        while (ids.size() >= kIdChunk || (all && !ids.empty())) {
            const std::size_t n = std::min(ids.size(), kIdChunk);
            WriterMsg msg;
            msg.kind = kind;
            msg.ids.assign(ids.begin(), ids.begin() + static_cast<std::ptrdiff_t>(n));
            ids.erase(ids.begin(), ids.begin() + static_cast<std::ptrdiff_t>(n));
            results.push(std::move(msg));
        }
    };

    for (const FolderInfo& watched : folders) {
        if (cancel) {
            break;
        }
        setFolder(watched.path);
        report(true);

        // What the database already knows about this folder.
        std::unordered_map<std::string, Known> known;
        const PathRange range = pathRangeUnder(watched.path);
        load.bind(1, range.low).bind(2, range.high);
        while (load.step()) {
            Known k;
            k.id = load.getInt(0);
            k.mtime = load.getInt(2);
            k.size = load.getInt(3);
            k.missing = load.getInt(4) != 0;
            known.emplace(load.getText(1), k);
        }
        load.reset();

        const fs::path root = pathFromUtf8(watched.path);
        std::error_code ec;
        const bool available = fs::is_directory(root, ec) && !ec;
        bool complete = true;  // may we conclude that unseen files are gone?
        std::vector<std::int64_t> restoreIds;

        if (!available) {
            std::lock_guard lock(mutex);
            unavailable.push_back(watched.path);
        } else {
            fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec);
            const fs::recursive_directory_iterator end;
            for (; !ec && it != end && !cancel; it.increment(ec)) {
                std::error_code fileEc;
                const fs::directory_entry& entry = *it;
                if (!entry.is_regular_file(fileEc) || fileEc || !isLibraryFile(entry.path())) {
                    continue;
                }
                ++found;
                WorkItem item;
                item.path = entry.path();
                item.utf8 = genericUtf8(entry.path());
                const auto size = entry.file_size(fileEc);
                const auto time = fileEc ? fs::file_time_type{} : entry.last_write_time(fileEc);
                const auto k = known.find(item.utf8);
                if (k != known.end()) {
                    k->second.seen = true;
                }
                if (fileEc) {  // can't stat it right now: leave it as it is, try again next scan
                    ++failed;
                    ++done;
                    addError(item.utf8 + ": " + fileEc.message());
                    continue;
                }
                item.size = static_cast<std::int64_t>(size);
                item.mtime = mtimeTicks(time);

                if (k != known.end() && k->second.mtime == item.mtime && k->second.size == item.size) {
                    ++unchanged;
                    ++done;
                    if (k->second.missing) {
                        restoreIds.push_back(k->second.id);
                        sendIds(WriterMsg::Kind::Restore, restoreIds, false);
                    }
                } else {
                    item.existing = k != known.end();
                    if (!work.push(std::move(item))) {
                        break;
                    }
                }
                report();
            }
            if (ec) {
                // The walk stopped early (drive pulled mid-scan, access error). Unseen files
                // might still exist, so don't mark anything missing for this folder.
                complete = false;
                addError(watched.path + ": folder walk stopped: " + ec.message());
            }
        }
        sendIds(WriterMsg::Kind::Restore, restoreIds, true);

        if (complete && !cancel) {
            std::vector<std::int64_t> gone;
            for (const auto& [path, k] : known) {
                if (!k.seen && !k.missing) {
                    gone.push_back(k.id);
                }
            }
            sendIds(WriterMsg::Kind::MarkMissing, gone, true);
        }
    }
    walkFinished = true;
    report(true);
}

// ---------------------------------------------------------------- tag readers

void Scanner::Impl::readerLoop(BoundedQueue<WorkItem>& work, BoundedQueue<WriterMsg>& results) {
    WorkItem item;
    while (work.pop(item)) {
        if (cancel) {
            continue;  // drain: the file is simply picked up by the next scan
        }
        WriterMsg msg;
        std::string error;
        if (readTags(item.path, msg.tags, error)) {
            msg.kind = WriterMsg::Kind::Tags;
            msg.item = std::move(item);
            results.push(std::move(msg));
        } else {
            ++failed;
            ++done;
            addError(item.utf8 + ": " + error);
        }
    }
}

// ---------------------------------------------------------------- writer

void Scanner::Impl::writerLoop(Database& db, BoundedQueue<WriterMsg>& results) {
    WriterMsg msg;
    try {
        Statement upsert = db.prepare(upsertSql());
        Statement restore = db.prepare("UPDATE tracks SET missing_since = NULL WHERE id = ?1 AND missing_since IS NOT NULL");
        Statement missing = db.prepare("UPDATE tracks SET missing_since = ?1 WHERE id = ?2 AND missing_since IS NULL");

        std::optional<Transaction> tx;
        std::size_t pending = 0;
        auto commit = [&] {
            if (tx) {
                tx->commit();
                tx.reset();
                pending = 0;
            }
        };

        for (;;) {
            const auto r = results.popFor(msg, std::chrono::milliseconds(250));
            if (r == BoundedQueue<WriterMsg>::PopResult::Closed) {
                break;
            }
            if (r == BoundedQueue<WriterMsg>::PopResult::Timeout) {
                commit();  // input went quiet: make what we have durable
                report();
                continue;
            }
            if (!tx) {
                tx.emplace(db);
            }
            switch (msg.kind) {
                case WriterMsg::Kind::Tags: {
                    const FileTags& t = msg.tags;
                    int i = 1;
                    upsert.bind(i++, msg.item.utf8);
                    upsert.bind(i++, msg.item.mtime);
                    upsert.bind(i++, msg.item.size);
                    upsert.bind(i++, t.title);
                    upsert.bind(i++, t.artist);
                    upsert.bind(i++, t.albumArtist);
                    upsert.bind(i++, t.composer);
                    upsert.bind(i++, t.album);
                    upsert.bind(i++, t.workTag);
                    upsert.bind(i++, t.movementNo);
                    upsert.bind(i++, t.movementTotal);
                    upsert.bind(i++, t.movementName);
                    upsert.bind(i++, t.discNo);
                    upsert.bind(i++, t.trackNo);
                    upsert.bind(i++, t.year);
                    upsert.bind(i++, t.genre);
                    upsert.bind(i++, t.durationMs);
                    upsert.bind(i++, t.codec);
                    upsert.bind(i++, t.sampleRate);
                    upsert.bind(i++, t.bitDepth);
                    upsert.bind(i++, t.channels);
                    upsert.bind(i++, t.bitrateKbps);
                    upsert.bind(i++, t.rgTrackGain);
                    upsert.bind(i++, t.rgAlbumGain);
                    upsert.bind(i++, t.credits);
                    upsert.bind(i++, matchKey(t));
                    upsert.bind(i++, unixSeconds());
                    upsert.run();
                    if (msg.item.existing) {
                        ++updated;
                    } else {
                        ++added;
                    }
                    ++done;
                    ++pending;
                    break;
                }
                case WriterMsg::Kind::Restore:
                    for (const std::int64_t id : msg.ids) {
                        restore.bind(1, id);
                        restore.run();
                        if (db.changes() > 0) {
                            ++restored;
                        }
                    }
                    pending += msg.ids.size();
                    break;
                case WriterMsg::Kind::MarkMissing: {
                    const std::int64_t now = unixSeconds();
                    for (const std::int64_t id : msg.ids) {
                        missing.bind(1, now).bind(2, id);
                        missing.run();
                        if (db.changes() > 0) {
                            ++markedMissing;
                        }
                    }
                    pending += msg.ids.size();
                    break;
                }
            }
            if (pending >= options.batchSize) {
                commit();
            }
            report();
        }
        commit();
    } catch (const std::exception& e) {
        {
            std::lock_guard lock(mutex);
            fatalError = e.what();
        }
        cancel = true;
        while (results.pop(msg)) {  // keep draining so the producers never block on a full queue
        }
    }
}

// ---------------------------------------------------------------- moved files

// A new file whose album + disc + track + duration match exactly one missing track is that track at
// a new place: move the row's file fields over and drop the fresh row. The old row keeps its id, so
// plays, playlists and overrides stay attached.
std::uint64_t Scanner::Impl::rematchMoved(Database& db, std::int64_t maxIdBefore) {
    struct Fresh {
        std::int64_t id;
        std::string key;
        std::int64_t durationMs;
    };
    std::vector<Fresh> fresh;
    {
        Statement s = db.prepare("SELECT id, match_key, duration_ms FROM tracks WHERE id > ?1 AND match_key <> ''");
        s.bind(1, maxIdBefore);
        while (s.step()) {
            fresh.push_back({s.getInt(0), s.getText(1), s.getInt(2)});
        }
    }
    if (fresh.empty()) {
        return 0;
    }

    // Everything that describes the file; the bookkeeping (id, added_at, art_hash, overrides) stays.
    std::string assignments = "path = ?1, mtime = (SELECT mtime FROM tracks WHERE id = ?2), size = (SELECT size FROM tracks WHERE id = ?2)";
    for (const char* c : kTagColumns) {
        assignments += std::string(", ") + c + " = (SELECT " + c + " FROM tracks WHERE id = ?2)";
    }
    assignments += ", match_key = (SELECT match_key FROM tracks WHERE id = ?2), resolved = 0, works_done = 0, missing_since = NULL";

    Transaction tx(db);
    Statement candidates = db.prepare(
        "SELECT id, duration_ms FROM tracks WHERE match_key = ?1 AND missing_since IS NOT NULL AND id <= ?2");
    Statement pathOf = db.prepare("SELECT path FROM tracks WHERE id = ?1");
    Statement park = db.prepare("UPDATE tracks SET path = '?moving:' || id WHERE id = ?1");
    Statement take = db.prepare("UPDATE tracks SET " + assignments + " WHERE id = ?3");
    Statement drop = db.prepare("DELETE FROM tracks WHERE id = ?1");

    std::uint64_t moved = 0;
    for (const Fresh& f : fresh) {
        std::vector<std::int64_t> matches;
        candidates.bind(1, f.key).bind(2, maxIdBefore);
        while (candidates.step()) {
            if (std::abs(candidates.getInt(1) - f.durationMs) <= kMoveToleranceMs) {
                matches.push_back(candidates.getInt(0));
            }
        }
        candidates.reset();
        if (matches.size() != 1) {
            continue;  // no candidate, or ambiguous (two missing copies): keep it as a new track
        }
        pathOf.bind(1, f.id);
        const std::string newPath = pathOf.step() ? pathOf.getText(0) : std::string();
        pathOf.reset();
        if (newPath.empty()) {
            continue;
        }
        park.bind(1, f.id);  // frees the UNIQUE path for the old row
        park.run();
        take.bind(1, newPath).bind(2, f.id).bind(3, matches.front());
        take.run();
        drop.bind(1, f.id);
        drop.run();
        ++moved;
    }
    tx.commit();
    return moved;
}

// ---------------------------------------------------------------- orchestration

void Scanner::Impl::run() {
    const auto started = Clock::now();
    ScanSummary summary;
    try {
        std::vector<FolderInfo> folders;
        std::int64_t maxIdBefore = 0;
        Database reader(dbFile);
        reader.migrate();
        {
            Statement s = reader.prepare("SELECT id, path FROM folders ORDER BY path");
            while (s.step()) {
                folders.push_back({s.getInt(0), s.getText(1)});
            }
            maxIdBefore = reader.scalarInt("SELECT COALESCE(MAX(id), 0) FROM tracks");
        }
        Database writer(dbFile);

        BoundedQueue<WorkItem> work(512);
        BoundedQueue<WriterMsg> results(512);

        const unsigned hardware = std::max(1u, std::thread::hardware_concurrency());
        const int workers = options.workerThreads > 0 ? options.workerThreads
                                                      : static_cast<int>(std::clamp(hardware, 2u, 8u));
        std::vector<std::thread> readers;
        for (int i = 0; i < workers; ++i) {
            readers.emplace_back([this, &work, &results] { readerLoop(work, results); });
        }
        std::thread writerThread([this, &writer, &results] { writerLoop(writer, results); });

        try {
            walk(reader, folders, work, results);
        } catch (const std::exception& e) {
            // Must not leave the threads running: stop the pipeline, then report below.
            cancel = true;
            std::lock_guard lock(mutex);
            if (fatalError.empty()) {
                fatalError = e.what();
            }
        }

        work.close();
        for (std::thread& t : readers) {
            t.join();
        }
        results.close();
        writerThread.join();

        phase = ScanProgress::Phase::Finishing;
        report(true);

        {
            std::lock_guard lock(mutex);
            summary.fatalError = fatalError;
        }
        if (summary.fatalError.empty()) {
            if (!cancel) {
                summary.moved = rematchMoved(writer, maxIdBefore);
            }
            resolveEntities(writer);  // also after a cancel: whatever was stored gets its albums
            resolveWorks(writer);     // then works and movements (needs the album and composer ids)
            resolveArt(writer, &cancel);  // then covers (one embedded-picture read per new album)
        }
    } catch (const std::exception& e) {
        std::lock_guard lock(mutex);
        summary.fatalError = e.what();
    }

    summary.cancelled = cancel;
    summary.filesSeen = found;
    summary.added = added;
    summary.updated = updated;
    summary.unchanged = unchanged;
    summary.restored = restored;
    summary.markedMissing = markedMissing;
    summary.failed = failed;
    summary.elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - started).count();
    {
        std::lock_guard lock(mutex);
        summary.errors = errors;
        summary.unavailableFolders = unavailable;
        if (summary.fatalError.empty()) {
            summary.fatalError = fatalError;
        }
    }
    phase = ScanProgress::Phase::Done;
    report(true);
    running = false;
    if (onDone) {
        onDone(summary);
    }
}

// ---------------------------------------------------------------- public API

Scanner::Scanner(fs::path dbFile, ScanOptions options) : d_(std::make_unique<Impl>()) {
    d_->dbFile = std::move(dbFile);
    d_->options = options;
}

Scanner::~Scanner() {
    cancel();
    wait();
}

bool Scanner::start(ProgressFn onProgress, DoneFn onDone) {
    if (d_->running) {
        return false;
    }
    if (d_->thread.joinable()) {
        d_->thread.join();  // the previous scan finished; reap its thread
    }
    d_->resetCounters();
    d_->cancel = false;
    d_->onProgress = std::move(onProgress);
    d_->onDone = std::move(onDone);
    d_->running = true;
    d_->thread = std::thread([impl = d_.get()] { impl->run(); });
    return true;
}

void Scanner::cancel() { d_->cancel = true; }

bool Scanner::running() const { return d_->running; }

void Scanner::wait() {
    if (d_->thread.joinable() && d_->thread.get_id() != std::this_thread::get_id()) {
        d_->thread.join();
    }
}

}  // namespace mp::core::library
