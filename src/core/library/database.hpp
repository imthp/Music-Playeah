#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>

struct sqlite3;
struct sqlite3_stmt;

namespace mp::core::library {

// Anything SQLite reports as an error. The message already contains the SQL when known.
class DatabaseError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class Database;

// One prepared statement. Bind with 1-based indices, then step() (rows) or run() (no rows).
// Reuse it in a loop: reset() clears the bindings. Must not outlive its Database.
class Statement {
public:
    Statement(Statement&& other) noexcept;
    Statement& operator=(Statement&& other) noexcept;
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;
    ~Statement();

    template <typename T>
        requires std::is_integral_v<T>
    Statement& bind(int index, T value) {
        return bindInt(index, static_cast<std::int64_t>(value));
    }
    Statement& bind(int index, double value);
    Statement& bind(int index, std::string_view text);  // copied; binds TEXT
    Statement& bind(int index, const char* text) { return bind(index, std::string_view(text)); }
    Statement& bind(int index, std::nullptr_t);         // binds NULL
    Statement& bind(int index, const std::optional<double>& value);

    // Next row. True if there is one, false when done. Throws on error.
    bool step();
    // Runs a statement that returns no rows (INSERT/UPDATE/DELETE) to completion, then resets it.
    void run();
    void reset();

    std::int64_t getInt(int column) const;
    double getDouble(int column) const;
    std::string getText(int column) const;  // "" for NULL
    bool isNull(int column) const;

private:
    friend class Database;
    Statement(Database* db, sqlite3_stmt* stmt) : db_(db), stmt_(stmt) {}
    Statement& bindInt(int index, std::int64_t value);

    Database* db_ = nullptr;
    sqlite3_stmt* stmt_ = nullptr;
};

// One SQLite connection. Use it from one thread at a time. SPEC §6: WAL mode, so a reader
// connection never blocks the scanner's single writer connection (and vice versa).
class Database {
public:
    // Opens (creating if missing) and sets the pragmas. Does not migrate: call migrate().
    explicit Database(const std::filesystem::path& file);
    ~Database();
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    // Brings the schema up to the newest version (PRAGMA user_version). Each step is one
    // transaction. Throws if the file was written by a newer app.
    void migrate();
    static int latestVersion();
    int userVersion();

    // Runs one or more statements separated by ';' (no bindings, no results).
    void exec(std::string_view sql);
    Statement prepare(std::string_view sql);

    // Convenience for "SELECT one number" queries; 0 if no row.
    std::int64_t scalarInt(std::string_view sql);

    std::int64_t lastInsertRowId() const;
    int changes() const;

    sqlite3* handle() const { return db_; }
    [[noreturn]] void fail(std::string_view what) const;

private:
    sqlite3* db_ = nullptr;
};

// BEGIN ... COMMIT. Rolls back in the destructor unless commit() was called, so an exception
// never leaves a half-written batch behind.
class Transaction {
public:
    explicit Transaction(Database& db);
    ~Transaction();
    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;

    void commit();

private:
    Database& db_;
    bool open_ = true;
};

}  // namespace mp::core::library
