// Janela principal com Qt Test na plataforma offscreen: árvore de objetos, filtros, favoritos, comandos, abas e
// log, sobre um SessionService de verdade e uma conexão falsa (sem servidor).

#include "ui/main/MainWindow.hpp"

#include "app/BuiltinModules.hpp"
#include "app/ModuleHost.hpp"
#include "infra/platform/IPlatform.hpp"
#include "infra/settings/MemorySettingsStore.hpp"
#include "support/FakeServer.hpp"
#include "ui/common/Icons.hpp"
#include "ui/common/QtDispatcher.hpp"
#include "ui/dialogs/QueryHistoryDialog.hpp"
#include "ui/dialogs/SnippetsDialog.hpp"
#include "ui/editors/EditorPreferences.hpp"
#include "ui/editors/QueryTab.hpp"
#include "ui/editors/SqlEditor.hpp"
#include "ui/models/ObjectTreeFilter.hpp"
#include "ui/models/ObjectTreeModel.hpp"
#include "ui/panels/LogPanel.hpp"

#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QCompleter>
#include <QDockWidget>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QTabWidget>
#include <QTableView>
#include <QTest>
#include <QTextBlock>
#include <QToolButton>
#include <QTreeView>
#include <QTreeWidget>

#include <algorithm>
#include <memory>

namespace {

using mydb::core::model::DbObject;
using mydb::core::model::ObjectRef;
using mydb::core::model::ObjectType;
using mydb::ui::ObjectTreeModel;

DbObject object(std::string name, ObjectType type, std::int64_t size = 0) {
    DbObject result;
    result.ref = ObjectRef{.database = "app", .schema = {}, .name = std::move(name), .type = type};
    result.sizeBytes = size;
    return result;
}

bool hasLibrary(const mydb::infra::platform::IPlatform& platform, std::string_view needle) {
    return std::ranges::any_of(platform.loadedLibraries(), [needle](const std::filesystem::path& path) {
        std::string name = path.filename().string();
        std::ranges::transform(name, name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return name.find(needle) != std::string::npos;
    });
}

} // namespace

class MainWindowTest : public QObject {
    Q_OBJECT

private:
    struct Fixture {
        std::unique_ptr<mydb::infra::platform::IPlatform> platform = mydb::infra::platform::createPlatform();
        mydb::app::ModuleHost modules{*platform};
        mydb::infra::settings::MemorySettingsStore settings;
        mydb::services::session::FavoritesStore favorites{settings};
        mydb::services::log::LogBuffer log;
        QObject context;
        mydb::tests::FakeServer server;
        mydb::services::query::QueryHistoryStore history{settings};
        mydb::services::query::SnippetStore snippets{settings};
        mydb::services::query::QueryTabsStore queryTabs{settings};
        mydb::services::session::SessionService sessions{modules.events(), mydb::ui::qtDispatcher(&context), log};
        mydb::services::query::QueryService queries{sessions, &history};
        int managerCalls = 0;
        std::unique_ptr<mydb::ui::MainWindow> window;

        Fixture() {
            mydb::app::registerBuiltinModules(modules);
            server.objects["app"] = {
                object("users", ObjectType::Table, 2048), object("orders", ObjectType::Table, 1024),
                object("active_users", ObjectType::View), object("cleanup", ObjectType::Procedure)};
            createWindow();
        }
        Fixture(const Fixture&) = delete;
        Fixture& operator=(const Fixture&) = delete;
        Fixture(Fixture&&) = delete;
        Fixture& operator=(Fixture&&) = delete;
        ~Fixture() { window.reset(); }

        void createWindow() {
            window.reset();
            window = std::make_unique<mydb::ui::MainWindow>(
                mydb::ui::MainWindow::Dependencies{.sessions = &sessions,
                                                   .favorites = &favorites,
                                                   .log = &log,
                                                   .events = &modules.events(),
                                                   .commands = &modules.commands(),
                                                   .queries = &queries,
                                                   .history = &history,
                                                   .snippets = &snippets,
                                                   .queryTabs = &queryTabs,
                                                   .settings = &settings,
                                                   .dispatcher = mydb::ui::qtDispatcher(&context),
                                                   .showSessionManager = [this] { ++managerCalls; },
                                                   .version = QStringLiteral("teste")});
            window->show();
        }

