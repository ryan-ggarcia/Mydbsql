#include "ui/main/MainWindow.hpp"

#include "core/sql/SqlFormatter.hpp"
#include "sdk/SessionEvents.hpp"
#include "ui/common/ErrorText.hpp"
#include "ui/common/Icons.hpp"
#include "ui/common/QtText.hpp"
#include "ui/dialogs/FindReplaceDialog.hpp"
#include "ui/dialogs/QueryHistoryDialog.hpp"
#include "ui/dialogs/SnippetsDialog.hpp"
#include "ui/editors/EditorPreferences.hpp"
#include "ui/editors/QueryTab.hpp"
#include "ui/editors/SqlEditor.hpp"
#include "ui/models/ObjectTreeFilter.hpp"
#include "ui/models/ObjectTreeModel.hpp"
#include "ui/panels/DatabasePanel.hpp"
#include "ui/panels/HostPanel.hpp"
#include "ui/panels/LogPanel.hpp"

#include <QAction>
#include <QCloseEvent>
#include <QColor>
#include <QDockWidget>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPointer>
#include <QSaveFile>
#include <QSplitter>
#include <QStatusBar>
#include <QTabBar>
#include <QTabWidget>
#include <QThreadPool>
#include <QToolBar>
#include <QToolButton>
#include <QTreeView>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <map>
#include <tuple>
#include <utility>

