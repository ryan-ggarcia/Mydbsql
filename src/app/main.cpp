// Raiz de composição (CLAUDE.md §4.3, D): o único lugar que conhece as implementações concretas.

#include "app/Application.hpp"
#include "app/BuiltinModules.hpp"
#include "app/CommandLine.hpp"
#include "app/ModuleDriverCatalog.hpp"
#include "app/ModuleHost.hpp"
#include "app/StartupProbe.hpp"
#include "infra/credentials/MemoryCredentialStore.hpp"
#include "infra/logging/Logging.hpp"
#include "infra/platform/IPlatform.hpp"
#include "infra/platform/SystemCredentialStore.hpp"
#include "infra/settings/JsonSettingsStore.hpp"
#include "services/log/LogBuffer.hpp"
#include "services/query/QueryDocuments.hpp"
#include "services/query/QueryHistoryStore.hpp"
#include "services/query/QueryService.hpp"
#include "services/session/FavoritesStore.hpp"
#include "services/session/SessionConnector.hpp"
#include "services/session/SessionRepository.hpp"
#include "services/session/SessionService.hpp"
#include "ui/common/QtDispatcher.hpp"
#include "ui/common/QtText.hpp"
#include "ui/dialogs/SessionManagerDialog.hpp"
#include "ui/main/MainWindow.hpp"

#include <QMessageBox>
#include <QObject>

#include <spdlog/spdlog.h>

#include <chrono>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <functional>
#include <memory>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

constexpr bool kDebugBuild = MYDB_DEBUG_BUILD != 0;

namespace platform = mydb::infra::platform;
namespace session = mydb::services::session;

/// Guarda uma cópia do arquivo de sessões ilegível antes que um "Salvar" o sobrescreva.
std::filesystem::path backupUnreadableSessions(const std::filesystem::path& configDirectory) {
    const std::filesystem::path file = configDirectory / "sessions.json";
    std::filesystem::path backup = file;
    backup += ".bak";
    std::error_code error;
    std::filesystem::copy_file(file, backup, std::filesystem::copy_options::overwrite_existing, error);
    return error ? std::filesystem::path() : backup;
}

