#pragma once

#include "core/interfaces/IDriver.hpp"

#include <filesystem>

namespace mydb::drivers::mysql {

/// Fábrica de conexões MySQL e MariaDB (id "mysql", ADR-0007).
class MySqlDriver final : public core::IDriver {
public:
    /// `pluginDirectory`: plugins de autenticação da libmariadb distribuídos com o driver; vazio usa o padrão.
    explicit MySqlDriver(std::filesystem::path pluginDirectory = {}) : m_pluginDirectory(std::move(pluginDirectory)) {}

    static constexpr std::string_view kId = "mysql";

    [[nodiscard]] std::string_view id() const noexcept override { return kId; }
    [[nodiscard]] std::string displayName() const override { return "MySQL / MariaDB"; }
    [[nodiscard]] std::unique_ptr<core::IConnection>
    createConnection(const core::ConnectionParams& params, core::ICredentialStore& credentials) const override;

private:
    std::filesystem::path m_pluginDirectory;
};

} // namespace mydb::drivers::mysql
