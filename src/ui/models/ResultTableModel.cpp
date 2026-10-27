#include "ui/models/ResultTableModel.hpp"

#include "ui/common/QtText.hpp"

#include <QBrush>
#include <QPalette>

#include <utility>

namespace mydb::ui {

namespace {

// Uma célula enorme (TEXT, BLOB) não é desenhada inteira; o valor completo vem com a grade da Fase 6.
constexpr std::size_t kMaxCellChars = 1024;

} // namespace

ResultTableModel::ResultTableModel(services::query::ResultTable table, QObject* parent)
    : QAbstractTableModel(parent), m_table(std::move(table)) {}

int ResultTableModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(m_table.rows.size());
}

int ResultTableModel::columnCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(m_table.columns.size());
}

QVariant ResultTableModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid()) {
        return {};
    }
    const auto& row = m_table.rows.at(static_cast<std::size_t>(index.row()));
    const auto column = static_cast<std::size_t>(index.column());
    const std::string* text = nullptr;
    if (column < row.size()) {
        if (const auto& cell = row[column]; cell.has_value()) {
            text = &*cell;
        }
    }
    const bool isNull = text == nullptr;
    switch (role) {
    case Qt::DisplayRole:
        if (isNull) {
            return QStringLiteral("NULL");
        }
        return toQt(std::string_view(*text).substr(0, kMaxCellChars));
    case Qt::ForegroundRole:
        if (isNull) {
            return QPalette().brush(QPalette::Disabled, QPalette::Text);
        }
        return {};
    default:
        return {};
    }
}

QVariant ResultTableModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (role != Qt::DisplayRole) {
        return {};
    }
    if (orientation == Qt::Vertical) {
        return section + 1;
    }
    const auto column = static_cast<std::size_t>(section);
    return column < m_table.columns.size() ? toQt(m_table.columns[column].name) : QVariant();
}

} // namespace mydb::ui
