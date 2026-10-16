#include "drivers/mysql/MySqlServerAdmin.hpp"

#include "core/errors/DbError.hpp"
#include "drivers/mysql/MySqlConnection.hpp"
#include "drivers/mysql/MySqlRows.hpp"

#include <algorithm>
#include <cctype>
#include <regex>
#include <string>

namespace mydb::drivers::mysql {

namespace {

using rows::number;
using rows::Row;
using rows::Rows;
using rows::runBatch;
using rows::text;

std::vector<core::ServerVariable> readVariables(MySqlConnection& connection, const std::string& sql, bool global) {
    std::vector<core::ServerVariable> variables;
    const std::vector<Rows> results = runBatch(connection, sql);
    variables.reserve(results.at(0).size());
    for (const Row& row : results.at(0)) {
        variables.push_back(core::ServerVariable{.name = text(row, 0), .value = text(row, 1), .global = global});
    }
    return variables;
}

bool isNumber(const std::string& value) {
    static const std::regex kNumber(R"(^-?(\d+(\.\d*)?|\.\d+)([eE][-+]?\d+)?$)");
    return std::regex_match(value, kNumber);
}

bool isDefaultKeyword(std::string value) {
    std::ranges::transform(value, value.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return value == "DEFAULT";
}

void runToEnd(MySqlConnection& connection, const std::string& sql) {
    const auto execution = connection.execute(sql);
    while (execution->nextResult()) {
    }
}

} // namespace

std::vector<core::ServerVariable> MySqlServerAdmin::variables(bool global) {
    return readVariables(*m_connection, global ? "SHOW GLOBAL VARIABLES" : "SHOW SESSION VARIABLES", global);
}

void MySqlServerAdmin::setVariable(const core::ServerVariable& variable) {
    static const std::regex kName(R"(^[A-Za-z_][A-Za-z0-9_.]*$)");
    if (!std::regex_match(variable.name, kName)) {
        throw core::QueryError("Nome de variável inválido: " + variable.name);
    }
    const std::string value = isNumber(variable.value) || isDefaultKeyword(variable.value)
                                  ? variable.value
                                  : m_connection->mysqlDialect().quoteString(variable.value);
    runToEnd(*m_connection,
             std::string("SET ") + (variable.global ? "GLOBAL " : "SESSION ") + variable.name + " = " + value);
}

std::vector<core::ServerVariable> MySqlServerAdmin::status() {
    return readVariables(*m_connection, "SHOW GLOBAL STATUS", true);
}

std::vector<core::ServerProcess> MySqlServerAdmin::processes() {
    // Colunas: Id, User, Host, db, Command, Time, State, Info (o MariaDB acrescenta Progress no fim).
    const std::vector<Rows> results = runBatch(*m_connection, "SHOW FULL PROCESSLIST");
    std::vector<core::ServerProcess> list;
    list.reserve(results.at(0).size());
    for (const Row& row : results.at(0)) {
        list.push_back(core::ServerProcess{.id = number<std::uint64_t>(row, 0).value_or(0),
                                           .user = text(row, 1),
                                           .host = text(row, 2),
                                           .database = text(row, 3),
                                           .command = text(row, 4),
                                           .seconds = number<std::int64_t>(row, 5).value_or(0),
                                           .state = text(row, 6),
                                           .info = text(row, 7)});
    }
    return list;
}

void MySqlServerAdmin::killProcess(std::uint64_t id) {
    runToEnd(*m_connection, "KILL " + std::to_string(id));
}

} // namespace mydb::drivers::mysql
