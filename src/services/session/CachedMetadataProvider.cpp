#include "services/session/CachedMetadataProvider.hpp"

#include <cstdint>

namespace mydb::services::session {

namespace {

// Chaves "banco␟schema␟nome␟tipo": o prefixo "banco␟" seleciona tudo de um banco.
constexpr char kSeparator = '\x1f';

std::string databasePrefix(std::string_view database) {
    std::string key(database);
    key += kSeparator;
    return key;
}

std::string objectKey(const core::model::ObjectRef& ref) {
    std::string key = databasePrefix(ref.database);
    key += ref.schema;
    key += kSeparator;
    key += ref.name;
    key += kSeparator;
    key += std::to_string(static_cast<std::uint8_t>(ref.type));
    return key;
}

} // namespace

CachedMetadataProvider::CachedMetadataProvider(core::IMetadataProvider& source)
    : CachedMetadataProvider(source, Limits{}) {}

CachedMetadataProvider::CachedMetadataProvider(core::IMetadataProvider& source, Limits limits)
    : m_source(&source), m_objects(limits.databases), m_tables(limits.objects), m_creates(limits.objects) {}

std::vector<std::string> CachedMetadataProvider::databases() {
    if (!m_databases) {
        m_databases = m_source->databases();
    }
    return *m_databases;
}

std::vector<core::model::DatabaseSummary> CachedMetadataProvider::databaseSummaries() {
    if (!m_summaries) {
        m_summaries = m_source->databaseSummaries();
    }
    return *m_summaries;
}

std::vector<core::model::DbObject> CachedMetadataProvider::objects(std::string_view database) {
    if (const auto* cached = m_objects.find(database)) {
        return *cached;
    }
    return m_objects.put(std::string(database), m_source->objects(database));
}

core::model::TableDefinition CachedMetadataProvider::table(const core::model::ObjectRef& ref) {
    const std::string key = objectKey(ref);
    if (const auto* cached = m_tables.find(key)) {
        return *cached;
    }
    return m_tables.put(key, m_source->table(ref));
}

std::string CachedMetadataProvider::createStatement(const core::model::ObjectRef& ref) {
    const std::string key = objectKey(ref);
    if (const auto* cached = m_creates.find(key)) {
        return *cached;
    }
    return m_creates.put(key, m_source->createStatement(ref));
}

void CachedMetadataProvider::invalidateAll() {
    m_databases.reset();
    m_summaries.reset();
    m_objects.clear();
    m_tables.clear();
    m_creates.clear();
}

void CachedMetadataProvider::invalidateDatabase(std::string_view database) {
    m_summaries.reset();
    m_objects.erase(database);
    const std::string prefix = databasePrefix(database);
    m_tables.eraseWithPrefix(prefix);
    m_creates.eraseWithPrefix(prefix);
}

void CachedMetadataProvider::invalidateObject(const core::model::ObjectRef& ref) {
    m_summaries.reset();
    m_objects.erase(ref.database);
    const std::string key = objectKey(ref);
    m_tables.erase(key);
    m_creates.erase(key);
}

} // namespace mydb::services::session