        void open(const std::string& id = "s1") {
            mydb::services::session::Session session;
            session.id = id;
            session.name = "Local " + id;
            session.params.sessionId = id;
            session.params.defaultDatabase = "app";
            QVERIFY(sessions.add(session, std::make_unique<mydb::tests::FakeServerConnection>(server)));
            window->selectSession(id);
        }

        [[nodiscard]] ObjectTreeModel& model() const { return window->treeModel(); }
        [[nodiscard]] QModelIndex viewIndex(const QModelIndex& source) const {
            return window->treeFilter().mapFromSource(source);
        }
        [[nodiscard]] int visibleChildren(const QModelIndex& source) const {
            return window->treeFilter().rowCount(viewIndex(source));
        }
        void expand(const std::string& database) {
            const QModelIndex index = model().databaseIndex("s1", database);
            window->treeView().expand(viewIndex(index));
        }
        [[nodiscard]] mydb::ui::QueryTab& newQueryTab(const QString& sql) {
            trigger("query.new");
            mydb::ui::QueryTab* tab = window->activeQueryTab();
            Q_ASSERT(tab != nullptr);
            tab->editor().setPlainText(sql);
            return *tab;
        }
        [[nodiscard]] std::string lastExecuted() {
            const std::scoped_lock lock(server.mutex);
            return server.executed.empty() ? std::string() : server.executed.back();
        }
        void trigger(std::string_view command) {
            QAction* action = window->commandAction(command);
            QVERIFY(action != nullptr);
            QVERIFY(action->isEnabled());
            action->trigger();
        }
    };

private Q_SLOTS:
    // Primeiro teste: nada desenhou ícones ainda neste processo.
    void svgIconsLoadQtSvgOnlyWhenDrawn() {
        const auto platform = mydb::infra::platform::createPlatform();
        QVERIFY(!hasLibrary(*platform, "qsvg"));
        const auto before = platform->processMemory();
        const QIcon icon = mydb::ui::appIcon("table");
        QVERIFY(!icon.isNull());
        QVERIFY(!hasLibrary(*platform, "qsvg"));
        const QImage image = icon.pixmap(QSize(32, 32)).toImage();
        QVERIFY(!image.isNull());
        QVERIFY(hasLibrary(*platform, "qsvg"));
        const auto after = platform->processMemory();
        qInfo("QtSvg carregado no primeiro ícone: %+lld kB de memória privada, %+lld kB residente",
              static_cast<long long>(after.privateKb) - static_cast<long long>(before.privateKb),
              static_cast<long long>(after.residentKb) - static_cast<long long>(before.residentKb));
        bool drawn = false;
        for (int y = 0; y < image.height() && !drawn; ++y) {
            for (int x = 0; x < image.width() && !drawn; ++x) {
                drawn = qAlpha(image.pixel(x, y)) > 0;
            }
        }
        QVERIFY(drawn);
        QVERIFY(mydb::ui::appIcon("nao-existe").isNull());
    }

    void openedSessionShowsItsDatabases() {
        Fixture f;
        f.open();
        QTRY_COMPARE(f.model().rowCount(f.model().sessionIndex("s1")), 2);
        QCOMPARE(f.model().nameAt(f.model().databaseIndex("s1", "logs")), QStringLiteral("logs"));
        QTRY_VERIFY(f.window->treeView().isExpanded(f.viewIndex(f.model().sessionIndex("s1"))));
        QCOMPARE(f.window->selection().sessionId, std::string("s1"));
    }

