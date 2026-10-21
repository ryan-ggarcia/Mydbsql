#include "ui/panels/HostPanel.hpp"

#include "ui/common/ErrorText.hpp"
#include "ui/common/Icons.hpp"
#include "ui/common/QtText.hpp"
#include "ui/panels/Cells.hpp"
#include "ui/panels/TableTab.hpp"

#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableView>
#include <QTimer>
#include <QVBoxLayout>

#include <map>
#include <utility>

namespace mydb::ui {

namespace {

constexpr int kDatabasesTab = 0;
constexpr int kVariablesTab = 1;
constexpr int kStatusTab = 2;
constexpr int kProcessesTab = 3;
constexpr int kCommandsTab = 4;

/// Chama `call(sessão, callback)` e entrega o resultado a `apply`, se a aba ainda mostrar a mesma sessão.
template <typename R, typename Call, typename Apply>
void request(HostPanel* panel, TableTab* table, Call call, Apply apply) {
    std::string session = panel->sessionId();
    table->setStatus(HostPanel::tr("Carregando…"));
    QPointer<HostPanel> self(panel);
    QPointer<TableTab> target(table);
    call(session, services::Completion<R>(
                      [self, target, session, apply = std::move(apply)](services::Outcome<R> outcome) mutable {
                          if (!self || !target || self->sessionId() != session) {
                              return;
                          }
                          if (!outcome.ok()) {
                              target->setStatus(errorText(outcome.error()));
                              return;
                          }
                          apply(std::move(outcome).value());
                      }));
}

} // namespace

HostPanel::HostPanel(services::session::SessionService& sessions, QWidget* parent)
    : QWidget(parent),
      m_sessions(sessions),
      m_admin(sessions),
      m_tabs(new QTabWidget(this)),
      m_hideSleeping(new QCheckBox(tr("Esconder ociosos"), this)),
      m_processInterval(new QSpinBox(this)),
      m_processTimer(new QTimer(this)) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_tabs);

    m_tables[kDatabasesTab] =
        new TableTab({tr("Banco"), tr("Tamanho"), tr("Itens"), tr("Tabelas"), tr("Views"), tr("Procedures"),
                      tr("Functions"), tr("Triggers"), tr("Events"), tr("Última alteração"), tr("Collation")},
                     true, this);
    m_tables[kVariablesTab] = new TableTab({tr("Variável"), tr("Sessão"), tr("Global")}, true, this);
    m_tables[kStatusTab] = new TableTab({tr("Variável"), tr("Valor")}, true, this);
    m_tables[kProcessesTab] = new TableTab(
        {tr("Id"), tr("Usuário"), tr("Host"), tr("Banco"), tr("Comando"), tr("Tempo (s)"), tr("Estado"), tr("Info")},
        true, this);
    m_tables[kCommandsTab] =
        new TableTab({tr("Comando"), tr("Total"), tr("Por hora"), tr("Por segundo"), tr("Percentual")}, false, this);

    m_tabs->addTab(m_tables[kDatabasesTab], appIcon("database"), tr("Bancos"));
    m_tabs->addTab(m_tables[kVariablesTab], tr("Variáveis"));
    m_tabs->addTab(m_tables[kStatusTab], tr("Status"));
    m_tabs->addTab(m_tables[kProcessesTab], tr("Processos"));
    m_tabs->addTab(m_tables[kCommandsTab], tr("Estatísticas de comandos"));

    // Processos: atualização automática, esconder os ociosos e KILL.
    QHBoxLayout& processBar = m_tables[kProcessesTab]->toolbar();
    m_processInterval->setRange(0, 3600);
    m_processInterval->setSuffix(tr(" s"));
    m_processInterval->setSpecialValueText(tr("manual"));
    m_processInterval->setToolTip(tr("Atualizar a lista a cada"));
    auto* kill = new QPushButton(appIcon("kill"), tr("Encerrar"), this);
    kill->setObjectName(QStringLiteral("killProcessButton"));
    kill->setToolTip(tr("Encerra a conexão selecionada no servidor (KILL)"));
    processBar.addWidget(m_hideSleeping);
    processBar.addWidget(new QLabel(tr("Atualizar a cada"), this));
    processBar.addWidget(m_processInterval);
    processBar.addWidget(kill);
    connect(kill, &QPushButton::clicked, this, &HostPanel::killSelectedProcess);
    connect(m_hideSleeping, &QCheckBox::toggled, this, [this] { loadProcesses(); });
    connect(m_processInterval, &QSpinBox::valueChanged, this, [this](int seconds) {
        constexpr int kMillisecondsPerSecond = 1000;
        if (seconds > 0) {
            m_processTimer->start(seconds * kMillisecondsPerSecond);
        } else {
            m_processTimer->stop();
        }
    });
    connect(m_processTimer, &QTimer::timeout, this, [this] {
        if (isVisible() && m_tabs->currentIndex() == kProcessesTab) {
            loadProcesses();
        }
    });

    connect(&m_tables[kDatabasesTab]->view(), &QTableView::doubleClicked, this, [this](const QModelIndex& index) {
        const int row = m_tables[kDatabasesTab]->sourceRow(index);
        if (row >= 0) {
            Q_EMIT databaseActivated(m_tables[kDatabasesTab]->model().rowAt(row).at(0).display.toString());
        }
    });
    connect(&m_tables[kVariablesTab]->view(), &QTableView::doubleClicked, this,
            [this](const QModelIndex& index) { editVariable(m_tables[kVariablesTab]->sourceRow(index)); });
    connect(m_tabs, &QTabWidget::currentChanged, this, [this](int tab) {
        if (tab >= 0 && tab < kTabCount && !m_loaded.at(static_cast<std::size_t>(tab))) {
            load(tab);
        }
    });
    updateAvailability();
}

