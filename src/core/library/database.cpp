#include "core/library/database.hpp"

#include <sqlite3.h>

#include <utility>

#include "core/text.hpp"

namespace mp::core::library {

// ---------------------------------------------------------------- Statement

Statement::Statement(Statement&& other) noexcept : db_(other.db_), stmt_(std::exchange(other.stmt_, nullptr)) {}

Statement& Statement::operator=(Statement&& other) noexcept {
    if (this != &other) {
        if (stmt_ != nullptr) {
            sqlite3_finalize(stmt_);
        }
        db_ = other.db_;
        stmt_ = std::exchange(other.stmt_, nullptr);
    }
    return *this;
}

Statement::~Statement() {
    if (stmt_ != nullptr) {
        sqlite3_finalize(stmt_);
    }
}

Statement& Statement::bindInt(int index, std::int64_t value) {
    if (sqlite3_bind_int64(stmt_, index, value) != SQLITE_OK) {
        db_->fail("bind");
    }
    return *this;
}

Statement& Statement::bind(int index, double value) {
    if (sqlite3_bind_double(stmt_, index, value) != SQLITE_OK) {
        db_->fail("bind");
    }
    return *this;
}

Statement& Statement::bind(int index, std::string_view text) {
    if (sqlite3_bind_text(stmt_, index, text.data(), static_cast<int>(text.size()), SQLITE_TRANSIENT) != SQLITE_OK) {
        db_->fail("bind");
    }
    return *this;
}

Statement& Statement::bind(int index, std::nullptr_t) {
    if (sqlite3_bind_null(stmt_, index) != SQLITE_OK) {
        db_->fail("bind");
    }
    return *this;
}

Statement& Statement::bind(int index, const std::optional<double>& value) {
    return value ? bind(index, *value) : bind(index, nullptr);
}

bool Statement::step() {
    const int rc = sqlite3_step(stmt_);
    if (rc == SQLITE_ROW) {
        return true;
    }
    if (rc == SQLITE_DONE) {
        return false;
    }
    db_->fail("step");
}

void Statement::run() {
    while (step()) {
    }
    reset();
}

void Statement::reset() {
    sqlite3_reset(stmt_);
    sqlite3_clear_bindings(stmt_);
}

std::int64_t Statement::getInt(int column) const { return sqlite3_column_int64(stmt_, column); }

double Statement::getDouble(int column) const { return sqlite3_column_double(stmt_, column); }

std::string Statement::getText(int column) const {
    const unsigned char* text = sqlite3_column_text(stmt_, column);
    if (text == nullptr) {
        return {};
    }
    return std::string(reinterpret_cast<const char*>(text), static_cast<std::size_t>(sqlite3_column_bytes(stmt_, column)));
}

bool Statement::isNull(int column) const { return sqlite3_column_type(stmt_, column) == SQLITE_NULL; }

// ---------------------------------------------------------------- Database

Database::Database(const std::filesystem::path& file) {
    const std::string utf8 = pathToUtf8(file);
    // NOMUTEX: a connection is only ever used by one thread at a time (see class comment).
    const int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_NOMUTEX;
    if (sqlite3_open_v2(utf8.c_str(), &db_, flags, nullptr) != SQLITE_OK) {
        const std::string message = db_ != nullptr ? sqlite3_errmsg(db_) : "out of memory";
        sqlite3_close(db_);
        db_ = nullptr;
        throw DatabaseError("Cannot open database " + utf8 + ": " + message);
    }
    // A second connection (UI thread while the scanner writes) waits instead of failing at once.
    sqlite3_busy_timeout(db_, 5000);
    exec("PRAGMA journal_mode = WAL;"
         "PRAGMA synchronous = NORMAL;"  // safe with WAL: a crash can lose the last commits, never corrupt
         "PRAGMA foreign_keys = ON;"
         "PRAGMA temp_store = MEMORY;"
         "PRAGMA cache_size = -20000;");  // 20 MB page cache
}

Database::~Database() {
    if (db_ != nullptr) {
        sqlite3_close(db_);
    }
}

void Database::fail(std::string_view what) const {
    throw DatabaseError(std::string("SQLite ") + std::string(what) + " failed: " + sqlite3_errmsg(db_));
}

void Database::exec(std::string_view sql) {
    const std::string text(sql);  // sqlite3_exec needs a terminated string
    char* message = nullptr;
    if (sqlite3_exec(db_, text.c_str(), nullptr, nullptr, &message) != SQLITE_OK) {
        const std::string detail = message != nullptr ? message : "unknown error";
        sqlite3_free(message);
        throw DatabaseError("SQLite error: " + detail + "\nin: " + text.substr(0, 200));
    }
}

Statement Database::prepare(std::string_view sql) {
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.data(), static_cast<int>(sql.size()), &stmt, nullptr) != SQLITE_OK) {
        throw DatabaseError(std::string("SQLite prepare failed: ") + sqlite3_errmsg(db_) + "\nin: " +
                            std::string(sql.substr(0, 200)));
    }
    return Statement(this, stmt);
}

std::int64_t Database::scalarInt(std::string_view sql) {
    Statement s = prepare(sql);
    return s.step() ? s.getInt(0) : 0;
}

std::int64_t Database::lastInsertRowId() const { return sqlite3_last_insert_rowid(db_); }

int Database::changes() const { return sqlite3_changes(db_); }

int Database::userVersion() { return static_cast<int>(scalarInt("PRAGMA user_version")); }

// ---------------------------------------------------------------- Transaction

Transaction::Transaction(Database& db) : db_(db) { db_.exec("BEGIN"); }

Transaction::~Transaction() {
    if (open_) {
        try {
            db_.exec("ROLLBACK");
        } catch (...) {
            // The connection is already unusable; nothing more to do while unwinding.
        }
    }
}

void Transaction::commit() {
    db_.exec("COMMIT");
    open_ = false;
}

}  // namespace mp::core::library