namespace mydb::ui {

namespace {

constexpr int kInitialWidth = 1180;
constexpr int kInitialHeight = 760;
constexpr int kTreeWidth = 300;
constexpr int kHostTab = 0;
constexpr int kDatabaseTab = 1;

constexpr std::array<std::string_view, 5> kMenuOrder{sdk::menus::kFile, sdk::menus::kEdit, sdk::menus::kView,
                                                     sdk::menus::kTools, sdk::menus::kHelp};

QString menuTitle(std::string_view id) {
    if (id == sdk::menus::kFile) {
        return MainWindow::tr("&Arquivo");
    }
    if (id == sdk::menus::kEdit) {
        return MainWindow::tr("&Editar");
    }
    if (id == sdk::menus::kView) {
        return MainWindow::tr("E&xibir");
    }
    if (id == sdk::menus::kTools) {
        return MainWindow::tr("&Ferramentas");
    }
    if (id == sdk::menus::kHelp) {
        return MainWindow::tr("Aj&uda");
    }
    if (id == "query") {
        return MainWindow::tr("&Query");
    }
    return toQt(id);
}

/// Posição do menu na barra: os conhecidos na ordem de kMenuOrder, os dos módulos antes de "Ajuda".
int menuRank(std::string_view id) {
    for (std::size_t rank = 0; rank < kMenuOrder.size(); ++rank) {
        if (kMenuOrder.at(rank) == id) {
            return id == sdk::menus::kHelp ? static_cast<int>(rank) + 1 : static_cast<int>(rank);
        }
    }
    return static_cast<int>(kMenuOrder.size()) - 1;
}

QKeySequence shortcutFor(const sdk::CommandInfo& info) {
    using sdk::StandardShortcut;
    switch (info.standardShortcut) {
    case StandardShortcut::None:
        break;
    case StandardShortcut::New:
        return {QKeySequence::New};
    case StandardShortcut::Open:
        return {QKeySequence::Open};
    case StandardShortcut::Save:
        return {QKeySequence::Save};
    case StandardShortcut::Close:
        return {QKeySequence::Close};
    case StandardShortcut::Quit:
        return {QKeySequence::Quit};
    case StandardShortcut::Refresh:
        return {QKeySequence::Refresh};
    case StandardShortcut::Find:
        return {QKeySequence::Find};
    case StandardShortcut::Preferences:
        return {QKeySequence::Preferences};
    }
    return QKeySequence::fromString(toQt(info.shortcut), QKeySequence::PortableText);
}

QAction::MenuRole menuRoleFor(const sdk::CommandInfo& info) {
    // No macOS, Sair, Sobre e Preferências vão para o menu do app (§2.6, regra 7).
    switch (info.standardShortcut) {
    case sdk::StandardShortcut::Quit:
        return QAction::QuitRole;
    case sdk::StandardShortcut::Preferences:
        return QAction::PreferencesRole;
    default:
        return info.menu == sdk::menus::kHelp ? QAction::AboutRole : QAction::NoRole;
    }
}

constexpr qint64 kMaxEditorFileBytes = qint64{100} * 1024 * 1024;
QString sqlFilter() {
    return MainWindow::tr("Arquivos SQL (*.sql);;Todos os arquivos (*)");
}

/// Roda `work` no pool de threads do Qt e entrega a continuação que ele devolve na thread da UI: leitura e
/// gravação de arquivos não rodam na thread da UI (§2.3).
void inBackground(const services::Dispatcher& ui, std::function<std::function<void()>()> work) {
    QThreadPool::globalInstance()->start([ui, task = std::move(work)] { ui(task()); });
}

} // namespace

MainWindow::MainWindow(Dependencies dependencies, QWidget* parent)
    : QMainWindow(parent),
      m_deps(std::move(dependencies)),
      m_model(new ObjectTreeModel(this)),
      m_filter(new ObjectTreeFilter(this)) {
    setWindowTitle(QStringLiteral("Mydbsql"));
    resize(kInitialWidth, kInitialHeight);

    m_model->setFavoriteCheck(
        [favorites = m_deps.favorites](const std::string& sessionId, const core::model::ObjectRef& object) {
            return favorites->contains(sessionId, object);
        });
    m_filter->setSourceModel(m_model);
    m_filter->setDatabaseFavorites(
        [favorites = m_deps.favorites](const std::string& sessionId, const std::string& database) {
            const auto list = favorites->list(sessionId);
            return std::ranges::any_of(list, [&](const auto& object) { return object.database == database; });
        });

    buildLayout();
    buildCommands();
    subscribe();
    for (const auto* session : m_deps.sessions->sessions()) {
        onSessionOpened(session->id());
    }
    m_deps.history->setKeepDays(EditorPreferences::load(*m_deps.settings).historyKeepDays);
    try {
        m_deps.history->load();
    } catch (const std::exception& error) {
        m_deps.log->append(core::LogCategory::Error, {},
                           std::string("Histórico de queries não carregado: ") + error.what() +
                               ". O arquivo não será alterado até o histórico ser limpo.");
    }
    restoreQueryTabs();
    updateCommandStates();
}

MainWindow::~MainWindow() = default;

void MainWindow::buildLayout() {
    // Árvore com os filtros em cima.
    auto* treePane = new QWidget(this);
    auto* treeLayout = new QVBoxLayout(treePane);
    treeLayout->setContentsMargins(2, 2, 2, 2);
    treeLayout->setSpacing(2);
    m_databaseFilter = new QLineEdit(treePane);
    m_databaseFilter->setObjectName(QStringLiteral("databaseFilter"));
    m_databaseFilter->setPlaceholderText(tr("Filtrar bancos"));
    m_databaseFilter->setClearButtonEnabled(true);
    m_objectFilter = new QLineEdit(treePane);
    m_objectFilter->setObjectName(QStringLiteral("objectFilter"));
    m_objectFilter->setPlaceholderText(tr("Filtrar objetos"));
    m_objectFilter->setClearButtonEnabled(true);
    m_favoritesOnly = new QToolButton(treePane);
    m_favoritesOnly->setObjectName(QStringLiteral("favoritesOnly"));
    m_favoritesOnly->setIcon(appIcon("favorite"));
    m_favoritesOnly->setCheckable(true);
    m_favoritesOnly->setToolTip(tr("Mostrar só os favoritos"));
    m_groupByType = new QToolButton(treePane);
    m_groupByType->setObjectName(QStringLiteral("groupByType"));
    m_groupByType->setIcon(appIcon("folder"));
    m_groupByType->setCheckable(true);
    m_groupByType->setToolTip(tr("Agrupar os objetos por tipo"));
    auto* filterRow = new QHBoxLayout();
    filterRow->setSpacing(2);
    filterRow->addWidget(m_databaseFilter, 1);
    filterRow->addWidget(m_favoritesOnly);
    filterRow->addWidget(m_groupByType);
    treeLayout->addLayout(filterRow);
    treeLayout->addWidget(m_objectFilter);

    m_tree = new QTreeView(treePane);
    m_tree->setObjectName(QStringLiteral("objectTree"));
    m_tree->setModel(m_filter);
    m_tree->setUniformRowHeights(true);
    m_tree->setHeaderHidden(false);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_tree->header()->setStretchLastSection(false);
    m_tree->header()->setSectionResizeMode(ObjectTreeModel::kNameColumn, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(ObjectTreeModel::kSizeColumn, QHeaderView::ResizeToContents);
    treeLayout->addWidget(m_tree, 1);

    connect(m_databaseFilter, &QLineEdit::textChanged, m_filter, &ObjectTreeFilter::setDatabaseFilter);
    connect(m_objectFilter, &QLineEdit::textChanged, this, [this](const QString& text) {
        m_filter->setObjectFilter(text);
        if (!text.isEmpty()) {
            m_tree->expandToDepth(1); // mostra os objetos que casaram dentro dos bancos já carregados
        }
    });
    connect(m_favoritesOnly, &QToolButton::toggled, m_filter, &ObjectTreeFilter::setFavoritesOnly);
    connect(m_groupByType, &QToolButton::toggled, m_model, &ObjectTreeModel::setGroupByType);
    connect(m_tree, &QTreeView::customContextMenuRequested, this, &MainWindow::showContextMenu);
    connect(m_tree->selectionModel(), &QItemSelectionModel::currentChanged, this, &MainWindow::onCurrentChanged);
    connect(m_model, &ObjectTreeModel::objectsRequested, this,
            [this](const QString& sessionId, const QString& db) { loadObjects(toStd(sessionId), toStd(db)); });

    // Abas à direita.
    m_tabs = new QTabWidget(this);
    m_tabs->setObjectName(QStringLiteral("mainTabs"));
    m_hostPanel = new HostPanel(*m_deps.sessions, m_tabs);
    m_databasePanel = new DatabasePanel(*m_deps.sessions, m_tabs);
    m_tabs->addTab(m_hostPanel, appIcon("server"), tr("Host"));
    m_tabs->addTab(m_databasePanel, appIcon("database"), tr("Banco"));
    connect(m_hostPanel, &HostPanel::databaseActivated, this,
            [this](const QString& database) { selectDatabase(m_hostPanel->sessionId(), toStd(database)); });
    connect(m_databasePanel, &DatabasePanel::objectActivated, this,
            [this](const core::model::ObjectRef& object) { selectObject(m_databasePanel->sessionId(), object); });
    m_tabs->setTabsClosable(true);
    for (const int fixed : {kHostTab, kDatabaseTab}) {
        m_tabs->tabBar()->setTabButton(fixed, QTabBar::RightSide, nullptr);
        m_tabs->tabBar()->setTabButton(fixed, QTabBar::LeftSide, nullptr);
    }
    connect(m_tabs, &QTabWidget::currentChanged, this, [this](int tab) {
        if (tab == kHostTab) {
            m_hostPanel->refresh();
        }
        updateCommandStates();
    });
    connect(m_tabs, &QTabWidget::tabCloseRequested, this, [this](int index) {
        if (auto* tab = qobject_cast<QueryTab*>(m_tabs->widget(index))) {
            if (confirmDiscard({tab})) {
                closeQueryTab(tab);
                saveQueryTabs();
            }
        }
    });
    connect(m_tabs, &QTabWidget::tabBarDoubleClicked, this, &MainWindow::renameQueryTab);

    auto* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->addWidget(treePane);
    splitter->addWidget(m_tabs);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({kTreeWidth, kInitialWidth - kTreeWidth});
    setCentralWidget(splitter);

    // Log embaixo.
    m_logPanel = new LogPanel(*m_deps.log, m_deps.dispatcher,
                              [this](const std::string& sessionId) { return sessionName(sessionId); });
    m_logDock = new QDockWidget(tr("Log"), this);
    m_logDock->setObjectName(QStringLiteral("logDock"));
    m_logDock->setWidget(m_logPanel);
    m_logDock->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable |
                           QDockWidget::DockWidgetFloatable);
    addDockWidget(Qt::BottomDockWidgetArea, m_logDock);
    connect(m_logDock, &QDockWidget::visibilityChanged, this, [this] { updateCommandStates(); });

    m_statusServer = new QLabel(this);
    statusBar()->addPermanentWidget(m_statusServer);
}

void MainWindow::buildCommands() {
    for (const auto& entry : m_deps.commands->entries()) {
        CommandEntry command;
        command.id = entry.id;
        command.command = m_deps.commands->create(entry.id);
        command.info = command.command->info();
        auto* action = new QAction(toQt(command.info.text), this);
        action->setObjectName(toQt(entry.id));
        action->setToolTip(toQt(command.info.description));
        action->setStatusTip(toQt(command.info.description));
        action->setShortcut(shortcutFor(command.info));
        action->setMenuRole(menuRoleFor(command.info));
        action->setCheckable(command.info.checkable);
        if (!command.info.icon.empty()) {
            action->setIcon(appIcon(command.info.icon));
        }
        sdk::ICommand* target = command.command.get();
        connect(action, &QAction::triggered, this, [this, target] {
            try {
                target->execute(*this);
            } catch (const std::exception& error) {
                QMessageBox::warning(this, windowTitle(), errorText(error));
            }
            updateCommandStates();
        });
        command.action = action;
        addAction(action); // o atalho vale mesmo se o comando não estiver em nenhum menu
        m_commands.push_back(std::move(command));
    }

    // Menus na ordem conhecida; dentro de cada um, por grupo e ordem, com separadores entre grupos.
    std::vector<const CommandEntry*> sorted;
    sorted.reserve(m_commands.size());
    for (const auto& command : m_commands) {
        sorted.push_back(&command);
    }
    std::ranges::stable_sort(sorted, [](const CommandEntry* a, const CommandEntry* b) {
        return std::tuple(menuRank(a->info.menu), a->info.menu, a->info.group, a->info.order) <
               std::tuple(menuRank(b->info.menu), b->info.menu, b->info.group, b->info.order);
    });
    std::map<std::string, QMenu*> menus;
    std::map<std::string, std::string> lastGroup;
    QToolBar* toolbar = addToolBar(tr("Principal"));
    toolbar->setObjectName(QStringLiteral("mainToolbar"));
    toolbar->setMovable(false);
    std::string lastToolbarMenu;
    for (const CommandEntry* command : sorted) {
        const sdk::CommandInfo& info = command->info;
        if (!info.menu.empty()) {
            QMenu*& menu = menus[info.menu];
            if (menu == nullptr) {
                menu = menuBar()->addMenu(menuTitle(info.menu));
                menu->setObjectName(toQt(info.menu) + QStringLiteral("Menu"));
                connect(menu, &QMenu::aboutToShow, this, [this] { updateCommandStates(); });
            } else if (lastGroup[info.menu] != info.group) {
                menu->addSeparator();
            }
            lastGroup[info.menu] = info.group;
            menu->addAction(command->action);
        }
        if (info.toolbar) {
            if (!lastToolbarMenu.empty() && lastToolbarMenu != info.menu) {
                toolbar->addSeparator();
            }
            lastToolbarMenu = info.menu;
            toolbar->addAction(command->action);
        }
    }
}

void MainWindow::updateCommandStates() {
    for (const auto& command : m_commands) {
        command.action->setEnabled(command.command->isEnabled(*this));
        if (command.info.checkable) {
            const QSignalBlocker blocker(command.action);
            command.action->setChecked(command.command->isChecked(*this));
        }
    }
}

QAction* MainWindow::commandAction(std::string_view commandId) const {
    const auto found = std::ranges::find(m_commands, commandId, &CommandEntry::id);
    return found == m_commands.end() ? nullptr : found->action;
}

void MainWindow::subscribe() {
    sdk::EventBus& events = *m_deps.events;
    m_subscriptions.push_back(
        events.subscribe<sdk::SessionOpened>([this](const auto& event) { onSessionOpened(event.sessionId); }));
    m_subscriptions.push_back(
        events.subscribe<sdk::SessionClosed>([this](const auto& event) { onSessionClosed(event.sessionId); }));
    m_subscriptions.push_back(events.subscribe<sdk::ObjectsChanged>([this](const auto& event) {
        onObjectsChanged(event.sessionId, event.database);
        for (QueryTab* tab : queryTabs()) {
            if (tab->sessionId() == event.sessionId) {
                tab->refreshCompletion();
            }
        }
    }));
    m_subscriptions.push_back(events.subscribe<sdk::DatabaseChanged>([this](const auto& event) {
        statusBar()->showMessage(tr("%1: banco atual %2").arg(sessionName(event.sessionId), toQt(event.database)));
        for (QueryTab* tab : queryTabs()) {
            if (tab->sessionId() == event.sessionId) {
                tab->refreshCompletion();
            }
        }
    }));
    m_subscriptions.push_back(events.subscribe<sdk::ConnectionStateChanged>([this](const auto& event) {
        statusBar()->showMessage(event.alive ? tr("%1: conexão restabelecida").arg(sessionName(event.sessionId))
                                             : tr("%1: conexão perdida").arg(sessionName(event.sessionId)));
    }));
}

void MainWindow::onSessionOpened(const std::string& sessionId) {
    const auto* session = m_deps.sessions->find(sessionId);
    if (session == nullptr || m_model->sessionIndex(sessionId).isValid()) {
        return;
    }
    const QColor color = QColor::fromString(toQt(session->session().color));
    m_model->addSession(sessionId, toQt(session->session().name), color);
    loadDatabases(sessionId);
    for (QueryTab* tab : queryTabs()) {
        if (tab->sessionId() == sessionId) {
            tab->setSessionId(sessionId); // aba restaurada: a sessão dela abriu agora
        }
    }
    updateCommandStates();
}

void MainWindow::onSessionClosed(const std::string& sessionId) {
    m_model->removeSession(sessionId);
    if (m_hostPanel->sessionId() == sessionId) {
        m_hostPanel->setSession({});
    }
    if (m_databasePanel->sessionId() == sessionId) {
        m_databasePanel->setDatabase({}, {});
    }
    onCurrentChanged();
}

void MainWindow::onObjectsChanged(const std::string& sessionId, const std::string& database) {
    if (database.empty()) {
        loadDatabases(sessionId);
        // Bancos já carregados recarregam os objetos (os outros carregam quando forem expandidos).
        const QModelIndex sessionIndex = m_model->sessionIndex(sessionId);
        for (int row = 0; row < m_model->rowCount(sessionIndex); ++row) {
            const std::string name = toStd(m_model->nameAt(m_model->index(row, 0, sessionIndex)));
            m_model->invalidateDatabase(sessionId, name);
        }
    } else {
        m_model->invalidateDatabase(sessionId, database);
    }
    if (m_hostPanel->sessionId() == sessionId) {
        m_hostPanel->setSession({});
        m_hostPanel->setSession(sessionId);
    }
    if (m_databasePanel->sessionId() == sessionId && (database.empty() || m_databasePanel->database() == database)) {
        m_databasePanel->refresh();
    }
}

void MainWindow::loadDatabases(std::string sessionId) {
    QPointer<MainWindow> self(this);
    m_deps.sessions->databases(sessionId, [self, sessionId](const auto& outcome) {
        if (!self) {
            return;
        }
        if (!outcome.ok()) {
            self->statusBar()->showMessage(errorText(outcome.error()));
            return;
        }
        const bool first = self->m_model->rowCount(self->m_model->sessionIndex(sessionId)) == 0;
        self->m_model->setDatabases(sessionId, outcome.value());
        if (first) {
            self->m_tree->expand(self->m_filter->mapFromSource(self->m_model->sessionIndex(sessionId)));
        }
    });
}

void MainWindow::loadObjects(std::string sessionId, std::string database) {
    QPointer<MainWindow> self(this);
    m_deps.sessions->objects(sessionId, database, [self, sessionId, database](auto outcome) {
        if (!self) {
            return;
        }
        if (!outcome.ok()) {
            self->m_model->setLoadFailed(sessionId, database);
            self->statusBar()->showMessage(errorText(outcome.error()));
            return;
        }
        self->m_model->setObjects(sessionId, database, std::move(outcome).value());
    });
}

void MainWindow::onCurrentChanged() {
    const sdk::Selection current = selection();
    updateCommandStates();
    if (current.empty()) {
        m_statusServer->clear();
        setWindowTitle(QStringLiteral("Mydbsql"));
        return;
    }
    const auto* session = m_deps.sessions->find(current.sessionId);
    if (session == nullptr) {
        return;
    }
    const QString server =
        toQt(session->server().productName) + QLatin1Char(' ') + toQt(session->server().versionString);
    m_statusServer->setText(server);
    setWindowTitle(tr("%1 — %2 — Mydbsql").arg(sessionName(current.sessionId), server));

    m_hostPanel->setSession(current.sessionId);
    if (current.database.empty()) {
        m_tabs->setCurrentIndex(kHostTab);
        return;
    }
    m_databasePanel->setDatabase(current.sessionId, current.database);
    m_tabs->setCurrentIndex(kDatabaseTab);
    if (session->currentDatabase() != current.database) {
        m_deps.sessions->useDatabase(current.sessionId, current.database, {});
    }
}

void MainWindow::showContextMenu(const QPoint& position) {
    updateCommandStates();
    std::vector<const CommandEntry*> items;
    for (const auto& command : m_commands) {
        if (command.info.objectContextMenu) {
            items.push_back(&command);
        }
    }
    std::ranges::stable_sort(items, [](const CommandEntry* a, const CommandEntry* b) {
        return std::tie(a->info.group, a->info.order) < std::tie(b->info.group, b->info.order);
    });
    QMenu menu(this);
    std::string lastGroup;
    for (const CommandEntry* item : items) {
        if (!menu.isEmpty() && item->info.group != lastGroup) {
            menu.addSeparator();
        }
        lastGroup = item->info.group;
        menu.addAction(item->action);
    }
    if (!menu.isEmpty()) {
        menu.exec(m_tree->viewport()->mapToGlobal(position));
    }
}

void MainWindow::selectSession(const std::string& sessionId) {
    const QModelIndex index = m_filter->mapFromSource(m_model->sessionIndex(sessionId));
    if (index.isValid()) {
        m_tree->setCurrentIndex(index);
    }
}

void MainWindow::selectDatabase(const std::string& sessionId, const std::string& database) {
    const QModelIndex index = m_filter->mapFromSource(m_model->databaseIndex(sessionId, database));
    if (index.isValid()) {
        m_tree->setCurrentIndex(index);
        m_tree->scrollTo(index);
    }
}

void MainWindow::selectObject(const std::string& sessionId, const core::model::ObjectRef& object) {
    const QModelIndex database = m_filter->mapFromSource(m_model->databaseIndex(sessionId, object.database));
    if (database.isValid()) {
        m_tree->expand(database);
    }
    const QModelIndex index = m_filter->mapFromSource(m_model->objectIndex(sessionId, object));
    if (index.isValid()) {
        m_tree->setCurrentIndex(index);
        m_tree->scrollTo(index);
    }
}

QString MainWindow::sessionName(const std::string& sessionId) const {
    const auto* session = m_deps.sessions->find(sessionId);
    return session != nullptr ? toQt(session->session().name) : QString();
}

sdk::Selection MainWindow::selection() const {
    return m_model->selectionAt(m_filter->mapToSource(m_tree->currentIndex()));
}

void MainWindow::showSessionManager() {
    if (m_deps.showSessionManager) {
        m_deps.showSessionManager();
    }
}

void MainWindow::closeSession(std::string_view sessionId) {
    m_deps.sessions->close(sessionId);
}

void MainWindow::refresh(const sdk::Selection& scope) {
    if (scope.empty()) {
        return;
    }
    m_deps.sessions->refresh(scope.sessionId, scope.database, scope.object);
}

bool MainWindow::isFavorite(const sdk::Selection& scope) const {
    return scope.object && m_deps.favorites->contains(scope.sessionId, *scope.object);
}

void MainWindow::setFavorite(const sdk::Selection& scope, bool favorite) {
    if (!scope.object) {
        return;
    }
    try {
        if (m_deps.favorites->set(scope.sessionId, *scope.object, favorite)) {
            m_model->favoriteChanged(scope.sessionId, *scope.object);
            if (m_filter->favoritesOnly()) {
                m_filter->refilter();
            }
        }
    } catch (const std::exception& error) {
        QMessageBox::warning(this, windowTitle(),
                             tr("Não foi possível salvar os favoritos:\n%1").arg(errorText(error)));
    }
}

bool MainWindow::panelVisible(sdk::Panel panel) const {
    switch (panel) {
    case sdk::Panel::Log:
        return !m_logDock->isHidden();
    }
    return false;
}

void MainWindow::setPanelVisible(sdk::Panel panel, bool visible) {
    switch (panel) {
    case sdk::Panel::Log:
        m_logDock->setVisible(visible);
        break;
    }
    updateCommandStates();
}

void MainWindow::showAbout() {
    QMessageBox::about(
        this, tr("Sobre o Mydbsql"),
        tr("<b>Mydbsql %1</b><br>Cliente de bancos de dados leve e multiplataforma.").arg(m_deps.version));
}

void MainWindow::quit() {
    close();
}

// ---- Abas de query (sdk::IQueryWorkbench) ----

QueryTab* MainWindow::activeQueryTab() const {
    return qobject_cast<QueryTab*>(m_tabs->currentWidget());
}

std::vector<QueryTab*> MainWindow::queryTabs() const {
    std::vector<QueryTab*> tabs;
    for (int index = 0; index < m_tabs->count(); ++index) {
        if (auto* tab = qobject_cast<QueryTab*>(m_tabs->widget(index))) {
            tabs.push_back(tab);
        }
    }
    return tabs;
}

QueryTab* MainWindow::addQueryTab(const services::query::QueryTabState& state) {
    auto* tab = new QueryTab(QueryTab::Dependencies{.sessions = m_deps.sessions, .queries = m_deps.queries}, m_tabs);
    ++m_queryCounter;
    tab->setTitle(tr("Query %1").arg(m_queryCounter));
    tab->setFilePath(toQt(state.file));
    if (!state.title.empty()) {
        tab->setTitle(toQt(state.title)); // o nome dado pelo usuário vale mais que o do arquivo
    }
    tab->editor().setPlainText(toQt(state.sql));
    tab->editor().document()->setModified(false);
    tab->setSessionId(state.sessionId);
    const int index = m_tabs->addTab(tab, appIcon("query"), tab->displayTitle());
    m_tabs->setTabToolTip(index, tab->filePath());
    connect(tab, &QueryTab::titleChanged, this, [this, tab] { updateQueryTabTitle(tab); });
    connect(tab, &QueryTab::runningChanged, this, [this] { updateCommandStates(); });
    return tab;
}

void MainWindow::updateQueryTabTitle(QueryTab* tab) {
    const int index = m_tabs->indexOf(tab);
    if (index >= 0) {
        m_tabs->setTabText(index, tab->displayTitle());
        m_tabs->setTabToolTip(index, tab->filePath());
    }
}

void MainWindow::closeQueryTab(QueryTab* tab) {
    tab->cancel();
    m_tabs->removeTab(m_tabs->indexOf(tab));
    tab->deleteLater();
    updateCommandStates();
}

bool MainWindow::confirmDiscard(const std::vector<QueryTab*>& tabs) {
    const bool modified = std::ranges::any_of(tabs, [](const QueryTab* tab) {
        return tab->editor().document()->isModified() && !tab->editor().document()->isEmpty();
    });
    return !modified ||
           QMessageBox::question(this, windowTitle(),
                                 tr("O texto alterado nas abas de query será perdido. Fechar mesmo assim?")) ==
               QMessageBox::Yes;
}

void MainWindow::renameQueryTab(int index) {
    auto* tab = qobject_cast<QueryTab*>(m_tabs->widget(index));
    if (tab == nullptr) {
        return;
    }
    bool ok = false;
    const QString title =
        QInputDialog::getText(this, tr("Renomear aba"), tr("Nome da aba:"), QLineEdit::Normal, tab->title(), &ok);
    if (ok && !title.trimmed().isEmpty()) {
        tab->setTitle(title.trimmed());
    }
}

void MainWindow::ensureSession(QueryTab& tab) const {
    if (m_deps.sessions->find(tab.sessionId()) == nullptr) {
        std::string selected = selection().sessionId;
        if (!selected.empty()) {
            tab.setSessionId(std::move(selected));
        }
    }
}

void MainWindow::restoreQueryTabs() {
    std::vector<services::query::QueryTabState> states;
    try {
        states = m_deps.queryTabs->load();
    } catch (const std::exception& error) {
        qWarning("Abas de query não restauradas: %s", error.what());
    }
    for (const auto& state : states) {
        (void)addQueryTab(state);
    }
}

void MainWindow::saveQueryTabs() const {
    std::vector<services::query::QueryTabState> states;
    for (const QueryTab* tab : queryTabs()) {
        states.push_back(tab->state());
    }
    try {
        m_deps.queryTabs->save(states);
    } catch (const std::exception& error) {
        qWarning("Abas de query não gravadas: %s", error.what());
    }
}

void MainWindow::closeEvent(QCloseEvent* event) {
    saveQueryTabs();
    QMainWindow::closeEvent(event);
}

void MainWindow::newQueryTab() {
    services::query::QueryTabState state;
    state.sessionId = selection().sessionId;
    QueryTab* tab = addQueryTab(state);
    m_tabs->setCurrentWidget(tab);
    tab->editor().setFocus();
}

bool MainWindow::hasActiveQuery() const {
    return activeQueryTab() != nullptr;
}

bool MainWindow::queryRunning() const {
    const QueryTab* tab = activeQueryTab();
    return tab != nullptr && tab->running();
}

void MainWindow::runQuery(sdk::QueryScope scope, bool explain) {
    if (QueryTab* tab = activeQueryTab()) {
        ensureSession(*tab);
        tab->run(scope, explain);
    }
}

void MainWindow::cancelQuery() {
    if (QueryTab* tab = activeQueryTab()) {
        tab->cancel();
    }
}

void MainWindow::openSqlFile() {
    const QString file = QFileDialog::getOpenFileName(this, tr("Abrir arquivo SQL"), {}, sqlFilter());
    if (!file.isEmpty()) {
        openSqlFileAt(file);
    }
}

void MainWindow::openSqlFileAt(const QString& file) {
    QPointer<MainWindow> self(this);
    inBackground(m_deps.dispatcher, [self, path = QString(file)]() -> std::function<void()> {
        QFile input(path);
        QString error;
        QString text;
        if (input.size() > kMaxEditorFileBytes) {
            error = tr("O arquivo é grande demais para o editor; use \"Executar arquivo SQL\" para rodá-lo.");
        } else if (!input.open(QIODevice::ReadOnly)) {
            error = input.errorString();
        } else {
            text = QString::fromUtf8(input.readAll());
            if (text.startsWith(QChar(0xFEFF))) {
                text.remove(0, 1);
            }
            text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
        }
        return [window = self, opened = path, text, error] {
            if (!window) {
                return;
            }
            if (!error.isEmpty()) {
                QMessageBox::warning(window, window->windowTitle(),
                                     tr("Não foi possível abrir %1:\n%2").arg(opened, error));
                return;
            }
            services::query::QueryTabState state;
            state.sessionId = window->selection().sessionId;
            state.file = toStd(opened);
            state.sql = toStd(text);
            QueryTab* tab = window->addQueryTab(state);
            window->m_tabs->setCurrentWidget(tab);
        };
    });
}

void MainWindow::saveSqlFile(bool saveAs) {
    QueryTab* tab = activeQueryTab();
    if (tab == nullptr) {
        return;
    }
    QString file = tab->filePath();
    if (saveAs || file.isEmpty()) {
        file = QFileDialog::getSaveFileName(this, tr("Salvar arquivo SQL"), file, sqlFilter());
    }
    if (!file.isEmpty()) {
        saveQueryTabTo(tab, file);
    }
}

void MainWindow::saveQueryTabTo(QueryTab* tab, const QString& file) {
    QPointer<MainWindow> self(this);
    QPointer<QueryTab> target(tab);
    QByteArray bytes = tab->editor().toPlainText().toUtf8();
    inBackground(m_deps.dispatcher, [self, target, path = QString(file), bytes]() -> std::function<void()> {
        QSaveFile output(path);
        QString error;
        if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size() || !output.commit()) {
            error = output.errorString();
        }
        return [window = self, saving = target, saved = path, error] {
            if (!window || !saving) {
                return;
            }
            if (!error.isEmpty()) {
                QMessageBox::warning(window, window->windowTitle(),
                                     tr("Não foi possível salvar %1:\n%2").arg(saved, error));
                return;
            }
            saving->setFilePath(saved);
            saving->editor().document()->setModified(false);
            window->updateQueryTabTitle(saving);
            window->saveQueryTabs();
        };
    });
}

