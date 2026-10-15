#pragma once

#include "core/interfaces/ICredentialStore.hpp"
#include "core/interfaces/ISettingsStore.hpp"
#include "services/session/Session.hpp"

#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace mydb::services::session {

class SettingsError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

/// Sessões salvas, organizadas em pastas (CLAUDE.md §5, Fase 3). Persiste no documento "sessions" do
/// ISettingsStore, com versão de esquema e migração; as senhas vão para o ICredentialStore.
/// Não é thread-safe: usado pela thread da UI.
class SessionRepository {
public:
    static constexpr int kSchemaVersion = 1;
    static constexpr std::string_view kDocument = "sessions";

    SessionRepository(core::ISettingsStore& settings, core::ICredentialStore& credentials) noexcept
        : m_settings(&settings), m_credentials(&credentials) {}

    /// Lê o documento. Lança SettingsError se ele estiver corrompido ou vier de uma versão mais nova do app.
    void load();
    void save() const;

    [[nodiscard]] const std::vector<Session>& sessions() const noexcept { return m_sessions; }
    [[nodiscard]] const Session* find(std::string_view id) const noexcept;

    /// Pastas em ordem alfabética: as criadas explicitamente e as que contêm sessões (com os pais).
    [[nodiscard]] std::vector<std::string> folders() const;

    /// Nova sessão com id único; ConnectionParams::sessionId já vem preenchido.
    Session& create(std::string name, std::string folder = {});
    /// Substitui a sessão de mesmo id. Lança std::out_of_range se não existir.
    void update(const Session& session);
    /// Remove a sessão e as senhas dela.
    void remove(std::string_view id);
    /// Cópia com id novo e nome "<nome> (cópia)"; as senhas também são copiadas.
    Session& duplicate(std::string_view id);

    void addFolder(std::string path);
    /// Renomeia a pasta e as subpastas, movendo as sessões junto.
    void renameFolder(std::string_view from, std::string_view to);
    /// Só remove pastas sem sessões; lança std::logic_error caso contrário.
    void removeFolder(std::string_view path);

    void setPassword(std::string_view id, std::string_view password);
    void clearPassword(std::string_view id);
    void setSshPassword(std::string_view id, std::string_view password);
    void clearSshPassword(std::string_view id);

    void recordConnection(std::string_view id, SessionStatistics::TimePoint when);

    /// JSON com as sessões escolhidas (todas, se `ids` vier vazio). Nunca inclui senhas.
    [[nodiscard]] std::string exportJson(std::span<const std::string> ids = {}) const;
    /// Acrescenta as sessões de um JSON exportado, com ids novos. Devolve quantas entraram.
    std::size_t importJson(std::string_view text);

private:
    Session& at(std::string_view id);

    core::ISettingsStore* m_settings;
    core::ICredentialStore* m_credentials;
    std::vector<Session> m_sessions;
    std::vector<std::string> m_folders;
};

} // namespace mydb::services::session
