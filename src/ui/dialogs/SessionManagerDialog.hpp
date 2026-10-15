#pragma once

#include "core/interfaces/IConnection.hpp"
#include "services/session/SessionConnector.hpp"
#include "services/session/SessionRepository.hpp"

#include <QDialog>

#include <functional>
#include <memory>
#include <thread>
#include <vector>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QTabWidget;
class QTreeWidget;
class QTreeWidgetItem;

namespace mydb::ui {

/// Gerenciador de sessões (CLAUDE.md §5, Fase 3): árvore de sessões em pastas e abas Configurações, SSH,
/// SSL e Avançado. Conectar e testar rodam fora da thread da UI (§2.3, regra 1).
class SessionManagerDialog final : public QDialog {
    Q_OBJECT

public:
    using ConnectedHandler = std::function<void(const services::session::Session&, std::unique_ptr<core::IConnection>)>;

    SessionManagerDialog(services::session::SessionRepository& repository,
                         services::session::SessionConnector& connector, std::vector<std::string> drivers,
                         bool passwordsPersist, QWidget* parent = nullptr);
    SessionManagerDialog(const SessionManagerDialog&) = delete;
    SessionManagerDialog& operator=(const SessionManagerDialog&) = delete;
    SessionManagerDialog(SessionManagerDialog&&) = delete;
    SessionManagerDialog& operator=(SessionManagerDialog&&) = delete;
    ~SessionManagerDialog() override;

    /// Chamado na thread da UI quando "Abrir" conecta; depois disso o diálogo fecha com accept().
    void setConnectedHandler(ConnectedHandler handler) { m_onConnected = std::move(handler); }

    /// Sessão selecionada (vazio se for uma pasta ou nada).
    [[nodiscard]] std::string selectedSessionId() const;
    void selectSession(const std::string& id);

    /// Aplica o formulário à sessão selecionada e grava. false se não houver sessão selecionada.
    bool saveCurrent();

Q_SIGNALS:
    /// Resultado de "Testar conexão" (útil também para os testes de UI).
    void testFinished(bool ok, const QString& message);

private:
    void buildUi();
    void rebuildTree(const std::string& selectId = {});
    void loadForm(const services::session::Session* session);
    void writeForm(services::session::Session& session) const;
    void setBusy(bool busy, const QString& status);
    void startConnect(bool openAfterwards);
    void finishConnect(const services::session::Session& session, std::unique_ptr<core::IConnection>& connection,
                       const QString& error, bool openAfterwards) noexcept;
    /// false se o usuário cancelar o pedido de senha.
    [[nodiscard]] bool askPasswordIfNeeded(const services::session::Session& session,
                                           std::optional<std::string>& password);
    bool saveSession(const std::string& id);

    void onNewSession();
    void onNewFolder();
    void onDuplicate();
    void onDelete();
    void onImport();
    void onExport();

    services::session::SessionRepository& m_repository;
    services::session::SessionConnector& m_connector;
    std::vector<std::string> m_drivers;
    bool m_passwordsPersist;
    ConnectedHandler m_onConnected;
    std::jthread m_worker;
    bool m_loading{false};
    QString m_typedPassword;

    QTreeWidget* m_tree{nullptr};
    QTabWidget* m_tabs{nullptr};
    QLabel* m_status{nullptr};
    QPushButton* m_testButton{nullptr};
    QPushButton* m_openButton{nullptr};
    QPushButton* m_saveButton{nullptr};

    // Configurações
    QLineEdit* m_name{nullptr};
    QComboBox* m_folder{nullptr};
    QComboBox* m_driver{nullptr};
    QLineEdit* m_host{nullptr};
    QSpinBox* m_port{nullptr};
    QLineEdit* m_user{nullptr};
    QLineEdit* m_password{nullptr};
    QCheckBox* m_savePassword{nullptr};
    QLineEdit* m_database{nullptr};
    QLineEdit* m_color{nullptr};
    QPlainTextEdit* m_comment{nullptr};
    // SSH
    QCheckBox* m_sshEnabled{nullptr};
    QLineEdit* m_sshHost{nullptr};
    QSpinBox* m_sshPort{nullptr};
    QLineEdit* m_sshUser{nullptr};
    QLineEdit* m_sshKey{nullptr};
    QLineEdit* m_sshPassword{nullptr};
    QSpinBox* m_sshLocalPort{nullptr};
    QSpinBox* m_sshTimeout{nullptr};
    // SSL
    QComboBox* m_sslMode{nullptr};
    QLineEdit* m_sslKey{nullptr};
    QLineEdit* m_sslCert{nullptr};
    QLineEdit* m_sslCa{nullptr};
    QLineEdit* m_sslCipher{nullptr};
    // Avançado
    QCheckBox* m_compressed{nullptr};
    QSpinBox* m_queryTimeout{nullptr};
    QSpinBox* m_keepAlive{nullptr};
    QPlainTextEdit* m_startupScript{nullptr};
    QLineEdit* m_ignoreDatabases{nullptr};
    QCheckBox* m_localTimeZone{nullptr};
};

} // namespace mydb::ui
