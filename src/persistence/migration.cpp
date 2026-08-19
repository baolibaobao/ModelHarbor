#include "persistence/migration.h"

#include "persistence/builtin_migration_sql.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <regex>
#include <sstream>
#include <string_view>

namespace modelharbor::persistence {
namespace {

constexpr std::string_view kCreateMigrationTable = R"SQL(
CREATE TABLE IF NOT EXISTS schema_migrations (
    version INTEGER PRIMARY KEY CHECK(version > 0),
    name TEXT NOT NULL,
    checksum TEXT NOT NULL,
    applied_at_utc_ms INTEGER NOT NULL CHECK(applied_at_utc_ms >= 0)
)
)SQL";

std::int64_t systemUtcNowMilliseconds() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

void validateMigrations(const std::vector<Migration>& migrations) {
    for (std::size_t index = 0; index < migrations.size(); ++index) {
        const auto& migration = migrations[index];
        const int expected = static_cast<int>(index + 1);
        if (migration.version != expected) {
            throw MigrationError("migration versions must be consecutive and start at 1");
        }
        if (migration.name.empty() || migration.sql.empty()) {
            throw MigrationError("migration name and SQL must not be empty");
        }
    }
}

bool hasMigrationTable(SqliteDatabase& database) {
    auto statement = database.prepare(
        "SELECT 1 FROM sqlite_master WHERE type = 'table' AND name = 'schema_migrations'",
        "migration.table_exists");
    return statement.step();
}

struct AppliedMigration {
    int version;
    std::string name;
    std::string checksum;
};

std::vector<AppliedMigration> readApplied(SqliteDatabase& database) {
    std::vector<AppliedMigration> result;
    auto statement =
        database.prepare("SELECT version, name, checksum FROM schema_migrations ORDER BY version",
                         "migration.read_applied");
    while (statement.step()) {
        result.push_back({static_cast<int>(statement.columnInt64(0)), statement.columnText(1),
                          statement.columnText(2)});
    }
    return result;
}

} // namespace

std::vector<Migration> builtInMigrations() {
    return {
        {1, "initial", generated::kMigration0001Sql},
        {2, "import_jobs", generated::kMigration0002Sql},
    };
}

std::vector<Migration> loadMigrations(const std::filesystem::path& directory) {
    if (!std::filesystem::is_directory(directory)) {
        throw MigrationError("migration directory does not exist");
    }

    const std::regex filenamePattern(R"(^([0-9]{4})_([a-z0-9_]+)\.sql$)");
    std::vector<Migration> migrations;
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        const std::string filename = entry.path().filename().string();
        std::smatch match;
        if (!std::regex_match(filename, match, filenamePattern)) {
            continue;
        }

        std::ifstream input(entry.path(), std::ios::binary);
        if (!input) {
            throw MigrationError("migration file could not be opened");
        }
        std::string sql((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        if (sql.size() >= 3 && static_cast<unsigned char>(sql[0]) == 0xef &&
            static_cast<unsigned char>(sql[1]) == 0xbb &&
            static_cast<unsigned char>(sql[2]) == 0xbf) {
            sql.erase(0, 3);
        }
        migrations.push_back({std::stoi(match[1].str()), match[2].str(), std::move(sql)});
    }

    std::sort(
        migrations.begin(), migrations.end(),
        [](const Migration& left, const Migration& right) { return left.version < right.version; });
    validateMigrations(migrations);
    return migrations;
}

std::string migrationChecksum(const Migration& migration) {
    // Stable FNV-1a is used as an immutability guard, not as a credential hash.
    std::uint64_t hash = 14695981039346656037ULL;
    const auto append = [&hash](std::string_view value) {
        for (const unsigned char byte : value) {
            hash ^= byte;
            hash *= 1099511628211ULL;
        }
    };
    append(std::to_string(migration.version));
    append("\n");
    append(migration.name);
    append("\n");
    append(migration.sql);

    std::ostringstream output;
    output << std::hex << std::setfill('0') << std::setw(16) << hash;
    return output.str();
}

MigrationRunner::MigrationRunner(UtcNowProvider utcNow)
    : utcNow_(utcNow ? std::move(utcNow) : UtcNowProvider(systemUtcNowMilliseconds)) {}

std::vector<int> MigrationRunner::apply(SqliteDatabase& database,
                                        const std::vector<Migration>& migrations) const {
    validateMigrations(migrations);
    database.execute(kCreateMigrationTable, "migration.bootstrap");

    const auto applied = readApplied(database);
    if (applied.size() > migrations.size()) {
        throw MigrationError("database schema is newer than this application");
    }
    for (std::size_t index = 0; index < applied.size(); ++index) {
        const auto& actual = applied[index];
        const auto& expected = migrations[index];
        if (actual.version != expected.version || actual.name != expected.name ||
            actual.checksum != migrationChecksum(expected)) {
            throw MigrationError("an applied migration no longer matches its published content");
        }
    }

    const int recordedVersion = applied.empty() ? 0 : applied.back().version;
    const int pragmaVersion = static_cast<int>(database.scalarInt64("PRAGMA user_version"));
    if (recordedVersion != pragmaVersion) {
        throw MigrationError("schema_migrations and PRAGMA user_version disagree");
    }

    std::vector<int> newlyApplied;
    for (std::size_t index = applied.size(); index < migrations.size(); ++index) {
        const auto& migration = migrations[index];
        Transaction transaction(database);
        database.execute(migration.sql, "migration.apply.v" + std::to_string(migration.version));

        auto record = database.prepare(
            "INSERT INTO schema_migrations(version, name, checksum, applied_at_utc_ms) "
            "VALUES(?1, ?2, ?3, ?4)",
            "migration.record");
        record.bindInt64(1, migration.version);
        record.bindText(2, migration.name);
        record.bindText(3, migrationChecksum(migration));
        record.bindInt64(4, utcNow_());
        record.execute();
        database.execute("PRAGMA user_version = " + std::to_string(migration.version),
                         "migration.user_version");
        transaction.commit();
        newlyApplied.push_back(migration.version);
    }
    return newlyApplied;
}

int MigrationRunner::currentVersion(SqliteDatabase& database) {
    if (!hasMigrationTable(database)) {
        return 0;
    }
    return static_cast<int>(database.scalarInt64(
        "SELECT COALESCE(MAX(version), 0) FROM schema_migrations", "migration.current_version"));
}

} // namespace modelharbor::persistence
