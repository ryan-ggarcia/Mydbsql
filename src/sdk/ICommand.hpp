#pragma once

#include "sdk/IWorkbench.hpp"
#include "sdk/Registry.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace mydb::sdk {

/// Atalhos com significado próprio em cada SO (no Qt, QKeySequence::StandardKey): o app traduz para a tecla
/// certa de cada plataforma, ex.: Refresh é F5 no Windows e no Linux e Cmd+R no macOS (CLAUDE.md §2.6, regra 7).
enum class StandardShortcut : std::uint8_t {
    None,
    New,
    Open,
    Save,
    Close,
    Quit,
    Refresh,
    Find,
    Preferences,
};

/// Menus da janela principal, nesta ordem. Um módulo pode criar outros, que vêm antes de "help".
namespace menus {
inline constexpr std::string_view kFile = "file";
inline constexpr std::string_view kEdit = "edit";
inline constexpr std::string_view kView = "view";
inline constexpr std::string_view kTools = "tools";
inline constexpr std::string_view kHelp = "help";
} // namespace menus

/// Como um comando aparece. Os textos são UTF-8.
struct CommandInfo {
    std::string text;        ///< "Atualizar"; "&" marca a tecla de acesso
    std::string description; ///< dica de ferramenta e barra de status
    std::string icon;        ///< nome de ícone do app ("refresh", "disconnect"…); vazio = sem ícone
    StandardShortcut standardShortcut{StandardShortcut::None};
    std::string shortcut; ///< usado se não houver standardShortcut, ex.: "Ctrl+Shift+F" (Ctrl vira Cmd no macOS)
    std::string menu;     ///< id do menu (veja `menus`); vazio = fora dos menus
    std::string group;    ///< itens de grupos diferentes ficam separados por uma linha
    int order{0};         ///< posição dentro do grupo
    bool toolbar{false};
    bool objectContextMenu{false}; ///< aparece no menu de contexto da árvore de objetos
    bool checkable{false};
};

/// Uma ação do usuário (CLAUDE.md §2.4). A janela principal monta menus, barra de ferramentas e atalhos a partir
/// do CommandRegistry: nenhum item fica fixo no código dela.
class ICommand {
public:
    virtual ~ICommand() = default;

    [[nodiscard]] virtual CommandInfo info() const = 0;
    [[nodiscard]] virtual bool isEnabled(const IWorkbench& workbench) const = 0;
    /// Só para comandos com CommandInfo::checkable.
    [[nodiscard]] virtual bool isChecked(const IWorkbench& workbench) const = 0;
    virtual void execute(IWorkbench& workbench) = 0;

protected:
    ICommand() = default;
    ICommand(const ICommand&) = default;
    ICommand(ICommand&&) = default;
    ICommand& operator=(const ICommand&) = default;
    ICommand& operator=(ICommand&&) = default;
};

using CommandRegistry = Registry<ICommand>;

} // namespace mydb::sdk
