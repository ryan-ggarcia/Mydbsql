#include "ui/dialogs/SessionManagerDialog.hpp"

#include "ui/common/ErrorText.hpp"
#include "ui/common/QtText.hpp"

#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFont>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include <QTabWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <array>
#include <exception>
#include <map>
#include <utility>

namespace mydb::ui {

namespace {

using services::session::Session;

constexpr int kKindRole = Qt::UserRole;
constexpr int kKeyRole = Qt::UserRole + 1;
constexpr int kFolderKind = 1;
constexpr int kSessionKind = 2;
constexpr int kDefaultMySqlPort = 3306;
constexpr int kMaxPort = 65535;
constexpr int kMaxSeconds = 86400;
constexpr int kIconSize = 12;

struct SslModeItem {
    const char* label;
    core::SslOptions::Mode mode;
};

constexpr std::array<SslModeItem, 5> kSslModes{{
    {.label = QT_TRANSLATE_NOOP("SessionManagerDialog", "Preferir (sem verificar o certificado)"),
     .mode = core::SslOptions::Mode::Preferred},
    {.label = QT_TRANSLATE_NOOP("SessionManagerDialog", "Desligado"), .mode = core::SslOptions::Mode::Disabled},
    {.label = QT_TRANSLATE_NOOP("SessionManagerDialog", "Obrigatório"), .mode = core::SslOptions::Mode::Required},
    {.label = QT_TRANSLATE_NOOP("SessionManagerDialog", "Verificar a CA"), .mode = core::SslOptions::Mode::VerifyCa},
    {.label = QT_TRANSLATE_NOOP("SessionManagerDialog", "Verificar a CA e o nome do host"),
     .mode = core::SslOptions::Mode::VerifyIdentity},
}};

QIcon colorIcon(const QString& color) {
    QPixmap pixmap(kIconSize, kIconSize);
    const QColor parsed = QColor::fromString(color);
    pixmap.fill(parsed.isValid() ? parsed : QColor(Qt::transparent));
    return {pixmap};
}

QLineEdit* fileField(QWidget* parent, QFormLayout* form, const QString& label, const QString& objectName) {
    auto* edit = new QLineEdit(parent);
    edit->setObjectName(objectName);
    auto* browse = new QPushButton(QObject::tr("…"), parent);
    browse->setFixedWidth(browse->fontMetrics().horizontalAdvance(QStringLiteral("……")) + 16);
    QObject::connect(browse, &QPushButton::clicked, parent, [edit, parent] {
        const QString file = QFileDialog::getOpenFileName(parent, QObject::tr("Escolher arquivo"), edit->text());
        if (!file.isEmpty()) {
            edit->setText(file);
        }
    });
    auto* row = new QHBoxLayout;
    row->addWidget(edit);
    row->addWidget(browse);
    form->addRow(label, row);
    return edit;
}

QSpinBox* spin(QWidget* parent, int maximum, const QString& objectName, const QString& specialZero = {}) {
    auto* box = new QSpinBox(parent);
    box->setObjectName(objectName);
    box->setRange(0, maximum);
    if (!specialZero.isEmpty()) {
        box->setSpecialValueText(specialZero);
    }
    return box;
}

} // namespace

SessionManagerDialog::SessionManagerDialog(services::session::SessionRepository& repository,
                                           services::session::SessionConnector& connector,
                                           std::vector<std::string> drivers, bool passwordsPersist, QWidget* parent)
    : QDialog(parent),
      m_repository(repository),
      m_connector(connector),
      m_drivers(std::move(drivers)),
      m_passwordsPersist(passwordsPersist) {
    setObjectName(QStringLiteral("SessionManagerDialog"));
    setWindowTitle(tr("Mydbsql — Gerenciador de sessões"));
    buildUi();
    rebuildTree();
    if (!m_repository.sessions().empty()) {
        selectSession(m_repository.sessions().front().id);
    } else {
        loadForm(nullptr);
    }
}

SessionManagerDialog::~SessionManagerDialog() = default;

void SessionManagerDialog::buildUi() {
    resize(820, 560);
    auto* splitter = new QSplitter(this);

    // Árvore e ações de sessões
    auto* left = new QWidget(splitter);
    auto* leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    m_tree = new QTreeWidget(left);
    m_tree->setObjectName(QStringLiteral("sessionTree"));
    m_tree->setHeaderHidden(true);
    leftLayout->addWidget(m_tree);
    auto* actions = new QHBoxLayout;
    const auto addAction = [&](const QString& text, const QString& name, void (SessionManagerDialog::*slot)()) {
        auto* button = new QPushButton(text, left);
        button->setObjectName(name);
        connect(button, &QPushButton::clicked, this, slot);
        actions->addWidget(button);
    };
    addAction(tr("Nova"), QStringLiteral("newSessionButton"), &SessionManagerDialog::onNewSession);
    addAction(tr("Pasta"), QStringLiteral("newFolderButton"), &SessionManagerDialog::onNewFolder);
    addAction(tr("Duplicar"), QStringLiteral("duplicateButton"), &SessionManagerDialog::onDuplicate);
    addAction(tr("Excluir"), QStringLiteral("deleteButton"), &SessionManagerDialog::onDelete);
    leftLayout->addLayout(actions);
    auto* transfer = new QHBoxLayout;
    auto* importButton = new QPushButton(tr("Importar…"), left);
    auto* exportButton = new QPushButton(tr("Exportar…"), left);
    connect(importButton, &QPushButton::clicked, this, &SessionManagerDialog::onImport);
    connect(exportButton, &QPushButton::clicked, this, &SessionManagerDialog::onExport);
    transfer->addWidget(importButton);
    transfer->addWidget(exportButton);
    leftLayout->addLayout(transfer);

    // Abas do formulário
    m_tabs = new QTabWidget(splitter);

    auto* general = new QWidget(m_tabs);
    auto* generalForm = new QFormLayout(general);
    m_name = new QLineEdit(general);
    m_name->setObjectName(QStringLiteral("nameEdit"));
    generalForm->addRow(tr("Nome"), m_name);
    m_folder = new QComboBox(general);
    m_folder->setObjectName(QStringLiteral("folderCombo"));
    m_folder->setEditable(true);
    generalForm->addRow(tr("Pasta"), m_folder);
    m_driver = new QComboBox(general);
    m_driver->setObjectName(QStringLiteral("driverCombo"));
    for (const std::string& id : m_drivers) {
        m_driver->addItem(toQt(id), toQt(id));
    }
    generalForm->addRow(tr("Tipo de banco"), m_driver);
    m_host = new QLineEdit(general);
    m_host->setObjectName(QStringLiteral("hostEdit"));
    m_host->setPlaceholderText(QStringLiteral("127.0.0.1"));
    generalForm->addRow(tr("Host"), m_host);
    m_port = spin(general, kMaxPort, QStringLiteral("portSpin"));
    generalForm->addRow(tr("Porta"), m_port);
    m_user = new QLineEdit(general);
    m_user->setObjectName(QStringLiteral("userEdit"));
    generalForm->addRow(tr("Usuário"), m_user);
    m_password = new QLineEdit(general);
    m_password->setObjectName(QStringLiteral("passwordEdit"));
    m_password->setEchoMode(QLineEdit::Password);
    generalForm->addRow(tr("Senha"), m_password);
    m_savePassword = new QCheckBox(tr("Guardar a senha no cofre do sistema"), general);
    m_savePassword->setObjectName(QStringLiteral("savePasswordCheck"));
    if (!m_passwordsPersist) {
        m_savePassword->setText(tr("Lembrar a senha até fechar o app (o sistema não tem cofre de senhas)"));
    }
    generalForm->addRow(QString(), m_savePassword);
    m_database = new QLineEdit(general);
    m_database->setObjectName(QStringLiteral("databaseEdit"));
    generalForm->addRow(tr("Banco padrão"), m_database);
    m_color = new QLineEdit(general);
    m_color->setObjectName(QStringLiteral("colorEdit"));
    m_color->setPlaceholderText(QStringLiteral("#RRGGBB"));
    generalForm->addRow(tr("Cor"), m_color);
    m_comment = new QPlainTextEdit(general);
    m_comment->setObjectName(QStringLiteral("commentEdit"));
    m_comment->setMaximumHeight(80);
    generalForm->addRow(tr("Comentário"), m_comment);
    m_tabs->addTab(general, tr("Configurações"));

    auto* ssh = new QWidget(m_tabs);
    auto* sshForm = new QFormLayout(ssh);
    auto* sshNote = new QLabel(tr("O túnel SSH é implementado na Fase 11; as opções já ficam salvas."), ssh);
    sshNote->setWordWrap(true);
    sshForm->addRow(sshNote);
    m_sshEnabled = new QCheckBox(tr("Conectar por túnel SSH"), ssh);
    m_sshEnabled->setObjectName(QStringLiteral("sshEnabledCheck"));
    sshForm->addRow(m_sshEnabled);
    m_sshHost = new QLineEdit(ssh);
    sshForm->addRow(tr("Host SSH"), m_sshHost);
    m_sshPort = spin(ssh, kMaxPort, QStringLiteral("sshPortSpin"));
    sshForm->addRow(tr("Porta SSH"), m_sshPort);
    m_sshUser = new QLineEdit(ssh);
    sshForm->addRow(tr("Usuário SSH"), m_sshUser);
    m_sshKey = fileField(ssh, sshForm, tr("Chave privada"), QStringLiteral("sshKeyEdit"));
    m_sshPassword = new QLineEdit(ssh);
    m_sshPassword->setEchoMode(QLineEdit::Password);
    sshForm->addRow(tr("Senha SSH"), m_sshPassword);
    m_sshLocalPort = spin(ssh, kMaxPort, QStringLiteral("sshLocalPortSpin"), tr("automática"));
    sshForm->addRow(tr("Porta local"), m_sshLocalPort);
    m_sshTimeout = spin(ssh, kMaxSeconds, QStringLiteral("sshTimeoutSpin"));
    m_sshTimeout->setSuffix(tr(" s"));
    sshForm->addRow(tr("Tempo limite"), m_sshTimeout);
    m_tabs->addTab(ssh, tr("SSH"));

    auto* ssl = new QWidget(m_tabs);
    auto* sslForm = new QFormLayout(ssl);
    m_sslMode = new QComboBox(ssl);
    m_sslMode->setObjectName(QStringLiteral("sslModeCombo"));
    for (const SslModeItem& item : kSslModes) {
        m_sslMode->addItem(tr(item.label), static_cast<int>(item.mode));
    }
    sslForm->addRow(tr("Modo"), m_sslMode);
    m_sslKey = fileField(ssl, sslForm, tr("Chave"), QStringLiteral("sslKeyEdit"));
    m_sslCert = fileField(ssl, sslForm, tr("Certificado"), QStringLiteral("sslCertEdit"));
    m_sslCa = fileField(ssl, sslForm, tr("Autoridade (CA)"), QStringLiteral("sslCaEdit"));
    m_sslCipher = new QLineEdit(ssl);
    sslForm->addRow(tr("Cifras"), m_sslCipher);
    m_tabs->addTab(ssl, tr("SSL"));

    auto* advanced = new QWidget(m_tabs);
    auto* advancedForm = new QFormLayout(advanced);
    m_compressed = new QCheckBox(tr("Comprimir o tráfego"), advanced);
    m_compressed->setObjectName(QStringLiteral("compressedCheck"));
    advancedForm->addRow(m_compressed);
    m_queryTimeout = spin(advanced, kMaxSeconds, QStringLiteral("queryTimeoutSpin"), tr("sem limite"));
    m_queryTimeout->setSuffix(tr(" s"));
    advancedForm->addRow(tr("Tempo limite das consultas"), m_queryTimeout);
    m_keepAlive = spin(advanced, kMaxSeconds, QStringLiteral("keepAliveSpin"), tr("desligado"));
    m_keepAlive->setSuffix(tr(" s"));
    advancedForm->addRow(tr("Manter a conexão viva a cada"), m_keepAlive);
    m_startupScript = new QPlainTextEdit(advanced);
    m_startupScript->setObjectName(QStringLiteral("startupScriptEdit"));
    advancedForm->addRow(tr("SQL ao conectar"), m_startupScript);
    m_ignoreDatabases = new QLineEdit(advanced);
    m_ignoreDatabases->setObjectName(QStringLiteral("ignoreDatabasesEdit"));
    m_ignoreDatabases->setPlaceholderText(tr("expressão regular, ex.: ^(tmp|teste)_"));
    advancedForm->addRow(tr("Esconder bancos"), m_ignoreDatabases);
    m_localTimeZone = new QCheckBox(tr("Usar o fuso horário local"), advanced);
    advancedForm->addRow(m_localTimeZone);
    m_tabs->addTab(advanced, tr("Avançado"));

    splitter->setStretchFactor(1, 1);

    // Rodapé
    m_status = new QLabel(this);
    m_status->setObjectName(QStringLiteral("statusLabel"));
    m_status->setWordWrap(true);
    auto* buttons = new QDialogButtonBox(this);
    m_testButton = buttons->addButton(tr("Testar conexão"), QDialogButtonBox::ActionRole);
    m_testButton->setObjectName(QStringLiteral("testButton"));
    m_saveButton = buttons->addButton(tr("Salvar"), QDialogButtonBox::ApplyRole);
    m_saveButton->setObjectName(QStringLiteral("saveButton"));
    m_openButton = buttons->addButton(tr("Abrir"), QDialogButtonBox::AcceptRole);
    m_openButton->setObjectName(QStringLiteral("openButton"));
    m_openButton->setDefault(true);
    buttons->addButton(QDialogButtonBox::Close);
    connect(m_testButton, &QPushButton::clicked, this, [this] { startConnect(false); });
    connect(m_saveButton, &QPushButton::clicked, this, [this] { (void)saveCurrent(); });
    connect(m_openButton, &QPushButton::clicked, this, [this] { startConnect(true); });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(splitter, 1);
    layout->addWidget(m_status);
    layout->addWidget(buttons);

    connect(m_tree, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem* current, QTreeWidgetItem* previous) {
                if (m_loading) {
                    return;
                }
                if (previous != nullptr && previous->data(0, kKindRole).toInt() == kSessionKind) {
                    // Trocar de sessão salva a anterior, como no HeidiSQL: nada se perde ao clicar em outra.
                    const std::string previousId = toStd(previous->data(0, kKeyRole).toString());
                    if (saveSession(previousId) && m_repository.find(previousId) != nullptr) {
                        const Session& saved = *m_repository.find(previousId);
                        previous->setText(0, toQt(saved.name));
                        previous->setIcon(0, colorIcon(toQt(saved.color)));
                    }
                }
                const bool isSession = current != nullptr && current->data(0, kKindRole).toInt() == kSessionKind;
                loadForm(isSession ? m_repository.find(toStd(current->data(0, kKeyRole).toString())) : nullptr);
            });
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item) {
        if (item->data(0, kKindRole).toInt() == kSessionKind) {
            startConnect(true);
        }
    });
}

