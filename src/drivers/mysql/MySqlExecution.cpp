#include "drivers/mysql/MySqlExecution.hpp"

#include "drivers/mysql/MySqlConnection.hpp"

namespace mydb::drivers::mysql {

MySqlExecution::MySqlExecution(MySqlConnection& connection, std::chrono::microseconds serverTime)
    : m_connection(&connection) {
    m_timing.server = serverTime;
}

MySqlExecution::~MySqlExecution() {
    if (m_connection != nullptr) {
        drain();
        m_connection->executionFinished(this);
    }
}

bool MySqlExecution::nextResult() {
    if (m_connection == nullptr || m_state == State::Finished) {
        return false;
    }
    if (m_state == State::Pending) {
        m_state = State::InResult;
        loadCurrent();
        return true;
    }

    m_result.reset(); // descarta as linhas não lidas do resultado atual
    MYSQL* handle = m_connection->handle();
    if (mysql_more_results(handle) == 0) {
        m_state = State::Finished;
        return false;
    }
    const auto started = std::chrono::steady_clock::now();
    const int status = mysql_next_result(handle);
    m_timing.server +=
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - started);
    if (status > 0) {
        m_state = State::Finished;
        m_connection->raise(handle);
    }
    if (status < 0) {
        m_state = State::Finished;
        return false;
    }
    loadCurrent();
    return true;
}

void MySqlExecution::loadCurrent() {
    MYSQL* handle = m_connection->handle();
    MYSQL_RES* result = mysql_use_result(handle);
    if (result != nullptr) {
        m_rowsAffected = -1;
        m_warnings = mysql_warning_count(handle);
        m_result = std::make_unique<MySqlResultSet>(*m_connection, ResultHandle(result), m_timing.network);
        return;
    }
    if (mysql_field_count(handle) != 0) {
        m_state = State::Finished;
        m_connection->raise(handle); // devia ter linhas e não veio nada: erro de leitura
    }
    m_rowsAffected = static_cast<std::int64_t>(mysql_affected_rows(handle));
    m_warnings = mysql_warning_count(handle);
}

void MySqlExecution::drain() noexcept {
    m_result.reset();
    MYSQL* handle = m_connection->handle();
    if (handle == nullptr) {
        return;
    }
    // Resultados pendentes travam a conexão ("Commands out of sync"): lê e descarta todos.
    if (m_state == State::Pending) {
        const ResultHandle discarded(mysql_use_result(handle));
    }
    while (mysql_more_results(handle) != 0 && mysql_next_result(handle) == 0) {
        const ResultHandle discarded(mysql_use_result(handle));
    }
    m_state = State::Finished;
}

void MySqlExecution::detach() noexcept {
    if (m_connection != nullptr) {
        drain();
        m_connection = nullptr;
    }
}

} // namespace mydb::drivers::mysql
