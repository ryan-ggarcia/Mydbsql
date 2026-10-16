#include "ui/models/RowsModel.hpp"

#include <utility>

namespace mydb::ui {

RowsModel::RowsModel(QStringList headers, QObject* parent)
    : QAbstractTableModel(parent), m_headers(std::move(headers)) {}

void RowsModel::setRows(std::vector<Row> rows) {
    beginResetModel();
    m_rows = std::move(rows);
    endResetModel();
}

int RowsModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(m_rows.size());
}

int RowsModel::columnCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(m_headers.size());
}

QVariant RowsModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= rowCount() || index.column() >= columnCount()) {
        return {};
    }
    const Row& row = m_rows[static_cast<std::size_t>(index.row())];
    if (static_cast<std::size_t>(index.column()) >= row.size()) {
        return {};
    }
    const Cell& value = row[static_cast<std::size_t>(index.column())];
    switch (role) {
    case Qt::DisplayRole:
    case Qt::ToolTipRole:
        return value.display;
    case kSortRole:
        return value.sort.isValid() ? value.sort : value.display;
    case Qt::TextAlignmentRole: {
        const QVariant& key = value.sort.isValid() ? value.sort : value.display;
        const bool numeric = key.typeId() == QMetaType::LongLong || key.typeId() == QMetaType::Int ||
                             key.typeId() == QMetaType::Double || key.typeId() == QMetaType::ULongLong;
        return numeric ? QVariant(Qt::AlignRight | Qt::AlignVCenter) : QVariant();
    }
    default:
        return {};
    }
}

QVariant RowsModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole && section < m_headers.size()) {
        return m_headers.at(section);
    }
    return {};
}

} // namespace mydb::ui