HostPanel::~HostPanel() = default;

void HostPanel::setSession(const std::string& sessionId) {
    if (sessionId == m_sessionId) {
        return;
    }
    m_sessionId = sessionId;
    m_loaded.fill(false);
    for (TableTab* table : m_tables) {
        table->setRows({});
        table->setStatus({});
    }
    updateAvailability();
    if (!m_sessionId.empty() && isVisible()) {
        load(m_tabs->currentIndex());
    }
}

void HostPanel::refresh() {
    if (!m_sessionId.empty()) {
        load(m_tabs->currentIndex());
    }
}

void HostPanel::updateAvailability() {
    const auto* session = m_sessions.find(m_sessionId);
    const bool admin = session != nullptr && session->hasServerAdmin();
    for (const int tab : {kVariablesTab, kStatusTab, kProcessesTab, kCommandsTab}) {
        m_tabs->setTabEnabled(tab, admin);
    }
    if (!admin) {
        m_tabs->setCurrentIndex(kDatabasesTab);
    }
}

void HostPanel::load(int tab) {
    if (m_sessionId.empty() || tab < 0 || tab >= kTabCount) {
        return;
    }
    m_loaded.at(static_cast<std::size_t>(tab)) = true;
    switch (tab) {
    case kDatabasesTab:
        loadDatabases();
        break;
    case kVariablesTab:
        loadVariables();
        break;
    case kStatusTab:
        loadStatus();
        break;
    case kProcessesTab:
        loadProcesses();
        break;
    case kCommandsTab:
        loadCommands();
        break;
    default:
        break;
    }
}

void HostPanel::loadDatabases() {
    TableTab* table = m_tables[kDatabasesTab];
    request<std::vector<core::model::DatabaseSummary>>(
        this, table, [this](std::string_view id, auto done) { m_sessions.databaseSummaries(id, std::move(done)); },
        [table](const std::vector<core::model::DatabaseSummary>& summaries) {
            std::vector<RowsModel::Row> rows;
            rows.reserve(summaries.size());
            for (const auto& s : summaries) {
                const std::int64_t items =
                    static_cast<std::int64_t>(s.tables) + s.views + s.procedures + s.functions + s.triggers + s.events;
                rows.push_back({cell(toQt(s.name)), sizeCell(s.sizeBytes), numberCell(items), numberCell(s.tables),
                                numberCell(s.views), numberCell(s.procedures), numberCell(s.functions),
                                numberCell(s.triggers), numberCell(s.events), timeCell(s.lastModified),
                                cell(toQt(s.collation))});
            }
            table->setStatus(tr("%n banco(s)", nullptr, static_cast<int>(rows.size())));
            table->setRows(std::move(rows));
        });
}

void HostPanel::loadVariables() {
    TableTab* table = m_tables[kVariablesTab];
    request<services::admin::ServerVariables>(
        this, table, [this](std::string_view id, auto done) { m_admin.variables(id, std::move(done)); },
        [table](const services::admin::ServerVariables& variables) {
            std::map<std::string, std::pair<QString, QString>> merged;
            for (const auto& variable : variables.session) {
                merged[variable.name].first = toQt(variable.value);
            }
            for (const auto& variable : variables.global) {
                merged[variable.name].second = toQt(variable.value);
            }
            std::vector<RowsModel::Row> rows;
            rows.reserve(merged.size());
            for (const auto& [name, values] : merged) {
                rows.push_back({cell(toQt(name)), cell(values.first), cell(values.second)});
            }
            table->setStatus(tr("%n variável(is). Duplo clique para editar.", nullptr, static_cast<int>(rows.size())));
            table->setRows(std::move(rows));
        });
}

void HostPanel::loadStatus() {
    TableTab* table = m_tables[kStatusTab];
    request<std::vector<core::ServerVariable>>(
        this, table, [this](std::string_view id, auto done) { m_admin.status(id, std::move(done)); },
        [table](const std::vector<core::ServerVariable>& status) {
            std::vector<RowsModel::Row> rows;
            rows.reserve(status.size());
            for (const auto& variable : status) {
                rows.push_back({cell(toQt(variable.name)), cell(toQt(variable.value))});
            }
            table->setStatus(tr("%n variável(is) de status", nullptr, static_cast<int>(rows.size())));
            table->setRows(std::move(rows));
        });
}

