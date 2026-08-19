#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace modelharbor::persistence {

class SqliteDatabase;

class AppSettingsRepository final {
  public:
    explicit AppSettingsRepository(SqliteDatabase& database) : database_(database) {}

    void upsert(std::string_view key, std::string_view value, std::int64_t updatedAtUtcMs);
    std::optional<std::string> find(std::string_view key);
    void remove(std::string_view key);

  private:
    SqliteDatabase& database_;
};

} // namespace modelharbor::persistence
