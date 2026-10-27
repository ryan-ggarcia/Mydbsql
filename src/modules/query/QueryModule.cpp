#include "modules/query/QueryModule.hpp"

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace mydb::modules::query {

namespace {

using sdk::CommandInfo;
using sdk::IQueryWorkbench;
using sdk::IWorkbench;
using sdk::QueryScope;
using sdk::StandardShortcut;

constexpr std::string_view kQueryMenu = "query";

/// Comando montado a partir de funções (um módulo não usa código de outro, então não vem do workbench).
class FunctionCommand final : public sdk::ICommand {
public:
    using Predicate = std::function<bool(const IWorkbench&)>;
    using Action = std::function<void(IWorkbench&)>;

    FunctionCommand(CommandInfo info, Predicate enabled, Action action)
        : m_info(std::move(info)), m_enabled(std::move(enabled)), m_action(std::move(action)) {}

    [[nodiscard]] CommandInfo info() const override { return m_info; }
    [[nodiscard]] bool isEnabled(const IWorkbench& workbench) const override {
        return !m_enabled || m_enabled(workbench);
    }
    [[nodiscard]] bool isChecked(const IWorkbench& /*workbench*/) const override { return false; }
    void execute(IWorkbench& workbench) override { m_action(workbench); }

private:
    CommandInfo m_info;
    Predicate m_enabled;
    Action m_action;
};

bool hasQuery(const IWorkbench& workbench) {
    return workbench.queries().hasActiveQuery();
}

bool canRun(const IWorkbench& workbench) {
    return workbench.queries().hasActiveQuery() && !workbench.queries().queryRunning();
}

bool isRunning(const IWorkbench& workbench) {
    return workbench.queries().queryRunning();
}

bool hasSession(const IWorkbench& workbench) {
    return !workbench.selection().empty();
}

class QueryModule final : public sdk::IModule {
public:
    [[nodiscard]] std::string_view id() const noexcept override { return "query"; }
    [[nodiscard]] sdk::Version apiVersion() const noexcept override { return sdk::kApiVersion; }

    void registerInto(sdk::ModuleContext& context) override {
        sdk::CommandRegistry& commands = context.commands();
        const auto add = [&commands](std::string commandId, CommandInfo info, FunctionCommand::Predicate enabled,
                                     const std::function<void(IQueryWorkbench&)>& action) {
            std::string name = info.text;
            FunctionCommand::Action run = [action](IWorkbench& workbench) {
                action(workbench.queries());
            };
            commands.add(std::move(commandId), std::move(name),
                         [info = std::move(info), enabled = std::move(enabled), run = std::move(run)] {
                             return std::make_unique<FunctionCommand>(info, enabled, run);
                         });
        };

        // Sem inicializadores designados parciais: o clang avisa de cada campo omitido.
        const auto info = [](std::string text, std::string_view menu, std::string group, int order,
                             std::string description) {
            CommandInfo result;
            result.text = std::move(text);
            result.description = std::move(description);
            result.menu = std::string(menu);
            result.group = std::move(group);
            result.order = order;
            return result;
        };

        // Arquivo
        CommandInfo fresh = info("Nova aba de &query", sdk::menus::kFile, "2-query", 10,
                                 "Abre um editor de SQL para a sessão selecionada");
        fresh.icon = "query";
        fresh.shortcut = "Ctrl+T";
        fresh.toolbar = true;
        add("query.new", std::move(fresh), {}, [](IQueryWorkbench& q) { q.newQueryTab(); });

        CommandInfo open = info("&Abrir arquivo SQL…", sdk::menus::kFile, "2-query", 20, "Abre um .sql numa aba nova");
        open.standardShortcut = StandardShortcut::Open;
        add("query.open", std::move(open), {}, [](IQueryWorkbench& q) { q.openSqlFile(); });

        CommandInfo save = info("&Salvar", sdk::menus::kFile, "2-query", 30, "Grava a aba de query no arquivo dela");
        save.standardShortcut = StandardShortcut::Save;
        add("query.save", std::move(save), hasQuery, [](IQueryWorkbench& q) { q.saveSqlFile(false); });

        CommandInfo saveAs =
            info("Salvar &como…", sdk::menus::kFile, "2-query", 40, "Grava a aba de query num arquivo novo");
        saveAs.shortcut = "Ctrl+Shift+S";
        add("query.saveAs", std::move(saveAs), hasQuery, [](IQueryWorkbench& q) { q.saveSqlFile(true); });

        CommandInfo runFile = info("E&xecutar arquivo SQL…", sdk::menus::kFile, "2-query", 50,
                                   "Executa um .sql grande direto do disco, sem abrir no editor");
        add("query.runFile", std::move(runFile), hasSession, [](IQueryWorkbench& q) { q.runSqlFile(); });

        CommandInfo closeAll =
            info("&Fechar todas as abas de query", sdk::menus::kFile, "2-query", 60, "Fecha todos os editores de SQL");
        add("query.closeAll", std::move(closeAll), {}, [](IQueryWorkbench& q) { q.closeAllQueryTabs(); });

        // Query
        CommandInfo run = info("&Executar", kQueryMenu, "1-run", 10, "Executa todo o SQL do editor");
        run.icon = "run";
        run.shortcut = "F9";
        run.toolbar = true;
        add("query.run", std::move(run), canRun, [](IQueryWorkbench& q) { q.runQuery(QueryScope::All, false); });

        CommandInfo selection = info("Executar a &seleção", kQueryMenu, "1-run", 20, "Executa só o SQL selecionado");
        selection.shortcut = "Ctrl+F9";
        add("query.runSelection", std::move(selection), canRun,
            [](IQueryWorkbench& q) { q.runQuery(QueryScope::Selection, false); });

        CommandInfo current =
            info("Executar a sentença &atual", kQueryMenu, "1-run", 30, "Executa a sentença onde está o cursor");
        current.shortcut = "Shift+F9";
        add("query.runCurrent", std::move(current), canRun,
            [](IQueryWorkbench& q) { q.runQuery(QueryScope::CurrentStatement, false); });

        CommandInfo explain = info("E&XPLAIN da sentença atual", kQueryMenu, "1-run", 40,
                                   "Mostra o plano de execução da sentença onde está o cursor");
        add("query.explain", std::move(explain), canRun,
            [](IQueryWorkbench& q) { q.runQuery(QueryScope::CurrentStatement, true); });

        CommandInfo cancel = info("&Cancelar", kQueryMenu, "1-run", 50, "Interrompe a execução em andamento");
        cancel.icon = "stop";
        cancel.toolbar = true;
        add("query.cancel", std::move(cancel), isRunning, [](IQueryWorkbench& q) { q.cancelQuery(); });

        CommandInfo history =
            info("&Histórico de queries…", kQueryMenu, "3-tools", 10, "Procura e reutiliza as queries já executadas");
        add("query.history", std::move(history), {}, [](IQueryWorkbench& q) { q.showQueryHistory(); });

        CommandInfo snippets = info("S&nippets…", kQueryMenu, "3-tools", 20, "Trechos de SQL salvos para reutilizar");
        add("query.snippets", std::move(snippets), {}, [](IQueryWorkbench& q) { q.showSnippets(); });

        // Editar
        CommandInfo find =
            info("&Localizar e substituir…", sdk::menus::kEdit, "2-find", 10, "Procura e substitui texto no editor");
        find.standardShortcut = StandardShortcut::Find;
        add("query.find", std::move(find), hasQuery, [](IQueryWorkbench& q) { q.showFindReplace(); });

        CommandInfo format =
            info("&Reformatar SQL", sdk::menus::kEdit, "3-sql", 10, "Reformata a seleção ou todo o editor");
        format.shortcut = "Ctrl+Shift+F";
        add("query.format", std::move(format), hasQuery, [](IQueryWorkbench& q) { q.formatQuery(); });

        CommandInfo comment = info("&Comentar ou descomentar", sdk::menus::kEdit, "3-sql", 20,
                                   "Comenta as linhas selecionadas com --, ou tira o comentário");
        comment.shortcut = "Ctrl+/";
        add("query.comment", std::move(comment), hasQuery, [](IQueryWorkbench& q) { q.toggleComment(); });
    }
};

} // namespace

sdk::IModule& module() {
    static QueryModule instance;
    return instance;
}

} // namespace mydb::modules::query