void SessionManagerDialog::rebuildTree(const std::string& selectId) {
    m_loading = true;
    m_tree->clear();
    std::map<std::string, QTreeWidgetItem*> folderItems;
    const auto folderItem = [&](const std::string& path) -> QTreeWidgetItem* {
        if (path.empty()) {
            return m_tree->invisibleRootItem();
        }
        if (const auto found = folderItems.find(path); found != folderItems.end()) {
            return found->second;
        }
        return nullptr;
    };
    for (const std::string& path : m_repository.folders()) {
        const std::size_t slash = path.rfind('/');
        QTreeWidgetItem* parent = folderItem(slash == std::string::npos ? std::string() : path.substr(0, slash));
        auto* item = new QTreeWidgetItem(parent != nullptr ? parent : m_tree->invisibleRootItem());
        item->setText(0, toQt(slash == std::string::npos ? path : path.substr(slash + 1)));
        item->setData(0, kKindRole, kFolderKind);
        item->setData(0, kKeyRole, toQt(path));
        QFont font = item->font(0);
        font.setBold(true);
        item->setFont(0, font);
        item->setExpanded(true);
        folderItems.emplace(path, item);
    }
    QTreeWidgetItem* selected = nullptr;
    for (const Session& session : m_repository.sessions()) {
        QTreeWidgetItem* parent = folderItem(session.folder);
        auto* item = new QTreeWidgetItem(parent != nullptr ? parent : m_tree->invisibleRootItem());
        item->setText(0, toQt(session.name));
        item->setIcon(0, colorIcon(toQt(session.color)));
        item->setData(0, kKindRole, kSessionKind);
        item->setData(0, kKeyRole, toQt(session.id));
        item->setToolTip(0, toQt(session.comment));
        if (session.id == selectId) {
            selected = item;
        }
    }
    m_tree->sortItems(0, Qt::AscendingOrder);
    m_folder->clear();
    m_folder->addItem(QString());
    for (const std::string& path : m_repository.folders()) {
        m_folder->addItem(toQt(path));
    }
    m_loading = false;
    if (selected != nullptr) {
        m_tree->setCurrentItem(selected);
    }
}

