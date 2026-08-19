#include "persistence/sqlite_database.h"

#include <sqlite3.h>

#include <chrono>
#include <filesystem>
#include <sstream>
#include <system_error>
#include <utility>

#ifdef _WIN32
#include <Windows.h>
#endif

namespace modelharbor::persistence {
namespace {

std::string pathAsUtf8(const std::filesystem::path& path) {
#ifdef _WIN32
    const auto value = path.u8string();
    return {reinterpret_cast<const char*>(value.data()), value.size()};
#else
    return path.string();
#endif
}

std::string errorMessage(sqlite3* database) {
    return database == nullptr ? "SQLite operation failed" : sqlite3_errmsg(database);
}

[[noreturn]] void throwDatabaseError(sqlite3* database, int code, std::string context) {
    const int extended = database == nullptr ? code : sqlite3_extended_errcode(database);
    throw DatabaseError(code & 0xff, extended, std::move(context), errorMessage(database));
}

void atomicReplace(const std::filesystem::path& source, const std::filesystem::path& destination) {
#ifdef _WIN32
    if (!MoveFileExW(source.c_str(), destination.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        throw std::system_error(static_cast<int>(GetLastError()), std::system_category(),
                                "backup_atomic_replace");
    }
#else
    std::error_code error;
    std::filesystem::rename(source, destination, error);
    if (error) {
        throw std::system_error(error, "backup_atomic_replace");
    }
#endif
}

} // namespace

DatabaseError::DatabaseError(int primaryCode, int extendedCode, std::string context,
                             std::string detail)
    : std::runtime_error(context + ": " + detail), primaryCode_(primaryCode),
      extendedCode_(extendedCode), context_(std::move(context)) {}

Statement::Statement(sqlite3* database, sqlite3_stmt* statement, std::string context)
    : database_(database), statement_(statement), context_(std::move(context)) {}

Statement::~Statement() {
    if (statement_ != nullptr) {
        sqlite3_finalize(statement_);
    }
}

Statement::Statement(Statement&& other) noexcept
    : database_(std::exchange(other.database_, nullptr)),
      statement_(std::exchange(other.statement_, nullptr)), context_(std::move(other.context_)) {}

Statement& Statement::operator=(Statement&& other) noexcept {
    if (this == &other) {
        return *this;
    }
    if (statement_ != nullptr) {
        sqlite3_finalize(statement_);
    }
    database_ = std::exchange(other.database_, nullptr);
    statement_ = std::exchange(other.statement_, nullptr);
    context_ = std::move(other.context_);
    return *this;
}

void Statement::throwLastError(std::string_view operation) const {
    throwDatabaseError(database_, sqlite3_errcode(database_),
                       context_ + "." + std::string(operation));
}

void Statement::bindInt64(int index, std::int64_t value) {
    if (sqlite3_bind_int64(statement_, index, value) != SQLITE_OK) {
        throwLastError("bind_int64");
    }
}

void Statement::bindText(int index, std::string_view value) {
    if (sqlite3_bind_text(statement_, index, value.data(), static_cast<int>(value.size()),
                          SQLITE_TRANSIENT) != SQLITE_OK) {
        throwLastError("bind_text");
    }
}

void Statement::bindNull(int index) {
    if (sqlite3_bind_null(statement_, index) != SQLITE_OK) {
        throwLastError("bind_null");
    }
}

bool Statement::step() {
    const int result = sqlite3_step(statement_);
    if (result == SQLITE_ROW) {
        return true;
    }
    if (result == SQLITE_DONE) {
        return false;
    }
    throwDatabaseError(database_, result, context_ + ".step");
}

void Statement::execute() {
    if (step()) {
        throw DatabaseError(SQLITE_MISUSE, SQLITE_MISUSE, context_ + ".execute",
                            "statement unexpectedly returned a row");
    }
}

void Statement::reset() {
    const int resetResult = sqlite3_reset(statement_);
    if (resetResult != SQLITE_OK) {
        throwDatabaseError(database_, resetResult, context_ + ".reset");
    }
    const int clearResult = sqlite3_clear_bindings(statement_);
    if (clearResult != SQLITE_OK) {
        throwDatabaseError(database_, clearResult, context_ + ".clear_bindings");
    }
}

std::int64_t Statement::columnInt64(int index) const {
    return sqlite3_column_int64(statement_, index);
}

std::string Statement::columnText(int index) const {
    const auto* value = sqlite3_column_text(statement_, index);
    if (value == nullptr) {
        return {};
    }
    const int size = sqlite3_column_bytes(statement_, index);
    return {reinterpret_cast<const char*>(value), static_cast<std::size_t>(size)};
}

bool Statement::columnIsNull(int index) const {
    return sqlite3_column_type(statement_, index) == SQLITE_NULL;
}

SqliteDatabase::SqliteDatabase(sqlite3* database, std::filesystem::path path)
    : database_(database), path_(std::move(path)) {}

SqliteDatabase SqliteDatabase::open(const std::filesystem::path& path) {
    if (path.empty()) {
        throw std::invalid_argument("database path is empty");
    }
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path());
    }

