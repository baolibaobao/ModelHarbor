#include "persistence/app_settings_repository.h"
#include "persistence/migration.h"
#include "persistence/sqlite_database.h"

#include <cstdlib>
#include <filesystem>

int main(int argc, char** argv) {
    if (argc != 2) {
        return 2;
    }

    auto database = modelharbor::persistence::SqliteDatabase::open(std::filesystem::path(argv[1]));
    modelharbor::persistence::MigrationRunner migrations;
    migrations.apply(database, modelharbor::persistence::builtInMigrations());
    modelharbor::persistence::AppSettingsRepository settings(database);
    settings.upsert("recovery.committed", "keep", 1'786'800'000'000LL);

    modelharbor::persistence::Transaction abandoned(database);
    settings.upsert("recovery.uncommitted", "discard", 1'786'800'000'001LL);
    std::_Exit(17);
}
