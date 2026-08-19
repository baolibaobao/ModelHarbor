#include "persistence/site_repository.h"

#include "persistence/sqlite_database.h"

#include <sqlite3.h>

namespace modelharbor::persistence {
namespace {

SiteRecord readSite(Statement& statement) {
    return {
        statement.columnInt64(0), statement.columnText(1),       statement.columnText(2),
        statement.columnText(3),  statement.columnInt64(4) != 0, statement.columnInt64(5),
        statement.columnInt64(6),
    };
}

} // namespace

std::int64_t SiteRepository::create(const SiteRecord& site) {
    auto statement = database_.prepare(
        "INSERT INTO sites(name, base_url, adapter_type, enabled, created_at_utc_ms, "
        "updated_at_utc_ms) VALUES(?1, ?2, ?3, ?4, ?5, ?6) RETURNING id",
        "sites.create");
    statement.bindText(1, site.name);
    statement.bindText(2, site.baseUrl);
    statement.bindText(3, site.adapterType);
    statement.bindInt64(4, site.enabled ? 1 : 0);
    statement.bindInt64(5, site.createdAtUtcMs);
    statement.bindInt64(6, site.updatedAtUtcMs);
    if (!statement.step()) {
        throw DatabaseError(SQLITE_NOTFOUND, SQLITE_NOTFOUND, "sites.create",
                            "insert returned no identifier");
    }
    const auto id = statement.columnInt64(0);
    statement.step();
    return id;
}

std::optional<SiteRecord> SiteRepository::findById(std::int64_t id) {
    auto statement = database_.prepare(
        "SELECT id, name, base_url, adapter_type, enabled, created_at_utc_ms, updated_at_utc_ms "
        "FROM sites WHERE id = ?1",
        "sites.find_by_id");
    statement.bindInt64(1, id);
    if (!statement.step()) {
        return std::nullopt;
    }
    return readSite(statement);
}

std::vector<SiteRecord> SiteRepository::list() {
    auto statement = database_.prepare(
        "SELECT id, name, base_url, adapter_type, enabled, created_at_utc_ms, updated_at_utc_ms "
        "FROM sites ORDER BY id",
        "sites.list");
    std::vector<SiteRecord> result;
    while (statement.step()) {
        result.push_back(readSite(statement));
    }
    return result;
}

bool SiteRepository::update(const SiteRecord& site) {
    auto statement = database_.prepare(
        "UPDATE sites SET name = ?1, base_url = ?2, adapter_type = ?3, enabled = ?4, "
        "updated_at_utc_ms = ?5 WHERE id = ?6 RETURNING id",
        "sites.update");
    statement.bindText(1, site.name);
    statement.bindText(2, site.baseUrl);
    statement.bindText(3, site.adapterType);
    statement.bindInt64(4, site.enabled ? 1 : 0);
    statement.bindInt64(5, site.updatedAtUtcMs);
    statement.bindInt64(6, site.id);
    const bool updated = statement.step();
    if (updated) {
        statement.step();
    }
    return updated;
}

bool SiteRepository::remove(std::int64_t id) {
    auto statement =
        database_.prepare("DELETE FROM sites WHERE id = ?1 RETURNING id", "sites.remove");
    statement.bindInt64(1, id);
    const bool removed = statement.step();
    if (removed) {
        statement.step();
    }
    return removed;
}

} // namespace modelharbor::persistence