    void expandingADatabaseLoadsItsObjects() {
        Fixture f;
        f.open();
        QTRY_COMPARE(f.model().rowCount(f.model().sessionIndex("s1")), 2);
        QCOMPARE(f.server.objectCalls.load(), 0); // nada é carregado antes de expandir
        f.expand("app");
        QTRY_COMPARE(f.model().rowCount(f.model().databaseIndex("s1", "app")), 4);
        const QModelIndex size = f.model().databaseIndex("s1", "app").siblingAtColumn(ObjectTreeModel::kSizeColumn);
        QCOMPARE(f.model().data(size, ObjectTreeModel::kSizeRole).toLongLong(), 3072);
    }

    void filtersHideDatabasesAndObjects() {
        Fixture f;
        f.open();
        QTRY_COMPARE(f.model().rowCount(f.model().sessionIndex("s1")), 2);
        f.expand("app");
        QTRY_COMPARE(f.model().rowCount(f.model().databaseIndex("s1", "app")), 4);

        f.window->findChild<QLineEdit*>(QStringLiteral("databaseFilter"))->setText(QStringLiteral("LOG"));
        QCOMPARE(f.visibleChildren(f.model().sessionIndex("s1")), 1);
        f.window->findChild<QLineEdit*>(QStringLiteral("databaseFilter"))->clear();
        QCOMPARE(f.visibleChildren(f.model().sessionIndex("s1")), 2);

        f.window->findChild<QLineEdit*>(QStringLiteral("objectFilter"))->setText(QStringLiteral("users"));
        QCOMPARE(f.visibleChildren(f.model().databaseIndex("s1", "app")), 2);
    }

    void groupingByTypeCreatesFolders() {
        Fixture f;
        f.open();
        QTRY_COMPARE(f.model().rowCount(f.model().sessionIndex("s1")), 2);
        f.expand("app");
        QTRY_COMPARE(f.model().rowCount(f.model().databaseIndex("s1", "app")), 4);
        f.window->findChild<QToolButton*>(QStringLiteral("groupByType"))->setChecked(true);
        const QModelIndex app = f.model().databaseIndex("s1", "app");
        QCOMPARE(f.model().rowCount(app), 3); // tabelas, views, procedures
        const QModelIndex tables = f.model().index(0, 0, app);
        QCOMPARE(f.model().kindAt(tables), ObjectTreeModel::Kind::Group);
        QCOMPARE(f.model().rowCount(tables), 2);
        QCOMPARE(f.model().data(tables).toString(), QStringLiteral("Tabelas (2)"));
        QCOMPARE(f.server.objectCalls.load(), 1); // reagrupar não consulta o servidor
    }

    void favoriteCommandMarksAndFilters() {
        Fixture f;
        f.open();
        QTRY_COMPARE(f.model().rowCount(f.model().sessionIndex("s1")), 2);
        f.expand("app");
        QTRY_COMPARE(f.model().rowCount(f.model().databaseIndex("s1", "app")), 4);
        const ObjectRef orders{.database = "app", .schema = {}, .name = "orders", .type = ObjectType::Table};
        f.window->treeView().setCurrentIndex(f.viewIndex(f.model().objectIndex("s1", orders)));
        QVERIFY(f.window->selection().object == orders);

        f.trigger("object.favorite");
        QVERIFY(f.favorites.contains("s1", orders));
        QVERIFY(f.window->commandAction("object.favorite")->isChecked());

        f.window->findChild<QToolButton*>(QStringLiteral("favoritesOnly"))->setChecked(true);
        QCOMPARE(f.visibleChildren(f.model().sessionIndex("s1")), 1);
        QCOMPARE(f.visibleChildren(f.model().databaseIndex("s1", "app")), 1);
    }

    void refreshReloadsTheSelectedDatabase() {
        Fixture f;
        f.open();
        QTRY_COMPARE(f.model().rowCount(f.model().sessionIndex("s1")), 2);
        f.expand("app");
        QTRY_COMPARE(f.server.objectCalls.load(), 1);
        f.window->treeView().setCurrentIndex(f.viewIndex(f.model().databaseIndex("s1", "app")));
        // Sem esperar a primeira carga terminar: o "atualizar" no meio dela ainda precisa trazer a versão nova.
        const int before = f.server.objectCalls.load();
        {
            const std::scoped_lock lock(f.server.mutex);
            f.server.objects["app"].push_back(object("audit", ObjectType::Table));
        }
        f.trigger("view.refresh");
        QTRY_COMPARE(f.model().rowCount(f.model().databaseIndex("s1", "app")), 5);
        QVERIFY(f.server.objectCalls.load() > before);
    }