    sqlite3* handle = nullptr;
    const std::string utf8Path = pathAsUtf8(path);
    const int result = sqlite3_open_v2(
        utf8Path.c_str(), &handle,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr);
    if (result != SQLITE_OK) {
        const std::string detail = errorMessage(handle);
        const int extended = handle == nullptr ? result : sqlite3_extended_errcode(handle);
        if (handle != nullptr) {
            sqlite3_close_v2(handle);
        }
        throw DatabaseError(result & 0xff, extended, "database_open", detail);
    }

    SqliteDatabase database(handle, path);
    sqlite3_extended_result_codes(handle, 1);
    sqlite3_busy_timeout(handle, 5000);
    database.execute("PRAGMA foreign_keys = ON", "database_config.foreign_keys");
    database.execute("PRAGMA journal_mode = WAL", "database_config.journal_mode");
    database.execute("PRAGMA synchronous = NORMAL", "database_config.synchronous");
    database.execute("PRAGMA temp_store = MEMORY", "database_config.temp_store");
#ifdef SQLITE_DBCONFIG_DEFENSIVE
    sqlite3_db_config(handle, SQLITE_DBCONFIG_DEFENSIVE, 1, nullptr);
#endif
#ifdef SQLITE_DBCONFIG_TRUSTED_SCHEMA
    sqlite3_db_config(handle, SQLITE_DBCONFIG_TRUSTED_SCHEMA, 0, nullptr);
#endif
    return database;
}

SqliteDatabase::~SqliteDatabase() { close(); }

SqliteDatabase::SqliteDatabase(SqliteDatabase&& other) noexcept
    : database_(std::exchange(other.database_, nullptr)), path_(std::move(other.path_)) {}

SqliteDatabase& SqliteDatabase::operator=(SqliteDatabase&& other) noexcept {
    if (this == &other) {
        return *this;
    }
    close();
    database_ = std::exchange(other.database_, nullptr);
    path_ = std::move(other.path_);
    return *this;
}

void SqliteDatabase::close() noexcept {
    if (database_ != nullptr) {
        sqlite3_close_v2(database_);
        database_ = nullptr;
    }
}

Statement SqliteDatabase::prepare(std::string_view sql, std::string context) {
    sqlite3_stmt* statement = nullptr;
    const int result = sqlite3_prepare_v3(database_, sql.data(), static_cast<int>(sql.size()),
                                          SQLITE_PREPARE_PERSISTENT, &statement, nullptr);
    if (result != SQLITE_OK) {
        throwDatabaseError(database_, result, std::move(context));
    }
    return Statement(database_, statement, std::move(context));
}

void SqliteDatabase::execute(std::string_view sql, std::string context) {
    std::string ownedSql(sql);
    char* error = nullptr;
    const int result = sqlite3_exec(database_, ownedSql.c_str(), nullptr, nullptr, &error);
    if (result != SQLITE_OK) {
        const std::string detail = error == nullptr ? errorMessage(database_) : error;
        sqlite3_free(error);
        throw DatabaseError(result & 0xff, sqlite3_extended_errcode(database_), std::move(context),
                            detail);
    }
}

void SqliteDatabase::executeNoThrow(std::string_view sql) noexcept {
    std::string ownedSql(sql);
    sqlite3_exec(database_, ownedSql.c_str(), nullptr, nullptr, nullptr);
}

