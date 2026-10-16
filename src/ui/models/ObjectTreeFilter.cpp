#include "ui/models/ObjectTreeFilter.hpp"

#include "ui/common/QtText.hpp"

#include <utility>

namespace mydb::ui {

ObjectTreeFilter::ObjectTreeFilter(QObject* parent) : QSortFilterProxyModel(parent) {
    setDynamicSortFilter(true);
}

void ObjectTreeFilter::setSourceModel(QAbstractItemModel* model) {
    m_tree = qobject_cast<ObjectTreeModel*>(model);
    QSortFilterProxyModel::setSourceModel(model);
}

void ObjectTreeFilter::setDatabaseFavorites(DatabaseFavorites check) {
    m_databaseFavorites = std::move(check);
}

void ObjectTreeFilter::setDatabaseFilter(const QString& text) {
    if (text != m_databaseFilter) {
        beginFilterChange();
        m_databaseFilter = text;
        endFilterChange(Direction::Rows);
    }
}

void ObjectTreeFilter::setObjectFilter(const QString& text) {
    if (text != m_objectFilter) {
        beginFilterChange();
        m_objectFilter = text;
        endFilterChange(Direction::Rows);
    }
}

void ObjectTreeFilter::setFavoritesOnly(bool only) {
    if (only != m_favoritesOnly) {
        beginFilterChange();
        m_favoritesOnly = only;
        endFilterChange(Direction::Rows);
    }
}

void ObjectTreeFilter::refilter() {
    beginFilterChange();
    endFilterChange(Direction::Rows);
}

bool ObjectTreeFilter::filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const {
    if (m_tree == nullptr) {
        return true;
    }
    const QModelIndex index = m_tree->index(sourceRow, ObjectTreeModel::kNameColumn, sourceParent);
    switch (ObjectTreeModel::kindAt(index)) {
    case ObjectTreeModel::Kind::Session:
        return true;
    case ObjectTreeModel::Kind::Database: {
        if (!m_databaseFilter.isEmpty() && !m_tree->nameAt(index).contains(m_databaseFilter, Qt::CaseInsensitive)) {
            return false;
        }
        if (m_favoritesOnly && m_databaseFavorites) {
            const sdk::Selection selection = m_tree->selectionAt(index);
            return m_databaseFavorites(selection.sessionId, selection.database);
        }
        return true;
    }
    case ObjectTreeModel::Kind::Group: {
        if (m_objectFilter.isEmpty() && !m_favoritesOnly) {
            return true;
        }
        const int count = m_tree->rowCount(index);
        for (int row = 0; row < count; ++row) {
            if (acceptsObject(m_tree->index(row, ObjectTreeModel::kNameColumn, index))) {
                return true;
            }
        }
        return false;
    }
    case ObjectTreeModel::Kind::Object:
        return acceptsObject(index);
    }
    return true;
}

bool ObjectTreeFilter::acceptsObject(const QModelIndex& index) const {
    if (!m_objectFilter.isEmpty() && !m_tree->nameAt(index).contains(m_objectFilter, Qt::CaseInsensitive)) {
        return false;
    }
    return !m_favoritesOnly || m_tree->data(index, ObjectTreeModel::kFavoriteRole).toBool();
}

} // namespace mydb::ui