    void selectingAnotherDatabaseSwitchesTheSession() {
        Fixture f;
        f.open();
        QTRY_COMPARE(f.model().rowCount(f.model().sessionIndex("s1")), 2);
        f.window->treeView().setCurrentIndex(f.viewIndex(f.model().databaseIndex("s1", "logs")));
        QTRY_COMPARE(f.sessions.find("s1")->currentDatabase(), std::string("logs"));
        auto* tabs = f.window->findChild<QTabWidget*>(QStringLiteral("mainTabs"));
        QCOMPARE(tabs->currentIndex(), 1);
    }

    void commandsBuildMenusAndToolbar() {
        Fixture f;
        QMenu* file = f.window->menuBar()->findChild<QMenu*>(QStringLiteral("fileMenu"));
        QVERIFY(file != nullptr);
        QVERIFY(file->actions().contains(f.window->commandAction("session.manager")));
        QVERIFY(!f.window->commandAction("session.disconnect")->isEnabled()); // nada selecionado
        f.trigger("session.manager");
        QCOMPARE(f.managerCalls, 1);

        QAction* log = f.window->commandAction("view.log");
        QVERIFY(log->isChecked());
        log->trigger();
        QVERIFY(f.window->findChild<QDockWidget*>(QStringLiteral("logDock"))->isHidden());
        QVERIFY(!log->isChecked());
    }

    void disconnectRemovesTheSession() {
        Fixture f;
        f.open();
        QTRY_COMPARE(f.model().rowCount(f.model().sessionIndex("s1")), 2);
        f.trigger("session.disconnect");
        QVERIFY(f.sessions.find("s1") == nullptr);
        QCOMPARE(f.model().rowCount(), 0);
        QVERIFY(f.server.closed);
    }

    void logPanelShowsTheSqlSent() {
        Fixture f;
        f.open();
        QTRY_VERIFY(f.window->logPanel().text().contains(QStringLiteral("SHOW DATABASES;")));
        QVERIFY(f.window->logPanel().text().contains(QStringLiteral("[Local s1]")));
        f.window->logPanel().setCategoryVisible(mydb::core::LogCategory::Sql, false);
        QVERIFY(!f.window->logPanel().text().contains(QStringLiteral("SHOW DATABASES")));
    }

    void hostTabListsDatabasesWithoutServerAdmin() {
        Fixture f;
        f.open();
        auto* tabs = f.window->findChild<QTabWidget*>(QStringLiteral("mainTabs"));
        QVERIFY(tabs != nullptr);
        QTRY_COMPARE(tabs->currentIndex(), 0);
        QTRY_COMPARE(f.server.summaryCalls.load(), 1);
        auto* hostTabs = tabs->widget(0)->findChild<QTabWidget*>();
        QVERIFY(hostTabs != nullptr);
        QVERIFY(!hostTabs->isTabEnabled(1)); // variáveis: a conexão falsa não tem IServerAdmin
    }

    void queryCommandsComeFromTheRegistry() {
        Fixture f;
        auto* query = f.window->menuBar()->findChild<QMenu*>(QStringLiteral("queryMenu"));
        QVERIFY(query != nullptr);
        QVERIFY(query->actions().contains(f.window->commandAction("query.run")));
        QCOMPARE(f.window->commandAction("query.run")->shortcut(), QKeySequence(Qt::Key_F9));
        QCOMPARE(f.window->commandAction("query.runSelection")->shortcut(), QKeySequence(Qt::CTRL | Qt::Key_F9));
        QCOMPARE(f.window->commandAction("query.runCurrent")->shortcut(), QKeySequence(Qt::SHIFT | Qt::Key_F9));
        QCOMPARE(f.window->commandAction("query.find")->shortcut(), QKeySequence(QKeySequence::Find));
        QVERIFY(!f.window->commandAction("query.run")->isEnabled()); // nenhuma aba de query
        f.trigger("query.new");
        QVERIFY(f.window->hasActiveQuery());
        QVERIFY(f.window->commandAction("query.run")->isEnabled());
        QVERIFY(!f.window->commandAction("query.cancel")->isEnabled());
    }

