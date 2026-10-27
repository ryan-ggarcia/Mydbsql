#pragma once

#include "core/model/ObjectRef.hpp"
#include "sdk/IQueryWorkbench.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace mydb::sdk {

/// O que está selecionado na árvore de objetos.
struct Selection {
    std::string sessionId; ///< vazio: nada selecionado
    std::string database;  ///< vazio: o servidor da sessão
    std::optional<core::model::ObjectRef> object;

    [[nodiscard]] bool empty() const noexcept { return sessionId.empty(); }
};

/// Painéis da janela principal que um comando pode mostrar ou esconder.
enum class Panel : std::uint8_t {
    Log,
};

/// A janela principal vista pelos comandos (CLAUDE.md §2.4): o que eles podem consultar e pedir, sem
/// conhecer o Qt nem a MainWindow. Usado só na thread da UI. Cresce com as fases; cada método novo muda o
/// major de kApiVersion.
class IWorkbench {
public:
    [[nodiscard]] virtual Selection selection() const = 0;

    virtual void showSessionManager() = 0;
    virtual void closeSession(std::string_view sessionId) = 0;
    /// Recarrega o que está em `scope` (servidor, banco ou objeto) e descarta o cache dele.
    virtual void refresh(const Selection& scope) = 0;

    [[nodiscard]] virtual bool isFavorite(const Selection& scope) const = 0;
    virtual void setFavorite(const Selection& scope, bool favorite) = 0;

    [[nodiscard]] virtual bool panelVisible(Panel panel) const = 0;
    virtual void setPanelVisible(Panel panel, bool visible) = 0;

    /// As abas de query (Fase 5). Nunca nulo.
    [[nodiscard]] virtual IQueryWorkbench& queries() = 0;
    [[nodiscard]] virtual const IQueryWorkbench& queries() const = 0;

    virtual void showAbout() = 0;
    virtual void quit() = 0;

protected:
    IWorkbench() = default;
    IWorkbench(const IWorkbench&) = default;
    IWorkbench(IWorkbench&&) = default;
    IWorkbench& operator=(const IWorkbench&) = default;
    IWorkbench& operator=(IWorkbench&&) = default;
    ~IWorkbench() = default;
};

} // namespace mydb::sdk