std::string SessionManagerDialog::selectedSessionId() const {
    const QTreeWidgetItem* item = m_tree->currentItem();
    if (item == nullptr || item->data(0, kKindRole).toInt() != kSessionKind) {
        return {};
    }
    return toStd(item->data(0, kKeyRole).toString());
}

void SessionManagerDialog::selectSession(const std::string& id) {
    if (const std::string current = selectedSessionId(); !current.empty() && current != id) {
        (void)saveSession(current); // como ao clicar em outra sessão na árvore
    }
    rebuildTree(id);
}

void SessionManagerDialog::loadForm(const Session* session) {
    const bool enabled = session != nullptr;
    m_tabs->setEnabled(enabled);
    m_testButton->setEnabled(enabled);
    m_openButton->setEnabled(enabled);
    m_saveButton->setEnabled(enabled);
    const Session empty;
    const Session& s = enabled ? *session : empty;
    const core::ConnectionParams& p = s.params;

    m_name->setText(toQt(s.name));
    m_folder->setCurrentText(toQt(s.folder));
    const int driverIndex = m_driver->findData(toQt(p.driverId));
    m_driver->setCurrentIndex(driverIndex >= 0 ? driverIndex : 0);
    m_host->setText(toQt(p.host));
    m_port->setValue(p.port == 0 ? kDefaultMySqlPort : p.port);
    m_user->setText(toQt(p.user));
    m_password->clear();
    m_password->setPlaceholderText(enabled && s.savePassword ? tr("(inalterada)") : QString());
    m_savePassword->setChecked(s.savePassword);
    m_database->setText(toQt(p.defaultDatabase));
    m_color->setText(toQt(s.color));
    m_comment->setPlainText(toQt(s.comment));

    const core::SshTunnelOptions ssh = p.ssh.value_or(core::SshTunnelOptions{});
    m_sshEnabled->setChecked(p.ssh.has_value());
    m_sshHost->setText(toQt(ssh.host));
    m_sshPort->setValue(ssh.port);
    m_sshUser->setText(toQt(ssh.user));
    m_sshKey->setText(pathToQt(ssh.privateKey));
    m_sshPassword->clear();
    m_sshLocalPort->setValue(ssh.localPort.value_or(0));
    m_sshTimeout->setValue(static_cast<int>(ssh.timeout.count()));

    const core::SslOptions ssl = p.ssl.value_or(core::SslOptions{});
    m_sslMode->setCurrentIndex(std::max(0, m_sslMode->findData(static_cast<int>(ssl.mode))));
    m_sslKey->setText(pathToQt(ssl.privateKey));
    m_sslCert->setText(pathToQt(ssl.certificate));
    m_sslCa->setText(pathToQt(ssl.certificateAuthority));
    m_sslCipher->setText(toQt(ssl.cipher));

    m_compressed->setChecked(p.compressed);
    m_queryTimeout->setValue(static_cast<int>(p.queryTimeout.count()));
    m_keepAlive->setValue(static_cast<int>(p.keepAlive.count()));
    m_startupScript->setPlainText(toQt(p.startupScript));
    m_ignoreDatabases->setText(toQt(p.ignoreDatabasePattern));
    m_localTimeZone->setChecked(p.localTimeZone);
}

