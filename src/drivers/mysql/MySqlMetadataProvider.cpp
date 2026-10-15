#include "drivers/mysql/MySqlMetadataProvider.hpp"

#include "core/errors/DbError.hpp"
#include "drivers/mysql/MySqlColumnParser.hpp"
#include "drivers/mysql/MySqlConnection.hpp"

#include <charconv>
#include <optional>
#include <regex>
#include <string>
#include <vector>

namespace mydb::drivers::mysql {

namespace {

using core::model::DbObject;
using core::model::ObjectRef;
using core::model::ObjectType;
using Row = std::vector<std::optional<std::string>>;
using Rows = std::vector<Row>;

Rows readAll(core::IResultSet& result) {
    Rows rows;
    const std::size_t count = result.columns().size();
    while (result.next()) {
        Row row;
        row.reserve(count);
        for (std::size_t i = 0; i < count; ++i) {
            row.push_back(result.isNull(i) ? std::nullopt : std::optional(std::string(result.text(i))));
        }
        rows.push_back(std::move(row));
    }
    return rows;
}

/// Executa um lote e devolve as linhas de cada resultado, na ordem das sentenças.
std::vector<Rows> runBatch(MySqlConnection& connection, const std::string& sql) {
    std::vector<Rows> results;
    const auto execution = connection.execute(sql);
    while (execution->nextResult()) {
        core::IResultSet* rows = execution->resultSet();
        results.push_back(rows != nullptr ? readAll(*rows) : Rows{});
    }
    return results;
}

std::string text(const Row& row, std::size_t column) {
    return row.at(column).value_or(std::string());
}

template <typename Number>
std::optional<Number> number(const Row& row, std::size_t column) {
    const auto& value = row.at(column);
    if (!value) {
        return std::nullopt;
    }
    Number result{};
    const auto [end, error] = std::from_chars(value->data(), value->data() + value->size(), result);
    if (error != std::errc{} || end != value->data() + value->size()) {
        return std::nullopt;
    }
    return result;
}

std::optional<std::chrono::system_clock::time_point> date(const Row& row, std::size_t column) {
    const auto& value = row.at(column);
    return value ? parseDateTime(*value) : std::nullopt;
}

ObjectType tableType(std::string_view type) {
    if (type == "VIEW" || type == "SYSTEM VIEW") {
        return ObjectType::View;
    }
    if (type == "SEQUENCE") {
        return ObjectType::Sequence;
    }
    return ObjectType::Table;
}

} // namespace

std::vector<std::string> MySqlMetadataProvider::databases() {
    const std::vector<Rows> results = runBatch(*m_connection, "SHOW DATABASES");
    const std::string& pattern = m_connection->params().ignoreDatabasePattern;
    std::optional<std::regex> ignore;
    if (!pattern.empty()) {
        try {
            ignore.emplace(pattern, std::regex::ECMAScript | std::regex::icase);
        } catch (const std::regex_error& error) {
            throw core::QueryError(
                core::DbErrorInfo{.message = "Expressão para ignorar bancos inválida: " + std::string(error.what()),
                                  .code = 0,
                                  .sqlState = {},
                                  .hint = "Corrija o filtro de bancos da sessão."});
        }
    }
    std::vector<std::string> names;
    for (const Row& row : results.at(0)) {
        std::string name = text(row, 0);
        if (!ignore || !std::regex_search(name, *ignore)) {
            names.push_back(std::move(name));
        }
    }
    return names;
}

std::vector<DbObject> MySqlMetadataProvider::objects(std::string_view database) {
    const MySqlDialect& dialect = m_connection->mysqlDialect();
    const std::string db = dialect.quoteString(database);
    const std::string sql =
        "SELECT TABLE_NAME, TABLE_TYPE, ENGINE, TABLE_ROWS, DATA_LENGTH + INDEX_LENGTH, CREATE_TIME, UPDATE_TIME, "
        "TABLE_COLLATION, TABLE_COMMENT FROM information_schema.TABLES WHERE TABLE_SCHEMA = " +
        db +
        " ORDER BY TABLE_NAME;"
        "SELECT ROUTINE_NAME, ROUTINE_TYPE, CREATED, LAST_ALTERED, ROUTINE_COMMENT FROM information_schema.ROUTINES "
        "WHERE ROUTINE_SCHEMA = " +
        db +
        " ORDER BY ROUTINE_NAME;"
        "SELECT TRIGGER_NAME, CREATED FROM information_schema.TRIGGERS WHERE TRIGGER_SCHEMA = " +
        db +
        " ORDER BY TRIGGER_NAME;"
        "SELECT EVENT_NAME, CREATED, LAST_ALTERED, EVENT_COMMENT FROM information_schema.EVENTS WHERE EVENT_SCHEMA = " +
        db + " ORDER BY EVENT_NAME";
    const std::vector<Rows> results = runBatch(*m_connection, sql);

    std::vector<DbObject> objects;
    const auto add = [&](std::string name, ObjectType type) -> DbObject& {
        DbObject& object = objects.emplace_back();
        object.ref = ObjectRef{.database = std::string(database), .schema = {}, .name = std::move(name), .type = type};
        return object;
    };
    for (const Row& row : results.at(0)) {
        DbObject& object = add(text(row, 0), tableType(text(row, 1)));
        object.engine = text(row, 2);
        object.rows = number<std::int64_t>(row, 3);
        object.sizeBytes = number<std::int64_t>(row, 4);
        object.created = date(row, 5);
        object.updated = date(row, 6);
        object.collation = text(row, 7);
        object.comment = text(row, 8);
    }
    for (const Row& row : results.at(1)) {
        DbObject& object = add(text(row, 0), text(row, 1) == "FUNCTION" ? ObjectType::Function : ObjectType::Procedure);
        object.created = date(row, 2);
        object.updated = date(row, 3);
        object.comment = text(row, 4);
    }
    for (const Row& row : results.at(2)) {
        add(text(row, 0), ObjectType::Trigger).created = date(row, 1);
    }
    for (const Row& row : results.at(3)) {
        DbObject& object = add(text(row, 0), ObjectType::Event);
        object.created = date(row, 1);
        object.updated = date(row, 2);
        object.comment = text(row, 3);
    }
    return objects;
}

core::model::TableDefinition MySqlMetadataProvider::table(const ObjectRef& ref) {
    const MySqlDialect& dialect = m_connection->mysqlDialect();
    const bool mariaDb = dialect.version().mariaDb;
    const std::string db = dialect.quoteString(ref.database);
    const std::string name = dialect.quoteString(ref.name);
    const std::string where = " TABLE_SCHEMA = " + db + " AND TABLE_NAME = " + name;
    std::string sql =
        "SELECT COLUMN_NAME, DATA_TYPE, COLUMN_TYPE, IS_NULLABLE, COLUMN_DEFAULT, EXTRA, CHARACTER_SET_NAME, "
        "COLLATION_NAME, COLUMN_COMMENT, GENERATION_EXPRESSION FROM information_schema.COLUMNS WHERE" +
        where +
        " ORDER BY ORDINAL_POSITION;"
        "SELECT INDEX_NAME, NON_UNIQUE, COLUMN_NAME, SUB_PART, INDEX_TYPE, COLLATION, INDEX_COMMENT "
        "FROM information_schema.STATISTICS WHERE" +
        where +
        " ORDER BY INDEX_NAME = 'PRIMARY' DESC, INDEX_NAME, SEQ_IN_INDEX;"
        "SELECT k.CONSTRAINT_NAME, k.COLUMN_NAME, k.REFERENCED_TABLE_SCHEMA, k.REFERENCED_TABLE_NAME, "
        "k.REFERENCED_COLUMN_NAME, r.UPDATE_RULE, r.DELETE_RULE FROM information_schema.KEY_COLUMN_USAGE k "
        "JOIN information_schema.REFERENTIAL_CONSTRAINTS r ON r.CONSTRAINT_SCHEMA = k.CONSTRAINT_SCHEMA "
        "AND r.CONSTRAINT_NAME = k.CONSTRAINT_NAME AND r.TABLE_NAME = k.TABLE_NAME "
        "WHERE k.TABLE_SCHEMA = " +
        db + " AND k.TABLE_NAME = " + name +
        " AND k.REFERENCED_TABLE_NAME IS NOT NULL ORDER BY k.CONSTRAINT_NAME, k.ORDINAL_POSITION;"
        "SELECT ENGINE, TABLE_COLLATION, TABLE_COMMENT, AUTO_INCREMENT FROM information_schema.TABLES WHERE" +
        where;
    const bool checks = dialect.supports(core::Feature::CheckConstraints);
    if (checks) {
        // No MariaDB o nome de um CHECK é único por tabela; no MySQL, por banco (e a tabela vem de TABLE_CONSTRAINTS).
        sql += ";SELECT c.CONSTRAINT_NAME, c.CHECK_CLAUSE FROM information_schema.CHECK_CONSTRAINTS c "
               "JOIN information_schema.TABLE_CONSTRAINTS t ON t.CONSTRAINT_SCHEMA = c.CONSTRAINT_SCHEMA "
               "AND t.CONSTRAINT_NAME = c.CONSTRAINT_NAME";
        if (mariaDb) {
            sql += " AND t.TABLE_NAME = c.TABLE_NAME";
        }
        sql += " WHERE t.TABLE_SCHEMA = " + db + " AND t.TABLE_NAME = " + name +
               " AND t.CONSTRAINT_TYPE = 'CHECK' ORDER BY c.CONSTRAINT_NAME";
    }
    const std::vector<Rows> results = runBatch(*m_connection, sql);

    core::model::TableDefinition table;
    table.ref = ref;
    table.ref.type = ObjectType::Table;
    if (results.at(0).empty()) {
        throw core::QueryError(core::DbErrorInfo{.message = "Tabela não encontrada: " + ref.database + "." + ref.name,
                                                 .code = 0,
                                                 .sqlState = "42S02",
                                                 .hint = {}});
    }
    for (const Row& row : results.at(0)) {
        table.columns.push_back(parseColumn(InformationSchemaColumn{.name = text(row, 0),
                                                                    .dataType = text(row, 1),
                                                                    .columnType = text(row, 2),
                                                                    .nullable = text(row, 3) == "YES",
                                                                    .defaultValue = row.at(4),
                                                                    .extra = text(row, 5),
                                                                    .charset = text(row, 6),
                                                                    .collation = text(row, 7),
                                                                    .comment = text(row, 8),
                                                                    .generationExpression = text(row, 9)},
                                            mariaDb));
    }

    for (const Row& row : results.at(1)) {
        const std::string indexName = text(row, 0);
        if (table.indexes.empty() || table.indexes.back().name != indexName) {
            core::model::Index index;
            index.name = indexName;
            const std::string type = text(row, 4);
            if (indexName == "PRIMARY") {
                index.kind = core::model::Index::Kind::Primary;
            } else if (type == "FULLTEXT") {
                index.kind = core::model::Index::Kind::Fulltext;
            } else if (type == "SPATIAL") {
                index.kind = core::model::Index::Kind::Spatial;
            } else if (text(row, 1) == "0") {
                index.kind = core::model::Index::Kind::Unique;
            }
            if (type == "BTREE" || type == "HASH") {
                index.algorithm = type;
            }
            index.comment = text(row, 6);
            table.indexes.push_back(std::move(index));
        }
        table.indexes.back().columns.push_back(core::model::IndexColumn{
            .name = text(row, 2), .prefixLength = number<std::uint32_t>(row, 3), .descending = text(row, 5) == "D"});
    }

    for (const Row& row : results.at(2)) {
        const std::string fkName = text(row, 0);
        if (table.foreignKeys.empty() || table.foreignKeys.back().name != fkName) {
            core::model::ForeignKey fk;
            fk.name = fkName;
            fk.referencedTable =
                ObjectRef{.database = text(row, 2), .schema = {}, .name = text(row, 3), .type = ObjectType::Table};
            fk.onUpdate = parseReferentialAction(text(row, 5));
            fk.onDelete = parseReferentialAction(text(row, 6));
            table.foreignKeys.push_back(std::move(fk));
        }
        table.foreignKeys.back().columns.push_back(text(row, 1));
        table.foreignKeys.back().referencedColumns.push_back(text(row, 4));
    }

    if (!results.at(3).empty()) {
        const Row& row = results.at(3).front();
        table.options.engine = text(row, 0);
        table.options.collation = text(row, 1);
        table.options.comment = text(row, 2);
        table.options.autoIncrement = number<std::uint64_t>(row, 3);
    }

    if (checks) {
        for (const Row& row : results.at(4)) {
            table.checks.push_back(core::model::CheckConstraint{.name = text(row, 0), .expression = text(row, 1)});
        }
    }
    return table;
}

std::string MySqlMetadataProvider::createStatement(const ObjectRef& ref) {
    const MySqlDialect& dialect = m_connection->mysqlDialect();
    const std::string name = dialect.quoteIdentifier(ref.database) + "." + dialect.quoteIdentifier(ref.name);
    std::string keyword;
    std::string column;
    switch (ref.type) {
    case ObjectType::Table:
    case ObjectType::Sequence:
        keyword = "TABLE";
        column = "Create Table";
        break;
    case ObjectType::View:
        keyword = "VIEW";
        column = "Create View";
        break;
    case ObjectType::Procedure:
        keyword = "PROCEDURE";
        column = "Create Procedure";
        break;
    case ObjectType::Function:
        keyword = "FUNCTION";
        column = "Create Function";
        break;
    case ObjectType::Trigger:
        keyword = "TRIGGER";
        column = "SQL Original Statement";
        break;
    case ObjectType::Event:
        keyword = "EVENT";
        column = "Create Event";
        break;
    case ObjectType::Other:
        throw core::QueryError("Tipo de objeto sem CREATE: " + ref.name);
    }

    const auto execution = m_connection->execute("SHOW CREATE " + keyword + " " + name);
    if (!execution->nextResult() || execution->resultSet() == nullptr) {
        throw core::QueryError("SHOW CREATE " + keyword + " não devolveu linhas");
    }
    core::IResultSet& result = *execution->resultSet();
    const auto columns = result.columns();
    std::size_t index = columns.size();
    for (std::size_t i = 0; i < columns.size(); ++i) {
        if (columns[i].name == column) {
            index = i;
            break;
        }
    }
    if (index == columns.size() || !result.next()) {
        throw core::QueryError("SHOW CREATE " + keyword + " não trouxe a coluna \"" + column + "\"");
    }
    return std::string(result.text(index));
}

} // namespace mydb::drivers::mysql
