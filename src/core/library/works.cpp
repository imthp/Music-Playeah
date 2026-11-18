#include "core/library/works.hpp"

#include <algorithm>
#include <functional>
#include <optional>
#include <unordered_map>
#include <vector>

#include "core/library/work_identity.hpp"
#include "core/library/work_parser.hpp"
#include "core/library/work_tree.hpp"
#include "core/text.hpp"

namespace mp::core::library {

namespace {

// D39: a work known only by form + number ("Lute Suite No. 2", no catalog number) joins the work of the
// same composer that has a catalog number and was seen under the same form + number, when there is exactly
// one such work.
// Two (editions number the suites differently) means the number cannot be trusted: nothing is merged.
// Runs over the whole table, so it does not matter which recording was scanned first.
std::vector<std::int64_t> mergeNumberedIntoCatalogued(Database& db) {
    std::vector<std::pair<std::int64_t, std::int64_t>> moves;  // from, to
    {
        // Decided by the work keys, not catalog_no: the key says what identified the work.
        Statement s = db.prepare(
            "WITH k AS (SELECT id, substr(work_key, 1, instr(work_key, char(31))) AS who, "
            "substr(work_key, instr(work_key, char(31)) + 1) AS what, number_key FROM works) "
            "SELECT w.id, MIN(c.id), COUNT(c.id) FROM k w JOIN k c "
            "ON c.who = w.who AND c.what LIKE 'c:%' "
            "AND instr(char(10) || c.number_key || char(10), char(10) || w.what || char(10)) > 0 "
            "WHERE w.what LIKE 'f:%#%' GROUP BY w.id");
        while (s.step()) {
            if (s.getInt(2) == 1) {
                moves.emplace_back(s.getInt(0), s.getInt(1));
            }
        }
    }
    Statement move = db.prepare("UPDATE tracks SET work_id = ?2 WHERE work_id = ?1");
    Statement titles = db.prepare(
        "UPDATE works SET titles = titles || char(10) || (SELECT title FROM works WHERE id = ?1) "
        "WHERE id = ?2 AND instr(char(10) || titles || char(10), char(10) || (SELECT title FROM works WHERE id = ?1) || char(10)) = 0");
    for (const auto& [from, to] : moves) {
        move.bind(1, from).bind(2, to);
        move.run();
        titles.bind(1, from).bind(2, to);
        titles.run();
    }
    std::vector<std::int64_t> targets;
    for (const auto& [from, to] : moves) {
        targets.push_back(to);
    }
    return targets;
}

// The Works view's family of each work (D38), from all its titles, so browsing runs no regex.
// The title is first read as stored, so a work's first title (its own) wins ties.
void storeFamilies(Database& db, const std::vector<std::int64_t>& ids) {
    Statement read = db.prepare("SELECT title, titles, substr(work_key, 1, instr(work_key, char(31))) FROM works WHERE id = ?1");
    // The shown title becomes the most common wording of the winning family ("Bach autrement II" or an
    // "arr. for guitar" title does not name the work just because it was scanned first).
    Statement write = db.prepare(
        "UPDATE works SET family_key = ?2, family_label = ?3, title = ?4, sort_key = ?5 WHERE id = ?1");
    Statement size = db.prepare(
        "SELECT COUNT(*) FROM works WHERE family_key = ?1 AND id <> ?2 AND substr(work_key, 1, instr(work_key, char(31))) = ?3");

    // Pass 1: every work by its own titles. Pass 2: ties, broken by the families pass 1 formed (D40).
    std::vector<std::int64_t> tiedIds;
    for (int pass = 1; pass <= 2; ++pass) {
        for (const std::int64_t id : pass == 1 ? ids : tiedIds) {
            read.bind(1, id);
            if (!read.step()) {
                read.reset();
                continue;
            }
            const std::string title = read.getText(0);
            const std::string all = read.getText(1);
            const std::string composer = read.getText(2);
            read.reset();
            std::vector<std::string> titles;
            for (std::size_t start = 0; start <= all.size();) {
                const std::size_t nl = std::min(all.find('\n', start), all.size());
                if (nl > start) {
                    titles.push_back(all.substr(start, nl - start));
                }
                start = nl + 1;
            }
            std::function<int(const std::string&)> familySize;
            if (pass == 2) {
                familySize = [&](const std::string& key) {
                    size.bind(1, key).bind(2, id).bind(3, composer);
                    const int n = size.step() ? static_cast<int>(size.getInt(0)) : 0;
                    size.reset();
                    return n;
                };
            }
            const WorkFamilyVote f = familyOfWork(title, titles, familySize);
            if (pass == 1 && f.tied) {
                tiedIds.push_back(id);
            }
            const std::string shown = f.title.empty() ? title : f.title;
            write.bind(1, id).bind(2, f.key).bind(3, f.label).bind(4, shown).bind(5, normalizeText(shown));
            write.run();
        }
    }
}

}  // namespace

WorkStats resolveWorks(Database& db) {
    WorkStats stats;
    Transaction tx(db);

    // Composer spellings of the whole library: a bare "Tchaikovsky" is only assigned once the library
    // knows who the Tchaikovsky is. When the mapping changes (a new spelling or a second person with the
    // same surname arrived), stored work keys may be stale: everything is detected again.
    std::vector<std::string> composerTags;
    {
        Statement s = db.prepare("SELECT DISTINCT composer FROM v_tags WHERE composer <> ''");
        while (s.step()) {
            composerTags.push_back(s.getText(0));
        }
    }
    const ComposerNames composers(composerTags);
    {
        Statement s = db.prepare("SELECT value FROM meta WHERE key = 'composer_fingerprint'");
        const bool known = s.step();
        const std::string stored = known ? s.getText(0) : std::string();
        s.reset();
        if (!known || stored != composers.fingerprint()) {
            db.exec("UPDATE tracks SET works_done = 0");
            Statement put = db.prepare("INSERT OR REPLACE INTO meta(key, value) VALUES ('composer_fingerprint', ?1)");
            put.bind(1, composers.fingerprint());
            put.run();
        }
    }

    // Neighbours: a changed track can change how its album groups.
    db.exec(
        "UPDATE tracks SET works_done = 0 WHERE works_done = 1 AND album_id IN "
        "(SELECT album_id FROM tracks WHERE works_done = 0 AND album_id IS NOT NULL)");

    std::vector<WorkTrackInput> inputs;
    std::vector<std::int64_t> composerIds;  // parallel to inputs
    {
        Statement s = db.prepare(
            "SELECT g.id, g.album_id, g.disc_no, g.track_no, g.title, g.composer, g.album_artist, g.work_tag, "
            "g.movement_name, g.movement_no, g.movement_total, g.composer_id "
            "FROM v_tags g JOIN tracks t ON t.id = g.id WHERE t.works_done = 0 AND t.resolved = 1");
        while (s.step()) {
            WorkTrackInput in;
            in.id = s.getInt(0);
            in.albumId = s.getInt(1);
            in.discNo = static_cast<int>(s.getInt(2));
            in.trackNo = static_cast<int>(s.getInt(3));
            in.title = s.getText(4);
            in.composer = s.getText(5);
            in.albumArtist = s.getText(6);
            in.workTag = s.getText(7);
            in.movementName = s.getText(8);
            in.movementNo = static_cast<int>(s.getInt(9));
            in.movementTotal = static_cast<int>(s.getInt(10));
            composerIds.push_back(s.getInt(11));
            inputs.push_back(std::move(in));
        }
    }
    if (inputs.empty()) {
        tx.commit();
        return stats;
    }

    const std::vector<WorkAssignment> found = detectWorks(inputs, &composers);

    std::unordered_map<std::string, std::int64_t> workIds;
    Statement findWork = db.prepare("SELECT id FROM works WHERE work_key = ?1");
    Statement insertWork = db.prepare(
        "INSERT INTO works(composer_id, title, catalog_no, sort_key, work_key, number_key, titles) "
        "VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?2)");
    // Every title a work is seen under (up to 16, newline-separated): the Works view names families by a vote
    // over them (D38). number_key: every form + number key its titles give ("f:concerto [brand]#1" from
    // "Brandenburg Concerto No. 1", "f:concerto#1" from "Concerto No. 1, BWV 1046"), newline-separated.
    Statement addTitle = db.prepare(
        "UPDATE works SET titles = CASE WHEN instr(char(10) || titles || char(10), char(10) || ?2 || char(10)) > 0 "
        "OR (length(titles) - length(replace(titles, char(10), ''))) >= 15 THEN titles "
        "WHEN titles = '' THEN ?2 ELSE titles || char(10) || ?2 END, "
        "number_key = CASE WHEN ?3 = '' OR instr(char(10) || number_key || char(10), char(10) || ?3 || char(10)) > 0 "
        "THEN number_key WHEN number_key = '' THEN ?3 ELSE number_key || char(10) || ?3 END WHERE id = ?1");
    auto workId = [&](const WorkAssignment& a, std::int64_t composerId) -> std::int64_t {
        std::int64_t id = 0;
        if (const auto it = workIds.find(a.workKey); it != workIds.end()) {
            id = it->second;
        } else {
            findWork.bind(1, a.workKey);
            if (findWork.step()) {
                id = findWork.getInt(0);
            }
            findWork.reset();
        }
        if (id == 0) {
            composerId != 0 ? insertWork.bind(1, composerId) : insertWork.bind(1, nullptr);
            insertWork.bind(2, a.workTitle);
            a.catalogNo.empty() ? insertWork.bind(3, nullptr) : insertWork.bind(3, a.catalogNo);
            insertWork.bind(4, normalizeText(a.workTitle));
            insertWork.bind(5, a.workKey);
            insertWork.bind(6, a.numberKey);
            insertWork.run();
            id = db.lastInsertRowId();
            ++stats.worksCreated;
        } else {
            addTitle.bind(1, id).bind(2, a.workTitle).bind(3, a.numberKey);
            addTitle.run();
        }
        workIds.emplace(a.workKey, id);
        return id;
    };

    Statement update = db.prepare(
        "UPDATE tracks SET work_id = ?1, det_movement_no = ?2, det_movement_name = ?3, works_done = 1 WHERE id = ?4");
    for (std::size_t i = 0; i < found.size(); ++i) {
        const WorkAssignment& a = found[i];
        ++stats.tracks;
        if (a.source == WorkSource::None) {
            update.bind(1, nullptr).bind(2, 0).bind(3, "").bind(4, a.trackId);
        } else {
            ++stats.tracksInWorks;
            update.bind(1, workId(a, composerIds[i]));
            // Always stored; v_tracks prefers the file's own tag when there is one, so these only show where
            // the tag is missing.
            update.bind(2, a.movementNo);
            update.bind(3, a.movementName);
            update.bind(4, a.trackId);
        }
        update.run();
    }

    std::vector<std::int64_t> touched;
    for (const auto& [key, id] : workIds) {
        touched.push_back(id);
    }
    for (const std::int64_t id : mergeNumberedIntoCatalogued(db)) {
        touched.push_back(id);
    }
    storeFamilies(db, touched);
    db.exec(
        "DELETE FROM works WHERE NOT EXISTS (SELECT 1 FROM tracks WHERE work_id = works.id)");
    tx.commit();
    return stats;
}

}  // namespace mp::core::library