std::int64_t SqliteDatabase::scalarInt64(std::string_view sql, std::string context) {
    auto statement = prepare(sql, std::move(context));
    if (!statement.step()) {
        throw DatabaseError(SQLITE_NOTFOUND, SQLITE_NOTFOUND, "scalar_int64",
                            "query returned no row");
    }
    return statement.columnInt64(0);
}

std::string SqliteDatabase::scalarText(std::string_view sql, std::string context) {
    auto statement = prepare(sql, std::move(context));
    if (!statement.step()) {
        throw DatabaseError(SQLITE_NOTFOUND, SQLITE_NOTFOUND, "scalar_text",
                            "query returned no row");
    }
    return statement.columnText(0);
}

void SqliteDatabase::backupTo(const std::filesystem::path& destination) const {
    if (destination.empty()) {
        throw std::invalid_argument("backup destination is empty");
    }
    const auto sourcePath = std::filesystem::absolute(path_).lexically_normal();
    const auto destinationPath = std::filesystem::absolute(destination).lexically_normal();
    if (sourcePath == destinationPath) {
        throw std::invalid_argument("backup destination matches source database");
    }
    if (!destinationPath.parent_path().empty()) {
        std::filesystem::create_directories(destinationPath.parent_path());
    }

    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    std::filesystem::path temporary = destinationPath;
    temporary += ".tmp-" + std::to_string(nonce);
    std::error_code cleanupError;
    std::filesystem::remove(temporary, cleanupError);

    sqlite3* target = nullptr;
    const std::string targetUtf8 = pathAsUtf8(temporary);
    int result = sqlite3_open_v2(targetUtf8.c_str(), &target,
                                 SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
                                 nullptr);
    if (result != SQLITE_OK) {
        const std::string detail = errorMessage(target);
        if (target != nullptr) {
            sqlite3_close_v2(target);
        }
        throw DatabaseError(result & 0xff, result, "backup_open", detail);
    }

    sqlite3_backup* backup = sqlite3_backup_init(target, "main", database_, "main");
    if (backup == nullptr) {
        const int extended = sqlite3_extended_errcode(target);
        const std::string detail = errorMessage(target);
        sqlite3_close_v2(target);
        std::filesystem::remove(temporary, cleanupError);
        throw DatabaseError(extended & 0xff, extended, "backup_init", detail);
    }

    int busyAttempts = 0;
    do {
        result = sqlite3_backup_step(backup, 256);
        if (result == SQLITE_BUSY || result == SQLITE_LOCKED) {
            if (++busyAttempts > 500) {
                break;
            }
            sqlite3_sleep(10);
        }
    } while (result == SQLITE_OK || result == SQLITE_BUSY || result == SQLITE_LOCKED);

    const int finishResult = sqlite3_backup_finish(backup);
    if (result == SQLITE_DONE) {
        result = finishResult;
    }
    if (result == SQLITE_OK) {
        result = sqlite3_errcode(target);
    }
    const int extended = sqlite3_extended_errcode(target);
    const std::string detail = errorMessage(target);
    const int closeResult = sqlite3_close_v2(target);

    if (result != SQLITE_OK || closeResult != SQLITE_OK) {
        std::filesystem::remove(temporary, cleanupError);
        const int reported = result == SQLITE_OK ? closeResult : result;
        throw DatabaseError(reported & 0xff, extended, "backup_copy", detail);
    }

    try {
        atomicReplace(temporary, destinationPath);
    } catch (...) {
        std::filesystem::remove(temporary, cleanupError);
        throw;
    }
}

Transaction::Transaction(SqliteDatabase& database, Mode mode) : database_(&database) {
    database_->execute(mode == Mode::Immediate ? "BEGIN IMMEDIATE" : "BEGIN DEFERRED",
                       "transaction_begin");
}

Transaction::~Transaction() {
    if (!finished_ && database_ != nullptr) {
        database_->executeNoThrow("ROLLBACK");
    }
}

void Transaction::commit() {
    if (finished_) {
        throw std::logic_error("transaction is already finished");
    }
    database_->execute("COMMIT", "transaction_commit");
    finished_ = true;
}

void Transaction::rollback() {
    if (finished_) {
        throw std::logic_error("transaction is already finished");
    }
    database_->execute("ROLLBACK", "transaction_rollback");
    finished_ = true;
}

} // namespace modelharbor::persistence
