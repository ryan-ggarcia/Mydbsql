#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace mydb::core {

struct SslOptions {
    enum class Mode : std::uint8_t {
        Disabled,
        Preferred,
        Required,
        VerifyCa,       ///< confere a cadeia de certificados
        VerifyIdentity, ///< confere a cadeia e o nome do host
    };

    Mode mode{Mode::Preferred};
    std::filesystem::path privateKey;
    std::filesystem::path certificate;
    std::filesystem::path certificateAuthority;
    std::string cipher;

    friend bool operator==(const SslOptions&, const SslOptions&) = default;
};

struct SshTunnelOptions {
    std::string host;
    std::uint16_t port{22};
    std::string user;
    std::filesystem::path privateKey;       ///< vazio: autenticação por senha (a senha fica no ICredentialStore)
    std::optional<std::uint16_t> localPort; ///< vazio: porta livre escolhida automaticamente
    std::chrono::seconds timeout{10};

    friend bool operator==(const SshTunnelOptions&, const SshTunnelOptions&) = default;
};

/// Parâmetros de uma conexão. Valor imutável e sem senha: as senhas ficam no ICredentialStore,
/// identificadas pelo sessionId (CLAUDE.md §0).
struct ConnectionParams {
    std::string sessionId;
    std::string driverId; ///< id do IDriver no DriverRegistry, ex.: "mysql", "postgresql", "sqlite"
    std::string host;     ///< para SQLite, o caminho do arquivo
    std::uint16_t port{0};
    std::string user;
    std::string defaultDatabase;
    std::optional<SslOptions> ssl;
    std::optional<SshTunnelOptions> ssh;
    std::chrono::seconds queryTimeout{0}; ///< 0 = sem limite
    std::chrono::seconds keepAlive{0};    ///< 0 = desligado
    std::string startupScript;
    std::string ignoreDatabasePattern; ///< regex
    bool compressed{false};
    bool localTimeZone{false};

    friend bool operator==(const ConnectionParams&, const ConnectionParams&) = default;
};

} // namespace mydb::core
