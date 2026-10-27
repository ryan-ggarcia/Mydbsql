#pragma once

#include "core/model/ObjectRef.hpp"
#include "sdk/IQueryWorkbench.hpp"
#include "services/common/Outcome.hpp"
#include "services/query/QueryDocuments.hpp"
#include "services/query/QueryTypes.hpp"

#include <QWidget>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <set>
#include <string>
#include <vector>

class QCheckBox;
class QComboBox;
class QLabel;
class QPlainTextEdit;
class QTabWidget;

namespace mydb::services::query {
class CompletionIndex;
class QueryService;
} // namespace mydb::services::query

namespace mydb::services::session {
class SessionService;
} // namespace mydb::services::session

namespace mydb::ui {

class SqlEditor;

/// Uma aba de query (Fase 5): editor, abas de resultado (uma por sentença que devolve linhas), mensagens e
/// métricas. Executa pelo QueryService; o realce e o autocomplete vêm do dialeto e dos metadados da sessão,
/// montados no worker dela.
class QueryTab final : public QWidget {
    Q_OBJECT

public:
    /// Os dois ponteiros são obrigatórios e precisam viver mais que a aba.
    struct Dependencies {
        services::session::SessionService* sessions{nullptr};
        services::query::QueryService* queries{nullptr};
    };

    explicit QueryTab(Dependencies dependencies, QWidget* parent = nullptr);
    QueryTab(const QueryTab&) = delete;
    QueryTab& operator=(const QueryTab&) = delete;
    QueryTab(QueryTab&&) = delete;
    QueryTab& operator=(QueryTab&&) = delete;
    ~QueryTab() override;

    [[nodiscard]] SqlEditor& editor() const noexcept { return *m_editor; }

    [[nodiscard]] const std::string& sessionId() const noexcept { return m_sessionId; }
    /// Troca a sessão e refaz o realce e o autocomplete.
    void setSessionId(std::string sessionId);
    /// Remonta o índice do autocomplete (os bancos ou objetos da sessão mudaram).
    void refreshCompletion();

    [[nodiscard]] const QString& title() const noexcept { return m_title; }
    void setTitle(const QString& title);
    /// O título com "*" se houver alterações não salvas.
    [[nodiscard]] QString displayTitle() const;
    [[nodiscard]] const QString& filePath() const noexcept { return m_file; }
    void setFilePath(const QString& file);

    [[nodiscard]] services::query::QueryTabState state() const;

    void run(sdk::QueryScope scope, bool explain);
    void runFile(const std::filesystem::path& file);
    void cancel();
    [[nodiscard]] bool running() const noexcept { return m_running; }

    void setExecutionMode(services::query::ExecutionMode mode);
    void setStopOnError(bool stop);

    // Para os testes de UI.
    [[nodiscard]] QTabWidget& results() const noexcept { return *m_results; }
    [[nodiscard]] QString messages() const;
    [[nodiscard]] QString metrics() const;

Q_SIGNALS:
    void runningChanged();
    void titleChanged();

private:
    [[nodiscard]] bool startRun();
    void onStatement(const services::query::StatementResult& result);
    void onDone(const services::Outcome<services::query::QuerySummary>& outcome);
    void addMessage(const QString& text);
    void requestColumns(std::vector<core::model::ObjectRef> tables);
    void updateSessionLabel();

    Dependencies m_deps;
    std::string m_sessionId;
    QString m_title;
    QString m_file;
    bool m_running{false};
    bool m_fileRun{false};
    bool m_errorShown{false};
    std::size_t m_fileStatements{0};
    std::uint64_t m_generation{0}; ///< descarta índices de autocomplete pedidos antes da última troca
    std::shared_ptr<const services::query::CompletionIndex> m_index;
    std::set<std::string> m_requestedTables;

    SqlEditor* m_editor;
    QLabel* m_session;
    QComboBox* m_mode;
    QCheckBox* m_stopOnError;
    QTabWidget* m_results;
    QPlainTextEdit* m_messages;
    QLabel* m_metrics;
};

} // namespace mydb::ui
