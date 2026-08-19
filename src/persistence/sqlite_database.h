#pragma once

#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>

struct sqlite3;
struct sqlite3_stmt;

namespace modelharbor::persistence {

class DatabaseError final : public std::runtime_error {
  public:
    DatabaseError(int primaryCode, int extendedCode, std::string context, std::string detail);

    int primaryCode() const noexcept { return primaryCode_; }
    int extendedCode() const noexcept { return extendedCode_; }
    const std::string& context() const noexcept { return context_; }

  private:
    int primaryCode_;
    int extendedCode_;
    std::string context_;
};

class Statement final {
  public:
    Statement() = default;
    ~Statement();

    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;
    Statement(Statement&& other) noexcept;
    Statement& operator=(Statement&& other) noexcept;

    void bindInt64(int index, std::int64_t value);
    void bindText(int index, std::string_view value);
    void bindNull(int index);
    bool step();
    void execute();
    void reset();

    std::int64_t columnInt64(int index) const;
    std::string columnText(int index) const;
    bool columnIsNull(int index) const;

  private:
    friend class SqliteDatabase;
    Statement(sqlite3* database, sqlite3_stmt* statement, std::string context);
    void throwLastError(std::string_view operation) const;

    sqlite3* database_ = nullptr;
    sqlite3_stmt* statement_ = nullptr;
    std::string context_;
};

class Transaction;

class SqliteDatabase final {
  public:
    static SqliteDatabase open(const std::filesystem::path& path);

    ~SqliteDatabase();
    SqliteDatabase(const SqliteDatabase&) = delete;
    SqliteDatabase& operator=(const SqliteDatabase&) = delete;
    SqliteDatabase(SqliteDatabase&& other) noexcept;
    SqliteDatabase& operator=(SqliteDatabase&& other) noexcept;

    Statement prepare(std::string_view sql, std::string context = "prepare");
    void execute(std::string_view sql, std::string context = "execute");
    std::int64_t scalarInt64(std::string_view sql, std::string context = "scalar_int64");
    std::string scalarText(std::string_view sql, std::string context = "scalar_text");
    void backupTo(const std::filesystem::path& destination) const;

    const std::filesystem::path& path() const noexcept { return path_; }

  private:
    friend class Transaction;
    explicit SqliteDatabase(sqlite3* database, std::filesystem::path path);
    void close() noexcept;
    void executeNoThrow(std::string_view sql) noexcept;

    sqlite3* database_ = nullptr;
    std::filesystem::path path_;
};

class Transaction final {
  public:
    enum class Mode {
        Deferred,
        Immediate,
    };

    explicit Transaction(SqliteDatabase& database, Mode mode = Mode::Immediate);
    ~Transaction();

    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;
    Transaction(Transaction&&) = delete;
    Transaction& operator=(Transaction&&) = delete;

    void commit();
    void rollback();

  private:
    SqliteDatabase* database_;
    bool finished_ = false;
};

} // namespace modelharbor::persistence
