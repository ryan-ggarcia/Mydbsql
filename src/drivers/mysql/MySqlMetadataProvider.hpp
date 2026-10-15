#pragma once

#include "core/interfaces/IMetadataProvider.hpp"

namespace mydb::drivers::mysql {

class MySqlConnection;

/// Introspecção pelo information_schema. Cada chamada é um único lote com várias consultas, numa ida ao
/// servidor (CLAUDE.md §2.3, regra 3).
class MySqlMetadataProvider final : public core::IMetadataProvider {
public:
    explicit MySqlMetadataProvider(MySqlConnection& connection) noexcept : m_connection(&connection) {}

    /// Bancos, sem os que casam com ConnectionParams::ignoreDatabasePattern.
    [[nodiscard]] std::vector<std::string> databases() override;
    /// Tabelas, views, sequences, rotinas, triggers e events.
    [[nodiscard]] std::vector<core::model::DbObject> objects(std::string_view database) override;
    [[nodiscard]] core::model::TableDefinition table(const core::model::ObjectRef& ref) override;
    [[nodiscard]] std::string createStatement(const core::model::ObjectRef& ref) override;

private:
    MySqlConnection* m_connection;
};

} // namespace mydb::drivers::mysql