    void runningShowsResultsMetricsAndTheErrorLine() {
        Fixture f;
        f.server.script = [](std::string_view sql) {
            mydb::tests::FakeResult result;
            if (sql.starts_with("SELECT")) {
                result.columns = {mydb::tests::fakeColumn("id"), mydb::tests::fakeColumn("name")};
                result.rows = {{"1", "ana"}, {"2", std::nullopt}};
            } else if (sql.starts_with("BROKEN")) {
                result.error = "syntax error";
            } else {
                result.rowsAffected = 3;
            }
            return std::vector<mydb::tests::FakeResult>{result};
        };
        f.open();
        mydb::ui::QueryTab& tab =
            f.newQueryTab(QStringLiteral("SELECT id, name FROM users;\nUPDATE users SET x = 1;\nBROKEN;"));
        QCOMPARE(tab.sessionId(), std::string("s1"));
        f.trigger("query.run");
        QTRY_VERIFY(!tab.running());
        QCOMPARE(tab.results().count(), 2); // mensagens + um resultado
        auto* view = qobject_cast<QTableView*>(tab.results().widget(1));
        QVERIFY(view != nullptr);
        QCOMPARE(view->model()->rowCount(), 2);
        QCOMPARE(view->model()->index(1, 1).data().toString(), QStringLiteral("NULL"));
        QVERIFY(tab.messages().contains(QStringLiteral("Linha 2: 3 linha(s) afetada(s)")));
        QVERIFY(tab.messages().contains(QStringLiteral("Erro na linha 3: syntax error")));
        QVERIFY(tab.metrics().contains(QStringLiteral("Erros: 1")));
        QCOMPARE(tab.editor().textCursor().blockNumber(), 2); // o cursor vai para a linha do erro
        QVERIFY(!f.history.search("UPDATE users").empty());
        QVERIFY(f.window->commandAction("query.run")->isEnabled());
    }

    void runsTheSelectionTheCurrentStatementAndExplain() {
        Fixture f;
        f.open();
        mydb::ui::QueryTab& tab = f.newQueryTab(QStringLiteral("SELECT 1;\nSELECT 2;"));
        QTextCursor cursor(tab.editor().document()->findBlockByNumber(1));
        tab.editor().setTextCursor(cursor);
        f.trigger("query.runCurrent");
        QTRY_VERIFY(!tab.running());
        QCOMPARE(f.lastExecuted(), std::string("SELECT 2"));
        f.trigger("query.explain");
        QTRY_VERIFY(!tab.running());
        QVERIFY(f.lastExecuted().starts_with("EXPLAIN"));
        cursor.setPosition(0);
        cursor.setPosition(8, QTextCursor::KeepAnchor);
        tab.editor().setTextCursor(cursor);
        f.trigger("query.runSelection");
        QTRY_VERIFY(!tab.running());
        QCOMPARE(f.lastExecuted(), std::string("SELECT 1"));
    }

    void editorCompletesWithTheSessionMetadata() {
        Fixture f;
        f.open();
        mydb::ui::QueryTab& tab = f.newQueryTab(QStringLiteral("SELECT * FROM users u WHERE u.id = 1 AND us"));
        mydb::ui::SqlEditor& editor = tab.editor();
        QTextCursor cursor = editor.textCursor();
        cursor.movePosition(QTextCursor::End);
        editor.setTextCursor(cursor);
        // O índice é montado no worker da sessão; até ele chegar, o popup não abre.
        QTRY_VERIFY((editor.showCompletion(), editor.completer()->popup()->isVisible()));
        QStringList texts;
        for (int row = 0; row < editor.completer()->completionModel()->rowCount(); ++row) {
            texts << editor.completer()->completionModel()->index(row, 0).data().toString();
        }
        QVERIFY(texts.contains(QStringLiteral("users")));
        QTRY_VERIFY(f.server.tableCalls.load() > 0); // as colunas de "users" são pedidas sob demanda
    }