void HostPanel::loadProcesses() {
    TableTab* table = m_tables[kProcessesTab];
    const bool hideSleeping = m_hideSleeping->isChecked();
    request<std::vector<core::ServerProcess>>(
        this, table, [this](std::string_view id, auto done) { m_admin.processes(id, std::move(done)); },
        [table, hideSleeping](const std::vector<core::ServerProcess>& processes) {
            std::vector<RowsModel::Row> rows;
            rows.reserve(processes.size());
            for (const auto& p : processes) {
                if (hideSleeping && p.command == "Sleep") {
                    continue;
                }
                rows.push_back({numberCell(static_cast<std::int64_t>(p.id)), cell(toQt(p.user)), cell(toQt(p.host)),
                                cell(toQt(p.database)), cell(toQt(p.command)), numberCell(p.seconds),
                                cell(toQt(p.state)), cell(toQt(p.info))});
            }
            table->setStatus(tr("%n processo(s)", nullptr, static_cast<int>(rows.size())));
            table->setRows(std::move(rows));
        });
}

void HostPanel::loadCommands() {
    TableTab* table = m_tables[kCommandsTab];
    request<std::vector<services::admin::CommandStatistic>>(
        this, table, [this](std::string_view id, auto done) { m_admin.commandStatistics(id, std::move(done)); },
        [table](const std::vector<services::admin::CommandStatistic>& statistics) {
            std::vector<RowsModel::Row> rows;
            rows.reserve(statistics.size());
            for (const auto& s : statistics) {
                rows.push_back({cell(toQt(s.command)), numberCell(static_cast<std::int64_t>(s.total)),
                                decimalCell(s.perHour, 1), decimalCell(s.perSecond, 3), decimalCell(s.percent, 1)});
            }
            table->setStatus(
                tr("%n comando(s) executado(s) desde que o servidor subiu", nullptr, static_cast<int>(rows.size())));
            table->setRows(std::move(rows));
        });
}

void HostPanel::editVariable(int sourceRow) {
    if (sourceRow < 0) {
        return;
    }
    const RowsModel::Row& row = m_tables[kVariablesTab]->model().rowAt(sourceRow);
    const QString name = row.at(0).display.toString();

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Editar variável"));
    auto* form = new QFormLayout(&dialog);
    auto* value = new QLineEdit(row.at(1).display.toString(), &dialog);
    value->setObjectName(QStringLiteral("variableValue"));
    auto* sessionScope = new QRadioButton(tr("Só nesta sessão"), &dialog);
    auto* globalScope = new QRadioButton(tr("Global (todas as conexões novas)"), &dialog);
    sessionScope->setChecked(true);
    connect(globalScope, &QRadioButton::toggled, value,
            [value, sessionValue = row.at(1).display.toString(), globalValue = row.at(2).display.toString()](
                bool global) { value->setText(global ? globalValue : sessionValue); });
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(new QLabel(QStringLiteral("<b>%1</b>").arg(name.toHtmlEscaped()), &dialog));
    form->addRow(tr("Valor"), value);
    form->addRow(sessionScope);
    form->addRow(globalScope);
    form->addRow(buttons);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    core::ServerVariable variable{
        .name = toStd(name), .value = toStd(value->text()), .global = globalScope->isChecked()};
    std::string session = m_sessionId;
    QPointer<HostPanel> self(this);
    m_admin.setVariable(session, std::move(variable),
                        [self, session](const services::Outcome<services::Done>& outcome) {
                            if (!self || self->m_sessionId != session) {
                                return;
                            }
                            if (!outcome.ok()) {
                                QMessageBox::warning(self, tr("Editar variável"), errorText(outcome.error()));
                                return;
                            }
                            self->loadVariables();
                        });
}

void HostPanel::killSelectedProcess() {
    TableTab* table = m_tables[kProcessesTab];
    const int row = table->currentSourceRow();
    if (row < 0) {
        return;
    }
    const auto id = static_cast<std::uint64_t>(table->model().rowAt(row).at(0).sort.toULongLong());
    const auto* session = m_sessions.find(m_sessionId);
    const bool own = session != nullptr && session->server().connectionId == id;
    const QString question = own ? tr("O processo %1 é a conexão do próprio Mydbsql. Encerrar mesmo assim?").arg(id)
                                 : tr("Encerrar o processo %1 no servidor?").arg(id);
    if (QMessageBox::question(this, tr("Encerrar processo"), question) != QMessageBox::Yes) {
        return;
    }
    std::string sessionId = m_sessionId;
    QPointer<HostPanel> self(this);
    m_admin.killProcess(sessionId, id, [self, sessionId](const services::Outcome<services::Done>& outcome) {
        if (!self || self->m_sessionId != sessionId) {
            return;
        }
        if (!outcome.ok()) {
            QMessageBox::warning(self, tr("Encerrar processo"), errorText(outcome.error()));
        }
        self->loadProcesses();
    });
}

} // namespace mydb::ui
