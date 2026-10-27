#pragma once

#include "core/interfaces/ISettingsStore.hpp"
#include "sdk/EventBus.hpp"
#include "sdk/ICommand.hpp"
#include "services/common/Dispatcher.hpp"
#include "services/log/LogBuffer.hpp"
#include "services/query/QueryDocuments.hpp"
#include "services/query/QueryHistoryStore.hpp"
#include "services/query/QueryService.hpp"
#include "services/session/FavoritesStore.hpp"
#include "services/session/SessionService.hpp"

#include <QDialog>
#include <QMainWindow>
#include <QPointer>

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

class QAction;
class QDockWidget;
class QLabel;
class QLineEdit;
class QMenu;
class QTabWidget;
class QToolButton;
class QTreeView;

namespace mydb::ui {

class DatabasePanel;
class HostPanel;
class LogPanel;
class QueryTab;
class ObjectTreeFilter;
class ObjectTreeModel;

/// Janela principal (CLAUDE.md §5, Fases 4 e 5): árvore de objetos à esquerda, abas à direita (Host, Banco e as
/// abas de query) e log embaixo. Criada só depois da primeira conexão (§1.4). Menus, barra de ferramentas e menu
/// de contexto da árvore vêm do CommandRegistry; a janela é o IWorkbench (e o IQueryWorkbench) que os comandos
/// enxergam.
class MainWindow final : public QMainWindow, public sdk::IWorkbench, public sdk::IQueryWorkbench {
    Q_OBJECT

public:
    /// Todos os ponteiros são obrigatórios e precisam viver mais que a janela.
    struct Dependencies {
        services::session::SessionService* sessions{nullptr};
        services::session::FavoritesStore* favorites{nullptr};
        services::log::LogBuffer* log{nullptr};
        sdk::EventBus* events{nullptr};
        const sdk::CommandRegistry* commands{nullptr};
        services::query::QueryService* queries{nullptr};
        services::query::QueryHistoryStore* history{nullptr};
        services::query::SnippetStore* snippets{nullptr};
        services::query::QueryTabsStore* queryTabs{nullptr};
        core::ISettingsStore* settings{nullptr};
        services::Dispatcher dispatcher;
        std::function<void()> showSessionManager;
        QString version;
    };

    explicit MainWindow(Dependencies dependencies, QWidget* parent = nullptr);
    MainWindow(const MainWindow&) = delete;
    MainWindow& operator=(const MainWindow&) = delete;
    MainWindow(MainWindow&&) = delete;
    MainWindow& operator=(MainWindow&&) = delete;
    ~MainWindow() override;

    /// Seleciona a sessão na árvore (ela já precisa estar aberta no SessionService).
    void selectSession(const std::string& sessionId);

    [[nodiscard]] sdk::Selection selection() const override;
    void showSessionManager() override;
    void closeSession(std::string_view sessionId) override;
    void refresh(const sdk::Selection& scope) override;
    [[nodiscard]] bool isFavorite(const sdk::Selection& scope) const override;
    void setFavorite(const sdk::Selection& scope, bool favorite) override;
    [[nodiscard]] bool panelVisible(sdk::Panel panel) const override;
    void setPanelVisible(sdk::Panel panel, bool visible) override;
    [[nodiscard]] sdk::IQueryWorkbench& queries() override { return *this; }
    [[nodiscard]] const sdk::IQueryWorkbench& queries() const override { return *this; }
    void showAbout() override;
    void quit() override;

    // sdk::IQueryWorkbench
    void newQueryTab() override;
    [[nodiscard]] bool hasActiveQuery() const override;
    [[nodiscard]] bool queryRunning() const override;
    void runQuery(sdk::QueryScope scope, bool explain) override;
    void cancelQuery() override;
    void openSqlFile() override;
    void saveSqlFile(bool saveAs) override;
    void runSqlFile() override;
    void formatQuery() override;
    void toggleComment() override;
    void showFindReplace() override;
    void showQueryHistory() override;
    void showSnippets() override;
    void closeAllQueryTabs() override;

    /// Abre um arquivo .sql numa aba nova (a leitura é feita fora da thread da UI).
    void openSqlFileAt(const QString& file);
    /// Grava a aba em `file` (fora da thread da UI).
    void saveQueryTabTo(QueryTab* tab, const QString& file);

    // Para os testes de UI.
    [[nodiscard]] ObjectTreeModel& treeModel() const noexcept { return *m_model; }
    [[nodiscard]] ObjectTreeFilter& treeFilter() const noexcept { return *m_filter; }
    [[nodiscard]] QTreeView& treeView() const noexcept { return *m_tree; }
    [[nodiscard]] QAction* commandAction(std::string_view commandId) const;
    [[nodiscard]] LogPanel& logPanel() const noexcept { return *m_logPanel; }
    [[nodiscard]] QueryTab* activeQueryTab() const;
    [[nodiscard]] std::vector<QueryTab*> queryTabs() const;
    [[nodiscard]] QTabWidget& tabs() const noexcept { return *m_tabs; }
    [[nodiscard]] QDialog* findDialog() const noexcept { return m_findDialog; }

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    struct CommandEntry {
        std::string id;
        std::unique_ptr<sdk::ICommand> command;
        sdk::CommandInfo info;
        QAction* action{nullptr};
    };

    void buildLayout();
    void buildCommands();
    void updateCommandStates();
    void subscribe();
    void showContextMenu(const QPoint& position);
    void onCurrentChanged();
    // Por valor: os lambdas copiam os textos e precisam ser movíveis sem lançar.
    void loadDatabases(std::string sessionId);
    void loadObjects(std::string sessionId, std::string database);
    void onSessionOpened(const std::string& sessionId);
    void onSessionClosed(const std::string& sessionId);
    void onObjectsChanged(const std::string& sessionId, const std::string& database);
    void selectDatabase(const std::string& sessionId, const std::string& database);
    void selectObject(const std::string& sessionId, const core::model::ObjectRef& object);
    [[nodiscard]] QString sessionName(const std::string& sessionId) const;
    QueryTab* addQueryTab(const services::query::QueryTabState& state);
    void closeQueryTab(QueryTab* tab);
    void renameQueryTab(int index);
    void updateQueryTabTitle(QueryTab* tab);
    /// A aba usa a sessão selecionada na árvore se a dela não estiver aberta.
    void ensureSession(QueryTab& tab) const;
    void restoreQueryTabs();
    void saveQueryTabs() const;
    void insertIntoEditor(const QString& sql);
    [[nodiscard]] bool confirmDiscard(const std::vector<QueryTab*>& tabs);

    Dependencies m_deps;
    std::vector<CommandEntry> m_commands;
    std::vector<sdk::EventBus::Subscription> m_subscriptions;

    ObjectTreeModel* m_model;
    ObjectTreeFilter* m_filter;
    QTreeView* m_tree{nullptr};
    QLineEdit* m_databaseFilter{nullptr};
    QLineEdit* m_objectFilter{nullptr};
    QToolButton* m_favoritesOnly{nullptr};
    QToolButton* m_groupByType{nullptr};
    QTabWidget* m_tabs{nullptr};
    HostPanel* m_hostPanel{nullptr};
    DatabasePanel* m_databasePanel{nullptr};
    QDockWidget* m_logDock{nullptr};
    LogPanel* m_logPanel{nullptr};
    QLabel* m_statusServer{nullptr};
    int m_queryCounter{0};
    bool m_snippetsLoaded{false};
    QPointer<QDialog> m_findDialog;
    QPointer<QDialog> m_historyDialog;
    QPointer<QDialog> m_snippetsDialog;
};

} // namespace mydb::ui