    void queryTabsAreRestoredOnReopen() {
        Fixture f;
        f.open();
        mydb::ui::QueryTab& tab = f.newQueryTab(QStringLiteral("SELECT 42"));
        tab.setTitle(QStringLiteral("minha"));
        f.window->close();
        f.createWindow();
        const auto tabs = f.window->queryTabs();
        QCOMPARE(tabs.size(), std::size_t{1});
        QCOMPARE(tabs.front()->title(), QStringLiteral("minha"));
        QCOMPARE(tabs.front()->editor().toPlainText(), QStringLiteral("SELECT 42"));
        QCOMPARE(tabs.front()->sessionId(), std::string("s1"));
        f.trigger("query.closeAll");
        QVERIFY(f.window->queryTabs().empty());
        QVERIFY(f.queryTabs.load().empty());
    }

    void formatAndCommentActOnTheActiveEditor() {
        Fixture f;
        mydb::ui::QueryTab& tab = f.newQueryTab(QStringLiteral("select a from t"));
        f.trigger("query.format");
        QVERIFY(tab.editor().toPlainText().contains(QStringLiteral("SELECT")));
        QVERIFY(tab.editor().toPlainText().contains(QStringLiteral("FROM")));
        tab.editor().selectAll();
        f.trigger("query.comment");
        QVERIFY(tab.editor().toPlainText().startsWith(QStringLiteral("-- ")));
    }

    void historySnippetsAndFindDialogsAreDestroyedOnClose() {
        Fixture f;
        f.open();
        mydb::ui::QueryTab& tab = f.newQueryTab(QStringLiteral("SELECT 1"));
        f.trigger("query.run");
        QTRY_VERIFY(!tab.running());
        tab.editor().clear();

        f.trigger("query.history");
        auto* history = f.window->findChild<mydb::ui::QueryHistoryDialog*>();
        QVERIFY(history != nullptr);
        QCOMPARE(history->list().topLevelItemCount(), 1);
        history->list().setCurrentItem(history->list().topLevelItem(0));
        Q_EMIT history->list().itemActivated(history->list().topLevelItem(0), 0);
        QCOMPARE(tab.editor().toPlainText(), QStringLiteral("SELECT 1"));
        history->setKeepDays(7);
        QCOMPARE(mydb::ui::EditorPreferences::load(f.settings).historyKeepDays, 7);
        history->clearHistory();
        QCOMPARE(history->list().topLevelItemCount(), 0);
        const QPointer<QDialog> historyGuard(history);
        history->close();
        QTRY_VERIFY(historyGuard.isNull());

        tab.editor().clear();
        f.trigger("query.snippets");
        auto* snippets = f.window->findChild<mydb::ui::SnippetsDialog*>();
        QVERIFY(snippets != nullptr);
        QVERIFY(snippets->saveCurrent(QStringLiteral("contar"), QStringLiteral("SELECT COUNT(*) FROM t")));
        QCOMPARE(f.snippets.snippets().size(), std::size_t{1});
        snippets->list().setCurrentRow(0);
        Q_EMIT snippets->list().itemActivated(snippets->list().item(0));
        QCOMPARE(tab.editor().toPlainText(), QStringLiteral("SELECT COUNT(*) FROM t"));
        const QPointer<QDialog> snippetsGuard(snippets);
        snippets->close();
        QTRY_VERIFY(snippetsGuard.isNull());

        f.trigger("query.find");
        QVERIFY(f.window->findDialog() != nullptr);
        f.window->findDialog()->close();
        QTRY_VERIFY(f.window->findDialog() == nullptr);
    }
};

QTEST_MAIN(MainWindowTest)
#include "MainWindowTest.moc"