void SessionManagerDialog::writeForm(Session& s) const {
    core::ConnectionParams& p = s.params;
    s.name = toStd(m_name->text().trimmed());
    s.folder = toStd(m_folder->currentText().trimmed());
    s.color = toStd(m_color->text().trimmed());
    s.comment = toStd(m_comment->toPlainText());
    s.savePassword = m_savePassword->isChecked();
    p.driverId = toStd(m_driver->currentData().toString());
    p.host = toStd(m_host->text().trimmed());
    p.port = static_cast<std::uint16_t>(m_port->value());
    p.user = toStd(m_user->text());
    p.defaultDatabase = toStd(m_database->text().trimmed());

    if (m_sshEnabled->isChecked()) {
        core::SshTunnelOptions ssh;
        ssh.host = toStd(m_sshHost->text().trimmed());
        ssh.port = static_cast<std::uint16_t>(m_sshPort->value());
        ssh.user = toStd(m_sshUser->text());
        ssh.privateKey = toPath(m_sshKey->text());
        if (m_sshLocalPort->value() != 0) {
            ssh.localPort = static_cast<std::uint16_t>(m_sshLocalPort->value());
        }
        ssh.timeout = std::chrono::seconds{m_sshTimeout->value()};
        p.ssh = std::move(ssh);
    } else {
        p.ssh.reset();
    }

    core::SslOptions ssl;
    ssl.mode = static_cast<core::SslOptions::Mode>(m_sslMode->currentData().toInt());
    ssl.privateKey = toPath(m_sslKey->text());
    ssl.certificate = toPath(m_sslCert->text());
    ssl.certificateAuthority = toPath(m_sslCa->text());
    ssl.cipher = toStd(m_sslCipher->text().trimmed());
    if (ssl == core::SslOptions{}) {
        p.ssl.reset(); // padrão: não grava nada
    } else {
        p.ssl = std::move(ssl);
    }

    p.compressed = m_compressed->isChecked();
    p.queryTimeout = std::chrono::seconds{m_queryTimeout->value()};
    p.keepAlive = std::chrono::seconds{m_keepAlive->value()};
    p.startupScript = toStd(m_startupScript->toPlainText());
    p.ignoreDatabasePattern = toStd(m_ignoreDatabases->text());
    p.localTimeZone = m_localTimeZone->isChecked();
}

