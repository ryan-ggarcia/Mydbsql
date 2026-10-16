#include "modules/workbench/WorkbenchModule.hpp"

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace mydb::modules::workbench {

namespace {

using sdk::CommandInfo;
using sdk::IWorkbench;
using sdk::StandardShortcut;

/// Comando montado a partir de funções: os comandos deste módulo são pequenos demais para uma classe cada.
class FunctionCommand final : public sdk::ICommand {
public:
    using Predicate = std::function<bool(const IWorkbench&)>;
    using Action = std::function<void(IWorkbench&)>;

    FunctionCommand(CommandInfo info, Predicate enabled, Action action, Predicate checked = {})
        : m_info(std::move(info)),
          m_enabled(std::move(enabled)),
          m_action(std::move(action)),
          m_checked(std::move(checked)) {}

    [[nodiscard]] CommandInfo info() const override { return m_info; }
    [[nodiscard]] bool isEnabled(const IWorkbench& workbench) const override {
        return !m_enabled || m_enabled(workbench);
    }
    [[nodiscard]] bool isChecked(const IWorkbench& workbench) const override {
        return m_checked && m_checked(workbench);
    }
    void execute(IWorkbench& workbench) override { m_action(workbench); }

private:
    CommandInfo m_info;
    Predicate m_enabled;
    Action m_action;
    Predicate m_checked;
};

bool hasSession(const IWorkbench& workbench) {
    return !workbench.selection().empty();
}

bool hasObject(const IWorkbench& workbench) {
    return workbench.selection().object.has_value();
}

class WorkbenchModule final : public sdk::IModule {
public:
    [[nodiscard]] std::string_view id() const noexcept override { return "workbench"; }
    [[nodiscard]] sdk::Version apiVersion() const noexcept override { return sdk::kApiVersion; }

    void registerInto(sdk::ModuleContext& context) override {
        sdk::CommandRegistry& commands = context.commands();
        const auto add = [&commands](std::string commandId, CommandInfo info, FunctionCommand::Predicate enabled,
                                     FunctionCommand::Action action, FunctionCommand::Predicate checked = {}) {
            std::string name = info.text;
            commands.add(std::move(commandId), std::move(name),
                         [info = std::move(info), enabled = std::move(enabled), action = std::move(action),
                          checked = std::move(checked)] {
                             return std::make_unique<FunctionCommand>(info, enabled, action, checked);
                         });
        };

        // Sem inicializadores designados parciais: o clang avisa de cada campo omitido.
        const auto info = [](std::string text, std::string_view menu, std::string group, int order = 0) {
            CommandInfo result;
            result.text = std::move(text);
            result.menu = std::string(menu);
            result.group = std::move(group);
            result.order = order;
            return result;
        };

        CommandInfo manager = info("&Gerenciador de sessões…", sdk::menus::kFile, "1-session", 10);
        manager.description = "Abre outra conexão ou edita as sessões salvas";
        manager.icon = "sessions";
        manager.standardShortcut = StandardShortcut::New;
        manager.toolbar = true;
        add("session.manager", std::move(manager), {}, [](IWorkbench& workbench) { workbench.showSessionManager(); });

        CommandInfo disconnect = info("&Desconectar", sdk::menus::kFile, "1-session", 20);
        disconnect.description = "Fecha a conexão selecionada na árvore";
        disconnect.icon = "disconnect";
        disconnect.toolbar = true;
        add("session.disconnect", std::move(disconnect), hasSession,
            [](IWorkbench& workbench) { workbench.closeSession(workbench.selection().sessionId); });

        CommandInfo quit = info("Sai&r", sdk::menus::kFile, "9-quit");
        quit.description = "Fecha todas as conexões e encerra o Mydbsql";
        quit.standardShortcut = StandardShortcut::Quit;
        add("app.quit", std::move(quit), {}, [](IWorkbench& workbench) { workbench.quit(); });

        CommandInfo refresh = info("&Atualizar", sdk::menus::kView, "1-refresh");
        refresh.description = "Recarrega o item selecionado na árvore, sem usar o cache";
        refresh.icon = "refresh";
        refresh.standardShortcut = StandardShortcut::Refresh;
        refresh.toolbar = true;
        refresh.objectContextMenu = true;
        add("view.refresh", std::move(refresh), hasSession,
            [](IWorkbench& workbench) { workbench.refresh(workbench.selection()); });

        CommandInfo log = info("Painel de &log", sdk::menus::kView, "2-panels");
        log.description = "Mostra ou esconde o SQL enviado e as mensagens das conexões";
        log.checkable = true;
        add(
            "view.log", std::move(log), {},
            [](IWorkbench& workbench) {
                workbench.setPanelVisible(sdk::Panel::Log, !workbench.panelVisible(sdk::Panel::Log));
            },
            [](const IWorkbench& workbench) { return workbench.panelVisible(sdk::Panel::Log); });

        CommandInfo favorite = info("&Favorito", {}, "5-favorite");
        favorite.description = "Marca o objeto para achá-lo depressa com o filtro de favoritos";
        favorite.icon = "favorite";
        favorite.objectContextMenu = true;
        favorite.checkable = true;
        add(
            "object.favorite", std::move(favorite), hasObject,
            [](IWorkbench& workbench) {
                const sdk::Selection selection = workbench.selection();
                workbench.setFavorite(selection, !workbench.isFavorite(selection));
            },
            [](const IWorkbench& workbench) { return workbench.isFavorite(workbench.selection()); });

        add("help.about", info("&Sobre o Mydbsql", sdk::menus::kHelp, "9-about"), {},
            [](IWorkbench& workbench) { workbench.showAbout(); });
    }
};

} // namespace

sdk::IModule& module() {
    static WorkbenchModule instance;
    return instance;
}

} // namespace mydb::modules::workbench
