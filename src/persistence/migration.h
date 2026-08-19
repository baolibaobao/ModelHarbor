#pragma once

#include "persistence/sqlite_database.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

namespace modelharbor::persistence {

struct Migration {
    int version = 0;
    std::string name;
    std::string sql;
};

class MigrationError final : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

std::vector<Migration> builtInMigrations();
std::vector<Migration> loadMigrations(const std::filesystem::path& directory);
std::string migrationChecksum(const Migration& migration);

class MigrationRunner final {
  public:
    using UtcNowProvider = std::function<std::int64_t()>;

    explicit MigrationRunner(UtcNowProvider utcNow = {});

    std::vector<int> apply(SqliteDatabase& database,
                           const std::vector<Migration>& migrations) const;
    static int currentVersion(SqliteDatabase& database);

  private:
    UtcNowProvider utcNow_;
};

} // namespace modelharbor::persistence
