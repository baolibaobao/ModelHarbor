#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace modelharbor::persistence {

class SqliteDatabase;

struct SiteRecord {
    std::int64_t id = 0;
    std::string name;
    std::string baseUrl;
    std::string adapterType;
    bool enabled = true;
    std::int64_t createdAtUtcMs = 0;
    std::int64_t updatedAtUtcMs = 0;
};

class SiteRepository final {
  public:
    explicit SiteRepository(SqliteDatabase& database) : database_(database) {}

    std::int64_t create(const SiteRecord& site);
    std::optional<SiteRecord> findById(std::int64_t id);
    std::vector<SiteRecord> list();
    bool update(const SiteRecord& site);
    bool remove(std::int64_t id);

  private:
    SqliteDatabase& database_;
};

} // namespace modelharbor::persistence
