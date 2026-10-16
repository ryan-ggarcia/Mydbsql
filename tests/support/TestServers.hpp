#pragma once

// Servidores dos testes de integração, lidos de variáveis de ambiente no formato
// "host=127.0.0.1;port=3307;user=root;password=" (MYDB_TEST_MYSQL, MYDB_TEST_MARIADB).

#include <cstdlib>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace mydb::tests {

using ServerValues = std::map<std::string, std::string, std::less<>>;

[[nodiscard]] inline ServerValues parseServer(std::string_view text) {
    ServerValues values;
    while (!text.empty()) {
        const std::size_t end = text.find(';');
        const std::string_view item = text.substr(0, end);
        if (const std::size_t equals = item.find('='); equals != std::string_view::npos) {
            values[std::string(item.substr(0, equals))] = std::string(item.substr(equals + 1));
        }
        if (end == std::string_view::npos) {
            break;
        }
        text.remove_prefix(end + 1);
    }
    return values;
}

/// nullopt se a variável não estiver definida: os testes daquele servidor são pulados.
[[nodiscard]] inline std::optional<ServerValues> serverFromEnvironment(const char* variable) {
    const char* raw = std::getenv(variable); // NOLINT(concurrency-mt-unsafe): lido antes de qualquer thread
    if (raw == nullptr || *raw == '\0') {
        return std::nullopt;
    }
    return parseServer(raw);
}

[[nodiscard]] inline std::string serverValue(const ServerValues& server, std::string_view key,
                                             std::string_view fallback) {
    const auto found = server.find(key);
    return found == server.end() ? std::string(fallback) : found->second;
}

} // namespace mydb::tests
