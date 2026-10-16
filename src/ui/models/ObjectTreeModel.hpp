#pragma once

#include "core/model/DbObject.hpp"
#include "sdk/IWorkbench.hpp"

#include <QAbstractItemModel>
#include <QColor>
#include <QIcon>
#include <QString>

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace mydb::ui {

/// Árvore de objetos: sessões → bancos → (grupos por tipo) → objetos (CLAUDE.md §5, Fase 4). Os objetos de um
/// banco são carregados quando ele é expandido: fetchMore() emite objectsRequested() e quem atende devolve a
/// lista com setObjects(). O modelo não faz I/O nem conhece os services.
class ObjectTreeModel final : public QAbstractItemModel {
    Q_OBJECT

public:
    enum class Kind : std::uint8_t {
        Session,
        Database,
        Group,
        Object,
    };

    // Papéis e colunas são int na API do Qt.
    static constexpr int kKindRole = Qt::UserRole + 1;
    static constexpr int kSessionIdRole = Qt::UserRole + 2;
    static constexpr int kDatabaseRole = Qt::UserRole + 3;
    static constexpr int kObjectTypeRole = Qt::UserRole + 4;
    static constexpr int kSizeRole = Qt::UserRole + 5; ///< qint64, -1 quando desconhecido
    static constexpr int kFavoriteRole = Qt::UserRole + 6;

    static constexpr int kNameColumn = 0;
    static constexpr int kSizeColumn = 1;
    static constexpr int kColumnCount = 2;

    using FavoriteCheck = std::function<bool(const std::string& sessionId, const core::model::ObjectRef& object)>;

    explicit ObjectTreeModel(QObject* parent = nullptr);
    ObjectTreeModel(const ObjectTreeModel&) = delete;
    ObjectTreeModel& operator=(const ObjectTreeModel&) = delete;
    ObjectTreeModel(ObjectTreeModel&&) = delete;
    ObjectTreeModel& operator=(ObjectTreeModel&&) = delete;
    ~ObjectTreeModel() override;

    void setFavoriteCheck(FavoriteCheck check);
    /// Agrupa os objetos de cada banco por tipo ("Tabelas", "Views"…).
    void setGroupByType(bool group);
    [[nodiscard]] bool groupByType() const noexcept { return m_groupByType; }

    void addSession(const std::string& sessionId, const QString& title, const QColor& color);
    void removeSession(const std::string& sessionId);
    /// Troca a lista de bancos da sessão, mantendo os objetos já carregados dos bancos que continuam.
    void setDatabases(const std::string& sessionId, const std::vector<std::string>& names);
    void setObjects(const std::string& sessionId, const std::string& database,
                    std::vector<core::model::DbObject> objects);
    /// A carga falhou: o banco fica vazio e uma nova expansão tenta de novo.
    void setLoadFailed(const std::string& sessionId, const std::string& database);
    /// Pede os objetos de novo se o banco já foi carregado (ou está carregando); senão, espera a expansão.
    void invalidateDatabase(const std::string& sessionId, const std::string& database);
    /// Avisa a view que um favorito mudou.
    void favoriteChanged(const std::string& sessionId, const core::model::ObjectRef& object);

    [[nodiscard]] QModelIndex sessionIndex(const std::string& sessionId) const;
    [[nodiscard]] QModelIndex databaseIndex(const std::string& sessionId, const std::string& database) const;
    [[nodiscard]] QModelIndex objectIndex(const std::string& sessionId, const core::model::ObjectRef& object) const;
    [[nodiscard]] bool isLoaded(const std::string& sessionId, const std::string& database) const;
    [[nodiscard]] std::vector<std::string> sessionIds() const;

    /// Sessão, banco e objeto de um índice (de qualquer coluna).
    [[nodiscard]] sdk::Selection selectionAt(const QModelIndex& index) const;
    [[nodiscard]] static Kind kindAt(const QModelIndex& index);
    /// Nome sem decoração (o filtro usa este, e não o texto exibido).
    [[nodiscard]] const QString& nameAt(const QModelIndex& index) const;
    [[nodiscard]] static const core::model::DbObject* objectAt(const QModelIndex& index);

    [[nodiscard]] QModelIndex index(int row, int column, const QModelIndex& parent = {}) const override;
    [[nodiscard]] QModelIndex parent(const QModelIndex& child) const override;
    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] int columnCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    [[nodiscard]] QVariant headerData(int section, Qt::Orientation orientation,
                                      int role = Qt::DisplayRole) const override;
    [[nodiscard]] bool hasChildren(const QModelIndex& parent = {}) const override;
    [[nodiscard]] bool canFetchMore(const QModelIndex& parent) const override;
    void fetchMore(const QModelIndex& parent) override;

Q_SIGNALS:
    /// O banco foi expandido e os objetos dele ainda não estão carregados.
    void objectsRequested(const QString& sessionId, const QString& database);

private:
    enum class LoadState : std::uint8_t {
        NotLoaded,
        Loading,
        Loaded,
    };

    struct Node {
        Kind kind{Kind::Session};
        Node* parent{nullptr};
        int row{0};
        std::vector<std::unique_ptr<Node>> children;
        QString name;
        std::string key; ///< id da sessão, nome do banco ou nome do objeto
        QColor color;
        core::model::ObjectType groupType{core::model::ObjectType::Table};
        const core::model::DbObject* object{nullptr}; ///< aponta para `loaded` do banco
        LoadState state{LoadState::NotLoaded};
        std::int64_t sizeBytes{-1};
        std::vector<core::model::DbObject> loaded; ///< objetos do banco, para reagrupar sem nova consulta
    };

    [[nodiscard]] static Node* nodeAt(const QModelIndex& index);
    [[nodiscard]] QModelIndex indexOf(const Node* node, int column = kNameColumn) const;
    [[nodiscard]] Node* findSession(const std::string& sessionId) const;
    [[nodiscard]] Node* findDatabase(const std::string& sessionId, const std::string& database) const;
    [[nodiscard]] const Node* sessionOf(const Node* node) const;
    [[nodiscard]] static const Node* databaseOf(const Node* node);
    void insertChildren(Node& parent, int position, std::vector<std::unique_ptr<Node>> nodes);
    void removeChildren(Node& parent, int first, int last);
    /// Remove os filhos do banco e, se `objects` vier, troca a lista carregada antes de recriá-los.
    void rebuildDatabase(Node& database, std::optional<std::vector<core::model::DbObject>> objects = {});
    [[nodiscard]] static QVariant displayData(const Node& node, int column);
    [[nodiscard]] QVariant decoration(const Node& node, int column) const;
    [[nodiscard]] bool isFavorite(const Node& node) const;

    Node m_root;
    bool m_groupByType{false};
    FavoriteCheck m_isFavorite;
    QIcon m_serverIcon;
    QIcon m_databaseIcon;
    QIcon m_folderIcon;
    QIcon m_favoriteIcon;
    std::array<QIcon, 8> m_objectIcons;
};

} // namespace mydb::ui
