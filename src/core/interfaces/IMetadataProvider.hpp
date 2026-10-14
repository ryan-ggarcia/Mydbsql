#pragma once

#include "core/model/DbObject.hpp"
#include "core/model/ObjectRef.hpp"
#include "core/model/TableDefinition.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace mydb::core {

/// Introspecção (information_schema, pg_catalog, sqlite_master…). As implementações buscam em lote:
/// uma consulta por banco, nunca uma por tabela (CLAUDE.md §2.3, regra 3).
class IMetadataProvider {
public:
    virtual ~IMetadataProvider() = default;

    [[nodiscard]] virtual std::vector<std::string> databases() = 0;
    [[nodiscard]] virtual std::vector<model::DbObject> objects(std::string_view database) = 0;
    [[nodiscard]] virtual model::TableDefinition table(const model::ObjectRef& ref) = 0;
    [[nodiscard]] virtual std::string createStatement(const model::ObjectRef& ref) = 0;

protected:
    IMetadataProvider() = default;
    IMetadataProvider(const IMetadataProvider&) = default;
    IMetadataProvider(IMetadataProvider&&) = default;
    IMetadataProvider& operator=(const IMetadataProvider&) = default;
    IMetadataProvider& operator=(IMetadataProvider&&) = default;
};

} // namespace mydb::core
