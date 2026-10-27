#pragma once

#include <cstdint>

namespace mydb::sdk {

/// Que parte do editor executar.
enum class QueryScope : std::uint8_t {
    All,
    Selection,        ///< o texto selecionado (ou tudo, se não houver seleção)
    CurrentStatement, ///< a sentença onde está o cursor
};

/// As abas de query vistas pelos comandos (Fase 5). Usado só na thread da UI. Cada método novo muda o
/// major de kApiVersion.
class IQueryWorkbench {
public:
    virtual void newQueryTab() = 0;
    /// Há uma aba de query em foco (as ações abaixo valem para ela).
    [[nodiscard]] virtual bool hasActiveQuery() const = 0;
    /// A aba em foco está executando.
    [[nodiscard]] virtual bool queryRunning() const = 0;

    virtual void runQuery(QueryScope scope, bool explain) = 0;
    virtual void cancelQuery() = 0;

    virtual void openSqlFile() = 0;
    virtual void saveSqlFile(bool saveAs) = 0;
    /// Roda um arquivo .sql do disco em streaming, sem abrir no editor.
    virtual void runSqlFile() = 0;

    virtual void formatQuery() = 0;
    virtual void toggleComment() = 0;
    virtual void showFindReplace() = 0;
    virtual void showQueryHistory() = 0;
    virtual void showSnippets() = 0;
    virtual void closeAllQueryTabs() = 0;

protected:
    IQueryWorkbench() = default;
    IQueryWorkbench(const IQueryWorkbench&) = default;
    IQueryWorkbench(IQueryWorkbench&&) = default;
    IQueryWorkbench& operator=(const IQueryWorkbench&) = default;
    IQueryWorkbench& operator=(IQueryWorkbench&&) = default;
    ~IQueryWorkbench() = default;
};

} // namespace mydb::sdk
