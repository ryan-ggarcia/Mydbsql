#pragma once

#include "services/admin/ServerAdminService.hpp"
#include "services/session/SessionService.hpp"

#include <QWidget>

#include <array>
#include <string>

class QCheckBox;
class QSpinBox;
class QTabWidget;
class QTimer;

namespace mydb::ui {

class TableTab;

/// Aba Host (CLAUDE.md §1.3): bancos com tamanhos, variáveis (editáveis), status, processos (com KILL e
/// atualização automática) e estatísticas de comandos. Cada sub-aba carrega na primeira vez que aparece.
class HostPanel final : public QWidget {
    Q_OBJECT

public:
    explicit HostPanel(services::session::SessionService& sessions, QWidget* parent = nullptr);
    HostPanel(const HostPanel&) = delete;
    HostPanel& operator=(const HostPanel&) = delete;
    HostPanel(HostPanel&&) = delete;
    HostPanel& operator=(HostPanel&&) = delete;
    ~HostPanel() override;

    /// Vazio limpa a aba.
    void setSession(const std::string& sessionId);
    [[nodiscard]] const std::string& sessionId() const noexcept { return m_sessionId; }
    /// Recarrega a sub-aba visível.
    void refresh();

Q_SIGNALS:
    /// Duplo clique num banco da lista.
    void databaseActivated(const QString& database);

private:
    static constexpr int kTabCount = 5;

    void load(int tab);
    void loadDatabases();
    void loadVariables();
    void loadStatus();
    void loadProcesses();
    void loadCommands();
    void editVariable(int sourceRow);
    void killSelectedProcess();
    void updateAvailability();

    services::session::SessionService& m_sessions;
    services::admin::ServerAdminService m_admin;
    std::string m_sessionId;
    std::array<bool, kTabCount> m_loaded{};

    QTabWidget* m_tabs;
    std::array<TableTab*, kTabCount> m_tables{};
    QCheckBox* m_hideSleeping;
    QSpinBox* m_processInterval;
    QTimer* m_processTimer;
};

} // namespace mydb::ui
