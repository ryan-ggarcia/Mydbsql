#pragma once

#include "ui/models/ObjectTreeModel.hpp"

#include <QSortFilterProxyModel>
#include <QString>

#include <functional>
#include <string>

namespace mydb::ui {

/// Filtros da árvore de objetos: texto nos nomes de bancos, texto nos nomes de objetos e "só favoritos".
/// Um banco que não passa no filtro some com tudo o que tem dentro; um grupo some se nenhum objeto dele passar.
class ObjectTreeFilter final : public QSortFilterProxyModel {
    Q_OBJECT

public:
    /// Diz se o banco tem favoritos, mesmo antes de os objetos dele serem carregados.
    using DatabaseFavorites = std::function<bool(const std::string& sessionId, const std::string& database)>;

    explicit ObjectTreeFilter(QObject* parent = nullptr);

    void setSourceModel(QAbstractItemModel* model) override;
    void setDatabaseFavorites(DatabaseFavorites check);

    void setDatabaseFilter(const QString& text);
    void setObjectFilter(const QString& text);
    void setFavoritesOnly(bool only);
    [[nodiscard]] bool favoritesOnly() const noexcept { return m_favoritesOnly; }
    /// Refaz o filtro (ex.: depois que um favorito mudou).
    void refilter();

protected:
    [[nodiscard]] bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override;

private:
    [[nodiscard]] bool acceptsObject(const QModelIndex& index) const;

    ObjectTreeModel* m_tree{nullptr};
    DatabaseFavorites m_databaseFavorites;
    QString m_databaseFilter;
    QString m_objectFilter;
    bool m_favoritesOnly{false};
};

} // namespace mydb::ui
