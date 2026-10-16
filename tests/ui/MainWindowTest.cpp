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
#include "ui/models/ObjectTreeFilter.hpp"
#include "ui/models/ObjectTreeModel.hpp"
#include "ui/panels/LogPanel.hpp"

#include <QAction>
#include <QApplication>
#include <QDockWidget>
#include <QLineEdit>
#include <QMenuBar>
#include <QTabWidget>
#include <QTest>
#include <QToolButton>
#include <QTreeView>

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
        mydb::services::session::SessionService sessions{modules.events(), mydb::ui::qtDispatcher(&context), log};
        int managerCalls = 0;
        std::unique_ptr<mydb::ui::MainWindow> window;

        Fixture() {
            mydb::app::registerBuiltinModules(modules);
            server.objects["app"] = {
                object("users", ObjectType::Table, 2048), object("orders", ObjectType::Table, 1024),
                object("active_users", ObjectType::View), object("cleanup", ObjectType::Procedure)};
            window = std::make_unique<mydb::ui::MainWindow>(
                mydb::ui::MainWindow::Dependencies{.sessions = &sessions,
                                                   .favorites = &favorites,
                                                   .log = &log,
                                                   .events = &modules.events(),
                                                   .commands = &modules.commands(),
                                                   .dispatcher = mydb::ui::qtDispatcher(&context),
                                                   .showSessionManager = [this] { ++managerCalls; },
                                                   .version = QStringLiteral("teste")});
            window->show();
        }
        Fixture(const Fixture&) = delete;
        Fixture& operator=(const Fixture&) = delete;
        Fixture(Fixture&&) = delete;
        Fixture& operator=(Fixture&&) = delete;
        ~Fixture() { window.reset(); }

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
};

QTEST_MAIN(MainWindowTest)
#include "MainWindowTest.moc"