bool SessionManagerDialog::saveCurrent() {
    const std::string id = selectedSessionId();
    if (!saveSession(id)) {
        return false;
    }
    m_status->setText(tr("Sessão salva."));
    rebuildTree(id);
    return true;
}

bool SessionManagerDialog::saveSession(const std::string& id) {
    const Session* existing = m_repository.find(id);
    if (existing == nullptr) {
        return false;
    }
    Session session = *existing;
    writeForm(session);
    if (session.name.empty()) {
        session.name = existing->name;
    }
    try {
        m_repository.update(session);
        if (!m_sshPassword->text().isEmpty()) {
            m_repository.setSshPassword(id, toStd(m_sshPassword->text()));
        }
        if (!session.savePassword) {
            m_repository.clearPassword(id);
        } else if (!m_password->text().isEmpty()) {
            m_repository.setPassword(id, toStd(m_password->text()));
        }
        if (!m_sshEnabled->isChecked()) {
            m_repository.clearSshPassword(id);
        }
        m_repository.save();
    } catch (const std::exception& error) {
        QMessageBox::warning(this, windowTitle(), tr("Não foi possível salvar a sessão:\n%1").arg(errorText(error)));
        return false;
    }
    m_password->clear();
    m_sshPassword->clear();
    return true;
}

