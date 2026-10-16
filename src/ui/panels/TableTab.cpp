#include "ui/panels/TableTab.hpp"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QTableView>
#include <QVBoxLayout>

#include <utility>

namespace mydb::ui {

TableTab::TableTab(const QStringList& headers, bool filterable, QWidget* parent)
    : QWidget(parent),
      m_model(new RowsModel(headers, this)),
      m_proxy(new QSortFilterProxyModel(this)),
      m_view(new QTableView(this)),
      m_toolbar(new QHBoxLayout()),
      m_status(new QLabel(this)) {
    m_proxy->setSourceModel(m_model);
    m_proxy->setSortRole(RowsModel::kSortRole);
    m_proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
    m_proxy->setFilterKeyColumn(-1);
    m_proxy->setSortCaseSensitivity(Qt::CaseInsensitive);

    m_view->setModel(m_proxy);
    m_view->setSortingEnabled(true);
    m_view->sortByColumn(-1, Qt::AscendingOrder);
    m_view->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_view->setAlternatingRowColors(true);
    m_view->setWordWrap(false);
    m_view->verticalHeader()->hide();
    m_view->verticalHeader()->setDefaultSectionSize(m_view->fontMetrics().height() + 6);
    m_view->horizontalHeader()->setStretchLastSection(true);
    m_view->horizontalHeader()->setSectionsMovable(true);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    if (filterable) {
        m_filter = new QLineEdit(this);
        m_filter->setPlaceholderText(tr("Filtrar"));
        m_filter->setClearButtonEnabled(true);
        m_toolbar->addWidget(m_filter, 1);
        connect(m_filter, &QLineEdit::textChanged, m_proxy, &QSortFilterProxyModel::setFilterFixedString);
    } else {
        m_toolbar->addStretch(1);
    }
    layout->addLayout(m_toolbar);
    layout->addWidget(m_view, 1);
    layout->addWidget(m_status);
}

void TableTab::setRows(std::vector<RowsModel::Row> rows) {
    m_model->setRows(std::move(rows));
    m_view->resizeColumnsToContents();
    constexpr int kMaxInitialWidth = 400;
    for (int column = 0; column < m_model->columnCount(); ++column) {
        if (m_view->columnWidth(column) > kMaxInitialWidth) {
            m_view->setColumnWidth(column, kMaxInitialWidth);
        }
    }
}

void TableTab::setStatus(const QString& text) {
    m_status->setText(text);
}

int TableTab::sourceRow(const QModelIndex& viewIndex) const {
    const QModelIndex source = m_proxy->mapToSource(viewIndex);
    return source.isValid() ? source.row() : -1;
}

int TableTab::currentSourceRow() const {
    return sourceRow(m_view->currentIndex());
}

} // namespace mydb::ui
