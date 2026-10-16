#pragma once

#include "core/interfaces/IMetadataProvider.hpp"
#include "services/session/LruCache.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mydb::services::session {

/// Proxy com cache da introspecção (CLAUDE.md §4.4). Guarda a lista de bancos e, com limite LRU, os objetos
/// por banco, as definições de tabela e os CREATE. A invalidação é seletiva (§2.3, regra 4): um DDL ou um
/// "atualizar" descarta só o que foi afetado. Não é thread-safe: vive na thread do ConnectionWorker.
class CachedMetadataProvider final : public core::IMetadataProvider {
public:
    struct Limits {
        std::size_t databases{64}; ///< bancos com a lista de objetos em cache
        std::size_t objects{512};  ///< definições de tabela e CREATE
    };

    explicit CachedMetadataProvider(core::IMetadataProvider& source);
    CachedMetadataProvider(core::IMetadataProvider& source, Limits limits);

    [[nodiscard]] std::vector<std::string> databases() override;
    [[nodiscard]] std::vector<core::model::DatabaseSummary> databaseSummaries() override;
    [[nodiscard]] std::vector<core::model::DbObject> objects(std::string_view database) override;
    [[nodiscard]] core::model::TableDefinition table(const core::model::ObjectRef& ref) override;
    [[nodiscard]] std::string createStatement(const core::model::ObjectRef& ref) override;

    /// Tudo: depois de "atualizar" o servidor ou de CREATE/DROP DATABASE.
    void invalidateAll();
    /// Os objetos de um banco e o que depende deles.
    void invalidateDatabase(std::string_view database);
    /// Um objeto: a definição, o CREATE e a lista do banco dele (nome, linhas e tamanho podem ter mudado).
    void invalidateObject(const core::model::ObjectRef& ref);

private:
    core::IMetadataProvider* m_source;
    std::optional<std::vector<std::string>> m_databases;
    std::optional<std::vector<core::model::DatabaseSummary>> m_summaries;
    LruCache<std::vector<core::model::DbObject>> m_objects;
    LruCache<core::model::TableDefinition> m_tables;
    LruCache<std::string> m_creates;
};

} // namespace mydb::services::session