bool SessionManagerDialog::askPasswordIfNeeded(const Session& session, std::optional<std::string>& password) {
    if (session.savePassword) {
        return true; // a senha está no cofre (gravada por saveCurrent)
    }
    if (!m_typedPassword.isEmpty()) {
        password = toStd(m_typedPassword);
        return true;
    }
    bool ok = false;
    const QString typed = QInputDialog::getText(
        this, windowTitle(), tr("Senha de %1 em %2:").arg(toQt(session.params.user), toQt(session.params.host)),
        QLineEdit::Password, QString(), &ok);
    if (!ok) {
        return false;
    }
    password = toStd(typed);
    return true;
}

void SessionManagerDialog::setBusy(bool busy, const QString& status) {
    m_tree->setEnabled(!busy);
    m_tabs->setEnabled(!busy);
    m_testButton->setEnabled(!busy);
    m_openButton->setEnabled(!busy);
    m_saveButton->setEnabled(!busy);
    m_status->setText(status);
}

void SessionManagerDialog::startConnect(bool openAfterwards) {
    // Sem guardar a senha, a digitada no formulário vale só para esta conexão (saveSession limpa o campo).
    m_typedPassword = m_password->text();
    if (!saveCurrent()) {
        return;
    }
    const Session* stored = m_repository.find(selectedSessionId());
    if (stored == nullptr) {
        return;
    }
    const Session session = *stored;
    std::optional<std::string> password;
    const bool proceed = askPasswordIfNeeded(session, password);
    m_typedPassword.clear();
    if (!proceed) {
        return;
    }
    setBusy(true, openAfterwards ? tr("Conectando a %1…").arg(toQt(session.name)) : tr("Testando a conexão…"));

    if (m_worker.joinable()) {
        m_worker.join();
    }
    // Só falta de memória pode escapar destes lambdas; nesse caso terminar o processo é o certo.
    // NOLINTNEXTLINE(bugprone-exception-escape)
    m_worker = std::jthread([this, session, password, openAfterwards] {
        auto connection = std::make_shared<std::unique_ptr<core::IConnection>>();
        QString error;
        try {
            *connection = m_connector.connect(session, password);
        } catch (const std::exception& e) {
            error = errorText(e);
        } catch (...) {
            error = tr("Erro desconhecido ao conectar.");
        }
        // De volta à thread da UI: só ela mexe nos widgets e no repositório.
        QMetaObject::invokeMethod(
            this,
            // NOLINTNEXTLINE(bugprone-exception-escape)
            [this, session, connection, error, openAfterwards] {
                finishConnect(session, *connection, error, openAfterwards);
            },
            Qt::QueuedConnection);
    });
}