int run(int argc, char** argv, std::chrono::steady_clock::time_point startedAt) {
    namespace logging = mydb::infra::logging;

    const std::vector<const char*> args(argv, argv + argc); // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    const auto options = mydb::app::parseCommandLine(args);
    if (!options.error.empty()) {
        std::fprintf(stderr, "mydbsql: %s\n", options.error.c_str());
        return 2;
    }

    const std::unique_ptr<platform::IPlatform> os = platform::createPlatform();
    logging::initialize({.directory = os->standardDirectory(platform::StandardDirectory::Cache) / "logs",
                         .console = kDebugBuild,
                         .verbose = kDebugBuild});

    // No Windows, o motor de fontes GDI abre a primeira janela em metade do tempo do DirectWrite
    // (ADR-0010). Quem definir QT_QPA_PLATFORM ou passar -platform continua mandando.
    // O valor vem do CMake e é vazio fora do Windows.
    // NOLINTNEXTLINE(readability-redundant-string-init)
    constexpr std::string_view kDefaultPlatform = MYDB_DEFAULT_QPA_PLATFORM;
    if (!kDefaultPlatform.empty() && qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM",
                QByteArray(kDefaultPlatform.data(), static_cast<qsizetype>(kDefaultPlatform.size())));
    }

    int exitCode = 0;
    {
        mydb::app::Application app(argc, argv, /*enableWatchdog=*/kDebugBuild);

        const std::filesystem::path configDirectory = os->standardDirectory(platform::StandardDirectory::Config);
        mydb::infra::settings::JsonSettingsStore settings(configDirectory);
        std::unique_ptr<mydb::core::ICredentialStore> credentials = platform::createSystemCredentialStore();
        if (!credentials) {
            spdlog::warn("Sem cofre de senhas no sistema: as senhas valem só até fechar o app");
            credentials = std::make_unique<mydb::infra::credentials::MemoryCredentialStore>();
        }

        // Ordem de destruição (inversa): janela, sessões abertas, conector, catálogo e, por último, os módulos
        // (o código das fábricas e o EventBus moram neles). O contexto do dispatcher vive mais que as sessões,
        // porque os workers delas entregam resultados por ele.
        mydb::app::ModuleHost modules(*os);
        mydb::app::registerBuiltinModules(modules);
        mydb::app::ModuleDriverCatalog drivers(modules, os->pluginDirectory());
        session::SessionRepository repository(settings, *credentials);
        session::SessionConnector connector(drivers, *credentials);
        session::FavoritesStore favorites(settings);
        mydb::services::log::LogBuffer log;
        QObject dispatchContext;
        const mydb::services::Dispatcher dispatcher = mydb::ui::qtDispatcher(&dispatchContext);
        mydb::services::query::QueryHistoryStore history(settings);
        mydb::services::query::SnippetStore snippets(settings);
        mydb::services::query::QueryTabsStore queryTabs(settings);
        session::SessionService sessions(modules.events(), dispatcher, log);
        mydb::services::query::QueryService queries(sessions, &history);
        std::unique_ptr<mydb::ui::MainWindow> mainWindow;

        std::string loadError;
        try {
            repository.load();
        } catch (const session::SettingsError& error) {
            loadError = error.what();
        }
        try {
            favorites.load();
        } catch (const session::SettingsError& error) {
            spdlog::warn("Favoritos ignorados: {}", error.what());
        }

        std::function<void()> showSessionManager;
        const mydb::ui::SessionManagerDialog::ConnectedHandler onConnected =
            [&](const session::Session& opened, std::unique_ptr<mydb::core::IConnection> connection) {
                if (!mainWindow) {
                    mainWindow = std::make_unique<mydb::ui::MainWindow>(
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
                                                           .dispatcher = dispatcher,
                                                           .showSessionManager = showSessionManager,
                                                           .version = QStringLiteral(MYDB_VERSION)});
                    mainWindow->show();
                }
                // Uma sessão já aberta não abre de novo: a conexão nova é fechada e a existente, selecionada.
                (void)sessions.add(opened, std::move(connection));
                mainWindow->selectSession(opened.id);
                mainWindow->activateWindow();
            };
        // Outras conexões: o gerenciador abre de novo, modal sobre a janela principal, e é destruído ao fechar.
        showSessionManager = [&] {
            mydb::ui::SessionManagerDialog dialog(repository, connector, drivers.availableDrivers(),
                                                  credentials->persistent(), mainWindow.get());
            dialog.setConnectedHandler(onConnected);
            dialog.exec();
        };

        // Destruído ao fechar (§2.2, regra 1): depois de conectar, a janela principal assume.
        auto* manager = new mydb::ui::SessionManagerDialog(repository, connector, drivers.availableDrivers(),
                                                           credentials->persistent());
        manager->setAttribute(Qt::WA_DeleteOnClose);
        manager->setConnectedHandler(onConnected);

        std::unique_ptr<mydb::app::StartupProbe> probe;
        if (options.measureStartup) {
            probe = std::make_unique<mydb::app::StartupProbe>(*os, startedAt, options);
        }
        manager->show();
        if (probe) {
            probe->attach(*manager);
        }
        if (!loadError.empty()) {
            const std::filesystem::path backup = backupUnreadableSessions(configDirectory);
            QMessageBox::warning(manager, manager->windowTitle(),
                                 QObject::tr("Não foi possível ler as sessões salvas:\n%1\n\nCópia do arquivo: %2")
                                     .arg(mydb::ui::toQt(loadError), mydb::ui::pathToQt(backup)));
        }
        spdlog::info("Mydbsql {} iniciado ({})", MYDB_VERSION, os->name());
        exitCode = mydb::app::Application::exec();
    }

    logging::shutdown();
    return exitCode;
}

} // namespace

int main(int argc, char* argv[]) {
    // Marco zero do --measure-startup (§2.1): o mais cedo possível.
    const auto startedAt = std::chrono::steady_clock::now();
    try {
        return run(argc, argv, startedAt);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "mydbsql: erro fatal: %s\n", error.what());
    } catch (...) {
        std::fprintf(stderr, "mydbsql: erro fatal desconhecido\n");
    }
    mydb::infra::logging::shutdown();
    return 1;
}
