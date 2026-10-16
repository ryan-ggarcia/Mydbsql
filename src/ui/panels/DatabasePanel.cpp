#include "ui/panels/DatabasePanel.hpp"

#include "ui/common/ErrorText.hpp"
#include "ui/common/Icons.hpp"
#include "ui/common/QtText.hpp"
#include "ui/panels/Cells.hpp"
#include "ui/panels/TableTab.hpp"

#include <QPointer>
#include <QTableView>
#include <QVBoxLayout>

namespace mydb::ui {

namespace {

QString typeName(core::model::ObjectType type) {
    using core::model::ObjectType;
    switch (type) {
    case ObjectType::Table:
        return DatabasePanel::tr("Tabela");
    case ObjectType::View:
        return DatabasePanel::tr("View");
    case ObjectType::Procedure:
        return DatabasePanel::tr("Procedure");
    case ObjectType::Function:
        return DatabasePanel::tr("Function");
    case ObjectType::Trigger:
        return DatabasePanel::tr("Trigger");
    case ObjectType::Event:
        return DatabasePanel::tr("Event");
    case ObjectType::Sequence:
        return DatabasePanel::tr("Sequence");
    case ObjectType::Other:
        break;
    }
    return DatabasePanel::tr("Outro");
}

} // namespace

DatabasePanel::DatabasePanel(services::session::SessionService& sessions, QWidget* parent)
    : QWidget(parent),
      m_sessions(sessions),
      m_table(new TableTab({tr("Nome"), tr("Tipo"), tr("Linhas"), tr("Tamanho"), tr("Criado"), tr("Alterado"),
                            tr("Engine"), tr("Collation"), tr("Comentário")},
                           true, this)) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_table);
    connect(&m_table->view(), &QTableView::doubleClicked, this, [this](const QModelIndex& index) {
        const int row = m_table->sourceRow(index);
        if (row >= 0 && static_cast<std::size_t>(row) < m_refs.size()) {
            Q_EMIT objectActivated(m_refs[static_cast<std::size_t>(row)]);
        }
    });
}

void DatabasePanel::setDatabase(const std::string& sessionId, const std::string& database) {
    if (sessionId == m_sessionId && database == m_database) {
        return;
    }
    m_sessionId = sessionId;
    m_database = database;
    showObjects({});
    refresh();
}

void DatabasePanel::refresh() {
    if (m_sessionId.empty() || m_database.empty()) {
        m_table->setStatus({});
        return;
    }
    m_table->setStatus(tr("Carregando…"));
    QPointer<DatabasePanel> self(this);
    std::string session = m_sessionId;
    std::string database = m_database;
    m_sessions.objects(session, database, [self, session, database](const auto& outcome) {
        if (!self || self->m_sessionId != session || self->m_database != database) {
            return;
        }
        if (!outcome.ok()) {
            self->m_table->setStatus(errorText(outcome.error()));
            return;
        }
        self->showObjects(outcome.value());
    });
}

void DatabasePanel::showObjects(const std::vector<core::model::DbObject>& objects) {
    std::vector<RowsModel::Row> rows;
    rows.reserve(objects.size());
    m_refs.clear();
    m_refs.reserve(objects.size());
    std::int64_t total = 0;
    for (const auto& object : objects) {
        m_refs.push_back(object.ref);
        total += object.sizeBytes.value_or(0);
        rows.push_back({cell(toQt(object.ref.name)), cell(typeName(object.ref.type)), numberCell(object.rows),
                        sizeCell(object.sizeBytes), timeCell(object.created), timeCell(object.updated),
                        cell(toQt(object.engine)), cell(toQt(object.collation)), cell(toQt(object.comment))});
    }
    m_table->setRows(std::move(rows));
    if (!m_database.empty() && !objects.empty()) {
        m_table->setStatus(tr("%1: %n objeto(s), %2", nullptr, static_cast<int>(objects.size()))
                               .arg(toQt(m_database), sizeCell(total).display.toString()));
    }
}

} // namespace mydb::ui