void SessionManagerDialog::finishConnect(const Session& session, std::unique_ptr<core::IConnection>& connection,
                                         const QString& error, bool openAfterwards) noexcept {
    try {
        setBusy(false, QString());
        if (!connection) {
            m_status->setText(tr("Falha ao conectar."));
            Q_EMIT testFinished(false, error);
            QMessageBox::warning(this, windowTitle(), error);
            return;
        }
        const core::ServerInfo info = connection->serverInfo();
        const QString server = toQt(info.productName) + QLatin1Char(' ') + toQt(info.versionString);
        m_repository.recordConnection(session.id, std::chrono::system_clock::now());
        try {
            m_repository.save();
        } catch (const std::exception& saveError) {
            // A estatística de uso se perde, mas a conexão segue.
            m_status->setText(tr("Conectado, mas as estatísticas não foram salvas: %1").arg(errorText(saveError)));
        }
        if (!openAfterwards) {
            connection->close();
            m_status->setText(tr("Conexão bem-sucedida: %1").arg(server));
            Q_EMIT testFinished(true, server);
            return;
        }
        if (m_onConnected) {
            m_onConnected(session, std::move(connection));
        }
        accept();
    } catch (const std::exception& unexpected) {
        m_status->setText(errorText(unexpected));
    } catch (...) {
        m_status->setText(tr("Erro inesperado."));
    }
}

