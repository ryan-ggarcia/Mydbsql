#pragma once

#include <stdexcept>
#include <string>
#include <string_view>

namespace mydb::core {

/// Dados de um erro vindo do SGBD ou do driver.
struct DbErrorInfo {
    std::string message;
    int code{0};          ///< código nativo do SGBD (ex.: 1045 no MySQL); 0 quando não existe
    std::string sqlState; ///< SQLSTATE de 5 caracteres; vazio quando o SGBD não informa
    std::string hint;     ///< dica para o usuário; vazio quando não há
};

/// Base de todos os erros de banco de dados. Falhas são reportadas por exceção (CLAUDE.md §6).
class DbError : public std::runtime_error {
public:
    explicit DbError(DbErrorInfo info);
    explicit DbError(std::string message);

    [[nodiscard]] int code() const noexcept { return m_info.code; }
    [[nodiscard]] std::string_view sqlState() const noexcept { return m_info.sqlState; }
    [[nodiscard]] std::string_view hint() const noexcept { return m_info.hint; }
    [[nodiscard]] const DbErrorInfo& info() const noexcept { return m_info; }

private:
    DbErrorInfo m_info;
};

/// Falha ao abrir, manter ou reabrir uma conexão (rede, autenticação, SSL, túnel).
class ConnectionError : public DbError {
public:
    using DbError::DbError;
};

/// O SGBD rejeitou ou falhou ao executar uma sentença.
class QueryError : public DbError {
public:
    using DbError::DbError;
};

/// A operação foi cancelada pelo usuário (std::stop_token ou IConnection::cancelCurrent()).
class CancelledError final : public DbError {
public:
    CancelledError();
};

} // namespace mydb::core
