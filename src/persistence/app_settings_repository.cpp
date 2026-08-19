#include "persistence/app_settings_repository.h"

#include "persistence/sqlite_database.h"

namespace modelharbor::persistence {

void AppSettingsRepository::upsert(std::string_view key, std::string_view value,
                                   std::int64_t updatedAtUtcMs) {
    auto statement = database_.prepare(
        "INSERT INTO app_settings(key, value, updated_at_utc_ms) VALUES(?1, ?2, ?3) "
        "ON CONFLICT(key) DO UPDATE SET value = excluded.value, "
        "updated_at_utc_ms = excluded.updated_at_utc_ms",
        "app_settings.upsert");
    statement.bindText(1, key);
    statement.bindText(2, value);
    statement.bindInt64(3, updatedAtUtcMs);
    statement.execute();
}

std::optional<std::string> AppSettingsRepository::find(std::string_view key) {
    auto statement =
        database_.prepare("SELECT value FROM app_settings WHERE key = ?1", "app_settings.find");
    statement.bindText(1, key);
    if (!statement.step()) {
        return std::nullopt;
    }
    return statement.columnText(0);
}

void AppSettingsRepository::remove(std::string_view key) {
    auto statement =
        database_.prepare("DELETE FROM app_settings WHERE key = ?1", "app_settings.remove");
    statement.bindText(1, key);
    statement.execute();
}

} // namespace modelharbor::persistence
