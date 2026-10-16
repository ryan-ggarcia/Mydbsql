#pragma once

#include "core/model/DbObject.hpp"
#include "services/session/SessionService.hpp"

#include <QWidget>

#include <string>
#include <vector>

namespace mydb::ui {

class TableTab;

/// Aba Database (CLAUDE.md §1.3): os objetos de um banco com linhas, tamanho, engine, collation e datas.
class DatabasePanel final : public QWidget {
    Q_OBJECT

public:
    explicit DatabasePanel(services::session::SessionService& sessions, QWidget* parent = nullptr);

    /// Mostra o banco; vazio limpa a aba. Usa o cache da sessão.
    void setDatabase(const std::string& sessionId, const std::string& database);
    void refresh();
    [[nodiscard]] const std::string& sessionId() const noexcept { return m_sessionId; }
    [[nodiscard]] const std::string& database() const noexcept { return m_database; }

Q_SIGNALS:
    /// Duplo clique num objeto da lista.
    void objectActivated(const core::model::ObjectRef& object);

private:
    void showObjects(const std::vector<core::model::DbObject>& objects);

    services::session::SessionService& m_sessions;
    std::string m_sessionId;
    std::string m_database;
    std::vector<core::model::ObjectRef> m_refs;
    TableTab* m_table;
};

} // namespace mydb::ui
