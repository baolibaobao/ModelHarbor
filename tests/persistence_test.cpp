#include "persistence/app_settings_repository.h"
#include "persistence/migration.h"
#include "persistence/site_repository.h"
#include "persistence/sqlite_database.h"
#include "support/test_data_dir.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

#ifdef _WIN32
#include <Windows.h>
#endif

namespace {

using modelharbor::persistence::AppSettingsRepository;
using modelharbor::persistence::Migration;
using modelharbor::persistence::MigrationRunner;
using modelharbor::persistence::SiteRepository;
using modelharbor::persistence::SqliteDatabase;
using modelharbor::persistence::Transaction;
using modelharbor::test_support::TestDataDir;

constexpr std::int64_t kFixedUtcMilliseconds = 1'786'800'000'000LL;

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

#ifdef _WIN32
DWORD runCrashWriter(const std::filesystem::path& databasePath) {
    const std::filesystem::path executable(MODELHARBOR_CRASH_WRITER);
    std::wstring command = L"\"" + executable.wstring() + L"\" \"" + databasePath.wstring() + L"\"";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) {
        throw std::system_error(static_cast<int>(GetLastError()), std::system_category(),
                                "crash_writer_start");
    }

    const DWORD waitResult = WaitForSingleObject(process.hProcess, 10'000);
    DWORD exitCode = 0;
    if (waitResult != WAIT_OBJECT_0 || !GetExitCodeProcess(process.hProcess, &exitCode)) {
        const DWORD error = waitResult == WAIT_OBJECT_0 ? GetLastError() : ERROR_TIMEOUT;
        TerminateProcess(process.hProcess, 99);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        throw std::system_error(static_cast<int>(error), std::system_category(),
                                "crash_writer_wait");
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return exitCode;
}
#endif

bool containsTable(SqliteDatabase& database, const std::string& name) {
    auto statement =
        database.prepare("SELECT 1 FROM sqlite_master WHERE type = 'table' AND name = ?1");
    statement.bindText(1, name);
    return statement.step();
}

void verifyFreshDatabase(const std::filesystem::path& root,
                         const std::vector<Migration>& migrations) {
    auto database = SqliteDatabase::open(root / "fresh.db");
    MigrationRunner runner([] { return kFixedUtcMilliseconds; });
    const auto applied = runner.apply(database, migrations);

    require(applied == std::vector<int>({1, 2}), "fresh database did not apply both migrations");
    require(runner.currentVersion(database) == 2, "fresh database schema version is incorrect");
    require(database.scalarText("PRAGMA journal_mode") == "wal", "WAL was not enabled");
    require(database.scalarInt64("PRAGMA foreign_keys") == 1, "foreign keys were not enabled");
    require(database.scalarInt64("PRAGMA synchronous") == 1,
            "synchronous=NORMAL was not configured");
    require(database.scalarInt64("PRAGMA user_version") == 2,
            "SQLite user_version was not synchronized");
    require(database.scalarText("PRAGMA integrity_check") == "ok",
            "fresh database integrity check failed");

    const std::vector<std::string> expectedTables{
        "account_groups", "app_settings",      "channel_models",   "channel_state_events",
        "channels",       "credential_tags",   "cost_experiments", "credentials",
        "daily_usage",    "fingerprint_runs",  "health_checks",    "import_job_items",
        "import_jobs",    "local_api_keys",    "logical_models",   "pricing_tables",
        "proxies",        "request_records",   "route_cursors",    "route_members",
        "routes",         "schema_migrations", "secrets",          "sites",
        "tags",
    };
    for (const auto& table : expectedTables) {
        require(containsTable(database, table), ("missing table: " + table).c_str());
    }

    SiteRepository sites(database);
    const auto firstId =
        sites.create({0, "Fixture provider", "https://fixture.invalid/v1", "openai_compatible",
                      true, kFixedUtcMilliseconds, kFixedUtcMilliseconds});
    const auto stored = sites.findById(firstId);
    require(stored.has_value() && stored->name == "Fixture provider",
            "site repository did not round-trip a row");

    try {
        Transaction transaction(database);
        sites.create({0, "Rolled back provider", "https://rollback.invalid/v1", "openai_compatible",
                      true, kFixedUtcMilliseconds, kFixedUtcMilliseconds});
        throw std::runtime_error("force rollback");
    } catch (const std::runtime_error&) {
    }
    require(sites.list().size() == 1, "RAII transaction did not roll back");

    {
        Transaction transaction(database);
        sites.create({0, "Committed provider", "https://commit.invalid/v1", "openai_compatible",
                      true, kFixedUtcMilliseconds, kFixedUtcMilliseconds});
        transaction.commit();
    }
    require(sites.list().size() == 2, "committed transaction was not persisted");

    AppSettingsRepository settings(database);
    settings.upsert("ui.theme", "dark", kFixedUtcMilliseconds);
    require(settings.find("ui.theme") == std::optional<std::string>("dark"),
            "settings repository did not round-trip a value");

    bool foreignKeyRejected = false;
    try {
        database.execute(
            "INSERT INTO channels(site_id, credential_id, name, enabled, priority, weight, "
            "concurrency_limit, created_at_utc_ms, updated_at_utc_ms) "
            "VALUES(999999, 999999, 'invalid', 1, 0, 1, 1, 0, 0)",
            "test_foreign_key");
    } catch (const modelharbor::persistence::DatabaseError&) {
        foreignKeyRejected = true;
    }
    require(foreignKeyRejected, "foreign-key violation was accepted");

    const auto backupPath = root / "snapshots" / "modelharbor.db";
    database.backupTo(backupPath);
    settings.upsert("ui.theme", "light", kFixedUtcMilliseconds + 1);
    database.backupTo(backupPath);

    auto backup = SqliteDatabase::open(backupPath);
    require(MigrationRunner::currentVersion(backup) == 2,
            "backup did not retain the schema version");
    AppSettingsRepository backupSettings(backup);
    require(backupSettings.find("ui.theme") == std::optional<std::string>("light"),
            "replacement backup did not contain the latest committed state");
    require(backup.scalarText("PRAGMA integrity_check") == "ok", "backup integrity check failed");
}

void verifyUpgradeFromPreviousVersion(const std::filesystem::path& root,
                                      const std::vector<Migration>& migrations) {
    const auto path = root / "upgrade.db";
    MigrationRunner runner([] { return kFixedUtcMilliseconds; });
    {
        auto previous = SqliteDatabase::open(path);
        runner.apply(previous, {migrations.front()});
        SiteRepository sites(previous);
        sites.create({0, "Preserved provider", "https://preserved.invalid/v1", "openai_compatible",
                      true, kFixedUtcMilliseconds, kFixedUtcMilliseconds});
        require(runner.currentVersion(previous) == 1, "previous schema fixture was not version 1");
    }

    auto upgraded = SqliteDatabase::open(path);
    const auto applied = runner.apply(upgraded, migrations);
    require(applied == std::vector<int>({2}), "upgrade did not apply only the missing migration");
    require(runner.currentVersion(upgraded) == 2, "upgrade schema version is incorrect");
    require(SiteRepository(upgraded).list().size() == 1, "upgrade lost existing data");
    require(containsTable(upgraded, "import_jobs"), "upgrade did not create stage 2 tables");

    auto changed = migrations;
    changed.front().sql += "\n-- changed after release";
    bool checksumRejected = false;
    try {
        runner.apply(upgraded, changed);
    } catch (const modelharbor::persistence::MigrationError&) {
        checksumRejected = true;
    }
    require(checksumRejected, "changed published migration was not rejected");
}

void verifyFailedMigrationRollback(const std::filesystem::path& root,
                                   const std::vector<Migration>& migrations) {
    auto database = SqliteDatabase::open(root / "failed.db");
    MigrationRunner runner([] { return kFixedUtcMilliseconds; });
    runner.apply(database, migrations);

    auto broken = migrations;
    broken.push_back({3, "broken_fixture",
                      "CREATE TABLE should_rollback(id INTEGER PRIMARY KEY);\n"
                      "INSERT INTO missing_table(value) VALUES(1);"});
    bool failed = false;
    try {
        runner.apply(database, broken);
    } catch (const modelharbor::persistence::DatabaseError&) {
        failed = true;
    }
    require(failed, "broken migration unexpectedly succeeded");
    require(runner.currentVersion(database) == 2, "failed migration advanced schema version");
    require(!containsTable(database, "should_rollback"),
            "failed migration left a partially-created table");
}

void verifyCrashStyleRecovery(const std::filesystem::path& root,
                              const std::vector<Migration>& migrations) {
    const auto path = root / "recovery.db";
    MigrationRunner runner([] { return kFixedUtcMilliseconds; });
#ifdef _WIN32
    const auto crashResult = runCrashWriter(path);
#else
    const std::string command =
        "\"" + std::string(MODELHARBOR_CRASH_WRITER) + "\" \"" + path.string() + "\"";
    const int crashResult = std::system(command.c_str());
#endif
    require(crashResult == 17, "crash writer did not terminate at the injected boundary");

    auto reopened = SqliteDatabase::open(path);
    runner.apply(reopened, migrations);
    AppSettingsRepository settings(reopened);
    require(settings.find("recovery.committed") == std::optional<std::string>("keep"),
            "WAL recovery lost committed data");
    require(!settings.find("recovery.uncommitted").has_value(),
            "reopen retained an abandoned transaction");
    require(reopened.scalarText("PRAGMA integrity_check") == "ok",
            "reopened WAL database failed integrity check");
}

} // namespace

int main() {
    try {
        TestDataDir data;
        const auto migrations = modelharbor::persistence::builtInMigrations();
        require(migrations.size() == 2, "unexpected built-in migration count");
        const auto fileMigrations = modelharbor::persistence::loadMigrations(
            std::filesystem::path(MODELHARBOR_MIGRATION_DIRECTORY));
        require(fileMigrations.size() == migrations.size(), "file migration count differs");
        for (std::size_t index = 0; index < migrations.size(); ++index) {
            require(modelharbor::persistence::migrationChecksum(fileMigrations[index]) ==
                        modelharbor::persistence::migrationChecksum(migrations[index]),
                    "built-in migration differs from its SQL source file");
        }
        verifyFreshDatabase(data.path(), migrations);
        verifyUpgradeFromPreviousVersion(data.path(), migrations);
        verifyFailedMigrationRollback(data.path(), migrations);
        verifyCrashStyleRecovery(data.path(), migrations);
        std::cout << "ModelHarbor persistence S2.1 OK\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "persistence test failed: " << error.what() << '\n';
        return 1;
    }
}