void MainWindow::runSqlFile() {
    const QString file = QFileDialog::getOpenFileName(this, tr("Executar arquivo SQL"), {}, sqlFilter());
    if (file.isEmpty()) {
        return;
    }
    if (activeQueryTab() == nullptr) {
        newQueryTab();
    }
    QueryTab* tab = activeQueryTab();
    ensureSession(*tab);
    tab->runFile(toPath(file));
}

void MainWindow::formatQuery() {
    QueryTab* tab = activeQueryTab();
    if (tab == nullptr) {
        return;
    }
    SqlEditor& editor = tab->editor();
    QTextCursor cursor = editor.textCursor();
    const QString source = cursor.hasSelection() ? editor.selectedSql() : editor.toPlainText();
    if (!cursor.hasSelection()) {
        cursor.select(QTextCursor::Document);
    }
    const std::string formatted = core::sql::formatSql(toStd(source), editor.syntax());
    cursor.insertText(toQt(formatted)); // um passo só no desfazer
    editor.setTextCursor(cursor);
}

void MainWindow::toggleComment() {
    if (QueryTab* tab = activeQueryTab()) {
        tab->editor().toggleComment();
    }
}

void MainWindow::showFindReplace() {
    if (m_findDialog) {
        m_findDialog->raise();
        m_findDialog->activateWindow();
        return;
    }
    QPointer<MainWindow> self(this);
    auto* dialog = new FindReplaceDialog(
        [self]() -> SqlEditor* {
            QueryTab* tab = self ? self->activeQueryTab() : nullptr;
            return tab != nullptr ? &tab->editor() : nullptr;
        },
        *m_deps.settings, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    m_findDialog = dialog;
    dialog->show();
}

void MainWindow::insertIntoEditor(const QString& sql) {
    if (activeQueryTab() == nullptr) {
        newQueryTab();
    }
    SqlEditor& editor = activeQueryTab()->editor();
    editor.textCursor().insertText(sql);
    editor.setFocus();
}

void MainWindow::showQueryHistory() {
    if (m_historyDialog) {
        m_historyDialog->raise();
        m_historyDialog->activateWindow();
        return;
    }
    auto* dialog = new QueryHistoryDialog(
        *m_deps.history, *m_deps.settings,
        [this](const std::string& sessionId) {
            const QString name = sessionName(sessionId);
            return name.isEmpty() ? toQt(sessionId) : name;
        },
        this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    connect(dialog, &QueryHistoryDialog::sqlChosen, this, &MainWindow::insertIntoEditor);
    m_historyDialog = dialog;
    dialog->show();
}

void MainWindow::showSnippets() {
    if (m_snippetsDialog) {
        m_snippetsDialog->raise();
        m_snippetsDialog->activateWindow();
        return;
    }
    if (!m_snippetsLoaded) {
        try {
            m_deps.snippets->load();
        } catch (const std::exception& error) {
            QMessageBox::warning(this, windowTitle(),
                                 tr("Não foi possível ler os snippets:\n%1").arg(errorText(error)));
            return;
        }
        m_snippetsLoaded = true;
    }
    auto* dialog = new SnippetsDialog(*m_deps.snippets, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    connect(dialog, &SnippetsDialog::sqlChosen, this, &MainWindow::insertIntoEditor);
    m_snippetsDialog = dialog;
    dialog->show();
}

void MainWindow::closeAllQueryTabs() {
    const std::vector<QueryTab*> tabs = queryTabs();
    if (tabs.empty() || !confirmDiscard(tabs)) {
        return;
    }
    for (QueryTab* tab : tabs) {
        closeQueryTab(tab);
    }
    saveQueryTabs();
}

} // namespace mydb::ui
