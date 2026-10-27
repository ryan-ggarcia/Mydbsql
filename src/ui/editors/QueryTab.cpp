#include "ui/editors/QueryTab.hpp"

#include "core/sql/SqlSplitter.hpp"
#include "core/sql/SqlStatement.hpp"
#include "services/query/CompletionIndex.hpp"
#include "services/query/QueryService.hpp"
#include "services/session/SessionService.hpp"
#include "ui/common/ErrorText.hpp"
#include "ui/common/QtText.hpp"
#include "ui/editors/SqlEditor.hpp"
#include "ui/models/ResultTableModel.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QPlainTextEdit>
#include <QPointer>
#include <QSplitter>
#include <QTabWidget>
#include <QTableView>
#include <QTextBlock>
#include <QVBoxLayout>

#include <chrono>
#include <exception>
#include <utility>

namespace mydb::ui {

namespace {

namespace query = services::query;

constexpr int kMessagesTab = 0;
constexpr int kMaxResultTabs = 50;
constexpr int kMaxMessageLines = 5000;
constexpr std::size_t kFileProgressEvery = 500;
constexpr int kEditorStretch = 3;
constexpr int kResultsStretch = 2;

/// O que o worker da sessão monta para o editor: as regras léxicas do dialeto e o índice do autocomplete.
struct CompletionSeed {
    core::sql::SqlSyntax syntax;
    std::shared_ptr<const query::CompletionIndex> index;
};

using IndexPtr = std::shared_ptr<const query::CompletionIndex>;

QString duration(std::chrono::microseconds value) {
    constexpr double kMicrosPerMilli = 1000.0;
    constexpr double kMillisPerSecond = 1000.0;
    const double ms = static_cast<double>(value.count()) / kMicrosPerMilli;
    const QLocale locale;
    if (ms >= kMillisPerSecond) {
        return QueryTab::tr("%1 s").arg(locale.toString(ms / kMillisPerSecond, 'f', 2));
    }
    return QueryTab::tr("%1 ms").arg(locale.toString(ms, 'f', 1));
}

/// O catálogo de funções do dialeto, de resources/functions (vazio se o dialeto não tiver um).
std::string functionCatalog(const std::string& driverId) {
    QFile file(QStringLiteral(":/functions/%1.json").arg(toQt(driverId)));
    if (driverId.empty() || !file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return file.readAll().toStdString();
}

} // namespace

QueryTab::QueryTab(Dependencies dependencies, QWidget* parent)
    : QWidget(parent),
      m_deps(dependencies),
      m_editor(new SqlEditor(this)),
      m_session(new QLabel(this)),
      m_mode(new QComboBox(this)),
      m_stopOnError(new QCheckBox(tr("Parar em erro"), this)),
      m_results(new QTabWidget(this)),
      m_messages(new QPlainTextEdit(this)),
      m_metrics(new QLabel(this)) {
    setObjectName(QStringLiteral("queryTab"));
    m_mode->setObjectName(QStringLiteral("executionMode"));
    m_mode->addItem(tr("Uma sentença por vez"));
    m_mode->addItem(tr("Em lote"));
    m_mode->setToolTip(tr("Em lote, várias sentenças vão ao servidor de uma vez (mais rápido, menos detalhes)"));
    m_stopOnError->setObjectName(QStringLiteral("stopOnError"));
    m_stopOnError->setChecked(true);
    m_messages->setObjectName(QStringLiteral("queryMessages"));
    m_messages->setReadOnly(true);
    m_messages->setMaximumBlockCount(kMaxMessageLines);
    m_metrics->setObjectName(QStringLiteral("queryMetrics"));
    m_metrics->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_results->setObjectName(QStringLiteral("queryResults"));
    m_results->setDocumentMode(true);
    m_results->addTab(m_messages, tr("Mensagens"));

    auto* options = new QHBoxLayout();
    options->addWidget(m_session, 1);
    options->addWidget(m_mode);
    options->addWidget(m_stopOnError);

    auto* bottom = new QWidget(this);
    auto* bottomLayout = new QVBoxLayout(bottom);
    bottomLayout->setContentsMargins(0, 0, 0, 0);
    bottomLayout->addWidget(m_results, 1);
    bottomLayout->addWidget(m_metrics);

    auto* splitter = new QSplitter(Qt::Vertical, this);
    splitter->addWidget(m_editor);
    splitter->addWidget(bottom);
    splitter->setStretchFactor(0, kEditorStretch);
    splitter->setStretchFactor(1, kResultsStretch);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->addLayout(options);
    layout->addWidget(splitter, 1);

    m_editor->setMissingTablesHandler(
        [this](std::vector<core::model::ObjectRef> tables) { requestColumns(std::move(tables)); });
    connect(m_editor->document(), &QTextDocument::modificationChanged, this, &QueryTab::titleChanged);
    updateSessionLabel();
}

QueryTab::~QueryTab() = default;

void QueryTab::setSessionId(std::string sessionId) {
    m_sessionId = std::move(sessionId);
    updateSessionLabel();
    refreshCompletion();
}

void QueryTab::updateSessionLabel() {
    const auto* open = m_deps.sessions->find(m_sessionId);
    m_session->setText(open != nullptr ? tr("Sessão: %1").arg(toQt(open->session().name))
                                       : tr("Sem sessão: usa a selecionada na árvore"));
}

void QueryTab::refreshCompletion() {
    const auto* open = m_deps.sessions->find(m_sessionId);
    if (open == nullptr) {
        return;
    }
    ++m_generation;
    std::string driverId = open->session().params.driverId;
    std::string current = open->currentDatabase();
    QPointer<QueryTab> self(this);
    const std::uint64_t generation = m_generation;
    // Montado no worker da sessão (§2.3): os metadados vêm do cache e o índice só é lido depois.
    m_deps.sessions->run<CompletionSeed>(
        m_sessionId,
        [driverId, current](services::session::WorkContext& context) {
            CompletionSeed seed{.syntax = context.connection().dialect().syntax(), .index = {}};
            auto index = std::make_shared<query::CompletionIndex>(seed.syntax);
            index->setKeywords(query::sqlKeywords(driverId));
            const std::string catalog = functionCatalog(driverId);
            if (!catalog.empty()) {
                index->setFunctions(query::parseFunctionCatalog(catalog));
            }
            core::IMetadataProvider& metadata = context.metadata();
            index->setDatabases(metadata.databases(), current);
            if (!current.empty()) {
                index->setObjects(current, metadata.objects(current));
            }
            seed.index = std::move(index);
            return seed;
        },
        [self, generation, driverId](services::Outcome<CompletionSeed> outcome) {
            if (!self || generation != self->m_generation || !outcome.ok()) {
                return;
            }
            CompletionSeed seed = std::move(outcome).value();
            self->m_editor->setSyntax(seed.syntax, query::sqlKeywords(driverId));
            self->m_requestedTables.clear();
            self->m_index = seed.index;
            self->m_editor->setCompletionIndex(std::move(seed.index));
        });
}

void QueryTab::requestColumns(std::vector<core::model::ObjectRef> tables) {
    std::erase_if(tables, [this](const core::model::ObjectRef& table) {
        return !m_requestedTables.insert(table.database + '\n' + table.name).second;
    });
    if (tables.empty() || !m_index) {
        return;
    }
    QPointer<QueryTab> self(this);
    const std::uint64_t generation = m_generation;
    IndexPtr base = m_index;
    std::vector<std::string> keys;
    keys.reserve(tables.size());
    for (const auto& table : tables) {
        keys.push_back(table.database + '\n' + table.name);
    }
    m_deps.sessions->run<IndexPtr>(
        m_sessionId,
        [base, tables](services::session::WorkContext& context) {
            auto next = std::make_shared<query::CompletionIndex>(*base);
            for (const core::model::ObjectRef& table : tables) {
                try {
                    next->setColumns(context.metadata().table(table));
                } catch (const std::exception&) { // NOLINT(bugprone-empty-catch): sem colunas, só não completa
                }
            }
            return IndexPtr(std::move(next));
        },
        [self, generation, base, keys](services::Outcome<IndexPtr> outcome) {
            if (!self || generation != self->m_generation || !outcome.ok()) {
                return;
            }
            if (self->m_index != base) {
                // Outro pedido trocou o índice no meio: estas tabelas serão pedidas de novo.
                for (const std::string& key : keys) {
                    self->m_requestedTables.erase(key);
                }
                return;
            }
            self->m_index = std::move(outcome).value();
            self->m_editor->setCompletionIndex(self->m_index);
        });
}

void QueryTab::setTitle(const QString& title) {
    m_title = title;
    Q_EMIT titleChanged();
}

QString QueryTab::displayTitle() const {
    return m_editor->document()->isModified() && !m_file.isEmpty() ? m_title + QLatin1Char('*') : m_title;
}

void QueryTab::setFilePath(const QString& file) {
    m_file = file;
    if (!file.isEmpty()) {
        setTitle(QFileInfo(file).fileName());
    }
}

query::QueryTabState QueryTab::state() const {
    return query::QueryTabState{.title = toStd(m_title),
                                .sessionId = m_sessionId,
                                .sql = toStd(m_editor->toPlainText()),
                                .file = toStd(m_file)};
}

void QueryTab::setExecutionMode(query::ExecutionMode mode) {
    m_mode->setCurrentIndex(mode == query::ExecutionMode::Batch ? 1 : 0);
}

void QueryTab::setStopOnError(bool stop) {
    m_stopOnError->setChecked(stop);
}

QString QueryTab::messages() const {
    return m_messages->toPlainText();
}

QString QueryTab::metrics() const {
    return m_metrics->text();
}

void QueryTab::addMessage(const QString& text) {
    m_messages->appendPlainText(text);
}

bool QueryTab::startRun() {
    if (m_running) {
        return false;
    }
    if (m_deps.sessions->find(m_sessionId) == nullptr) {
        addMessage(tr("Nenhuma sessão aberta para esta aba: conecte-se ou selecione uma sessão na árvore."));
        m_results->setCurrentIndex(kMessagesTab);
        return false;
    }
    while (m_results->count() > 1) {
        QWidget* page = m_results->widget(1);
        m_results->removeTab(1);
        page->deleteLater();
    }
    m_messages->clear();
    m_errorShown = false;
    m_fileStatements = 0;
    m_running = true;
    m_metrics->setText(tr("Executando…"));
    Q_EMIT runningChanged();
    return true;
}

void QueryTab::run(sdk::QueryScope scope, bool explain) {
    query::QueryRequest request;
    request.mode = m_mode->currentIndex() == 1 ? query::ExecutionMode::Batch : query::ExecutionMode::OneByOne;
    request.stopOnError = m_stopOnError->isChecked();
    request.explain = explain;
    const QTextCursor cursor = m_editor->textCursor();
    if (scope == sdk::QueryScope::Selection && cursor.hasSelection()) {
        request.sql = toStd(m_editor->selectedSql());
        request.firstLine =
            static_cast<std::uint32_t>(m_editor->document()->findBlock(cursor.selectionStart()).blockNumber() + 1);
    } else if (scope == sdk::QueryScope::CurrentStatement) {
        const QString text = m_editor->toPlainText();
        const std::string sql = toStd(text);
        const std::vector<core::sql::SqlStatement> statements = core::sql::SqlSplitter::split(sql, m_editor->syntax());
        const auto offset = static_cast<std::uint64_t>(text.left(cursor.position()).toUtf8().size());
        const core::sql::SqlStatement* statement = core::sql::statementAt(statements, offset);
        if (statement == nullptr) {
            addMessage(tr("Não há sentença no cursor."));
            return;
        }
        request.sql = statement->text;
        request.firstLine = statement->line;
    } else {
        request.sql = toStd(m_editor->toPlainText());
    }
    if (QString::fromStdString(request.sql).trimmed().isEmpty()) {
        addMessage(tr("Nada para executar."));
        return;
    }
    if (!startRun()) {
        return;
    }
    m_fileRun = false;
    QPointer<QueryTab> self(this);
    m_deps.queries->execute(
        m_sessionId, std::move(request),
        [self](const query::StatementResult& result) {
            if (self) {
                self->onStatement(result);
            }
        },
        [self](const services::Outcome<query::QuerySummary>& outcome) {
            if (self) {
                self->onDone(outcome);
            }
        });
}

void QueryTab::runFile(const std::filesystem::path& file) {
    if (!startRun()) {
        return;
    }
    m_fileRun = true;
    addMessage(tr("Executando o arquivo %1").arg(pathToQt(file)));
    QPointer<QueryTab> self(this);
    m_deps.queries->runFile(
        m_sessionId, file, m_stopOnError->isChecked(),
        [self](const query::StatementResult& result) {
            if (self) {
                self->onStatement(result);
            }
        },
        [self](const services::Outcome<query::QuerySummary>& outcome) {
            if (self) {
                self->onDone(outcome);
            }
        });
}

void QueryTab::cancel() {
    if (m_running) {
        m_deps.queries->cancel(m_sessionId);
    }
}

void QueryTab::onStatement(const query::StatementResult& result) {
    if (!result.error.empty()) {
        addMessage(tr("Erro na linha %1: %2").arg(result.line).arg(toQt(result.error)));
        if (!m_errorShown && !m_fileRun) {
            m_errorShown = true;
            const QTextBlock block = m_editor->document()->findBlockByNumber(static_cast<int>(result.line) - 1);
            if (block.isValid()) {
                m_editor->setTextCursor(QTextCursor(block));
                m_editor->ensureCursorVisible();
            }
        }
        return;
    }
    if (m_fileRun) {
        // Um arquivo grande tem milhões de sentenças: só o progresso, de tempos em tempos.
        if (++m_fileStatements % kFileProgressEvery == 0) {
            m_metrics->setText(tr("Executando… %1 sentenças").arg(m_fileStatements));
        }
        return;
    }
    QString text;
    if (result.table) {
        const auto rows = static_cast<int>(result.table->rows.size());
        text = tr("Linha %1: %2 linha(s) devolvida(s)").arg(result.line).arg(rows);
        if (result.table->truncated) {
            text += tr(" (só as primeiras %1)").arg(rows);
        }
        if (m_results->count() <= kMaxResultTabs) {
            auto* view = new QTableView(m_results);
            view->setModel(new ResultTableModel(*result.table, view));
            view->setEditTriggers(QAbstractItemView::NoEditTriggers);
            view->verticalHeader()->setDefaultSectionSize(view->fontMetrics().height() + 4);
            m_results->addTab(view, tr("Resultado %1").arg(result.index + 1));
        }
    } else if (result.rowsAffected >= 0) {
        text = tr("Linha %1: %2 linha(s) afetada(s)").arg(result.line).arg(result.rowsAffected);
    } else {
        text = tr("Linha %1: OK").arg(result.line);
    }
    text += tr(" — servidor %1, rede %2").arg(duration(result.timing.server), duration(result.timing.network));
    if (result.warnings > 0) {
        text += tr(", %1 aviso(s)").arg(result.warnings);
    }
    addMessage(text);
}

void QueryTab::onDone(const services::Outcome<query::QuerySummary>& outcome) {
    m_running = false;
    if (!outcome.ok()) {
        addMessage(tr("Erro: %1").arg(errorText(outcome.error())));
        m_metrics->setText(tr("Falhou."));
        m_results->setCurrentIndex(kMessagesTab);
        Q_EMIT runningChanged();
        return;
    }
    const query::QuerySummary& summary = outcome.value();
    QString text = tr("Sentenças: %1 · Linhas: %2 · Afetadas: %3 · Servidor: %4 · Rede: %5 · Avisos: %6 · Erros: %7")
                       .arg(summary.statements)
                       .arg(summary.rowsReturned)
                       .arg(summary.rowsAffected)
                       .arg(duration(summary.timing.server), duration(summary.timing.network))
                       .arg(summary.warnings)
                       .arg(summary.failed);
    if (summary.cancelled) {
        text += tr(" · cancelada");
        addMessage(tr("Execução cancelada."));
    }
    m_metrics->setText(text);
    m_results->setCurrentIndex(summary.failed == 0 && m_results->count() > 1 ? 1 : kMessagesTab);
    Q_EMIT runningChanged();
}

} // namespace mydb::ui
