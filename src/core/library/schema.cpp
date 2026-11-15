// The database schema and its migrations (SPEC §6). Compiled into Database::migrate().
//
// Rules for changing the schema later:
//  - Never edit a migration that has shipped. Add a new one at the end of kMigrations.
//  - Each migration runs in one transaction and ends by bumping PRAGMA user_version.
//  - Raw file tags live in `tracks`; corrections live in `tag_overrides` (D18). Everything that
//    shows, searches or scrobbles a tag reads the `v_tracks` view, never `tracks` directly.

#include <iterator>
#include <string>

#include "core/library/database.hpp"

namespace mp::core::library {

namespace {

// ---- v1 ---------------------------------------------------------------------------------

// Tags a user may override (D18). The name is both the `tag_overrides.field` value and the
// `tracks` column it replaces, so the view below can be generated instead of written 18 times.
constexpr const char* kOverridableText[] = {"title",    "artist",         "album_artist",   "album",
                                            "composer", "genre",          "work_tag",       "movement_name"};
constexpr const char* kOverridableInt[] = {"year", "disc_no", "track_no", "movement_no", "movement_total"};

// Columns the view passes through untouched.
constexpr const char* kPassThrough[] = {"id",          "path",       "mtime",        "size",         "artist_id",
                                        "composer_id", "work_id",    "album_id",     "duration_ms",  "codec",
                                        "sample_rate", "bit_depth",  "channels",     "bitrate",      "rg_track_gain",
                                        "rg_album_gain", "art_hash", "added_at",     "missing_since"};

std::string effective(const char* field, bool integer) {
    const std::string f = field;
    const std::string pick = integer ? "CAST(o.value AS INTEGER)" : "o.value";
    return "COALESCE((SELECT " + pick + " FROM tag_overrides o WHERE o.track_id = t.id AND o.field = '" + f +
           "'), t." + f + ") AS " + f;
}

// v_tracks: the *effective* tags. Override if present, else the file tag. `missing_since` is
// passed through; screens filter `WHERE missing_since IS NULL`.
std::string tracksView() {
    std::string sql = "CREATE VIEW v_tracks AS SELECT ";
    bool first = true;
    auto add = [&](const std::string& column) {
        sql += first ? "" : ", ";
        sql += column;
        first = false;
    };
    for (const char* c : kPassThrough) {
        add(std::string("t.") + c);
    }
    for (const char* c : kOverridableText) {
        add(effective(c, false));
    }
    for (const char* c : kOverridableInt) {
        add(effective(c, true));
    }
    sql += " FROM tracks t";
    return sql;
}

void migrateToV1(Database& db) {
    db.exec(R"sql(
CREATE TABLE folders (
  id        INTEGER PRIMARY KEY,
  path      TEXT NOT NULL UNIQUE,            -- UTF-8, '/' separators, no trailing slash
  added_at  INTEGER NOT NULL
);

-- Performers and composers alike; the role comes from the track column (SPEC §6).
CREATE TABLE artists (
  id         INTEGER PRIMARY KEY,
  name       TEXT NOT NULL,
  name_key   TEXT NOT NULL UNIQUE,           -- normalizeText(name)
  sort_name  TEXT NOT NULL,
  mbid       TEXT
);

CREATE TABLE albums (
  id               INTEGER PRIMARY KEY,
  title            TEXT NOT NULL,
  album_artist     TEXT NOT NULL,
  album_artist_id  INTEGER REFERENCES artists(id) ON DELETE SET NULL,
  year             INTEGER NOT NULL DEFAULT 0,
  art_hash         TEXT,
  sort_key         TEXT NOT NULL UNIQUE      -- normalized album_artist + 0x1F + normalized title
);

-- One row per work across the whole library (D24). Filled by milestone 3b.
CREATE TABLE works (
  id          INTEGER PRIMARY KEY,
  composer_id INTEGER REFERENCES artists(id) ON DELETE SET NULL,
  title       TEXT NOT NULL,
  catalog_no  TEXT,
  sort_key    TEXT NOT NULL,
  work_key    TEXT NOT NULL UNIQUE           -- normalized composer + 0x1F + (catalog_no, or title if none)
);

CREATE TABLE tracks (
  id              INTEGER PRIMARY KEY AUTOINCREMENT,   -- ids are never reused: plays/overrides point at them
  path            TEXT NOT NULL UNIQUE,
  mtime           INTEGER NOT NULL,          -- file_time_type ticks in microseconds (only compared, never shown)
  size            INTEGER NOT NULL,
  -- Raw file tags ("" / 0 when absent). Read them through v_tracks, not from here.
  title           TEXT NOT NULL DEFAULT '',
  artist          TEXT NOT NULL DEFAULT '',
  artist_id       INTEGER REFERENCES artists(id) ON DELETE SET NULL,
  album_artist    TEXT NOT NULL DEFAULT '',
  composer        TEXT NOT NULL DEFAULT '',
  composer_id     INTEGER REFERENCES artists(id) ON DELETE SET NULL,
  album           TEXT NOT NULL DEFAULT '',
  work_tag        TEXT NOT NULL DEFAULT '',  -- raw WORK tag; grouping into `works` is milestone 3b
  work_id         INTEGER REFERENCES works(id) ON DELETE SET NULL,
  movement_no     INTEGER NOT NULL DEFAULT 0,
  movement_total  INTEGER NOT NULL DEFAULT 0,
  movement_name   TEXT NOT NULL DEFAULT '',
  disc_no         INTEGER NOT NULL DEFAULT 0,
  track_no        INTEGER NOT NULL DEFAULT 0,
  year            INTEGER NOT NULL DEFAULT 0,
  genre           TEXT NOT NULL DEFAULT '',
  -- Audio properties
  duration_ms     INTEGER NOT NULL DEFAULT 0,
  codec           TEXT NOT NULL DEFAULT '',  -- "FLAC", "MP3", "AAC", "ALAC", "WAV", "AIFF", "DSD", "Opus", "Vorbis"
  sample_rate     INTEGER NOT NULL DEFAULT 0,
  bit_depth       INTEGER NOT NULL DEFAULT 0,
  channels        INTEGER NOT NULL DEFAULT 0,
  bitrate         INTEGER NOT NULL DEFAULT 0,   -- kbps
  rg_track_gain   REAL,                      -- dB, NULL if the file has none
  rg_album_gain   REAL,
  -- Library bookkeeping
  album_id        INTEGER REFERENCES albums(id) ON DELETE SET NULL,
  art_hash        TEXT,
  added_at        INTEGER NOT NULL,          -- Unix seconds
  missing_since   INTEGER,                   -- Unix seconds; NULL while the file exists (never deleted: history survives)
  resolved        INTEGER NOT NULL DEFAULT 0,-- 0 = artist/album ids still to be derived from the tags
  match_key       TEXT NOT NULL DEFAULT ''   -- album + disc + track, to re-find a file that moved (SPEC §7)
);
CREATE INDEX idx_tracks_album_id    ON tracks(album_id);
CREATE INDEX idx_tracks_artist_id   ON tracks(artist_id);
CREATE INDEX idx_tracks_composer_id ON tracks(composer_id);
CREATE INDEX idx_tracks_work_id     ON tracks(work_id);
CREATE INDEX idx_tracks_match_key   ON tracks(match_key) WHERE match_key <> '';
CREATE INDEX idx_tracks_missing     ON tracks(missing_since) WHERE missing_since IS NOT NULL;
CREATE INDEX idx_tracks_unresolved  ON tracks(resolved) WHERE resolved = 0;
CREATE INDEX idx_albums_artist_id   ON albums(album_artist_id);
CREATE INDEX idx_works_composer_id  ON works(composer_id);

-- Who played what (instrument/role), from file tags now, MusicBrainz later (D27).
CREATE TABLE credits (
  track_id    INTEGER NOT NULL REFERENCES tracks(id) ON DELETE CASCADE,
  artist_id   INTEGER NOT NULL REFERENCES artists(id) ON DELETE CASCADE,
  role        TEXT NOT NULL,                 -- performer / conductor / ensemble / ...
  instrument  TEXT,
  source      TEXT NOT NULL DEFAULT 'file'   -- file / musicbrainz / user
);
CREATE INDEX idx_credits_track  ON credits(track_id);
CREATE INDEX idx_credits_artist ON credits(artist_id);

CREATE TABLE album_tags (
  album_id  INTEGER NOT NULL REFERENCES albums(id) ON DELETE CASCADE,
  tag       TEXT NOT NULL,
  kind      TEXT NOT NULL,                   -- instrument / user
  source    TEXT NOT NULL DEFAULT 'user',    -- file / musicbrainz / user
  PRIMARY KEY (album_id, tag, kind)
);

-- Online artist/composer info cache (SPEC §8c, milestone 9).
CREATE TABLE artist_info (
  artist_id   INTEGER PRIMARY KEY REFERENCES artists(id) ON DELETE CASCADE,
  summary     TEXT,
  born        TEXT,
  died        TEXT,
  image_hash  TEXT,
  source_url  TEXT,
  license     TEXT,
  fetched_at  INTEGER
);

-- Covers and artist photos the user chose (D25). Files stay untouched.
CREATE TABLE image_overrides (
  kind        TEXT NOT NULL,                 -- album / artist
  ref_id      INTEGER NOT NULL,
  image_hash  TEXT NOT NULL,
  source      TEXT NOT NULL,                 -- file / url / coverartarchive
  updated_at  INTEGER NOT NULL,
  PRIMARY KEY (kind, ref_id)
);

-- Corrections that win over the file tags (D18). `field` is a tracks column name.
CREATE TABLE tag_overrides (
  track_id    INTEGER NOT NULL REFERENCES tracks(id) ON DELETE CASCADE,
  field       TEXT NOT NULL,
  value       TEXT NOT NULL,
  source      TEXT NOT NULL,                 -- manual / musicbrainz / discogs
  source_id   TEXT,                          -- release id, so a fix can be refreshed
  updated_at  INTEGER NOT NULL,
  PRIMARY KEY (track_id, field)
);

CREATE TABLE plays (
  id          INTEGER PRIMARY KEY,
  track_id    INTEGER NOT NULL REFERENCES tracks(id),
  started_at  INTEGER NOT NULL,
  played_ms   INTEGER NOT NULL,
  completed   INTEGER NOT NULL DEFAULT 0
);
CREATE INDEX idx_plays_track   ON plays(track_id);
CREATE INDEX idx_plays_started ON plays(started_at);

CREATE TABLE scrobble_queue (
  id           INTEGER PRIMARY KEY,
  artist       TEXT NOT NULL,
  track        TEXT NOT NULL,
  album        TEXT,
  started_at   INTEGER NOT NULL,
  duration_s   INTEGER,
  attempts     INTEGER NOT NULL DEFAULT 0
);

CREATE TABLE playlists (
  id          INTEGER PRIMARY KEY,
  name        TEXT NOT NULL,
  created_at  INTEGER NOT NULL
);
CREATE TABLE playlist_items (
  playlist_id INTEGER NOT NULL REFERENCES playlists(id) ON DELETE CASCADE,
  position    INTEGER NOT NULL,
  track_id    INTEGER NOT NULL REFERENCES tracks(id),
  PRIMARY KEY (playlist_id, position)
);

CREATE TABLE lastfm_cache (
  method      TEXT NOT NULL,
  key         TEXT NOT NULL,
  json        TEXT NOT NULL,
  fetched_at  INTEGER NOT NULL,              -- 7-day TTL (SPEC §8)
  PRIMARY KEY (method, key)
);

-- 2,048 bins x (peak, RMS), 8 bits each; made when a track first plays (D15).
CREATE TABLE waveforms (
  track_id  INTEGER PRIMARY KEY REFERENCES tracks(id) ON DELETE CASCADE,
  bins      BLOB NOT NULL,
  made_at   INTEGER NOT NULL
);
)sql");
    db.exec(tracksView());
}

struct Migration {
    int version;
    void (*apply)(Database&);
};

constexpr Migration kMigrations[] = {
    {1, migrateToV1},
};

}  // namespace

int Database::latestVersion() { return kMigrations[std::size(kMigrations) - 1].version; }

void Database::migrate() {
    const int current = userVersion();
    if (current > latestVersion()) {
        throw DatabaseError("The library database (version " + std::to_string(current) +
                            ") was written by a newer version of the app (this one knows up to " +
                            std::to_string(latestVersion()) + ").");
    }
    for (const Migration& m : kMigrations) {
        if (m.version <= current) {
            continue;
        }
        Transaction tx(*this);
        m.apply(*this);
        exec("PRAGMA user_version = " + std::to_string(m.version));
        tx.commit();
    }
}

}  // namespace mp::core::library