void SessionManagerDialog::onNewSession() {
    // A sessão nova entra na pasta selecionada ou na pasta da sessão selecionada.
    std::string folder;
    if (const QTreeWidgetItem* item = m_tree->currentItem(); item != nullptr) {
        if (item->data(0, kKindRole).toInt() == kFolderKind) {
            folder = toStd(item->data(0, kKeyRole).toString());
        } else if (const Session* selected = m_repository.find(selectedSessionId()); selected != nullptr) {
            folder = selected->folder;
        }
    }
    Session& session = m_repository.create(toStd(tr("Nova sessão")), folder);
    if (!m_drivers.empty()) {
        session.params.driverId = m_drivers.front();
    }
    const std::string id = session.id;
    rebuildTree(id);
    m_name->setFocus();
    m_name->selectAll();
}

void SessionManagerDialog::onNewFolder() {
    bool ok = false;
    const QString name = QInputDialog::getText(this, windowTitle(), tr("Nome da pasta (use / para subpastas):"),
                                               QLineEdit::Normal, QString(), &ok)
                             .trimmed();
    if (!ok || name.isEmpty()) {
        return;
    }
    m_repository.addFolder(toStd(name));
    m_repository.save();
    rebuildTree(selectedSessionId());
}

void SessionManagerDialog::onDuplicate() {
    const std::string id = selectedSessionId();
    if (id.empty()) {
        return;
    }
    const std::string copy = m_repository.duplicate(id).id;
    m_repository.save();
    rebuildTree(copy);
}

void SessionManagerDialog::onDelete() {
    const QTreeWidgetItem* item = m_tree->currentItem();
    if (item == nullptr) {
        return;
    }
    const QString key = item->data(0, kKeyRole).toString();
    if (item->data(0, kKindRole).toInt() == kFolderKind) {
        try {
            m_repository.removeFolder(toStd(key));
            m_repository.save();
        } catch (const std::logic_error&) {
            QMessageBox::information(this, windowTitle(), tr("A pasta ainda tem sessões."));
            return;
        }
    } else {
        const auto answer = QMessageBox::question(
            this, windowTitle(), tr("Excluir a sessão \"%1\" e a senha guardada dela?").arg(item->text(0)));
        if (answer != QMessageBox::Yes) {
            return;
        }
        m_repository.remove(toStd(key));
        m_repository.save();
    }
    rebuildTree();
    loadForm(nullptr);
}

void SessionManagerDialog::onImport() {
    const QString file = QFileDialog::getOpenFileName(this, tr("Importar sessões"), QString(), tr("JSON (*.json)"));
    if (file.isEmpty()) {
        return;
    }
    QFile input(file);
    if (!input.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, windowTitle(), tr("Não foi possível ler %1").arg(file));
        return;
    }
    try {
        const QByteArray content = input.readAll();
        const std::size_t count = m_repository.importJson(std::string_view(content.constData(), content.size()));
        m_repository.save();
        rebuildTree();
        m_status->setText(tr("%n sessão(ões) importada(s).", nullptr, static_cast<int>(count)));
    } catch (const std::exception& error) {
        QMessageBox::warning(this, windowTitle(), errorText(error));
    }
}

void SessionManagerDialog::onExport() {
    const QString file = QFileDialog::getSaveFileName(this, tr("Exportar sessões (sem senhas)"),
                                                      QStringLiteral("sessoes-mydbsql.json"), tr("JSON (*.json)"));
    if (file.isEmpty()) {
        return;
    }
    QFile output(file);
    const std::string json = m_repository.exportJson();
    if (!output.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
        output.write(json.data(), static_cast<qint64>(json.size())) != static_cast<qint64>(json.size())) {
        QMessageBox::warning(this, windowTitle(), tr("Não foi possível gravar %1").arg(file));
        return;
    }
    m_status->setText(tr("Sessões exportadas (sem senhas)."));
}

} // namespace mydb::ui
