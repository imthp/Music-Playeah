#include "core/library/library.hpp"

#include <chrono>
#include <system_error>

#include "core/text.hpp"

namespace mp::core::library {

namespace fs = std::filesystem;

namespace {

std::int64_t nowSeconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}

// Is `inner` the same folder as `outer` or inside it? Case-insensitive (Windows paths are).
bool isInsideOrSame(const std::string& inner, const std::string& outer) {
    const std::string a = normalizeText(inner);
    const std::string b = normalizeText(outer);
    if (a == b) {
        return true;
    }
    return a.size() > b.size() && a.compare(0, b.size(), b) == 0 && a[b.size()] == '/';
}

}  // namespace

std::string normalizeFolderPath(const fs::path& folder) {
    std::error_code ec;
    fs::path absolute = fs::absolute(folder, ec);
    if (ec) {
        absolute = folder;
    }
    std::string text = genericUtf8(absolute.lexically_normal());
    while (text.size() > 1 && text.back() == '/' && !(text.size() == 3 && text[1] == ':')) {
        text.pop_back();
    }
    return text;
}

PathRange pathRangeUnder(const std::string& folderPath) {
    // '0' is the character right after '/', so [P + "/", P + "0") is exactly the paths under P.
    std::string base = folderPath;
    while (!base.empty() && base.back() == '/') {
        base.pop_back();
    }
    return {base + "/", base + "0"};
}

Library::Library(fs::path dbFile) : dbFile_(std::move(dbFile)), db_([&]() -> Database {
        std::error_code ec;
        if (dbFile_.has_parent_path()) {
            fs::create_directories(dbFile_.parent_path(), ec);
        }
        return Database(dbFile_);
    }()) {
    db_.migrate();
}

std::vector<FolderInfo> Library::folders() {
    std::vector<FolderInfo> out;
    Statement s = db_.prepare("SELECT id, path FROM folders ORDER BY path");
    while (s.step()) {
        out.push_back({s.getInt(0), s.getText(1)});
    }
    return out;
}

AddFolderResult Library::addFolder(const fs::path& folder) {
    AddFolderResult result;
    std::error_code ec;
    if (!fs::is_directory(folder, ec)) {
        result.error = "Not a folder: " + pathToUtf8(folder);
        return result;
    }
    const std::string path = normalizeFolderPath(folder);
    for (const FolderInfo& existing : folders()) {
        if (isInsideOrSame(path, existing.path)) {
            result.error = path == existing.path ? "Already in the library: " + path
                                                 : path + " is inside " + existing.path + ", which is already watched";
            return result;
        }
        if (isInsideOrSame(existing.path, path)) {
            result.error = path + " contains " + existing.path +
                           ", which is already watched. Remove that folder first.";
            return result;
        }
    }
    Statement insert = db_.prepare("INSERT INTO folders(path, added_at) VALUES (?1, ?2)");
    insert.bind(1, path).bind(2, nowSeconds());
    insert.run();
    result.ok = true;
    result.path = path;
    return result;
}

bool Library::removeFolder(const std::string& folderPath) {
    Transaction tx(db_);
    Statement find = db_.prepare("SELECT id FROM folders WHERE path = ?1");
    find.bind(1, folderPath);
    if (!find.step()) {
        return false;
    }
    const std::int64_t id = find.getInt(0);
    find.reset();

    const PathRange range = pathRangeUnder(folderPath);
    Statement mark = db_.prepare(
        "UPDATE tracks SET missing_since = ?1 WHERE missing_since IS NULL AND path >= ?2 AND path < ?3");
    mark.bind(1, nowSeconds()).bind(2, range.low).bind(3, range.high);
    mark.run();

    Statement remove = db_.prepare("DELETE FROM folders WHERE id = ?1");
    remove.bind(1, id);
    remove.run();
    tx.commit();
    return true;
}

LibraryCounts Library::counts() {
    LibraryCounts c;
    c.tracks = db_.scalarInt("SELECT COUNT(*) FROM tracks WHERE missing_since IS NULL");
    c.missing = db_.scalarInt("SELECT COUNT(*) FROM tracks WHERE missing_since IS NOT NULL");
    c.albums = db_.scalarInt(
        "SELECT COUNT(DISTINCT album_id) FROM tracks WHERE missing_since IS NULL AND album_id IS NOT NULL");
    c.artists = db_.scalarInt(
        "SELECT COUNT(*) FROM ("
        " SELECT artist_id AS id FROM tracks WHERE missing_since IS NULL AND artist_id IS NOT NULL"
        " UNION SELECT composer_id FROM tracks WHERE missing_since IS NULL AND composer_id IS NOT NULL"
        " UNION SELECT al.album_artist_id FROM albums al WHERE al.album_artist_id IS NOT NULL AND EXISTS ("
        "   SELECT 1 FROM tracks t WHERE t.album_id = al.id AND t.missing_since IS NULL))");
    return c;
}

}  // namespace mp::core::library
