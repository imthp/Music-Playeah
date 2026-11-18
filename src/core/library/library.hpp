#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "core/library/database.hpp"

namespace mp::core::library {

struct FolderInfo {
    std::int64_t id = 0;
    std::string path;  // UTF-8, '/' separators
};

struct LibraryCounts {
    std::int64_t tracks = 0;   // files that currently exist
    std::int64_t missing = 0;  // known files in a *watched* folder that are gone (unplugged drive, deleted...)
    std::int64_t unwatched = 0;  // remembered files of folders the user removed: not "missing", just not watched
                                 // (kept so history survives and a re-added or moved folder is re-matched)
    std::int64_t albums = 0;   // with at least one existing track
    std::int64_t artists = 0;  // performers, album artists and composers of existing tracks
};

struct AddFolderResult {
    bool ok = false;
    std::string error;  // set when !ok
    std::string path;   // normalized path that was stored
};

// The library database plus the operations that are quick enough for the UI thread: watched
// folders and counts. Scanning is Scanner's job (own thread, own connection).
// Not thread-safe: use it from one thread (the UI thread).
class Library {
public:
    // Opens or creates the database file (and its folder) and migrates it. Throws DatabaseError.
    explicit Library(std::filesystem::path dbFile);

    const std::filesystem::path& dbFile() const { return dbFile_; }
    Database& db() { return db_; }

    std::vector<FolderInfo> folders();

    // Folders must exist and must not contain, or lie inside, an already watched folder
    // (the same file would be scanned twice).
    AddFolderResult addFolder(const std::filesystem::path& folder);

    // Stops watching a folder. Its tracks are marked missing, not deleted, so history and
    // overrides survive (SPEC §7). Returns false if the folder wasn't watched.
    bool removeFolder(const std::string& folderPath);

    LibraryCounts counts();

private:
    std::filesystem::path dbFile_;
    Database db_;
};

// Normalizes a folder the way it is stored: absolute, '/' separators, no trailing slash.
std::string normalizeFolderPath(const std::filesystem::path& folder);

// The two bounds of `path >= low AND path < high` that select everything under `folderPath`.
// Uses the index on tracks.path (unlike LIKE) and needs no escaping of % or _.
struct PathRange {
    std::string low;
    std::string high;
};
PathRange pathRangeUnder(const std::string& folderPath);

}  // namespace mp::core::library
